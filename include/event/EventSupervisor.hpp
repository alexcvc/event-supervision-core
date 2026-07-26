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

#include "event/EventId.hpp"
#include "event/EventValue.hpp"
#include "event/IEventDescriptor.hpp"

namespace app::event {

/**
 * @brief Owns a fixed set of `IEventDescriptor`s and one dedicated worker
 *        thread that ticks them and adaptively sleeps until the next
 *        actionable deadline.
 *
 * @details Mandatory lifecycle order:
 * `registerDescriptor()` → (optionally) `emitInitialSnapshot()` →
 * `start()` → `trigger()` / `stop()`.
 *
 * The worker thread calls `tick(now)` on every registered descriptor each
 * time it wakes, then computes the earliest `nextDeadline()` across all
 * descriptors and sleeps precisely until then — `tickPeriod` is only used
 * as an idle-poll fallback interval when no descriptor currently has an
 * armed deadline (e.g. before any `trigger()` has been received).
 *
 * @note Not copyable or movable — owns a running `std::thread` that
 * captures `this` by reference.
 */
class EventSupervisor {
 public:
  /// Default idle-poll interval used when no descriptor has an armed
  /// deadline (i.e. the fallback sleep duration for `run()`).
  static constexpr std::chrono::milliseconds kDefaultTickPeriod{100};

  /**
   * @brief Constructs a supervisor with no registered descriptors and no
   *        running worker thread.
   *
   * @param tickPeriod Idle-poll fallback interval used when nothing is
   * armed; defaults to `kDefaultTickPeriod`.
   */
  explicit EventSupervisor(std::chrono::milliseconds tickPeriod = kDefaultTickPeriod) noexcept
      : m_TickPeriod(tickPeriod) {}

  /**
   * @brief Stops the worker thread (if running) and joins it.
   *
   * @details Calls `stop()`, which is idempotent, so it is always safe to
   * destroy the supervisor regardless of whether `start()`/`stop()` were
   * called explicitly beforehand.
   */
  ~EventSupervisor() {
    stop();
  }

  EventSupervisor(const EventSupervisor&) = delete;
  EventSupervisor& operator=(const EventSupervisor&) = delete;

  // Not movable: owns a running std::thread that captures `this`.
  EventSupervisor(EventSupervisor&&) = delete;
  EventSupervisor& operator=(EventSupervisor&&) = delete;

  /**
   * @brief Registers a descriptor to be supervised.
   *
   * @param descriptor Owned descriptor to add; ownership transfers to the
   * supervisor.
   * @return `true` if the descriptor was added; `false` if `kMaxEvents`
   * (32) descriptors are already registered.
   *
   * @pre Must be called before `start()`. Asserted, and **not**
   * thread-safe with respect to `start()`/`tick()` — call only from the
   * setup thread prior to starting the worker.
   */
  bool registerDescriptor(std::unique_ptr<IEventDescriptor> descriptor) {
    assert(!m_Running.load(std::memory_order_acquire) && "registerDescriptor() only allowed before start()");
    if (m_Descriptors.size() >= kMaxEvents) {
      return false;
    }
    m_Descriptors.push_back(std::move(descriptor));
    return true;
  }

  /**
   * @brief Sends the current image of every registered descriptor
   *        immediately, bypassing delay/debounce.
   *
   * @details Call once, after all `registerDescriptor()` calls, before
   * `start()`. Lets a freshly (re)started supervisor inform receivers of
   * every event's current state without waiting for a change or a
   * debounce/heartbeat cycle.
   *
   * @pre Must be called before `start()` (asserted).
   */
  void emitInitialSnapshot() noexcept {
    assert(!m_Running.load(std::memory_order_acquire) && "call before start()");
    for (auto& d : m_Descriptors) {
      d->emitSnapshot();
    }
  }

  /**
   * @brief Reports a raw value change for the descriptor matching `id`.
   *
   * @param id    Identifier of the event being reported.
   * @param value Type-erased payload; must hold the alternative matching
   * the target descriptor's `TValue`, or unwrapping it is a hard crash.
   *
   * @details Thread-safe — callable from any external thread (e.g. a
   * netlink callback or NTP client) to report a raw condition change. Does
   * nothing if no registered descriptor matches `id`. After delegating to
   * the matching descriptor's `trigger()`, notifies the worker thread's
   * condition variable in case this call armed a deadline earlier than the
   * one the worker last computed and is currently sleeping until.
   */
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

  /**
   * @brief Starts the worker thread, which begins ticking all registered
   *        descriptors.
   *
   * @pre All descriptors must already be registered (and any initial
   * snapshot emitted) — `registerDescriptor()`/`emitInitialSnapshot()` are
   * not safe to call concurrently with a running worker.
   */
  void start() {
    m_Running.store(true, std::memory_order_release);
    m_Worker = std::thread(&EventSupervisor::run, this);
  }

  /**
   * @brief Stops the worker thread and joins it.
   *
   * @details Idempotent — calling `stop()` when not running (or more than
   * once) is a safe no-op. Wakes the worker via `notify_all()` so it can
   * observe the stop request immediately rather than waiting out its
   * current sleep.
   */
  void stop() noexcept {
    if (!m_Running.exchange(false, std::memory_order_acq_rel)) {
      return;
    }
    m_Cv.notify_all();
    if (m_Worker.joinable()) {
      m_Worker.join();
    }
  }

  /**
   * @brief Returns the number of currently registered descriptors.
   *
   * @return Count of descriptors added via `registerDescriptor()`.
   */
  [[nodiscard]] std::size_t eventCount() const noexcept {
    return m_Descriptors.size();
  }

 private:
  /**
   * @brief Worker thread body: ticks all descriptors and adaptively sleeps
   *        until the earliest reported deadline.
   *
   * @details Loops while `m_Running` is set. On each iteration:
   * 1. Captures `now` from `std::steady_clock`.
   * 2. Calls `tick(now)` on every descriptor.
   * 3. Computes the minimum `nextDeadline()` across all descriptors (if
   *    any are armed).
   * 4. Sleeps via `wait_until()` on `m_Cv` until that deadline, or for
   *    `m_TickPeriod` if nothing is armed — waking early and re-looping if
   *    `stop()` is called or `trigger()` notifies the condition variable.
   */
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

  /// Maximum number of descriptors this supervisor can hold.
  static constexpr std::size_t kMaxEvents = 32;

  /// Idle-poll fallback interval used when no descriptor has an armed deadline.
  std::chrono::milliseconds m_TickPeriod;
  /// All registered descriptors, owned by the supervisor.
  std::vector<std::unique_ptr<IEventDescriptor>> m_Descriptors;
  /// Guards waits/notifications on `m_Cv`; not used to protect descriptor state.
  std::mutex m_CvMutex;
  /// Used to wake the worker early on `trigger()`/`stop()` instead of at a fixed interval.
  std::condition_variable m_Cv;
  /// Set by `start()`, cleared by `stop()`; gates the worker loop and guard assertions.
  std::atomic<bool> m_Running{false};
  /// Dedicated thread running `run()`; joined by `stop()`.
  std::thread m_Worker;
};

}  // namespace app::event