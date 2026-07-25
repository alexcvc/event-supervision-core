#pragma once

#include "app/event/EventId.hpp"

namespace app::event {

// Owns the knowledge of "where does this event go" — the event/descriptor
// itself never knows about the receiver.
template <typename TValue>
class IEventSender {
 public:
  virtual ~IEventSender() = default;
  virtual void Send(EventId id, TValue value) noexcept = 0;
};

}  // namespace app::event
