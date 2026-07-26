#pragma once

#include <atomic>

namespace tickguard {

/**
 * @brief Minimal spinlock built on `std::atomic_flag` for guarding short
 *        critical sections.
 *
 * @details Intended to protect a single `EventDescriptor`'s internal state
 * (e.g. `m_Image`, `m_Pending`, `m_ArmedAt`) during brief operations such as
 * reading/updating timing state inside `trigger()` or `tick()`. Because it
 * busy-waits instead of yielding or blocking via the OS, it must **only**
 * guard very short critical sections — never hold it across I/O, logging,
 * `IEventSender::send()` calls, or any other potentially blocking operation.
 *
 * Satisfies the `Lockable` named requirement, so it can be used directly
 * with `std::lock_guard<SpinLock>` / `std::scoped_lock<SpinLock>`.
 *
 * @note Not reentrant: locking twice from the same thread will deadlock
 * (busy-loop forever).
 */
class SpinLock {
 public:
  /**
   * @brief Acquires the lock, busy-waiting until it becomes available.
   *
   * @details Spins on `std::atomic_flag::test_and_set` using
   * `memory_order_acquire`, ensuring all subsequent reads/writes by this
   * thread happen-after the corresponding `unlock()` release on whichever
   * thread previously held the lock.
   */
  void lock() noexcept {
    while (m_Flag.test_and_set(std::memory_order_acquire)) {
      // busy-wait
    }
  }

  /**
   * @brief Releases the lock.
   *
   * @details Clears the flag with `memory_order_release`, publishing all
   * writes made while the lock was held to the next thread that
   * successfully calls `lock()`.
   *
   * @pre The calling thread currently holds the lock (i.e. previously
   * called `lock()` without an intervening `unlock()`).
   */
  void unlock() noexcept {
    m_Flag.clear(std::memory_order_release);
  }

 private:
  /// Backing flag for the spinlock; `true` (set) means "locked".
  std::atomic_flag m_Flag = ATOMIC_FLAG_INIT;
};

}  // namespace tickguard
