#pragma once

#include <chrono>
#include <optional>

#include "app/event/EventId.hpp"
#include "app/event/EventValue.hpp"

namespace app::event {

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

  virtual void tick(std::chrono::milliseconds now) noexcept = 0;
  virtual void trigger(EventValue value) noexcept = 0;

  // Sends the current image immediately, bypassing delay/debounce.
  // Used for the initial startup snapshot.
  virtual void emitSnapshot() noexcept = 0;

  [[nodiscard]] virtual EventId id() const noexcept = 0;

  // Deadline (same clock/epoch as tick()'s `now`) at which this descriptor
  // next needs a tick() call to act, or std::nullopt if no timer is armed.
  // Lets the supervisor sleep precisely until the next actionable moment
  // instead of polling at a fixed period.
  [[nodiscard]] virtual std::optional<std::chrono::milliseconds> nextDeadline() const noexcept = 0;
};

}  // namespace app::event
