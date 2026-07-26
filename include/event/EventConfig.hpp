#pragma once

#include <chrono>
#include <cstdint>

namespace app::event {

/// @brief Defines how an event is reported once triggered.
enum class EventMode : std::uint8_t {
  OneShot,  ///< Report the event once after the configured delay, then stop.
  Interval  ///< Repeatedly report the event at a fixed Interval after the initial delay.
};

/// @brief Configuration parameters controlling the timing behavior of an event.
struct EventConfig {
  static constexpr std::chrono::milliseconds kDelay{0};         ///< Default delay before the event is first reported.
  static constexpr std::chrono::milliseconds kInterval{60000};  ///< Default interval between successive reports.

  /// @brief The reporting mode for the event (single-shot or repeating Interval).
  EventMode mode = EventMode::OneShot;

  /// @brief Delay before the event is first reported, starting from when it is triggered.
  std::chrono::milliseconds delay{kDelay};

  /// @brief Time between successive reports when `mode` is `EventMode::Interval`.
  /// Ignored when `mode` is `EventMode::OneShot`.
  std::chrono::milliseconds interval{kInterval};
};

}  // namespace app::event