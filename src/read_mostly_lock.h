// A reader-writer lock for data that is read far more often than it is written.
//
// std::shared_mutex makes every reader update one shared counter, so readers on different
// cores contend for the same cache line even though they never block each other. Here
// each reader thread increments a counter in its own cache-line-sized slot, and a writer
// pays instead: it raises a flag and waits for every slot to drain. ALEX tables in TPC-C
// are read on every transaction and almost never change after loading.
//
// Satisfies SharedLockable, so std::shared_lock and std::unique_lock work with it.
// Not recursive.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

namespace lim {

class ReadMostlyLock {
 public:
  static constexpr size_t kSlots = 64;

  void lock_shared() {
    Slot& slot = slots_[slot_index()];
    for (;;) {
      // Announce the reader, then check for a writer. Both are sequentially consistent,
      // pairing with the writer's flag store and slot loads, so either the writer sees
      // this reader or this reader sees the writer.
      slot.readers.fetch_add(1, std::memory_order_seq_cst);
      if (!writer_.load(std::memory_order_seq_cst)) return;
      slot.readers.fetch_sub(1, std::memory_order_release);
      while (writer_.load(std::memory_order_acquire)) std::this_thread::yield();
    }
  }

  void unlock_shared() { slots_[slot_index()].readers.fetch_sub(1, std::memory_order_release); }

  void lock() {
    writer_mutex_.lock();
    writer_.store(true, std::memory_order_seq_cst);
    for (Slot& slot : slots_) {
      while (slot.readers.load(std::memory_order_seq_cst) != 0) std::this_thread::yield();
    }
  }

  void unlock() {
    writer_.store(false, std::memory_order_release);
    writer_mutex_.unlock();
  }

 private:
  struct alignas(64) Slot {
    std::atomic<uint32_t> readers{0};
  };

  // Threads get slots round-robin, so up to kSlots threads never share one. A thread
  // keeps its slot for life, which keeps lock_shared and unlock_shared on the same slot.
  static size_t slot_index() {
    static std::atomic<size_t> next{0};
    thread_local size_t index = next.fetch_add(1, std::memory_order_relaxed) % kSlots;
    return index;
  }

  Slot slots_[kSlots];
  alignas(64) std::atomic<bool> writer_{false};
  std::mutex writer_mutex_;
};

}  // namespace lim
