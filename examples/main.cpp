#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "event/EventDescriptor.hpp"
#include "event/EventSupervisor.hpp"

using app::event::EventConfig;
using app::event::EventDescriptor;
using app::event::EventId;
using app::event::EventMode;
using app::event::EventSupervisor;
using app::event::EventValue;
using app::event::IEventSender;
using namespace std::chrono_literals;

namespace {

class ConsoleSender final : public IEventSender<bool> {
 public:
  void send(EventId id, bool value) noexcept override {
    std::cout << "[controller] event=" << static_cast<int>(id) << " value=" << (value ? "true" : "false") << '\n';
  }
};

}  // namespace

int main() {
  ConsoleSender sender;
  EventSupervisor supervisor(100ms);

  supervisor.registerDescriptor(std::make_unique<EventDescriptor<bool>>(
      EventId::ChannelLifeEthernet0, EventConfig{.mode = EventMode::Interval, .delay = 1000ms, .interval = 3000ms},
      sender, /*initial=*/false));

  supervisor.registerDescriptor(std::make_unique<EventDescriptor<bool>>(
      EventId::NtpAlive1, EventConfig{.mode = EventMode::OneShot, .delay = 500ms, .interval = 1000ms}, sender,
      /*initial=*/false));

  std::cout << "--- startup snapshot ---\n";
  supervisor.emitInitialSnapshot();

  supervisor.start();

  std::cout << "--- simulating cable flap (absorbed by debounce) ---\n";
  supervisor.trigger(EventId::ChannelLifeEthernet0, EventValue{true});
  std::this_thread::sleep_for(200ms);
  supervisor.trigger(EventId::ChannelLifeEthernet0, EventValue{false});
  std::this_thread::sleep_for(200ms);
  supervisor.trigger(EventId::ChannelLifeEthernet0, EventValue{true});

  std::cout << "--- simulating NTP sync confirmed ---\n";
  supervisor.trigger(EventId::NtpAlive1, EventValue{true});

  std::cout << "--- running for 10s, watch heartbeat repeats ---\n";
  std::this_thread::sleep_for(10s);

  supervisor.stop();
  return 0;
}
