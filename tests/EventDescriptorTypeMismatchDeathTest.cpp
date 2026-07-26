#include <chrono>
#include <string>

#include "tickguard/EventDescriptor.hpp"

using namespace std::chrono_literals;

// Standalone (no Catch2) executable: feeding an EventDescriptor<bool> an EventValue
// holding a std::string must terminate the process via std::get<TValue>'s hard-crash
// contract (see EventValue.hpp). Registered as a CTest death test in
// tests/CMakeLists.txt, which passes based on the crash message it prints.
int main() {
  class NullSender final : public tickguard::IEventSender<bool> {
   public:
    void send(tickguard::EventId, bool) noexcept override {}
  };

  NullSender sender;
  tickguard::EventDescriptor<bool> event(
      tickguard::EventId::NtpAlive1,
      tickguard::EventConfig{.mode = tickguard::EventMode::OneShot, .delay = 1000ms, .interval = 1000ms}, sender,
      /*initial=*/false);

  event.trigger(tickguard::EventValue{std::string{"mismatched"}});

  return 0;  // Unreachable if the crash contract holds.
}
