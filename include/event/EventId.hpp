#pragma once

#include <cstdint>

namespace app::event {

/**
 * @brief Enumerates all monitored conditions supervised by
 *        `EventSupervisor`.
 *
 * @details Each value identifies one `EventDescriptor` registered with the
 * supervisor. To add a new event, append a new value **before** `Count`
 * (never reorder or remove existing values, since that would change the
 * underlying numeric IDs of unrelated events). After adding an ID here,
 * implement an `IEventSender<TValue>` for it and register a corresponding
 * `EventDescriptor<TValue>` via `EventSupervisor::registerDescriptor()`.
 */
enum class EventId : std::uint8_t {
  /// Link-state of Ethernet interface 0.
  ChannelLifeEthernet0,
  /// Link-state of Ethernet interface 1.
  ChannelLifeEthernet1,
  /// Link-state of Ethernet interface 2.
  ChannelLifeEthernet2,
  /// Link-state of Ethernet interface 3.
  ChannelLifeEthernet3,
  /// Liveness/sync state of the first monitored NTP source.
  NtpAlive1,
  /// Liveness/sync state of the second monitored NTP source.
  NtpAlive2,
  /// Sizing marker equal to the number of real event IDs above — not a
  /// real event; must always remain the last enumerator.
  Count
};

}  // namespace app::event
