#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <variant>

#include "app/event/EventConfig.hpp"
#include "app/event/EventId.hpp"
#include "app/event/EventMetrics.hpp"
#include "app/event/EventValue.hpp"
#include "app/event/IEventDescriptor.hpp"
#include "app/event/IEventSender.hpp"
#include "app/event/SpinLock.hpp"

namespace app::event {

/// @brief Owns timing/debounce logic for a single monitored condition.
///
/// Does NOT know its receiver (that's IEventSender's job) and does NOT
/// decide what "true" means for the underlying condition — callers of
/// Trigger() own that decision.
///
/// @tparam TValue The payload type carried by this event (e.g. bool, int).
template <typename TValue>
class EventDescriptor final : public IEventDescriptor {
 public:
  /// @brief Constructs an event descriptor bound to a fixed id, config, and sender.
  /// @param id      Identifier of the monitored event.
  /// @param config  Timing configuration (mode, delay, Interval).
  /// @param sender  Non-owning reference to the object that will receive Send() calls.
  ///                Must outlive this EventDescriptor.
  /// @param initial Initial value used as both the current image and pending value.
  EventDescriptor(EventId id, EventConfig config, IEventSender<TValue>& sender, TValue initial) noexcept
      : m_id_(id), config_(config), sender_(&sender), image_(initial), pending_(initial) {}

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
  void Trigger(EventValue value) noexcept override {
    const std::scoped_lock<SpinLock> lock(spin_);
    pending_ = std::get<TValue>(value);
    metrics_.RecordTrigger();

    const bool immediate = config_.mode == EventMode::Interval && config_.delay == std::chrono::milliseconds{0};

    if (immediate) {
      FireIfChangedLocked();
      armedAt_ = lastNow_ + config_.interval;
      phase_ = Phase::Heartbeat;
      return;
    }

    // delay > 0 (OneShot or Interval): every real trigger re-enters the
    // debounce phase and (re)arms the delay timer, even if we were
    // already in the Heartbeat phase.
    armedAt_ = lastNow_ + config_.delay;
    phase_ = Phase::Debounce;
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
  void Tick(std::chrono::milliseconds now) noexcept override {
    const std::scoped_lock<SpinLock> lock(spin_);
    lastNow_ = now;

    if (!armedAt_.has_value() || now < *armedAt_) {
      return;
    }

    if (config_.mode == EventMode::Interval && phase_ == Phase::Heartbeat) {
      // Heartbeat: unconditional resend of the current image.
      // Protects the remote controller against losing state on its
      // own reset — it will get the current value again within one
      // Interval, without needing a new physical trigger.
      sender_->Send(m_id_, image_);
      metrics_.RecordRaised();
      armedAt_ = now + config_.interval;
    } else {
      // Debounce phase (or OneShot): only send if the value actually
      // changed since the last sent image. This is what absorbs short
      // flapping that resolves back to the prior state within delay.
      FireIfChangedLocked();

      if (config_.mode == EventMode::Interval) {
        armedAt_ = now + config_.interval;
        phase_ = Phase::Heartbeat;
      } else {
        armedAt_.reset();
      }
    }
  }

  /// @brief Immediately sends the current image, bypassing delay/debounce.
  ///
  /// Intended for emitting an initial snapshot of state before normal
  /// triggering begins. Thread-safe: guarded internally by a spin lock.
  void EmitSnapshot() noexcept override {
    const std::scoped_lock<SpinLock> lock(spin_);
    sender_->Send(m_id_, image_);
    metrics_.RecordRaised();
  }

  /// @brief Returns the identifier of the monitored event.
  [[nodiscard]] EventId Id() const noexcept override {
    return m_id_;
  }

  /// @brief Returns the last value that was actually sent to the receiver.
  [[nodiscard]] TValue image() const noexcept {
    const std::scoped_lock<SpinLock> lock(spin_);
    return image_;
  }

  /// @brief Returns the metrics counters (triggered/raised/suppressed) for this descriptor.
  [[nodiscard]] const EventMetrics& Metrics() const noexcept {
    return metrics_;
  }

 private:
  /// @brief Internal timing phase of the descriptor.
  ///
  /// - Debounce: waiting to see if a triggered change persists past delay.
  /// - Heartbeat: periodic unconditional resend of the current image
  ///   (Interval mode only, entered after the first debounce cycle settles).
  enum class Phase { Debounce, Heartbeat };

  /// @brief Sends the pending value if it differs from the current image.
  ///
  /// On change: updates `image_` to `pending_`, sends it, and records a
  /// "raised" metric. On no change: records a "suppressed" metric instead.
  /// Must be called while `spin_` is held.
  void FireIfChangedLocked() noexcept {
    if (pending_ != image_) {
      image_ = pending_;
      sender_->Send(m_id_, image_);
      metrics_.RecordRaised();
    } else {
      metrics_.RecordSuppressed();
    }
  }

  EventId m_id_;                                       ///< Identifier of the monitored event.
  EventConfig config_;                                ///< Timing configuration (mode, delay, Interval).
  IEventSender<TValue>* sender_;                      ///< Non-owning pointer to the event receiver.
  TValue image_;                                      ///< Last value actually sent to the receiver.
  TValue pending_;                                    ///< Latest raw value reported via Trigger().
  std::optional<std::chrono::milliseconds> armedAt_;  ///< Deadline at which the next timer action fires, if armed.
  std::chrono::milliseconds lastNow_{0};              ///< Most recent time seen via Tick().
  EventMetrics metrics_;                              ///< Triggered/raised/suppressed counters.
  mutable SpinLock spin_;                             ///< Guards all mutable state above.
  Phase phase_{Phase::Debounce};                      ///< Current timing phase (Debounce or Heartbeat).
};

}  // namespace app::event