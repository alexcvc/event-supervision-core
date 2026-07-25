#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "app/event/EventId.hpp"
#include "app/event/EventValue.hpp"
#include "app/event/IEventDescriptor.hpp"

namespace app::event {

// Owns a fixed set of event descriptors and a dedicated worker thread that
// ticks them periodically. All events must be registered before start().
class EventSupervisor {
 public:
  static constexpr std::chrono::milliseconds kDefaultTickPeriod{100};

  explicit EventSupervisor(std::chrono::milliseconds tickPeriod = kDefaultTickPeriod) noexcept
      : m_TickPeriod(tickPeriod) {}

  ~EventSupervisor() {
    stop();
  }

  EventSupervisor(const EventSupervisor&) = delete;
  EventSupervisor& operator=(const EventSupervisor&) = delete;

  // Not movable: owns a running std::thread that captures `this`.
  EventSupervisor(EventSupervisor&&) = delete;
  EventSupervisor& operator=(EventSupervisor&&) = delete;

  // Only valid before start(). Not thread-safe with respect to start()/tick().
  bool registerDescriptor(std::unique_ptr<IEventDescriptor> descriptor) {
    assert(!m_Running.load(std::memory_order_acquire) && "registerDescriptor() only allowed before start()");
    if (m_Descriptors.size() >= kMaxEvents) {
      return false;
    }
    m_Descriptors.push_back(std::move(descriptor));
    return true;
  }

  // Sends the current image of every registered event immediately,
  // bypassing delay/debounce. Call once, after registerDescriptor(), before start().
  void emitInitialSnapshot() noexcept {
    assert(!m_Running.load(std::memory_order_acquire) && "call before start()");
    for (auto& d : m_Descriptors) {
      d->emitSnapshot();
    }
  }

  // Thread-safe: callable from any external thread (e.g. netlink callback,
  // NTP client) to report a raw state change.
  void trigger(EventId id, EventValue value) noexcept {
    for (auto& d : m_Descriptors) {
      if (d->id() == id) {
        d->trigger(value);
        break;
      }
    }
  }

  void start() {
    m_Running.store(true, std::memory_order_release);
    m_Worker = std::thread(&EventSupervisor::run, this);
  }

  void stop() noexcept {
    if (!m_Running.exchange(false, std::memory_order_acq_rel)) {
      return;
    }
    m_Cv.notify_all();
    if (m_Worker.joinable()) {
      m_Worker.join();
    }
  }

  [[nodiscard]] std::size_t eventCount() const noexcept {
    return m_Descriptors.size();
  }

 private:
  void run() {
    auto next = std::chrono::steady_clock::now();
    while (m_Running.load(std::memory_order_acquire)) {
      next += m_TickPeriod;

      const auto now =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
      for (auto& d : m_Descriptors) {
        d->tick(now);
      }

      std::unique_lock<std::mutex> lock(m_CvMutex);
      m_Cv.wait_until(lock, next, [this] {
        return !m_Running.load(std::memory_order_acquire);
      });
    }
  }

  static constexpr std::size_t kMaxEvents = 32;

  std::chrono::milliseconds m_TickPeriod;
  std::vector<std::unique_ptr<IEventDescriptor>> m_Descriptors;
  std::mutex m_CvMutex;
  std::condition_variable m_Cv;
  std::atomic<bool> m_Running{false};
  std::thread m_Worker;
};

}  // namespace app::event
