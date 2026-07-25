#pragma once

#include <atomic>
#include <cstdint>

namespace app::event
{

// Prometheus-friendly counters. Safe to read concurrently from any thread.
class EventMetrics
{
public:
    void RecordTrigger() noexcept { triggered_.fetch_add(1, std::memory_order_relaxed); }
    void RecordRaised() noexcept { raised_.fetch_add(1, std::memory_order_relaxed); }
    void RecordSuppressed() noexcept { suppressed_.fetch_add(1, std::memory_order_relaxed); }

    [[nodiscard]] std::uint32_t Triggered() const noexcept { return triggered_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint32_t Raised() const noexcept { return raised_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint32_t Suppressed() const noexcept { return suppressed_.load(std::memory_order_relaxed); }

private:
    std::atomic<std::uint32_t> triggered_{0};
    std::atomic<std::uint32_t> raised_{0};
    std::atomic<std::uint32_t> suppressed_{0};
};

} // namespace app::event
