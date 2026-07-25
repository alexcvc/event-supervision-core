#pragma once

#include <atomic>

namespace app::event
{

// Lightweight lock for short critical sections guarding a single
// EventDescriptor's internal state. Not intended for long-held locks.
class SpinLock
{
public:
    void lock() noexcept
    {
        while (flag_.test_and_set(std::memory_order_acquire))
        {
            // busy-wait
        }
    }

    void unlock() noexcept { flag_.clear(std::memory_order_release); }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

} // namespace app::event
