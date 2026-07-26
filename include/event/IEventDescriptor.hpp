#pragma once

#include <chrono>
#include <optional>

#include "event/EventId.hpp"
#include "event/EventValue.hpp"

namespace app::event {

/**
 * @brief Non-template interface for a single supervised event's timing
 *        state machine.
 *
 * @details Exists so `EventSupervisor` can hold a heterogeneous
 * `std::vector<std::unique_ptr<IEventDescriptor>>` regardless of each
 * concrete `EventDescriptor<TValue>`'s payload type. A descriptor owns all
 * debounce/heartbeat timing state for one event but never knows its
 * receiver — it only reports outcomes via its associated `IEventSender`.
 */
class IEventDescriptor {
 public:
  IEventDescriptor() = default;
  virtual ~IEventDescriptor() = default;

  // Non-copyable/non-movable: used polymorphically through a base pointer,
  // so slicing must be prevented outright.
  IEventDescriptor(const IEventDescriptor&) = delete;
  IEventDescriptor& operator=(const IEventDescriptor&) = delete;
  IEventDescriptor(IEventDescriptor&&) = delete;
  IEventDescriptor& operator=(IEventDescriptor&&) = delete;

  /**
   * @brief Advances this descriptor's timing state machine to the given
   *        point in time, acting (sending) if a deadline has elapsed.
   *
   * @param now Current time, expressed on the same clock/epoch used
   * throughout the supervisor (not necessarily wall-clock time — tests
   * inject arbitrary values directly).
   *
   * @details Called by `EventSupervisor`'s worker thread on every
   * descriptor each time it wakes up. Depending on the descriptor's mode
   * and phase, this may fire a OneShot event, settle a debounce cycle, or
   * resend a Heartbeat image.
   */
  virtual void tick(std::chrono::milliseconds now) noexcept = 0;

  /**
   * @brief Reports a new raw value for this event from its condition
   *        source.
   *
   * @param value Type-erased payload; must hold the alternative matching
   * the concrete descriptor's `TValue`, or accessing it is a hard crash.
   *
   * @details Thread-safe — may be called concurrently with `tick()` and
   * from any thread (e.g. a netlink callback or NTP client). Updates the
   * pending value and (re-)arms the appropriate timer depending on
   * `EventMode`; for `Interval` mode with `delay == 0`, this may send
   * synchronously before returning.
   */
  virtual void trigger(EventValue value) noexcept = 0;

  /**
   * @brief Sends the current image immediately, bypassing delay/debounce.
   *
   * @details Used for the initial startup snapshot, letting a freshly
   * (re)started supervisor inform the receiver of every event's current
   * state without waiting for a change or a debounce/heartbeat cycle.
   * Must only be called before `EventSupervisor::start()`.
   */
  virtual void emitSnapshot() noexcept = 0;

  /**
   * @brief Returns the identifier of the event this descriptor manages.
   *
   * @return The `EventId` this descriptor was constructed with.
   */
  [[nodiscard]] virtual EventId id() const noexcept = 0;

  /**
   * @brief Reports when this descriptor next needs a `tick()` call to act.
   *
   * @return The deadline (same clock/epoch as `tick()`'s `now`) at which
   * this descriptor should next be ticked, or `std::nullopt` if no timer
   * is currently armed.
   *
   * @details Lets `EventSupervisor` sleep precisely until the earliest
   * deadline reported by any registered descriptor instead of polling at a
   * fixed period — `tickPeriod` is only used as an idle-poll fallback when
   * nothing is armed.
   */
  [[nodiscard]] virtual std::optional<std::chrono::milliseconds> nextDeadline() const noexcept = 0;
};

}  // namespace app::event