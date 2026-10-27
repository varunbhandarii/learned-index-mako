// An ordered map from short binary keys to pointers, backed by the ALEX learned index.
//
// This header deliberately avoids ALEX's headers: ALEX relies on std::allocator::rebind,
// which C++20 removed, so alex_index.cc is compiled as C++17 while the code that includes
// this header (Mako) is compiled as C++23. Only plain types cross the boundary.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace lim {

// Keys are compared as byte strings (shorter prefix first), the order Masstree uses.
//
// Every key in one index has the same length, fixed by the first insert, and at most 8
// bytes. A key is stored as the big-endian integer value of its bytes ("code"), which
// preserves byte order exactly, so ALEX's numeric order is the table's key order and
// range scans walk the index in order. Nothing is hashed, so distinct keys never collide.
//
// All methods are thread-safe: readers share a lock and writers take it exclusively.
// version() changes on every insert or erase. An optimistic transaction that records the
// version when a lookup misses, or when it scans, can detect phantoms by comparing it
// again at commit time.
class AlexIndex {
 public:
  static constexpr size_t kMaxKeyLen = 8;

  struct Entry {
    uint64_t code;
    void* value;
  };

  enum class InsertStatus { kInserted, kExists, kBadKey };

  struct InsertResult {
    InsertStatus status;
    void* existing;        // the stored value when status == kExists
    uint64_t old_version;  // version before the insert
    uint64_t new_version;  // version after the insert (unchanged unless kInserted)
  };

  // Progress of a scan that is read in batches. Start with a default-constructed state
  // and keep calling scan() or rscan() with the same bounds until done is set.
  struct ScanState {
    bool started = false;
    bool done = false;
    bool conflict = false;  // the index changed structurally between batches
    size_t key_len = 0;     // key length in effect for this scan
    uint64_t version = 0;   // version observed by the first batch
    uint64_t next = 0;      // next code to visit
    uint64_t last = 0;      // final code of the range (inclusive)
  };

  AlexIndex();
  ~AlexIndex();
  AlexIndex(const AlexIndex&) = delete;
  AlexIndex& operator=(const AlexIndex&) = delete;

  // Point lookup. Returns the stored value, or nullptr if the key is absent. If version is
  // non-null it receives the version observed under the same lock as the lookup.
  void* find(std::string_view key, uint64_t* version = nullptr) const;

  // Inserts key -> value unless the key is already present, atomically. Returns kBadKey if
  // the key's length differs from the index's key length, exceeds kMaxKeyLen, is zero, or
  // the key is the reserved 8-byte all-0xFF key.
  InsertResult insert_if_absent(std::string_view key, void* value);

  // Replaces the value stored for an existing key. Returns false if the key is absent.
  bool replace(std::string_view key, void* value);

  // Removes a key. Returns the value it had, or nullptr if it was absent.
  void* erase(std::string_view key);

  // Forward scan of keys k with lo <= k < hi, ascending; hi == nullptr means unbounded.
  // Reverse scan of keys k with hi < k <= lo, descending; hi == nullptr means unbounded.
  // Bounds may have any length. Each call writes up to max entries to out and returns how
  // many it wrote.
  size_t scan(std::string_view lo, const std::string_view* hi, ScanState* state, Entry* out,
              size_t max) const;
  size_t rscan(std::string_view lo, const std::string_view* hi, ScanState* state, Entry* out,
               size_t max) const;

  // Writes the key_len bytes of the key with this code to out and returns key_len.
  static size_t decode(uint64_t code, size_t key_len, char* out);

  uint64_t version() const;
  size_t size() const;
  size_t key_len() const;  // 0 until the first insert

  // Bytes used by ALEX's data and model nodes (excludes the values the pointers refer to).
  size_t index_bytes() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Prints the index's key count and index_bytes() to stderr when the process exits, as
// "[learned-index-mako] alex table=<name> keys=<n> index_bytes=<n> bytes_per_key=<x>".
// An index destroyed before then is dropped from the report.
void report_at_exit(const std::string& table_name, const AlexIndex* index);

// True when MAKO_INDEX=alex and table_name is listed in MAKO_ALEX_TABLES (comma-separated,
// default "item,stock,warehouse,district"). A trailing "_<digits>" partition suffix is
// ignored, and remote proxy tables ("..._remote_...") are never selected.
bool alex_enabled_for(const std::string& table_name);

}  // namespace lim
