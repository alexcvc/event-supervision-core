#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
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
    // Wake the worker in case this trigger armed a deadline earlier than the
    // one it last computed and is currently sleeping until.
    m_Cv.notify_one();
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
    while (m_Running.load(std::memory_order_acquire)) {
      const auto now =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
      for (auto& d : m_Descriptors) {
        d->tick(now);
      }

      // Sleep exactly until the earliest deadline any descriptor has armed,
      // instead of waking at a fixed period regardless of need. Falls back
      // to tickPeriod as an idle poll interval when nothing is armed yet
      // (e.g. before any trigger() has been received).
      std::optional<std::chrono::milliseconds> nextDeadline;
      for (auto& d : m_Descriptors) {
        if (const auto deadline = d->nextDeadline()) {
          nextDeadline = nextDeadline ? std::min(*nextDeadline, *deadline) : *deadline;
        }
      }

      const auto sleepFor = nextDeadline.has_value() ? (*nextDeadline - now) : m_TickPeriod;
      const auto wakeAt = std::chrono::steady_clock::now() + sleepFor;

      // trigger() may notify_one() concurrently right after nextDeadline is
      // computed above but before wait_until() starts waiting; the narrow
      // race is bounded (the loop still wakes at the stale wakeAt and
      // recomputes correctly then), so it is not worth a coarser lock.
      std::unique_lock<std::mutex> lock(m_CvMutex);
      m_Cv.wait_until(lock, wakeAt, [this] {
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
