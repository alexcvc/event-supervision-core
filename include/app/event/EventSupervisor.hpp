#pragma once

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "app/event/EventId.hpp"
#include "app/event/EventValue.hpp"
#include "app/event/IEventDescriptor.hpp"

namespace app::event {

// Owns a fixed set of event descriptors and a dedicated worker thread that
// ticks them periodically. All events must be registered before Start().
class EventSupervisor {
 public:
  explicit EventSupervisor(std::chrono::milliseconds tickPeriod = std::chrono::milliseconds{100}) noexcept
      : tickPeriod_(tickPeriod) {}

  ~EventSupervisor() {
    Stop();
  }

  EventSupervisor(const EventSupervisor&) = delete;
  EventSupervisor& operator=(const EventSupervisor&) = delete;

  // Only valid before Start(). Not thread-safe with respect to Start()/Tick().
  bool Register(std::unique_ptr<IEventDescriptor> descriptor) {
    assert(!running_.load(std::memory_order_acquire) && "Register() only allowed before Start()");
    if (descriptors_.size() >= MaxEvents) {
      return false;
    }
    descriptors_.push_back(std::move(descriptor));
    return true;
  }

  // Sends the current image of every registered event immediately,
  // bypassing delay/debounce. Call once, after Register(), before Start().
  void EmitInitialSnapshot() noexcept {
    assert(!running_.load(std::memory_order_acquire) && "call before Start()");
    for (auto& d : descriptors_) {
      d->EmitSnapshot();
    }
  }

  // Thread-safe: callable from any external thread (e.g. netlink callback,
  // NTP client) to report a raw state change.
  void Trigger(EventId id, EventValue value) noexcept {
    for (auto& d : descriptors_) {
      if (d->Id() == id) {
        d->Trigger(value);
        break;
      }
    }
  }

  void Start() {
    running_.store(true, std::memory_order_release);
    worker_ = std::thread(&EventSupervisor::Run, this);
  }

  void Stop() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
      return;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  [[nodiscard]] std::size_t EventCount() const noexcept {
    return descriptors_.size();
  }

 private:
  void Run() {
    auto next = std::chrono::steady_clock::now();
    while (running_.load(std::memory_order_acquire)) {
      next += tickPeriod_;

      const auto now =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
      for (auto& d : descriptors_) {
        d->Tick(now);
      }

      std::unique_lock<std::mutex> lock(cvMutex_);
      cv_.wait_until(lock, next, [this] {
        return !running_.load(std::memory_order_acquire);
      });
    }
  }

  static constexpr std::size_t MaxEvents = 32;

  std::chrono::milliseconds tickPeriod_;
  std::vector<std::unique_ptr<IEventDescriptor>> descriptors_;
  std::mutex cvMutex_;
  std::condition_variable cv_;
  std::atomic<bool> running_{false};
  std::thread worker_;
};

}  // namespace app::event
