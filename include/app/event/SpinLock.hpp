#pragma once

#include <atomic>

namespace app::event {

// Lightweight lock for short critical sections guarding a single
// EventDescriptor's internal state. Not intended for long-held locks.
class SpinLock {
 public:
  void lock() noexcept {
    while (m_Flag.test_and_set(std::memory_order_acquire)) {
      // busy-wait
    }
  }

  void unlock() noexcept {
    m_Flag.clear(std::memory_order_release);
  }

 private:
  std::atomic_flag m_Flag = ATOMIC_FLAG_INIT;
};

}  // namespace app::event
