#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <catch2/catch.hpp>

#include "app/event/EventDescriptor.hpp"
#include "app/event/EventSupervisor.hpp"

using namespace app::event;
using namespace std::chrono_literals;

namespace {

// Thread-safe sender: EventSupervisor's worker thread calls Send() while the
// test thread may inspect calls concurrently, so guard with a mutex.
class ThreadSafeSender final : public IEventSender<bool> {
 public:
  struct Call {
    EventId id;
    bool value;
  };

  void Send(EventId id, bool value) noexcept override {
    std::lock_guard<std::mutex> lock(mutex_);
    calls_.push_back({id, value});
  }

  [[nodiscard]] std::size_t Count() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return calls_.size();
  }

  [[nodiscard]] std::vector<Call> Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return calls_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<Call> calls_;
};

}  // namespace

TEST_CASE("EventSupervisor: EmitInitialSnapshot sends all registered events before Start", "[EventSupervisor]") {
  ThreadSafeSender sender;
  EventSupervisor supervisor(50ms);

  supervisor.Register(std::make_unique<EventDescriptor<bool>>(
      EventId::ChannelLifeEthernet0, EventConfig{EventMode::Interval, 1000ms, 5000ms}, sender, false));
  supervisor.Register(std::make_unique<EventDescriptor<bool>>(
      EventId::NtpAlive1, EventConfig{EventMode::OneShot, 1000ms, 1000ms}, sender, false));

  supervisor.EmitInitialSnapshot();

  REQUIRE(sender.Count() == 2);
}

TEST_CASE("EventSupervisor: Trigger from external thread eventually reaches sender via worker Tick",
          "[EventSupervisor]") {
  ThreadSafeSender sender;
  EventSupervisor supervisor(20ms);

  supervisor.Register(std::make_unique<EventDescriptor<bool>>(
      EventId::ChannelLifeEthernet0, EventConfig{EventMode::OneShot, 100ms, 1000ms}, sender, false));

  supervisor.Start();

  supervisor.Trigger(EventId::ChannelLifeEthernet0, EventValue{true});

  // Poll for up to ~1s; delay is 100ms plus tick granularity.
  bool received = false;
  for (int i = 0; i < 50 && !received; ++i) {
    std::this_thread::sleep_for(20ms);
    received = sender.Count() == 1;
  }

  supervisor.Stop();

  REQUIRE(received);
  auto calls = sender.Snapshot();
  REQUIRE(calls.size() == 1);
  REQUIRE(calls[0].value == true);
}

TEST_CASE("EventSupervisor: Interval event heartbeats repeatedly without further triggers", "[EventSupervisor]") {
  ThreadSafeSender sender;
  EventSupervisor supervisor(20ms);

  supervisor.Register(std::make_unique<EventDescriptor<bool>>(
      EventId::ChannelLifeEthernet1, EventConfig{EventMode::Interval, 50ms, 100ms}, sender, false));

  supervisor.Start();
  supervisor.Trigger(EventId::ChannelLifeEthernet1, EventValue{true});

  // Wait long enough for the initial send (delay=50ms) plus a couple of
  // heartbeat periods (Interval=100ms).
  std::this_thread::sleep_for(400ms);

  supervisor.Stop();

  // Expect at least the initial send plus 2 heartbeats (>=3), allowing some
  // scheduling slack rather than an exact count.
  REQUIRE(sender.Count() >= 3);
}

TEST_CASE("EventSupervisor: Register returns false once MaxEvents capacity is exceeded", "[EventSupervisor]") {
  ThreadSafeSender sender;
  EventSupervisor supervisor;

  // MaxEvents is 32 (implementation constant); push past it using distinct
  // EventId values is not required since Id() collisions are irrelevant here.
  bool allRegistered = true;
  for (int i = 0; i < 32; ++i) {
    allRegistered &= supervisor.Register(std::make_unique<EventDescriptor<bool>>(
        EventId::ChannelLifeEthernet0, EventConfig{EventMode::OneShot, 1000ms, 1000ms}, sender, false));
  }
  REQUIRE(allRegistered);

  const bool overflowRegistered = supervisor.Register(std::make_unique<EventDescriptor<bool>>(
      EventId::NtpAlive1, EventConfig{EventMode::OneShot, 1000ms, 1000ms}, sender, false));

  REQUIRE_FALSE(overflowRegistered);
  REQUIRE(supervisor.EventCount() == 32);
}
