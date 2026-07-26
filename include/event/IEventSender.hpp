#pragma once

#include "event/EventId.hpp"

namespace app::event {

/**
 * @brief Transport-facing interface for delivering event notifications.
 *
 * @tparam TValue Payload type carried by this sender's events (must match
 * the `TValue` of the `EventDescriptor<TValue>` that holds a pointer to
 * this sender).
 *
 * @details Owns the knowledge of "where does this event go" — e.g. a
 * wire/serial protocol frame to a remote controller, an IPC message, a log
 * sink, etc. The event/descriptor itself never knows about the receiver;
 * it only calls `send()` on this interface. Implement this for each
 * transport an application needs and pass a pointer to it when
 * constructing an `EventDescriptor<TValue>`.
 *
 * @note `EventDescriptor` holds a non-owning raw pointer to an
 * `IEventSender<TValue>` — implementations of this interface **must**
 * outlive every descriptor that references them.
 */
template <typename TValue>
class IEventSender {
 public:
  IEventSender() = default;
  virtual ~IEventSender() = default;

  // Non-copyable/non-movable: used polymorphically through a base reference,
  // so slicing must be prevented outright.
  IEventSender(const IEventSender&) = delete;
  IEventSender& operator=(const IEventSender&) = delete;
  IEventSender(IEventSender&&) = delete;
  IEventSender& operator=(IEventSender&&) = delete;

  /**
   * @brief Delivers an event to the underlying transport.
   *
   * @param id    Identifier of the event being reported.
   * @param value Current payload value associated with the event.
   *
   * @details Called by an `EventDescriptor<TValue>` whenever it decides an
   * event must be raised — this includes initial snapshots
   * (`emitInitialSnapshot()`), debounce-settled changes, OneShot firings,
   * and periodic Heartbeat resends. Implementations must be safe to invoke
   * from the `EventSupervisor` worker thread and should not block for long
   * periods, since they may be called while a descriptor's `SpinLock` is
   * held or on the time-critical tick path.
   *
   * @note Marked `noexcept` — implementations must not throw; handle and
   * suppress/report transport errors internally instead.
   */
  virtual void send(EventId id, TValue value) noexcept = 0;
};

}  // namespace app::event