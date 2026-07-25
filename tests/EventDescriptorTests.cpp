#define CATCH_CONFIG_MAIN
#include <vector>

#include <catch2/catch.hpp>

#include "app/event/EventDescriptor.hpp"

using namespace app::event;
using namespace std::chrono_literals;

namespace
{

class FakeSender final : public IEventSender<bool>
{
public:
    struct Call
    {
        EventId id;
        bool    value;
    };

    void Send(EventId id, bool value) noexcept override { calls.push_back({id, value}); }

    std::vector<Call> calls;
};

} // namespace

TEST_CASE("Initial image is false and no send happens before first Tick", "[EventDescriptor]")
{
    FakeSender            sender;
    EventDescriptor<bool> ev(EventId::ChannelLifeEthernet0, EventConfig{EventMode::oneShot, 5000ms, 1000ms}, sender,
                             /*initial=*/false);

    REQUIRE(ev.Image() == false);
    REQUIRE(sender.calls.empty());
}

TEST_CASE("EmitSnapshot sends current image immediately, bypassing delay", "[EventDescriptor]")
{
    FakeSender            sender;
    EventDescriptor<bool> ev(EventId::NtpAlive1, EventConfig{EventMode::oneShot, 10000ms, 1000ms}, sender,
                             /*initial=*/false);

    ev.EmitSnapshot();

    REQUIRE(sender.calls.size() == 1);
    REQUIRE(sender.calls[0].id == EventId::NtpAlive1);
    REQUIRE(sender.calls[0].value == false);
}

TEST_CASE("oneShot fires exactly once after delay elapses with no intervening trigger", "[EventDescriptor]")
{
    FakeSender            sender;
    EventDescriptor<bool> ev(EventId::NtpAlive1, EventConfig{EventMode::oneShot, 10000ms, 1000ms}, sender,
                             /*initial=*/false);

    ev.Trigger(EventValue{true});

    ev.Tick(0ms);
    REQUIRE(sender.calls.empty());

    ev.Tick(9999ms);
    REQUIRE(sender.calls.empty());

    ev.Tick(10000ms);
    REQUIRE(sender.calls.size() == 1);
    REQUIRE(sender.calls[0].value == true);

    // Further ticks must not re-fire (oneShot).
    ev.Tick(20000ms);
    REQUIRE(sender.calls.size() == 1);
}

TEST_CASE("oneShot debounce: rapid flapping within delay window suppresses event if value returns to image",
          "[EventDescriptor]")
{
    FakeSender            sender;
    EventDescriptor<bool> ev(EventId::ChannelLifeEthernet0, EventConfig{EventMode::oneShot, 10000ms, 1000ms}, sender,
                             /*initial=*/true);

    ev.Trigger(EventValue{false}); // armedAt = 0 + 10000ms
    ev.Tick(2000ms);

    ev.Trigger(EventValue{true}); // rearm at 2000 + 10000 = 12000ms
    ev.Tick(11000ms);
    REQUIRE(sender.calls.empty());

    ev.Tick(12000ms);
    // pending (true) == image (true) -> suppressed, no send.
    REQUIRE(sender.calls.empty());
}

TEST_CASE("oneShot debounce: change that persists past delay is sent", "[EventDescriptor]")
{
    FakeSender            sender;
    EventDescriptor<bool> ev(EventId::ChannelLifeEthernet0, EventConfig{EventMode::oneShot, 10000ms, 1000ms}, sender,
                             /*initial=*/true);

    ev.Trigger(EventValue{false});
    ev.Tick(9999ms);
    REQUIRE(sender.calls.empty());

    ev.Tick(10000ms);
    REQUIRE(sender.calls.size() == 1);
    REQUIRE(sender.calls[0].value == false);
}

TEST_CASE("interval with delay==0 fires immediately on Trigger if value changed", "[EventDescriptor]")
{
    FakeSender            sender;
    EventDescriptor<bool> ev(EventId::ChannelLifeEthernet1, EventConfig{EventMode::interval, 0ms, 30000ms}, sender,
                             /*initial=*/false);

    ev.Trigger(EventValue{true});

    REQUIRE(sender.calls.size() == 1);
    REQUIRE(sender.calls[0].value == true);
}

TEST_CASE("interval with delay==0 does not resend on Trigger if value unchanged", "[EventDescriptor]")
{
    FakeSender            sender;
    EventDescriptor<bool> ev(EventId::ChannelLifeEthernet1, EventConfig{EventMode::interval, 0ms, 30000ms}, sender,
                             /*initial=*/false);

    ev.Trigger(EventValue{false}); // same as initial

    REQUIRE(sender.calls.empty());
}

TEST_CASE("interval sends heartbeat unconditionally on each period, even without new Trigger", "[EventDescriptor]")
{
    FakeSender            sender;
    EventDescriptor<bool> ev(EventId::ChannelLifeEthernet0, EventConfig{EventMode::interval, 10000ms, 30000ms}, sender,
                             /*initial=*/false);

    ev.Trigger(EventValue{true});
    ev.Tick(10000ms);
    REQUIRE(sender.calls.size() == 1);
    REQUIRE(sender.calls[0].value == true);

    // No new Trigger for a long time, but heartbeat must keep firing every interval.
    ev.Tick(40000ms);
    REQUIRE(sender.calls.size() == 2);
    REQUIRE(sender.calls[1].value == true); // same value, still resent (controller-reset protection)

    ev.Tick(70000ms);
    REQUIRE(sender.calls.size() == 3);
}

TEST_CASE("interval: cable flap shorter than delay is absorbed, no event sent", "[EventDescriptor]")
{
    FakeSender            sender;
    EventDescriptor<bool> ev(EventId::ChannelLifeEthernet2, EventConfig{EventMode::interval, 10000ms, 30000ms}, sender,
                             /*initial=*/true);

    ev.Trigger(EventValue{false}); // cable pulled
    ev.Tick(2000ms);

    ev.Trigger(EventValue{true}); // cable reinserted 2s later -> rearm at 2000+10000=12000
    ev.Tick(11999ms);
    REQUIRE(sender.calls.empty());

    ev.Tick(12000ms);
    REQUIRE(sender.calls.empty()); // pending==image==true -> absorbed
}

TEST_CASE("interval: cable down longer than delay produces a false event", "[EventDescriptor]")
{
    FakeSender            sender;
    EventDescriptor<bool> ev(EventId::ChannelLifeEthernet3, EventConfig{EventMode::interval, 10000ms, 30000ms}, sender,
                             /*initial=*/true);

    ev.Trigger(EventValue{false}); // cable pulled, stays down
    ev.Tick(9999ms);
    REQUIRE(sender.calls.empty());

    ev.Tick(10000ms);
    REQUIRE(sender.calls.size() == 1);
    REQUIRE(sender.calls[0].value == false);
}

TEST_CASE("Metrics track triggered, raised, and suppressed counts", "[EventDescriptor][Metrics]")
{
    FakeSender            sender;
    EventDescriptor<bool> ev(EventId::ChannelLifeEthernet0, EventConfig{EventMode::oneShot, 1000ms, 1000ms}, sender,
                             /*initial=*/true);

    ev.Trigger(EventValue{false}); // triggered: 1
    ev.Tick(1000ms);               // raised: 1 (value changed)

    REQUIRE(ev.Metrics().Triggered() == 1);
    REQUIRE(ev.Metrics().Raised() == 1);
    REQUIRE(ev.Metrics().Suppressed() == 0);
}
