#include <chrono>
#include <string>

#include "event/EventDescriptor.hpp"

using namespace std::chrono_literals;

// Standalone (no Catch2) executable: feeding an EventDescriptor<bool> an EventValue
// holding a std::string must terminate the process via std::get<TValue>'s hard-crash
// contract (see EventValue.hpp). Registered as a CTest death test in
// tests/CMakeLists.txt, which passes based on the crash message it prints.
int main() {
  class NullSender final : public app::event::IEventSender<bool> {
   public:
    void send(app::event::EventId, bool) noexcept override {}
  };

  NullSender sender;
  app::event::EventDescriptor<bool> event(
      app::event::EventId::NtpAlive1,
      app::event::EventConfig{.mode = app::event::EventMode::OneShot, .delay = 1000ms, .interval = 1000ms}, sender,
      /*initial=*/false);

  event.trigger(app::event::EventValue{std::string{"mismatched"}});

  return 0;  // Unreachable if the crash contract holds.
}
