// Index-only microbenchmark: lim::AlexIndex vs the Masstree configuration Mako's tables use.
//
//   lim_index_bench <alex|alex-rebuilt|masstree> <dense4|dense8|random8> <keys> <threads>
//                   <lookups/thread>
//
// Builds an index over the key set, inserting pointers as values (no records are
// allocated, so memory is the index alone), then runs uniform random point lookups from
// each thread. "alex" is built by inserts, as in Mako; "alex-rebuilt" is then repacked
// with AlexIndex::rebuild(). Prints one line of key=value fields. Run one configuration per
// process so that the resident-memory delta belongs to a single index.
//
// Key sets, all encoded big-endian as Mako encodes integer fields:
//   dense4   4-byte ids 1..N, like TPC-C item (inserted in order)
//   dense8   8-byte (warehouse, item) pairs, 100,000 items per warehouse, like TPC-C stock
//            (inserted in order)
//   random8  N distinct uniformly random 8-byte keys (inserted in random order)

#include <dlfcn.h>
#include <malloc.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <unistd.h>
#include <vector>

#include "masstree.hh"
#include "kvthread.hh"
#include "masstree_tcursor.hh"
#include "masstree_insert.hh"

#include "alex_index.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Key {
  char bytes[8];
  uint8_t len;
  std::string_view view() const { return {bytes, len}; }
};

Key encode(uint64_t v, uint8_t len) {
  Key k{};
  k.len = len;
  for (int i = 0; i < len; ++i) k.bytes[len - 1 - i] = static_cast<char>((v >> (8 * i)) & 0xff);
  return k;
}

std::vector<Key> make_keys(const std::string& set, size_t n) {
  std::vector<Key> keys;
  keys.reserve(n);
  if (set == "dense4") {
    for (size_t i = 1; i <= n; ++i) keys.push_back(encode(i, 4));
  } else if (set == "dense8") {
    const uint64_t items = 100000;
    for (size_t i = 0; i < n; ++i) keys.push_back(encode(((i / items + 1) << 32) | (i % items + 1), 8));
  } else if (set == "random8") {
    std::mt19937_64 rng(12345);
    std::unordered_set<uint64_t> seen;
    while (keys.size() < n) {
      uint64_t v = rng();
      if (v != UINT64_MAX && seen.insert(v).second) keys.push_back(encode(v, 8));
    }
  } else {
    std::fprintf(stderr, "unknown key set %s\n", set.c_str());
    std::exit(2);
  }
  return keys;
}

// Returns freed pages to the OS, so that resident memory counts live data only. Mako links
// jemalloc, which otherwise keeps freed pages resident for several seconds.
void release_free_memory() {
  using mallctl_fn = int (*)(const char*, void*, size_t*, void*, size_t);
  if (auto mallctl = reinterpret_cast<mallctl_fn>(dlsym(RTLD_DEFAULT, "mallctl"))) {
    mallctl("arena.4096.purge", nullptr, nullptr, nullptr, 0);  // 4096 = all arenas
  } else {
    malloc_trim(0);
  }
}

size_t resident_bytes() {
  release_free_memory();
  long pages = 0, resident = 0;
  FILE* f = std::fopen("/proc/self/statm", "r");
  if (!f || std::fscanf(f, "%ld %ld", &pages, &resident) != 2) std::exit(3);
  std::fclose(f);
  return static_cast<size_t>(resident) * static_cast<size_t>(sysconf(_SC_PAGESIZE));
}

void* value_for(size_t i) { return reinterpret_cast<void*>(i + 1); }

// Same node parameters as MassTrans's table_params.
struct masstree_params : public Masstree::nodeparams<15, 15> {
  typedef void* value_type;
  typedef Masstree::value_print<value_type> value_print_type;
  typedef threadinfo threadinfo_type;
};
using masstree_table = Masstree::basic_table<masstree_params>;

class MasstreeIndex {
 public:
  explicit MasstreeIndex(threadinfo* ti) { table_.initialize(*ti); }
  void insert(const Key& k, void* v, threadinfo* ti) {
    Masstree::tcursor<masstree_params> lp(table_, Masstree::Str(k.bytes, k.len));
    lp.find_insert(*ti);
    lp.value() = v;
    lp.finish(1, *ti);
  }
  void* find(const Key& k, threadinfo* ti) const {
    Masstree::unlocked_tcursor<masstree_params> lp(table_, Masstree::Str(k.bytes, k.len));
    return lp.find_unlocked(*ti) ? lp.value() : nullptr;
  }

 private:
  masstree_table table_;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6) {
    std::fprintf(stderr, "usage: %s <alex|masstree> <dense4|dense8|random8> <keys> <threads> <lookups>\n",
                 argv[0]);
    return 2;
  }
  const std::string index = argv[1], set = argv[2];
  const size_t n = std::stoul(argv[3]);
  const int threads = std::stoi(argv[4]);
  const size_t lookups = std::stoul(argv[5]);

  std::vector<Key> keys = make_keys(set, n);
  std::vector<size_t> order(n);
  for (size_t i = 0; i < n; ++i) order[i] = i;
  if (set == "random8") std::shuffle(order.begin(), order.end(), std::mt19937_64(7));

  // Lookup streams are generated before timing so the timed loop only searches.
  std::vector<std::vector<uint32_t>> streams(threads);
  for (int t = 0; t < threads; ++t) {
    std::mt19937_64 rng(1000 + t);
    streams[t].resize(lookups);
    for (auto& i : streams[t]) i = static_cast<uint32_t>(rng() % n);
  }

  threadinfo* main_ti = threadinfo::make(threadinfo::TI_MAIN, -1);
  lim::AlexIndex alex;
  MasstreeIndex* masstree = nullptr;

  const size_t rss_before = resident_bytes();
  const auto load_start = Clock::now();
  const bool use_alex = index == "alex" || index == "alex-rebuilt";
  if (use_alex) {
    for (size_t i : order) {
      if (alex.insert_if_absent(keys[i].view(), value_for(i)).status !=
          lim::AlexIndex::InsertStatus::kInserted) {
        std::fprintf(stderr, "insert failed\n");
        return 1;
      }
    }
    if (index == "alex-rebuilt") alex.rebuild();
  } else if (index == "masstree") {
    masstree = new MasstreeIndex(main_ti);
    for (size_t i : order) masstree->insert(keys[i], value_for(i), main_ti);
  } else {
    std::fprintf(stderr, "unknown index %s\n", index.c_str());
    return 2;
  }
  const double load_ms =
      std::chrono::duration<double, std::milli>(Clock::now() - load_start).count();
  const size_t rss_after = resident_bytes();

  std::vector<size_t> wrong(threads, 0);
  std::vector<std::thread> workers;
  const auto lookup_start = Clock::now();
  for (int t = 0; t < threads; ++t) {
    workers.emplace_back([&, t] {
      threadinfo* ti = index == "masstree" ? threadinfo::make(threadinfo::TI_PROCESS, t) : nullptr;
      size_t bad = 0;
      for (uint32_t i : streams[t]) {
        void* v = use_alex ? alex.find(keys[i].view()) : masstree->find(keys[i], ti);
        bad += v != value_for(i);
      }
      wrong[t] = bad;
    });
  }
  for (auto& w : workers) w.join();
  const double lookup_s = std::chrono::duration<double>(Clock::now() - lookup_start).count();

  size_t total_wrong = 0;
  for (size_t w : wrong) total_wrong += w;
  if (total_wrong) {
    std::fprintf(stderr, "%zu lookups returned the wrong value\n", total_wrong);
    return 1;
  }
  const double total_lookups = static_cast<double>(lookups) * threads;
  const size_t rss_delta = rss_after > rss_before ? rss_after - rss_before : 0;
  std::printf("index=%s keys=%s n=%zu threads=%d load_ms=%.1f rss_delta_bytes=%zu "
              "bytes_per_key=%.1f alex_index_bytes=%zu lookup_mops=%.2f lookup_ns=%.1f\n",
              index.c_str(), set.c_str(), n, threads, load_ms, rss_delta,
              static_cast<double>(rss_delta) / n, use_alex ? alex.index_bytes() : 0,
              total_lookups / lookup_s / 1e6, lookup_s * 1e9 * threads / total_lookups);
  return 0;
}
