#pragma once

#include <chrono>

#include "app/event/EventId.hpp"
#include "app/event/EventValue.hpp"

namespace app::event
{

class IEventDescriptor
{
public:
    virtual ~IEventDescriptor() = default;

    virtual void Tick(std::chrono::milliseconds now) noexcept = 0;
    virtual void Trigger(EventValue value) noexcept = 0;

    // Sends the current image immediately, bypassing delay/debounce.
    // Used for the initial startup snapshot.
    virtual void EmitSnapshot() noexcept = 0;

    [[nodiscard]] virtual EventId Id() const noexcept = 0;
};

} // namespace app::event
