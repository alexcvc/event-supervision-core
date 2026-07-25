#pragma once

#include "app/event/EventId.hpp"

namespace app::event {

// Owns the knowledge of "where does this event go" — the event/descriptor
// itself never knows about the receiver.
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

  virtual void send(EventId id, TValue value) noexcept = 0;
};

}  // namespace app::event
