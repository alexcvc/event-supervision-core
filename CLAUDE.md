# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A small, header-only C++20 library (`event_system`) for supervising discrete state/condition events (e.g.
Ethernet link-up, NTP sync) and reporting them to a remote controller with debounce and heartbeat semantics. No
`.cpp` files in the library itself — everything lives under `include/event/*.hpp`.

## Build & test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure          # run all tests
ctest --test-dir build -R "<test name regex>"       # run a single test/section
```

- `EVENT_SYSTEM_BUILD_TESTS` (default ON) and `EVENT_SYSTEM_BUILD_EXAMPLES` (default ON) are CMake options.
- Tests use Catch2 v2.13.10, fetched automatically via `FetchContent` in `tests/CMakeLists.txt` — no manual
  install needed, but the first configure requires network access.
- `CMAKE_EXPORT_COMPILE_COMMANDS` is ON, so `compile_commands.json` is generated in the build dir for
  clang-tidy/clangd.
- The demo executable (`event_system_demo`, from `examples/main.cpp`) runs live for ~10s simulating trigger
  events and heartbeat repeats — useful for manually observing debounce/heartbeat timing behavior.

## Linting/formatting

- `.clang-format`: Google-based but with Allman braces, 4-space indent, 120 column limit, aligned consecutive
  declarations — do not reformat to stock Google style.
- `.clang-tidy`: broad ruleset (`*` with specific families disabled — see file header comments for rationale
  on each disabled check before re-enabling one).

## Architecture

Three-layer design per event:

1. **`EventSupervisor`** — owns a fixed vector (`MaxEvents = 32`) of `IEventDescriptor`s and one dedicated
   worker thread that calls `Tick()` on every descriptor, then sleeps precisely until the earliest deadline any
   descriptor reports via `NextDeadline()` — not a fixed `tickPeriod_` poll. `tickPeriod_` is only used as the
   idle-poll fallback when no descriptor currently has an armed deadline. `Trigger()` wakes the worker
   (`notify_one`) in case it just armed a deadline earlier than the one the worker is currently sleeping until.
   `Register()` is only valid before `Start()` (asserted, not thread-safe). `Trigger(EventId, EventValue)` is the
   thread-safe entry point external code (e.g. a netlink callback or NTP client) uses to report a raw condition
   change from any thread.

2. **`EventDescriptor<TValue>`** (implements `IEventDescriptor`) — owns all timing/debounce/heartbeat state for
   *one* event. It does not know its receiver and does not decide what a "true" condition means — that
   decision belongs to whoever calls `Trigger()`. Internally guarded by a per-descriptor `SpinLock` (short
   critical sections only). Key state machine (`Phase::Debounce` / `Phase::Heartbeat`):
   - `EventMode::OneShot`: on `Trigger()`, (re)arms a delay timer; on expiry, sends only if the value changed
     since the last sent image (`FireIfChangedLocked`), then goes idle.
   - `EventMode::Interval` with `delay == 0`: fires immediately on `Trigger()` if changed, then enters
     `Heartbeat` phase, unconditionally resending the current image every `interval` — this protects a remote
     controller that may lose state on its own reset.
   - `EventMode::Interval` with `delay > 0`: every trigger re-enters `Debounce` (re-arming the delay timer,
     even from `Heartbeat`); once the debounce settles, transitions to `Heartbeat` and repeats forever.
   - `EventConfig{mode, delay, interval}` drives all of the above.

3. **`IEventSender<TValue>`** — the only thing that knows "where does this event go" (e.g. wire protocol to a
   remote controller). Descriptors hold a non-owning pointer to a sender; callers implement `Send(EventId,
   TValue)`.

Supporting types:
- `EventId` (`include/event/EventId.hpp`) — enum of all known monitored conditions; `Count` is a sizing
  marker, not a real event.
- `EventValue` — `std::variant<bool, std::int32_t>`; extend this variant (and corresponding
  `EventDescriptor<TValue>` instantiations) to support new payload types.
- `EventMetrics` — lock-free atomic counters (`Triggered`/`Raised`/`Suppressed`) per descriptor, intended to be
  Prometheus-exported; `Raised` increments on every actual `Send()` (including heartbeats), `Suppressed`
  increments when a debounce cycle resolves back to the unchanged value.

Startup flow: `Register()` every descriptor → optionally `EmitInitialSnapshot()` (sends every descriptor's
current image immediately, bypassing delay/debounce; must be called before `Start()`) → `Start()`.

## Conventions worth preserving

- Interfaces (`IEventDescriptor`, `IEventSender`) exist to keep descriptors receiver-agnostic and the
  supervisor descriptor-type-agnostic — don't collapse these to concrete types for convenience.
- Locking is scoped to a single descriptor's internal state (`SpinLock`); the supervisor itself does not lock
  around descriptor iteration since `Register()` is required to happen before `Start()`.
- New `EventMode`/timing behavior should be added inside `EventDescriptor::Trigger`/`Tick`'s existing
  Debounce/Heartbeat phase machine rather than introducing a parallel mechanism.