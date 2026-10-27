#include "read_mostly_lock.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

namespace {

using lim::ReadMostlyLock;
using namespace std::chrono_literals;

TEST(ReadMostlyLock, ReadersShareTheLock) {
  ReadMostlyLock lock;
  std::shared_lock<ReadMostlyLock> first(lock);
  std::atomic<bool> second_got_it{false};
  std::thread reader([&] {
    std::shared_lock<ReadMostlyLock> second(lock);
    second_got_it = true;
  });
  reader.join();  // would hang if readers excluded each other
  EXPECT_TRUE(second_got_it);
}

TEST(ReadMostlyLock, WriterWaitsForReaders) {
  ReadMostlyLock lock;
  std::atomic<bool> writer_in{false};
  std::shared_lock<ReadMostlyLock> reader(lock);
  std::thread writer([&] {
    std::unique_lock<ReadMostlyLock> w(lock);
    writer_in = true;
  });
  std::this_thread::sleep_for(50ms);
  EXPECT_FALSE(writer_in);
  reader.unlock();
  writer.join();
  EXPECT_TRUE(writer_in);
}

TEST(ReadMostlyLock, ReadersWaitForWriter) {
  ReadMostlyLock lock;
  std::atomic<bool> reader_in{false};
  std::unique_lock<ReadMostlyLock> writer(lock);
  std::thread reader([&] {
    std::shared_lock<ReadMostlyLock> r(lock);
    reader_in = true;
  });
  std::this_thread::sleep_for(50ms);
  EXPECT_FALSE(reader_in);
  writer.unlock();
  reader.join();
  EXPECT_TRUE(reader_in);
}

// Writers update two plain counters together; readers must never see them differ.
// Under ThreadSanitizer this also checks that the lock orders the plain accesses.
TEST(ReadMostlyLock, WritersAreExclusiveUnderLoad) {
  ReadMostlyLock lock;
  long a = 0, b = 0;
  std::atomic<bool> stop{false};
  std::atomic<long> torn{0}, reads{0};

  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&] {
      while (!stop) {
        std::shared_lock<ReadMostlyLock> r(lock);
        if (a != b) torn++;
        reads++;
      }
    });
  }
  for (int t = 0; t < 2; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < 2000; ++i) {
        std::unique_lock<ReadMostlyLock> w(lock);
        ++a;
        std::this_thread::yield();
        ++b;
      }
    });
  }
  threads[4].join();
  threads[5].join();
  stop = true;
  for (int t = 0; t < 4; ++t) threads[t].join();

  EXPECT_EQ(torn.load(), 0);
  EXPECT_EQ(a, 4000);
  EXPECT_EQ(b, 4000);
  EXPECT_GT(reads.load(), 0);
}

}  // namespace
