#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <catch2/catch.hpp>

#include "app/event/EventDescriptor.hpp"
#include "app/event/EventSupervisor.hpp"

using app::event::EventConfig;
using app::event::EventDescriptor;
using app::event::EventId;
using app::event::EventMode;
using app::event::EventSupervisor;
using app::event::EventValue;
using app::event::IEventSender;
using namespace std::chrono_literals;

namespace {

// Thread-safe sender: EventSupervisor's worker thread calls send() while the
// test thread may inspect calls concurrently, so guard with a mutex.
class ThreadSafeSender final : public IEventSender<bool> {
 public:
  struct Call {
    EventId id;
    bool value;
  };

  void send(EventId id, bool value) noexcept override {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_Calls.push_back({id, value});
  }

  [[nodiscard]] std::size_t count() const noexcept {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Calls.size();
  }

  [[nodiscard]] std::vector<Call> snapshot() const {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Calls;
  }

 private:
  mutable std::mutex m_Mutex;
  std::vector<Call> m_Calls;
};

}  // namespace

TEST_CASE("EventSupervisor: emitInitialSnapshot sends all registered events before start", "[EventSupervisor]") {
  ThreadSafeSender sender;
  EventSupervisor supervisor(50ms);

  supervisor.registerDescriptor(std::make_unique<EventDescriptor<bool>>(
      EventId::ChannelLifeEthernet0, EventConfig{.mode = EventMode::Interval, .delay = 1000ms, .interval = 5000ms},
      sender, false));
  supervisor.registerDescriptor(std::make_unique<EventDescriptor<bool>>(
      EventId::NtpAlive1, EventConfig{.mode = EventMode::OneShot, .delay = 1000ms, .interval = 1000ms}, sender, false));

  supervisor.emitInitialSnapshot();

  REQUIRE(sender.count() == 2);
}

TEST_CASE("EventSupervisor: trigger from external thread eventually reaches sender via worker tick",
          "[EventSupervisor]") {
  ThreadSafeSender sender;
  EventSupervisor supervisor(20ms);

  supervisor.registerDescriptor(std::make_unique<EventDescriptor<bool>>(
      EventId::ChannelLifeEthernet0, EventConfig{.mode = EventMode::OneShot, .delay = 100ms, .interval = 1000ms},
      sender, false));

  supervisor.start();

  supervisor.trigger(EventId::ChannelLifeEthernet0, EventValue{true});

  // Poll for up to ~1s; delay is 100ms plus tick granularity.
  constexpr int kMaxPolls = 50;
  bool received = false;
  for (int i = 0; i < kMaxPolls && !received; ++i) {
    std::this_thread::sleep_for(20ms);
    received = sender.count() == 1;
  }

  supervisor.stop();

  REQUIRE(received);
  auto calls = sender.snapshot();
  REQUIRE(calls.size() == 1);
  REQUIRE(calls[0].value == true);
}

TEST_CASE("EventSupervisor: Interval event heartbeats repeatedly without further triggers", "[EventSupervisor]") {
  ThreadSafeSender sender;
  EventSupervisor supervisor(20ms);

  supervisor.registerDescriptor(std::make_unique<EventDescriptor<bool>>(
      EventId::ChannelLifeEthernet1, EventConfig{.mode = EventMode::Interval, .delay = 50ms, .interval = 100ms}, sender,
      false));

  supervisor.start();
  supervisor.trigger(EventId::ChannelLifeEthernet1, EventValue{true});

  // Wait long enough for the initial send (delay=50ms) plus a couple of
  // heartbeat periods (Interval=100ms).
  std::this_thread::sleep_for(400ms);

  supervisor.stop();

  // Expect at least the initial send plus 2 heartbeats (>=3), allowing some
  // scheduling slack rather than an exact count.
  REQUIRE(sender.count() >= 3);
}

TEST_CASE("EventSupervisor: registerDescriptor returns false once kMaxEvents capacity is exceeded",
          "[EventSupervisor]") {
  ThreadSafeSender sender;
  EventSupervisor supervisor;

  // Mirrors EventSupervisor's private kMaxEvents; push past it using distinct
  // EventId values is not required since id() collisions are irrelevant here.
  constexpr int kMaxEvents = 32;
  bool allRegistered = true;
  for (int i = 0; i < kMaxEvents; ++i) {
    allRegistered &= supervisor.registerDescriptor(std::make_unique<EventDescriptor<bool>>(
        EventId::ChannelLifeEthernet0, EventConfig{.mode = EventMode::OneShot, .delay = 1000ms, .interval = 1000ms},
        sender, false));
  }
  REQUIRE(allRegistered);

  const bool overflowRegistered = supervisor.registerDescriptor(std::make_unique<EventDescriptor<bool>>(
      EventId::NtpAlive1, EventConfig{.mode = EventMode::OneShot, .delay = 1000ms, .interval = 1000ms}, sender, false));

  REQUIRE_FALSE(overflowRegistered);
  REQUIRE(supervisor.eventCount() == kMaxEvents);
}
