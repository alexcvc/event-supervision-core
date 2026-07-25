#include <chrono>
#include <cstdio>
#include <thread>

#include "app/event/EventDescriptor.hpp"
#include "app/event/EventSupervisor.hpp"

using namespace app::event;
using namespace std::chrono_literals;

namespace
{

class ConsoleSender final : public IEventSender<bool>
{
public:
    void Send(EventId id, bool value) noexcept override
    {
        std::printf("[controller] event=%d value=%s\n", static_cast<int>(id), value ? "true" : "false");
    }
};

} // namespace

int main()
{
    ConsoleSender   sender;
    EventSupervisor supervisor(100ms);

    supervisor.Register(std::make_unique<EventDescriptor<bool>>(
        EventId::ChannelLifeEthernet0, EventConfig{EventMode::interval, 1000ms, 3000ms}, sender, /*initial=*/false));

    supervisor.Register(std::make_unique<EventDescriptor<bool>>(
        EventId::NtpAlive1, EventConfig{EventMode::oneShot, 500ms, 1000ms}, sender, /*initial=*/false));

    std::printf("--- startup snapshot ---\n");
    supervisor.EmitInitialSnapshot();

    supervisor.Start();

    std::printf("--- simulating cable flap (absorbed by debounce) ---\n");
    supervisor.Trigger(EventId::ChannelLifeEthernet0, EventValue{true});
    std::this_thread::sleep_for(200ms);
    supervisor.Trigger(EventId::ChannelLifeEthernet0, EventValue{false});
    std::this_thread::sleep_for(200ms);
    supervisor.Trigger(EventId::ChannelLifeEthernet0, EventValue{true});

    std::printf("--- simulating NTP sync confirmed ---\n");
    supervisor.Trigger(EventId::NtpAlive1, EventValue{true});

    std::printf("--- running for 10s, watch heartbeat repeats ---\n");
    std::this_thread::sleep_for(10s);

    supervisor.Stop();
    return 0;
}
