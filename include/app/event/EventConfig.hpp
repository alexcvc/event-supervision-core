#pragma once

#include <chrono>
#include <cstdint>

namespace app::event
{

enum class EventMode : std::uint8_t
{
    oneShot,
    interval
};

struct EventConfig
{
    EventMode                 mode = EventMode::oneShot;
    std::chrono::milliseconds delay{0};
    std::chrono::milliseconds interval{1000};
};

} // namespace app::event
