#pragma once

#include <atomic>
#include <cstdint>

namespace app::event {

// Prometheus-friendly counters. Safe to read concurrently from any thread.
class EventMetrics {
 public:
  void recordTrigger() noexcept {
    m_Triggered.fetch_add(1, std::memory_order_relaxed);
  }
  void recordRaised() noexcept {
    m_Raised.fetch_add(1, std::memory_order_relaxed);
  }
  void recordSuppressed() noexcept {
    m_Suppressed.fetch_add(1, std::memory_order_relaxed);
  }

  [[nodiscard]] std::uint32_t triggered() const noexcept {
    return m_Triggered.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint32_t raised() const noexcept {
    return m_Raised.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint32_t suppressed() const noexcept {
    return m_Suppressed.load(std::memory_order_relaxed);
  }

 private:
  std::atomic<std::uint32_t> m_Triggered{0};
  std::atomic<std::uint32_t> m_Raised{0};
  std::atomic<std::uint32_t> m_Suppressed{0};
};

}  // namespace app::event
