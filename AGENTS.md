# AGENTS.md — event-supervision-core

## Project overview

C++20 **header-only** library. All logic lives in `include/tickguard/`. Namespace: `tickguard` throughout. CMake exposes an `INTERFACE` target named `event_system`; consumers `target_link_libraries(... event_system)`.

## Key components

| File | Role |
|---|---|
| `EventSupervisor.hpp` | Owns ≤32 descriptors (`kMaxEvents`) + one worker thread; calls `tick(now)` on every descriptor, then sleeps exactly until the earliest `nextDeadline()` reported by any descriptor (adaptive wake), falling back to `tickPeriod` (default 100 ms) only as an idle-poll interval when nothing is armed. |
| `EventDescriptor.hpp` | `EventDescriptor<TValue>` — debounce/interval timing for one event. Holds `m_Image` (last sent) and `m_Pending` (latest raw value). |
| `IEventDescriptor.hpp` | Non-template interface for the heterogeneous `vector<unique_ptr<IEventDescriptor>>` inside the supervisor; also declares `nextDeadline()`. |
| `IEventSender.hpp` | `IEventSender<TValue>` — implement this to route events to any transport. The descriptor never knows the receiver. |
| `EventConfig.hpp` | `EventMode::{OneShot, Interval}` + `{delay, interval}`. |
| `EventId.hpp` | `enum class EventId : uint16_t` — append before `Count` to add IDs. |
| `EventValue.hpp` | `std::variant<bool, int32_t>` — type-erased payload for `Supervisor::trigger()`. |
| `EventMetrics.hpp` | Atomic `triggered / raised / suppressed` counters per descriptor. |
| `SpinLock.hpp` | Minimal `std::atomic_flag`-based spinlock guarding each descriptor's internal state (short critical sections only). |

Method names in code are **camelCase** (`registerDescriptor`, `trigger`, `tick`, `emitInitialSnapshot`, `emitSnapshot`, `start`, `stop`, `nextDeadline`, `image`, `metrics`, `eventCount`); private members use `m_PascalCase` (e.g. `m_Image`, `m_Pending`, `m_ArmedAt`).

## Mandatory lifecycle order

```
registerDescriptor()  →  [emitInitialSnapshot()]  →  start()  →  trigger() / stop()
```

`registerDescriptor()` and `emitInitialSnapshot()` are **asserted** to be called before `start()`. `stop()` is idempotent; the destructor calls it automatically.

## Event modes

- **OneShot** — fires once after `delay` if `pending != image`; timer disarms after firing.
- **Interval** — *Debounce* phase: wait `delay`, send if changed, then enter *Heartbeat* phase which unconditionally resends `m_Image` every `interval` (controller-reset protection). Any new `trigger()` re-enters Debounce.
- **`Interval` + `delay == 0`** — fires synchronously inside `trigger()` itself, bypassing `tick()`.

## Thread-safety rules

- `trigger(EventId, EventValue)` — thread-safe, callable from any thread (netlink, NTP callbacks, etc.). Uses `SpinLock` internally, then calls `notify_one()` to wake the worker in case it just armed an earlier deadline.
- `registerDescriptor()` / `emitInitialSnapshot()` — **not** thread-safe with `start()`; call only before `start()`.
- `IEventSender<TValue>*` is a raw pointer — the sender **must** outlive its descriptor.

## Type safety

`EventDescriptor<TValue>::trigger()` calls `std::get<TValue>(value)` — passing a mismatched `EventValue` variant is a hard crash. Callers must match the declared `TValue`. To add a payload type, extend the variant in `EventValue.hpp`.

## Build & test

```bash
cmake -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
cd cmake-build-debug && ctest --output-on-failure
./cmake-build-debug/examples/event_system_demo
```

Catch2 is fetched automatically via `FetchContent` (see `tests/CMakeLists.txt`). Test binary: `cmake-build-debug/tests/event_system_tests`.

## Test patterns

Tests **inject time manually** — no `sleep`, no `steady_clock`. Drive `EventDescriptor` directly by calling `ev.tick(Xms)`. Use a local `FakeSender` that records `{EventId, TValue}` into a `std::vector` and assert on `sender.calls`. See `tests/EventDescriptorTests.cpp` for canonical examples. `tests/EventSupervisorTests.cpp` tests the real worker thread with `std::this_thread::sleep_for`, using a mutex-guarded `ThreadSafeSender` since the worker thread calls `send()` concurrently with test-thread assertions.

## Adding a new event — checklist

1. Append an ID to `EventId` (before `Count`).
2. Implement `IEventSender<YourType>` in application code.
3. `supervisor.registerDescriptor(std::make_unique<EventDescriptor<YourType>>(id, config, sender, initial));`
4. Call `supervisor.trigger(id, EventValue{value})` from the raw-event source.

