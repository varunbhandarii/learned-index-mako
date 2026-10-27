#include "alex_index.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

using lim::AlexIndex;
using Status = AlexIndex::InsertStatus;

void* ptr(uintptr_t v) { return reinterpret_cast<void*>(v); }

// Mako encodes integer key fields big-endian, so byte order matches numeric order.
std::string be(uint64_t v, size_t len) {
  std::string s(len, '\0');
  for (size_t i = 0; i < len; ++i) s[len - 1 - i] = static_cast<char>((v >> (8 * i)) & 0xff);
  return s;
}

struct Row {
  std::string key;
  void* value;
  bool operator==(const Row& o) const { return key == o.key && value == o.value; }
};

std::vector<Row> scan_all(const AlexIndex& idx, const std::string& lo, const std::string* hi,
                          bool reverse, size_t batch = 64) {
  std::vector<Row> rows;
  AlexIndex::ScanState st;
  std::vector<AlexIndex::Entry> buf(batch);
  std::string_view hi_view = hi ? std::string_view(*hi) : std::string_view();
  const std::string_view* hp = hi ? &hi_view : nullptr;
  while (!st.done) {
    size_t n = reverse ? idx.rscan(lo, hp, &st, buf.data(), batch)
                       : idx.scan(lo, hp, &st, buf.data(), batch);
    EXPECT_FALSE(st.conflict);
    for (size_t i = 0; i < n; ++i) {
      char k[AlexIndex::kMaxKeyLen];
      size_t len = AlexIndex::decode(buf[i].code, st.key_len, k);
      rows.push_back({std::string(k, len), buf[i].value});
    }
  }
  return rows;
}

// Reference results with Masstree's scan semantics on byte strings.
std::vector<Row> ref_scan(const std::map<std::string, void*>& m, const std::string& lo,
                          const std::string* hi, bool reverse) {
  std::vector<Row> rows;
  if (!reverse) {
    for (auto it = m.lower_bound(lo); it != m.end() && (!hi || it->first < *hi); ++it)
      rows.push_back({it->first, it->second});
  } else {
    for (auto it = std::make_reverse_iterator(m.upper_bound(lo));
         it != m.rend() && (!hi || it->first > *hi); ++it)
      rows.push_back({it->first, it->second});
  }
  return rows;
}

TEST(AlexIndex, EmptyIndex) {
  AlexIndex idx;
  uint64_t v = 99;
  EXPECT_EQ(idx.find(be(1, 4), &v), nullptr);
  EXPECT_EQ(v, 0u);
  EXPECT_EQ(idx.size(), 0u);
  EXPECT_EQ(idx.key_len(), 0u);
  EXPECT_TRUE(scan_all(idx, "", nullptr, false).empty());
  EXPECT_TRUE(scan_all(idx, "\xff\xff\xff\xff\xff", nullptr, true).empty());
}

TEST(AlexIndex, InsertFindAndVersions) {
  AlexIndex idx;
  auto r = idx.insert_if_absent(be(7, 4), ptr(70));
  EXPECT_EQ(r.status, Status::kInserted);
  EXPECT_EQ(r.old_version, 0u);
  EXPECT_EQ(r.new_version, 1u);
  EXPECT_EQ(idx.version(), 1u);
  EXPECT_EQ(idx.key_len(), 4u);

  r = idx.insert_if_absent(be(7, 4), ptr(71));
  EXPECT_EQ(r.status, Status::kExists);
  EXPECT_EQ(r.existing, ptr(70));
  EXPECT_EQ(r.new_version, r.old_version);
  EXPECT_EQ(idx.version(), 1u);

  uint64_t v = 0;
  EXPECT_EQ(idx.find(be(7, 4), &v), ptr(70));
  EXPECT_EQ(v, 1u);
  EXPECT_EQ(idx.find(be(8, 4)), nullptr);
}

// The old adapter turned keys into integers with std::stoull and fell back to hashing, so
// "12ab" and "12xy" became the same key and "007" found "7".
TEST(AlexIndex, DistinctKeysNeverCollide) {
  AlexIndex idx;
  EXPECT_EQ(idx.insert_if_absent("12ab", ptr(1)).status, Status::kInserted);
  EXPECT_EQ(idx.insert_if_absent("12xy", ptr(2)).status, Status::kInserted);
  EXPECT_EQ(idx.find("12ab"), ptr(1));
  EXPECT_EQ(idx.find("12xy"), ptr(2));
  EXPECT_EQ(idx.size(), 2u);

  AlexIndex numeric;
  numeric.insert_if_absent("007", ptr(7));
  EXPECT_EQ(numeric.find("7"), nullptr);
  EXPECT_EQ(numeric.find("070"), nullptr);
}

TEST(AlexIndex, RejectsKeysItCannotStore) {
  AlexIndex idx;
  EXPECT_EQ(idx.insert_if_absent("", ptr(1)).status, Status::kBadKey);
  EXPECT_EQ(idx.insert_if_absent("123456789", ptr(1)).status, Status::kBadKey);
  EXPECT_EQ(idx.insert_if_absent(std::string(8, '\xff'), ptr(1)).status, Status::kBadKey);
  EXPECT_EQ(idx.key_len(), 0u);  // rejected keys don't fix the key length
  EXPECT_EQ(idx.insert_if_absent(be(1, 8), ptr(1)).status, Status::kInserted);
  EXPECT_EQ(idx.insert_if_absent(be(1, 4), ptr(1)).status, Status::kBadKey);
  EXPECT_EQ(idx.find(be(1, 4)), nullptr);
  EXPECT_EQ(idx.find(std::string(8, '\xff')), nullptr);
  EXPECT_EQ(idx.insert_if_absent(std::string(7, '\xff') + "\xfe", ptr(2)).status,
            Status::kInserted);
}

TEST(AlexIndex, ReplaceAndErase) {
  AlexIndex idx;
  idx.insert_if_absent(be(5, 2), ptr(50));
  uint64_t v0 = idx.version();
  EXPECT_TRUE(idx.replace(be(5, 2), ptr(51)));
  EXPECT_EQ(idx.version(), v0);  // replacing a value is not a structural change
  EXPECT_FALSE(idx.replace(be(6, 2), ptr(60)));
  EXPECT_EQ(idx.find(be(5, 2)), ptr(51));

  EXPECT_EQ(idx.erase(be(6, 2)), nullptr);
  EXPECT_EQ(idx.version(), v0);
  EXPECT_EQ(idx.erase(be(5, 2)), ptr(51));
  EXPECT_EQ(idx.version(), v0 + 1);
  EXPECT_EQ(idx.size(), 0u);
  EXPECT_EQ(idx.find(be(5, 2)), nullptr);
  EXPECT_EQ(idx.insert_if_absent(be(5, 2), ptr(52)).status, Status::kInserted);
}

TEST(AlexIndex, ScansFollowByteOrder) {
  AlexIndex idx;
  std::map<std::string, void*> ref;
  std::mt19937_64 rng(1);
  for (int i = 0; i < 5000; ++i) {
    uint64_t k = rng() % 100000;
    std::string key = be(k, 4);
    if (idx.insert_if_absent(key, ptr(k + 1)).status == Status::kInserted) ref[key] = ptr(k + 1);
  }
  ASSERT_EQ(idx.size(), ref.size());
  EXPECT_EQ(scan_all(idx, "", nullptr, false), ref_scan(ref, "", nullptr, false));
  std::string top = "\xff\xff\xff\xff\xff";
  EXPECT_EQ(scan_all(idx, top, nullptr, true), ref_scan(ref, top, nullptr, true));
}

TEST(AlexIndex, BoundsOfAnyLength) {
  AlexIndex idx;
  std::map<std::string, void*> ref;
  for (uint64_t k = 0; k < 600; k += 3) {
    idx.insert_if_absent(be(k, 2), ptr(k + 1));
    ref[be(k, 2)] = ptr(k + 1);
  }
  // Bounds shorter than, equal to, and longer than the key length, on and between keys.
  std::vector<std::string> bounds = {"", std::string(1, '\0'), std::string("\x01", 1),
                                     be(0, 2), be(3, 2), be(4, 2), be(300, 2),
                                     be(3, 2) + std::string(1, '\0'), be(3, 2) + "\x01",
                                     be(598, 2), be(597, 2) + "zz", "\x02", "\xff", "\xff\xff",
                                     "\xff\xff\xff"};
  for (const auto& lo : bounds) {
    for (bool reverse : {false, true}) {
      EXPECT_EQ(scan_all(idx, lo, nullptr, reverse), ref_scan(ref, lo, nullptr, reverse));
      for (const auto& hi : bounds) {
        EXPECT_EQ(scan_all(idx, lo, &hi, reverse), ref_scan(ref, lo, &hi, reverse))
            << "lo=" << testing::PrintToString(lo) << " hi=" << testing::PrintToString(hi)
            << " reverse=" << reverse;
      }
    }
  }
}

TEST(AlexIndex, BatchesMatchOneShotScan) {
  AlexIndex idx;
  for (uint64_t k = 0; k < 1000; ++k) idx.insert_if_absent(be(k * 7, 4), ptr(k + 1));
  std::string hi = be(5000, 4);
  for (size_t batch : {1, 2, 3, 63, 64, 1000, 5000}) {
    EXPECT_EQ(scan_all(idx, be(10, 4), &hi, false, batch),
              scan_all(idx, be(10, 4), &hi, false, 100000));
    EXPECT_EQ(scan_all(idx, hi, nullptr, true, batch), scan_all(idx, hi, nullptr, true, 100000));
  }
}

TEST(AlexIndex, ScanReportsStructuralChangeBetweenBatches) {
  AlexIndex idx;
  for (uint64_t k = 0; k < 100; ++k) idx.insert_if_absent(be(k * 2, 4), ptr(k + 1));
  AlexIndex::ScanState st;
  AlexIndex::Entry buf[10];
  EXPECT_EQ(idx.scan("", nullptr, &st, buf, 10), 10u);
  EXPECT_EQ(st.version, idx.version());
  idx.insert_if_absent(be(1, 4), ptr(1000));
  EXPECT_EQ(idx.scan("", nullptr, &st, buf, 10), 0u);
  EXPECT_TRUE(st.conflict);
  EXPECT_TRUE(st.done);
}

TEST(AlexIndex, RebuildKeepsContentsAndVersion) {
  AlexIndex idx;
  std::map<std::string, void*> ref;
  for (uint64_t k = 1; k <= 50000; ++k) {
    idx.insert_if_absent(be(k * 3, 4), ptr(k));
    ref[be(k * 3, 4)] = ptr(k);
  }
  size_t before = idx.index_bytes();
  uint64_t version = idx.version();
  idx.rebuild();
  EXPECT_EQ(idx.version(), version);
  EXPECT_EQ(idx.size(), ref.size());
  EXPECT_LE(idx.index_bytes(), before);  // bulk loading packs at least as tightly
  EXPECT_EQ(scan_all(idx, "", nullptr, false), ref_scan(ref, "", nullptr, false));
  EXPECT_EQ(idx.insert_if_absent(be(1, 4), ptr(1)).status, Status::kInserted);

  AlexIndex empty;
  empty.rebuild();
  EXPECT_EQ(empty.size(), 0u);
  EXPECT_EQ(empty.find(be(1, 4)), nullptr);
}

TEST(AlexIndex, RebuildRegisteredIndexes) {
  AlexIndex idx;
  for (uint64_t k = 1; k <= 1000; ++k) idx.insert_if_absent(be(k, 4), ptr(k));
  lim::register_table("rebuild_test", &idx);
  lim::rebuild_registered_indexes();
  EXPECT_EQ(idx.size(), 1000u);
  for (uint64_t k = 1; k <= 1000; ++k) ASSERT_EQ(idx.find(be(k, 4)), ptr(k));
}  // idx unregisters itself here, so the exit report never sees a dangling pointer

TEST(AlexIndex, IndexBytesAreReported) {
  AlexIndex idx;
  for (uint64_t k = 1; k <= 100000; ++k) idx.insert_if_absent(be(k, 4), ptr(k));
  size_t bytes = idx.index_bytes();
  EXPECT_GT(bytes, 100000u * sizeof(uint64_t));  // at least the keys
  EXPECT_LT(bytes, 100000u * 128);               // and not absurdly more
}

// Random operations on every key length, compared against std::map. Key bytes come from a
// small alphabet that includes 0x00 and 0xff to exercise padding and boundary cases.
class AlexIndexDifferential : public testing::TestWithParam<size_t> {};

TEST_P(AlexIndexDifferential, MatchesStdMap) {
  const size_t key_len = GetParam();
  const char alphabet[] = {'\x00', '\x01', '\x7f', '\x80', '\xfe', '\xff'};
  std::mt19937_64 rng(key_len * 7919);
  auto rand_bytes = [&](size_t len) {
    std::string s(len, '\0');
    for (auto& c : s) c = alphabet[rng() % sizeof(alphabet)];
    return s;
  };
  auto rand_key = [&] { return rand_bytes(key_len); };
  auto rand_bound = [&] { return rand_bytes(rng() % (key_len + 3)); };

  AlexIndex idx;
  std::map<std::string, void*> ref;
  uintptr_t next_value = 1;
  for (int op = 0; op < 20000; ++op) {
    switch (rng() % 8) {
      case 0: case 1: case 2: {
        std::string k = rand_key();
        void* v = ptr(next_value++);
        auto r = idx.insert_if_absent(k, v);
        if (k == std::string(8, '\xff')) {
          ASSERT_EQ(r.status, Status::kBadKey);
        } else if (ref.count(k)) {
          ASSERT_EQ(r.status, Status::kExists);
          ASSERT_EQ(r.existing, ref[k]);
        } else {
          ASSERT_EQ(r.status, Status::kInserted);
          ref[k] = v;
        }
        break;
      }
      case 3: {
        std::string k = rand_key();
        auto it = ref.find(k);
        ASSERT_EQ(idx.erase(k), it == ref.end() ? nullptr : it->second);
        if (it != ref.end()) ref.erase(it);
        break;
      }
      case 4: {
        std::string k = rand_key();
        void* v = ptr(next_value++);
        bool present = ref.count(k) > 0;
        ASSERT_EQ(idx.replace(k, v), present);
        if (present) ref[k] = v;
        break;
      }
      case 5: {
        std::string k = rand_key();
        auto it = ref.find(k);
        ASSERT_EQ(idx.find(k), it == ref.end() ? nullptr : it->second);
        break;
      }
      default: {
        std::string lo = rand_bound();
        std::string hi = rand_bound();
        bool reverse = rng() % 2;
        bool bounded = rng() % 3 != 0;
        size_t batch = 1 + rng() % 40;
        ASSERT_EQ(scan_all(idx, lo, bounded ? &hi : nullptr, reverse, batch),
                  ref_scan(ref, lo, bounded ? &hi : nullptr, reverse));
        break;
      }
    }
    if (op % 2500 == 2499) idx.rebuild();
    ASSERT_EQ(idx.size(), ref.size());
  }
}

INSTANTIATE_TEST_SUITE_P(KeyLengths, AlexIndexDifferential, testing::Values(1, 2, 3, 4, 7, 8));

// Readers run lookups and scans while a writer inserts and erases. Every value is the key's
// own code plus one, so a reader can check that it never sees a value under the wrong key.
// Meant to be run under ThreadSanitizer as well.
TEST(AlexIndex, ConcurrentReadersAndWriter) {
  AlexIndex idx;
  for (uint64_t k = 0; k < 20000; k += 2) idx.insert_if_absent(be(k, 4), ptr(k + 1));
  std::atomic<bool> stop{false};
  std::atomic<uint64_t> bad{0};

  std::vector<std::thread> readers;
  for (int t = 0; t < 4; ++t) {
    readers.emplace_back([&, t] {
      std::mt19937_64 rng(t);
      AlexIndex::Entry buf[32];
      while (!stop.load()) {
        uint64_t k = rng() % 20000;
        void* v = idx.find(be(k, 4));
        if (v && v != ptr(k + 1)) bad++;
        AlexIndex::ScanState st;
        std::string lo = be(k, 4);
        size_t n = idx.scan(lo, nullptr, &st, buf, 32);
        for (size_t i = 0; i < n; ++i) {
          if (buf[i].value != ptr(buf[i].code + 1)) bad++;
          if (i > 0 && buf[i].code <= buf[i - 1].code) bad++;
        }
      }
    });
  }
  std::mt19937_64 rng(42);
  for (int i = 0; i < 20000; ++i) {
    uint64_t k = rng() % 20000;
    if (rng() % 2) {
      idx.insert_if_absent(be(k, 4), ptr(k + 1));
    } else {
      idx.erase(be(k, 4));
    }
    if (i % 5000 == 4999) idx.rebuild();
  }
  stop = true;
  for (auto& r : readers) r.join();
  EXPECT_EQ(bad.load(), 0u);
}

}  // namespace
