#pragma once

#include <atomic>
#include <cstdint>

namespace tickguard {

/**
 * @brief Lock-free, Prometheus-friendly counters tracking one
 *        `EventDescriptor`'s activity.
 *
 * @details Intended to be exported as metrics (e.g. via a Prometheus
 * exporter). All counters are atomic and safe to read concurrently from
 * any thread while a descriptor's worker thread is recording updates —
 * no external synchronization is required.
 */
class EventMetrics {
 public:
  /**
   * @brief Increments the count of raw `trigger()` calls received.
   *
   * @details Called once per `trigger()` invocation on the owning
   * descriptor, regardless of whether the triggered value ends up being
   * sent, suppressed, or merely re-arms a timer.
   */
  void recordTrigger() noexcept {
    m_Triggered.fetch_add(1, std::memory_order_relaxed);
  }

  /**
   * @brief Increments the count of actual sends performed.
   *
   * @details Incremented on every `IEventSender::send()` call the owning
   * descriptor makes, including debounce-settled changes, OneShot firings,
   * initial snapshots, and periodic Heartbeat resends.
   */
  void recordRaised() noexcept {
    m_Raised.fetch_add(1, std::memory_order_relaxed);
  }

  /**
   * @brief Increments the count of debounce cycles that resolved back to
   *        the unchanged value.
   *
   * @details Incremented when a debounce cycle settles but the pending
   * value matches the already-sent image, so no `send()` was necessary.
   */
  void recordSuppressed() noexcept {
    m_Suppressed.fetch_add(1, std::memory_order_relaxed);
  }

  /**
   * @brief Returns the total number of `trigger()` calls recorded.
   *
   * @return Current value of the triggered counter.
   */
  [[nodiscard]] std::uint32_t triggered() const noexcept {
    return m_Triggered.load(std::memory_order_relaxed);
  }

  /**
   * @brief Returns the total number of `send()` calls recorded.
   *
   * @return Current value of the raised counter.
   */
  [[nodiscard]] std::uint32_t raised() const noexcept {
    return m_Raised.load(std::memory_order_relaxed);
  }

  /**
   * @brief Returns the total number of suppressed (unchanged) debounce
   *        settlements recorded.
   *
   * @return Current value of the suppressed counter.
   */
  [[nodiscard]] std::uint32_t suppressed() const noexcept {
    return m_Suppressed.load(std::memory_order_relaxed);
  }

 private:
  /// Count of raw `trigger()` calls received by the owning descriptor.
  std::atomic<std::uint32_t> m_Triggered{0};
  /// Count of actual `IEventSender::send()` calls performed.
  std::atomic<std::uint32_t> m_Raised{0};
  /// Count of debounce cycles that settled back to the unchanged value.
  std::atomic<std::uint32_t> m_Suppressed{0};
};

}  // namespace tickguard