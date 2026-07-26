#define CATCH_CONFIG_MAIN
#include <vector>

#include <catch2/catch.hpp>

#include "tickguard/EventDescriptor.hpp"

using tickguard::EventConfig;
using tickguard::EventDescriptor;
using tickguard::EventId;
using tickguard::EventMode;
using tickguard::EventValue;
using tickguard::IEventSender;
using namespace std::chrono_literals;

namespace {

class FakeSender final : public IEventSender<bool> {
 public:
  struct Call {
    EventId id;
    bool value;
  };

  void send(EventId id, bool value) noexcept override {
    calls.push_back({id, value});
  }

  std::vector<Call> calls;
};

}  // namespace

TEST_CASE("Initial image is false and no send happens before first tick", "[EventDescriptor]") {
  FakeSender sender;
  const EventDescriptor<bool> event(EventId::ChannelLifeEthernet0,
                                    EventConfig{.mode = EventMode::OneShot, .delay = 5000ms, .interval = 1000ms},
                                    sender, /*initial=*/false);

  REQUIRE(event.image() == false);
  REQUIRE(sender.calls.empty());
}

TEST_CASE("emitSnapshot sends current image immediately, bypassing delay", "[EventDescriptor]") {
  FakeSender sender;
  EventDescriptor<bool> event(EventId::NtpAlive1,
                              EventConfig{.mode = EventMode::OneShot, .delay = 10000ms, .interval = 1000ms}, sender,
                              /*initial=*/false);

  event.emitSnapshot();

  REQUIRE(sender.calls.size() == 1);
  REQUIRE(sender.calls[0].id == EventId::NtpAlive1);
  REQUIRE(sender.calls[0].value == false);
}

TEST_CASE("OneShot fires exactly once after delay elapses with no intervening trigger", "[EventDescriptor]") {
  FakeSender sender;
  EventDescriptor<bool> event(EventId::NtpAlive1,
                              EventConfig{.mode = EventMode::OneShot, .delay = 10000ms, .interval = 1000ms}, sender,
                              /*initial=*/false);

  event.trigger(EventValue{true});

  event.tick(0ms);
  REQUIRE(sender.calls.empty());

  event.tick(9999ms);
  REQUIRE(sender.calls.empty());

  event.tick(10000ms);
  REQUIRE(sender.calls.size() == 1);
  REQUIRE(sender.calls[0].value == true);

  // Further ticks must not re-fire (OneShot).
  event.tick(20000ms);
  REQUIRE(sender.calls.size() == 1);
}

TEST_CASE("OneShot debounce: rapid flapping within delay window suppresses event if value returns to image",
          "[EventDescriptor]") {
  FakeSender sender;
  EventDescriptor<bool> event(EventId::ChannelLifeEthernet0,
                              EventConfig{.mode = EventMode::OneShot, .delay = 10000ms, .interval = 1000ms}, sender,
                              /*initial=*/true);

  event.trigger(EventValue{false});  // armedAt = 0 + 10000ms
  event.tick(2000ms);

  event.trigger(EventValue{true});  // rearm at 2000 + 10000 = 12000ms
  event.tick(11000ms);
  REQUIRE(sender.calls.empty());

  event.tick(12000ms);
  // pending (true) == image (true) -> suppressed, no send.
  REQUIRE(sender.calls.empty());
}

TEST_CASE("OneShot debounce: change that persists past delay is sent", "[EventDescriptor]") {
  FakeSender sender;
  EventDescriptor<bool> event(EventId::ChannelLifeEthernet0,
                              EventConfig{.mode = EventMode::OneShot, .delay = 10000ms, .interval = 1000ms}, sender,
                              /*initial=*/true);

  event.trigger(EventValue{false});
  event.tick(9999ms);
  REQUIRE(sender.calls.empty());

  event.tick(10000ms);
  REQUIRE(sender.calls.size() == 1);
  REQUIRE(sender.calls[0].value == false);
}

TEST_CASE("Interval with delay==0 fires immediately on trigger if value changed", "[EventDescriptor]") {
  FakeSender sender;
  EventDescriptor<bool> event(EventId::ChannelLifeEthernet1,
                              EventConfig{.mode = EventMode::Interval, .delay = 0ms, .interval = 30000ms}, sender,
                              /*initial=*/false);

  event.trigger(EventValue{true});

  REQUIRE(sender.calls.size() == 1);
  REQUIRE(sender.calls[0].value == true);
}

TEST_CASE("Interval with delay==0 does not resend on trigger if value unchanged", "[EventDescriptor]") {
  FakeSender sender;
  EventDescriptor<bool> event(EventId::ChannelLifeEthernet1,
                              EventConfig{.mode = EventMode::Interval, .delay = 0ms, .interval = 30000ms}, sender,
                              /*initial=*/false);

  event.trigger(EventValue{false});  // same as initial

  REQUIRE(sender.calls.empty());
}

TEST_CASE("Interval sends heartbeat unconditionally on each period, even without new trigger", "[EventDescriptor]") {
  FakeSender sender;
  EventDescriptor<bool> event(EventId::ChannelLifeEthernet0,
                              EventConfig{.mode = EventMode::Interval, .delay = 10000ms, .interval = 30000ms}, sender,
                              /*initial=*/false);

  event.trigger(EventValue{true});
  event.tick(10000ms);
  REQUIRE(sender.calls.size() == 1);
  REQUIRE(sender.calls[0].value == true);

  // No new trigger for a long time, but heartbeat must keep firing every Interval.
  event.tick(40000ms);
  REQUIRE(sender.calls.size() == 2);
  REQUIRE(sender.calls[1].value == true);  // same value, still resent (controller-reset protection)

  event.tick(70000ms);
  REQUIRE(sender.calls.size() == 3);
}

TEST_CASE("Interval: cable flap shorter than delay is absorbed, no event sent", "[EventDescriptor]") {
  FakeSender sender;
  EventDescriptor<bool> event(EventId::ChannelLifeEthernet2,
                              EventConfig{.mode = EventMode::Interval, .delay = 10000ms, .interval = 30000ms}, sender,
                              /*initial=*/true);

  event.trigger(EventValue{false});  // cable pulled
  event.tick(2000ms);

  event.trigger(EventValue{true});  // cable reinserted 2s later -> rearm at 2000+10000=12000
  event.tick(11999ms);
  REQUIRE(sender.calls.empty());

  event.tick(12000ms);
  REQUIRE(sender.calls.empty());  // pending==image==true -> absorbed
}

TEST_CASE("Interval: cable down longer than delay produces a false event", "[EventDescriptor]") {
  FakeSender sender;
  EventDescriptor<bool> event(EventId::ChannelLifeEthernet3,
                              EventConfig{.mode = EventMode::Interval, .delay = 10000ms, .interval = 30000ms}, sender,
                              /*initial=*/true);

  event.trigger(EventValue{false});  // cable pulled, stays down
  event.tick(9999ms);
  REQUIRE(sender.calls.empty());

  event.tick(10000ms);
  REQUIRE(sender.calls.size() == 1);
  REQUIRE(sender.calls[0].value == false);
}

TEST_CASE("nextDeadline reflects the armed/disarmed state across OneShot and Interval", "[EventDescriptor]") {
  FakeSender sender;
  EventDescriptor<bool> oneShot(EventId::NtpAlive1,
                                EventConfig{.mode = EventMode::OneShot, .delay = 5000ms, .interval = 1000ms}, sender,
                                /*initial=*/false);

  REQUIRE_FALSE(oneShot.nextDeadline().has_value());  // never triggered: no timer armed

  oneShot.trigger(EventValue{true});
  REQUIRE(oneShot.nextDeadline() == std::optional{5000ms});

  oneShot.tick(5000ms);  // OneShot fires and disarms
  REQUIRE_FALSE(oneShot.nextDeadline().has_value());

  EventDescriptor<bool> interval(EventId::ChannelLifeEthernet0,
                                 EventConfig{.mode = EventMode::Interval, .delay = 1000ms, .interval = 3000ms},
                                 sender, /*initial=*/false);

  interval.trigger(EventValue{true});
  REQUIRE(interval.nextDeadline() == std::optional{1000ms});

  interval.tick(1000ms);  // settles into Heartbeat, re-arms +interval
  REQUIRE(interval.nextDeadline() == std::optional{4000ms});

  interval.tick(4000ms);  // heartbeat fires again, still armed forever
  REQUIRE(interval.nextDeadline() == std::optional{7000ms});
}

TEST_CASE("Metrics track triggered, raised, and suppressed counts", "[EventDescriptor][Metrics]") {
  FakeSender sender;
  EventDescriptor<bool> event(EventId::ChannelLifeEthernet0,
                              EventConfig{.mode = EventMode::OneShot, .delay = 1000ms, .interval = 1000ms}, sender,
                              /*initial=*/true);

  event.trigger(EventValue{false});  // triggered: 1
  event.tick(1000ms);                // raised: 1 (value changed)

  REQUIRE(event.metrics().triggered() == 1);
  REQUIRE(event.metrics().raised() == 1);
  REQUIRE(event.metrics().suppressed() == 0);
}
