#pragma once

#include <chrono>
#include <exception>
#include <mutex>
#include <optional>
#include <variant>

#include "event/EventConfig.hpp"
#include "event/EventId.hpp"
#include "event/EventMetrics.hpp"
#include "event/EventValue.hpp"
#include "event/IEventDescriptor.hpp"
#include "event/IEventSender.hpp"
#include "event/SpinLock.hpp"

namespace app::event {

/// @brief Owns timing/debounce logic for a single monitored condition.
///
/// Does NOT know its receiver (that's IEventSender's job) and does NOT
/// decide what "true" means for the underlying condition — callers of
/// trigger() own that decision.
///
/// @tparam TValue The payload type carried by this event (e.g. bool, int).
template <typename TValue>
class EventDescriptor final : public IEventDescriptor {
 public:
  /// @brief Constructs an event descriptor bound to a fixed id, config, and sender.
  /// @param id      Identifier of the monitored event.
  /// @param config  Timing configuration (mode, delay, Interval).
  /// @param sender  Non-owning reference to the object that will receive send() calls.
  ///                Must outlive this EventDescriptor.
  /// @param initial Initial value used as both the current image and pending value.
  EventDescriptor(EventId id, const EventConfig& config, IEventSender<TValue>& sender, TValue initial) noexcept
      : m_Id(id), m_Config(config), m_Sender(&sender), m_Image(initial), m_Pending(initial) {}

  /// @brief Reports a raw change in the underlying condition.
  ///
  /// Called from any thread when the underlying raw condition changes.
  /// Thread-safe: guarded internally by a spin lock.
  ///
  /// Behavior:
  /// - If mode is `Interval` and `delay == 0`, the value is applied and sent
  ///   immediately (if changed) and the descriptor enters the Heartbeat phase.
  /// - Otherwise (delay > 0, either `OneShot` or `Interval`), every trigger
  ///   re-enters the Debounce phase and (re)arms the delay timer, even if the
  ///   descriptor was already in the Heartbeat phase.
  ///
  /// @param value New raw value, type-erased; must hold a `TValue` alternative.
  void trigger(EventValue value) noexcept override {
    const std::scoped_lock<SpinLock> lock(m_Spin);
    try {
      m_Pending = std::get<TValue>(value);
    } catch (const std::bad_variant_access&) {
      std::terminate();
    }
    m_Metrics.recordTrigger();

    const bool immediate = m_Config.mode == EventMode::Interval && m_Config.delay == std::chrono::milliseconds{0};

    if (immediate) {
      fireIfChangedLocked();
      m_ArmedAt = m_LastNow + m_Config.interval;
      m_Phase = Phase::Heartbeat;
      return;
    }

    // delay > 0 (OneShot or Interval): every real trigger re-enters the
    // debounce phase and (re)arms the delay timer, even if we were
    // already in the Heartbeat phase.
    m_ArmedAt = m_LastNow + m_Config.delay;
    m_Phase = Phase::Debounce;
  }

  /// @brief Advances the descriptor's internal clock and fires timers as needed.
  ///
  /// Called periodically by the supervisor thread. Thread-safe: guarded
  /// internally by a spin lock.
  ///
  /// Behavior:
  /// - No-op if no timer is armed or the armed deadline has not yet elapsed.
  /// - In Heartbeat phase (`Interval` mode only): unconditionally resends the
  ///   current image and re-arms the timer for another `Interval`.
  /// - Otherwise (Debounce phase, or `OneShot`): sends only if the pending
  ///   value differs from the current image, then either transitions to
  ///   Heartbeat (Interval mode) or disarms the timer (OneShot mode).
  ///
  /// @param now Current time, expressed as milliseconds since an external epoch.
  void tick(std::chrono::milliseconds now) noexcept override {
    const std::scoped_lock<SpinLock> lock(m_Spin);
    m_LastNow = now;

    if (!m_ArmedAt.has_value() || now < *m_ArmedAt) {
      return;
    }

    if (m_Config.mode == EventMode::Interval && m_Phase == Phase::Heartbeat) {
      // Heartbeat: unconditional resend of the current image.
      // Protects the remote controller against losing state on its
      // own reset — it will get the current value again within one
      // Interval, without needing a new physical trigger.
      m_Sender->send(m_Id, m_Image);
      m_Metrics.recordRaised();
      m_ArmedAt = now + m_Config.interval;
    } else {
      // Debounce phase (or OneShot): only send if the value actually
      // changed since the last sent image. This is what absorbs short
      // flapping that resolves back to the prior state within delay.
      fireIfChangedLocked();

      if (m_Config.mode == EventMode::Interval) {
        m_ArmedAt = now + m_Config.interval;
        m_Phase = Phase::Heartbeat;
      } else {
        m_ArmedAt.reset();
      }
    }
  }

  /// @brief Immediately sends the current image, bypassing delay/debounce.
  ///
  /// Intended for emitting an initial snapshot of state before normal
  /// triggering begins. Thread-safe: guarded internally by a spin lock.
  void emitSnapshot() noexcept override {
    const std::scoped_lock<SpinLock> lock(m_Spin);
    m_Sender->send(m_Id, m_Image);
    m_Metrics.recordRaised();
  }

  /// @brief Returns the identifier of the monitored event.
  [[nodiscard]] EventId id() const noexcept override {
    return m_Id;
  }

  /// @brief Returns the deadline at which this descriptor next needs a tick() call, if any.
  [[nodiscard]] std::optional<std::chrono::milliseconds> nextDeadline() const noexcept override {
    const std::scoped_lock<SpinLock> lock(m_Spin);
    return m_ArmedAt;
  }

  /// @brief Returns the last value that was actually sent to the receiver.
  [[nodiscard]] TValue image() const noexcept {
    const std::scoped_lock<SpinLock> lock(m_Spin);
    return m_Image;
  }

  /// @brief Returns the metrics counters (triggered/raised/suppressed) for this descriptor.
  [[nodiscard]] const EventMetrics& metrics() const noexcept {
    return m_Metrics;
  }

 private:
  /// @brief Internal timing phase of the descriptor.
  ///
  /// - Debounce: waiting to see if a triggered change persists past delay.
  /// - Heartbeat: periodic unconditional resend of the current image
  ///   (Interval mode only, entered after the first debounce cycle settles).
  enum class Phase : std::uint8_t { Debounce, Heartbeat };

  /// @brief Sends the pending value if it differs from the current image.
  ///
  /// On change: updates `m_Image` to `m_Pending`, sends it, and records a
  /// "raised" metric. On no change: records a "suppressed" metric instead.
  /// Must be called while `m_Spin` is held.
  void fireIfChangedLocked() noexcept {
    if (m_Pending != m_Image) {
      m_Image = m_Pending;
      m_Sender->send(m_Id, m_Image);
      m_Metrics.recordRaised();
    } else {
      m_Metrics.recordSuppressed();
    }
  }

  EventId m_Id;                                        ///< Identifier of the monitored event.
  EventConfig m_Config;                                ///< Timing configuration (mode, delay, Interval).
  IEventSender<TValue>* m_Sender;                      ///< Non-owning pointer to the event receiver.
  TValue m_Image;                                      ///< Last value actually sent to the receiver.
  TValue m_Pending;                                    ///< Latest raw value reported via trigger().
  std::optional<std::chrono::milliseconds> m_ArmedAt;  ///< Deadline at which the next timer action fires, if armed.
  std::chrono::milliseconds m_LastNow{0};              ///< Most recent time seen via tick().
  EventMetrics m_Metrics;                              ///< Triggered/raised/suppressed counters.
  mutable SpinLock m_Spin;                             ///< Guards all mutable state above.
  Phase m_Phase{Phase::Debounce};                      ///< Current timing phase (Debounce or Heartbeat).
};

}  // namespace app::event
