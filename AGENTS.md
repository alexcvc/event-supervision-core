# AGENTS.md — event-supervision-core

## Project overview

C++20 **header-only** library. All logic lives in `include/app/event/`. Namespace: `app::event` throughout. CMake exposes an `INTERFACE` target named `event_system`; consumers `target_link_libraries(... event_system)`.

## Key components

| File | Role |
|---|---|
| `EventSupervisor.hpp` | Owns ≤32 descriptors + one worker thread; calls `Tick()` on every descriptor each `tickPeriod` (default 100 ms). |
| `EventDescriptor.hpp` | `EventDescriptor<TValue>` — debounce/interval timing for one event. Holds `image_` (last sent) and `pending_` (latest raw value). |
| `IEventDescriptor.hpp` | Non-template interface for the heterogeneous `vector<unique_ptr<IEventDescriptor>>` inside the supervisor. |
| `IEventSender.hpp` | `IEventSender<TValue>` — implement this to route events to any transport. The descriptor never knows the receiver. |
| `EventConfig.hpp` | `EventMode::{OneShot, Interval}` + `{delay, interval}`. |
| `EventId.hpp` | `enum class EventId : uint16_t` — append before `Count` to add IDs. |
| `EventValue.hpp` | `std::variant<bool, int32_t>` — type-erased payload for `Supervisor::Trigger()`. |
| `EventMetrics.hpp` | Atomic `triggered / raised / suppressed` counters per descriptor. |

## Mandatory lifecycle order

```
Register()  →  [EmitInitialSnapshot()]  →  Start()  →  Trigger() / Stop()
```

`Register()` and `EmitInitialSnapshot()` are **asserted** to be called before `Start()`. `Stop()` is idempotent; the destructor calls it automatically.

## Event modes

- **OneShot** — fires once after `delay` if `pending != image`; timer disarms after firing.
- **Interval** — *Debounce* phase: wait `delay`, send if changed, then enter *Heartbeat* phase which unconditionally resends `image_` every `interval` (controller-reset protection). Any new `Trigger()` re-enters Debounce.
- **`Interval` + `delay == 0`** — fires synchronously inside `Trigger()` itself, bypassing `Tick()`.

## Thread-safety rules

- `Trigger(EventId, EventValue)` — thread-safe, callable from any thread (netlink, NTP callbacks, etc.). Uses `SpinLock` internally.
- `Register()` / `EmitInitialSnapshot()` — **not** thread-safe with `Start()`; call only before `Start()`.
- `IEventSender<TValue>*` is a raw pointer — the sender **must** outlive its descriptor.

## Type safety

`EventDescriptor<TValue>::Trigger()` calls `std::get<TValue>(value)` — passing a mismatched `EventValue` variant is a hard crash. Callers must match the declared `TValue`. To add a payload type, extend the variant in `EventValue.hpp`.

## Build & test

```bash
cmake -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
cd cmake-build-debug && ctest --output-on-failure
./cmake-build-debug/examples/event_system_demo
```

Catch2 is fetched automatically via `FetchContent` (see `tests/CMakeLists.txt`). Test binary: `cmake-build-debug/tests/event_system_tests`.

## Test patterns

Tests **inject time manually** — no `sleep`, no `steady_clock`. Drive `EventDescriptor` directly by calling `ev.Tick(Xms)`. Use a local `FakeSender` that records `{EventId, TValue}` into a `std::vector` and assert on `sender.calls`. See `tests/EventDescriptorTests.cpp` for canonical examples. `tests/EventSupervisorTests.cpp` tests the real worker thread with `std::this_thread::sleep_for`.

## Adding a new event — checklist

1. Append an ID to `EventId` (before `Count`).
2. Implement `IEventSender<YourType>` in application code.
3. `supervisor.Register(std::make_unique<EventDescriptor<YourType>>(id, config, sender, initial));`
4. Call `supervisor.Trigger(id, EventValue{value})` from the raw-event source.

