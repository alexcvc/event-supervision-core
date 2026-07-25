#pragma once

#include <cstdint>

namespace app::event
{

enum class EventId : std::uint16_t
{
    ChannelLifeEthernet0,
    ChannelLifeEthernet1,
    ChannelLifeEthernet2,
    ChannelLifeEthernet3,
    NtpAlive1,
    NtpAlive2,
    Count // marker, not a real event
};

} // namespace app::event
