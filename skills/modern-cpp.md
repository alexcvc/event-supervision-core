# Modern C++20 Concurrency Conventions (TickGuard)

Reference doc, not a Claude Code Skill — no auto-invocation, just the patterns already
in use in this codebase so new code extends them instead of introducing a parallel style.

## Worker-thread lifecycle: prefer patterns compatible with `std::jthread`/`std::stop_token`

`EventSupervisor` owns one dedicated worker thread with an explicit `start()` /
`stop()` lifecycle and a condition-variable wakeup (`notify_one()` from
`trigger()`). When adding new threads or extending this one:

- Favor `std::jthread` semantics (automatic join on destruction, cooperative
  cancellation via `std::stop_token`) over manually flagging a bool and
  joining by hand — even where the current code predates this and uses a
  raw `std::thread` + manual join, new worker-thread code should default to
  `jthread`/`stop_token` unless there's a concrete reason not to (e.g. a
  toolchain without full `<stop_token>` support).
- Use `stop_token::stop_requested()` (or a `stop_callback` to `notify_one()`
  the condition variable) instead of a separate atomic "running" flag when a
  `jthread` is available — it collapses two synchronization primitives into
  one.

## Locking: match the granularity that's already there

- `EventDescriptor` uses a lightweight per-descriptor `SpinLock`
  (`SpinLock.hpp`, `std::atomic_flag`-based) around short critical sections
  only (`m_Pending`, `m_Image`, `m_ArmedAt`). Reach for the same pattern for
  new per-object state that's touched by both an external caller thread and
  the worker thread, as long as the critical section stays short
  (no blocking calls, no `send()` inside the lock).
- `EventSupervisor` itself does **not** lock around the descriptor collection
  — it relies on `registerDescriptor()` being asserted to happen only before
  `start()`. Don't add a mutex around the descriptor vector; if a new use case
  needs runtime registration, that's a design decision big enough for its own
  ADR, not a quick lock addition.
- Use `std::mutex`/`std::condition_variable` (not `SpinLock`) for anything
  that can block for a nontrivial time — e.g. the supervisor's own
  wait-until-deadline loop already does this correctly with
  `std::condition_variable::wait_until`.

## Atomics for metrics, not for control flow

`EventMetrics` (`Triggered`/`Raised`/`Suppressed`) are plain atomics with
relaxed-enough semantics for counters that only need to be eventually
consistent for Prometheus scraping — don't upgrade these to a mutex, and
don't rely on their ordering to synchronize anything else. If a new metric
needs strict ordering guarantees relative to other state, that's a sign it
isn't really "just a counter" anymore.

## Naming (enforced by `.clang-tidy`, restated here for generated code)

- Functions/methods and local variables: `camelBack` (`registerDescriptor`,
  `nextDeadline`).
- Private/protected members: `m_`-prefixed `CamelCase` (`m_Image`,
  `m_ArmedAt`).
- Constants (`constexpr`/global/static): `k`-prefixed `CamelCase` (`kMaxEvents`).

Generated code that violates these will fail `.clang-tidy`'s
`readability-identifier-naming` checks — match them up front rather than
relying on a later lint pass to catch it.
