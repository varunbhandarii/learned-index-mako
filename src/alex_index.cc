// Compiled as C++17 against ALEX with patches/alex.patch applied; see alex_index.h.
#ifndef ALEX_TRACK_LOOKUP_STATS
#define ALEX_TRACK_LOOKUP_STATS 0  // lookups must not write shared state (see the patch)
#endif

#include "alex_index.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <shared_mutex>  // std::shared_lock
#include <unordered_set>
#include <utility>
#include <vector>

#include "alex.h"
#include "read_mostly_lock.h"

#if ALEX_TRACK_LOOKUP_STATS || !defined(ALEX_LOOKUP_STAT)
#error "alex_index.cc needs ALEX patched with patches/alex.patch and lookup stats disabled"
#endif

namespace lim {
namespace {

// Default template arguments on purpose: ALEX's node-splitting code casts data nodes to
// AlexDataNode<T, P>, so any other arguments (e.g. allow_duplicates=false) make that cast a
// type confusion. Keys stay unique because insert_if_absent() checks under the write lock.
using Map = alex::Alex<uint64_t, void*>;

// ALEX uses the largest key value as an internal sentinel, so it can't be stored.
constexpr uint64_t kReservedCode = std::numeric_limits<uint64_t>::max();

uint64_t max_code(size_t key_len) {
  return key_len == 8 ? kReservedCode - 1 : (uint64_t{1} << (8 * key_len)) - 1;
}

// Big-endian value of the first min(s.size(), key_len) bytes of s, right-padded with zero
// bytes to key_len bytes.
uint64_t prefix_code(std::string_view s, size_t key_len) {
  uint64_t code = 0;
  for (size_t i = 0; i < key_len; ++i) {
    code = (code << 8) | (i < s.size() ? static_cast<uint8_t>(s[i]) : 0);
  }
  return code;
}

// Sets [*lo, *hi] to the codes of exactly the key_len-byte keys k with start <= k < stop
// (stop == nullptr: no upper bound). Returns false if there are none.
bool forward_range(std::string_view start, const std::string_view* stop, size_t key_len,
                   uint64_t* lo, uint64_t* hi) {
  uint64_t first = prefix_code(start, key_len);
  if (start.size() > key_len) {
    // A key equal to start's prefix is shorter than start, so it sorts before start.
    if (first >= max_code(key_len)) return false;
    ++first;
  }
  uint64_t last = max_code(key_len);
  if (stop) {
    uint64_t bound = prefix_code(*stop, key_len);
    if (stop->size() <= key_len) {
      // k < stop exactly when code(k) < bound. (When stop is longer, a key equal to its
      // prefix is shorter than stop and still qualifies.)
      if (bound == 0) return false;
      --bound;
    }
    last = std::min(last, bound);
  }
  *lo = first;
  *hi = last;
  return first <= last;
}

// Sets [*lo, *hi] to the codes of exactly the key_len-byte keys k with stop < k <= start
// (stop == nullptr: no lower bound). Returns false if there are none.
bool reverse_range(std::string_view start, const std::string_view* stop, size_t key_len,
                   uint64_t* lo, uint64_t* hi) {
  uint64_t last = prefix_code(start, key_len);
  if (start.size() < key_len) {
    // Keys that extend start sort after it, so only codes below the padded start qualify.
    if (last == 0) return false;
    --last;
  }
  last = std::min(last, max_code(key_len));
  uint64_t first = 0;
  if (stop) {
    first = prefix_code(*stop, key_len);
    if (stop->size() >= key_len) {
      // k > stop exactly when code(k) > first. (When stop is shorter, keys that extend it,
      // including the padded stop itself, are all greater than stop.)
      if (first >= max_code(key_len)) return false;
      ++first;
    }
  }
  *lo = first;
  *hi = last;
  return first <= last;
}

std::mutex& registry_mutex() {
  static std::mutex mu;
  return mu;
}

std::vector<std::pair<std::string, AlexIndex*>>& registry() {
  static std::vector<std::pair<std::string, AlexIndex*>> list;
  return list;
}

void print_report() {
  std::lock_guard<std::mutex> lock(registry_mutex());
  for (const auto& [name, index] : registry()) {
    size_t keys = index->size();
    size_t bytes = index->index_bytes();
    std::fprintf(stderr,
                 "[learned-index-mako] alex table=%s keys=%zu index_bytes=%zu bytes_per_key=%.1f\n",
                 name.c_str(), keys, bytes, keys ? static_cast<double>(bytes) / keys : 0.0);
  }
}

}  // namespace

struct AlexIndex::Impl {
  mutable ReadMostlyLock mu;
  // Held by pointer so rebuild() can swap in a freshly bulk-loaded map. Lookups go through
  // ALEX's non-const API, and only under mu.
  std::unique_ptr<Map> map = std::make_unique<Map>();
  size_t key_len = 0;  // guarded by mu
  size_t count = 0;    // guarded by mu
  // Written only while mu is held exclusively; read without the lock by version().
  std::atomic<uint64_t> version{0};

  // The code for key, or false if key can't be in this index.
  bool encode(std::string_view key, uint64_t* code) const {
    if (key_len == 0 || key.size() != key_len) return false;
    *code = prefix_code(key, key_len);
    return *code != kReservedCode;
  }

  void bump_version() { version.store(version.load(std::memory_order_relaxed) + 1,
                                      std::memory_order_release); }
};

AlexIndex::AlexIndex() : impl_(std::make_unique<Impl>()) {}

AlexIndex::~AlexIndex() {
  std::lock_guard<std::mutex> lock(registry_mutex());
  auto& list = registry();
  list.erase(std::remove_if(list.begin(), list.end(),
                            [this](const auto& entry) { return entry.second == this; }),
             list.end());
}

void* AlexIndex::find(std::string_view key, uint64_t* version) const {
  std::shared_lock<ReadMostlyLock> lock(impl_->mu);
  if (version) *version = impl_->version.load(std::memory_order_relaxed);
  uint64_t code;
  if (!impl_->encode(key, &code)) return nullptr;
  auto it = impl_->map->find(code);
  return it == impl_->map->end() ? nullptr : it.payload();
}

AlexIndex::InsertResult AlexIndex::insert_if_absent(std::string_view key, void* value) {
  std::unique_lock<ReadMostlyLock> lock(impl_->mu);
  uint64_t old_version = impl_->version.load(std::memory_order_relaxed);
  InsertResult result{InsertStatus::kBadKey, nullptr, old_version, old_version};
  if (key.empty() || key.size() > kMaxKeyLen) return result;
  if (impl_->key_len != 0 && key.size() != impl_->key_len) return result;
  uint64_t code = prefix_code(key, key.size());
  if (code == kReservedCode) return result;

  if (impl_->key_len == 0) impl_->key_len = key.size();
  auto it = impl_->map->find(code);
  if (it != impl_->map->end()) {
    result.status = InsertStatus::kExists;
    result.existing = it.payload();
    return result;
  }
  impl_->map->insert(code, value);
  ++impl_->count;
  impl_->bump_version();
  result.status = InsertStatus::kInserted;
  result.new_version = old_version + 1;
  return result;
}

bool AlexIndex::replace(std::string_view key, void* value) {
  std::unique_lock<ReadMostlyLock> lock(impl_->mu);
  uint64_t code;
  if (!impl_->encode(key, &code)) return false;
  auto it = impl_->map->find(code);
  if (it == impl_->map->end()) return false;
  it.payload() = value;
  return true;
}

void* AlexIndex::erase(std::string_view key) {
  std::unique_lock<ReadMostlyLock> lock(impl_->mu);
  uint64_t code;
  if (!impl_->encode(key, &code)) return nullptr;
  auto it = impl_->map->find(code);
  if (it == impl_->map->end()) return nullptr;
  void* value = it.payload();
  impl_->map->erase(it);
  --impl_->count;
  impl_->bump_version();
  return value;
}

void AlexIndex::rebuild() {
  std::unique_lock<ReadMostlyLock> lock(impl_->mu);
  std::vector<std::pair<uint64_t, void*>> entries;
  entries.reserve(impl_->count);
  for (auto it = impl_->map->begin(); !it.is_end(); ++it) entries.emplace_back(it.key(), it.payload());
  auto fresh = std::make_unique<Map>();
  if (!entries.empty()) fresh->bulk_load(entries.data(), static_cast<int>(entries.size()));
  impl_->map = std::move(fresh);
}

size_t AlexIndex::scan(std::string_view lo, const std::string_view* hi, ScanState* state,
                       Entry* out, size_t max) const {
  std::shared_lock<ReadMostlyLock> lock(impl_->mu);
  if (state->done) return 0;
  uint64_t version = impl_->version.load(std::memory_order_relaxed);
  if (!state->started) {
    state->started = true;
    state->version = version;
    state->key_len = impl_->key_len;
    if (state->key_len == 0 ||
        !forward_range(lo, hi, state->key_len, &state->next, &state->last)) {
      state->done = true;
      return 0;
    }
  } else if (version != state->version) {
    state->conflict = true;
    state->done = true;
    return 0;
  }
  if (max == 0) return 0;

  size_t n = 0;
  for (auto it = impl_->map->lower_bound(state->next); !it.is_end() && n < max; ++it) {
    uint64_t code = it.key();
    if (code > state->last) break;
    out[n++] = Entry{code, it.payload()};
  }
  if (n < max || out[n - 1].code == state->last) {
    state->done = true;
  } else {
    state->next = out[n - 1].code + 1;
  }
  return n;
}

size_t AlexIndex::rscan(std::string_view lo, const std::string_view* hi, ScanState* state,
                        Entry* out, size_t max) const {
  std::shared_lock<ReadMostlyLock> lock(impl_->mu);
  if (state->done) return 0;
  uint64_t version = impl_->version.load(std::memory_order_relaxed);
  if (!state->started) {
    state->started = true;
    state->version = version;
    state->key_len = impl_->key_len;
    // For a reverse scan, state->next walks down from the top of the range to state->last.
    if (state->key_len == 0 ||
        !reverse_range(lo, hi, state->key_len, &state->last, &state->next)) {
      state->done = true;
      return 0;
    }
  } else if (version != state->version) {
    state->conflict = true;
    state->done = true;
    return 0;
  }
  if (max == 0) return 0;

  // Start at the last key <= state->next: step back from the first key above it.
  auto above = impl_->map->upper_bound(state->next);
  Map::ReverseIterator it = above.is_end() ? impl_->map->rbegin() : Map::ReverseIterator(above);
  while (!it.is_end() && it.key() > state->next) ++it;

  size_t n = 0;
  for (; !it.is_end() && n < max; ++it) {
    uint64_t code = it.key();
    if (code < state->last) break;
    out[n++] = Entry{code, it.payload()};
  }
  if (n < max || out[n - 1].code == state->last) {
    state->done = true;
  } else {
    state->next = out[n - 1].code - 1;
  }
  return n;
}

size_t AlexIndex::decode(uint64_t code, size_t key_len, char* out) {
  for (size_t i = 0; i < key_len; ++i) {
    out[key_len - 1 - i] = static_cast<char>(code & 0xff);
    code >>= 8;
  }
  return key_len;
}

uint64_t AlexIndex::version() const { return impl_->version.load(std::memory_order_acquire); }

size_t AlexIndex::size() const {
  std::shared_lock<ReadMostlyLock> lock(impl_->mu);
  return impl_->count;
}

size_t AlexIndex::key_len() const {
  std::shared_lock<ReadMostlyLock> lock(impl_->mu);
  return impl_->key_len;
}

size_t AlexIndex::index_bytes() const {
  std::shared_lock<ReadMostlyLock> lock(impl_->mu);
  return static_cast<size_t>(impl_->map->data_size() + impl_->map->model_size());
}

void register_table(const std::string& table_name, AlexIndex* index) {
  std::lock_guard<std::mutex> lock(registry_mutex());
  if (registry().empty()) std::atexit(print_report);
  registry().emplace_back(table_name, index);
}

void rebuild_registered_indexes() {
  const char* setting = std::getenv("MAKO_ALEX_REBUILD");
  if (setting && std::string(setting) == "0") return;
  std::lock_guard<std::mutex> lock(registry_mutex());
  if (registry().empty()) return;
  const auto start = std::chrono::steady_clock::now();
  for (const auto& entry : registry()) entry.second->rebuild();
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  std::fprintf(stderr, "[learned-index-mako] rebuilt %zu ALEX indexes in %.1f ms\n",
               registry().size(), ms);
}

bool alex_enabled_for(const std::string& table_name) {
  static const std::unordered_set<std::string> tables = [] {
    std::unordered_set<std::string> names;
    const char* index = std::getenv("MAKO_INDEX");
    if (!index || std::string(index) != "alex") return names;
    const char* list = std::getenv("MAKO_ALEX_TABLES");
    std::string s = list ? list : "item,stock,warehouse,district";
    size_t pos = 0;
    while (pos <= s.size()) {
      size_t comma = std::min(s.find(',', pos), s.size());
      if (comma > pos) names.insert(s.substr(pos, comma - pos));
      pos = comma + 1;
    }
    return names;
  }();
  if (tables.empty() || table_name.find("_remote_") != std::string::npos) return false;

  std::string base = table_name;
  size_t underscore = base.rfind('_');
  if (underscore != std::string::npos && underscore + 1 < base.size() &&
      base.find_first_not_of("0123456789", underscore + 1) == std::string::npos) {
    base.resize(underscore);
  }
  return tables.count(base) > 0;
}

}  // namespace lim
