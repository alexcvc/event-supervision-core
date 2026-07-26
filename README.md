# TickGuard

**TickGuard** is a lightweight, header-only C++20 library for supervising the aliveness of critical devices and conditions — link status, sync state, sensor heartbeats — and reporting them reliably to a remote controller. Instead of naively polling every millisecond, TickGuard's supervisor wakes only when a device's deadline actually demands it, ticking each monitored event just in time and going back to sleep the instant nothing needs attention.

Under the hood, every event carries its own debounce and heartbeat logic: a flickering signal won't spam the controller with noise, and a healthy device won't go silent either — TickGuard keeps resending its last known state so the controller never mistakes silence for failure. Built for embedded and real-time systems where correctness and low overhead both matter, TickGuard gives you a small, dependency-free building block for turning raw hardware events into a trustworthy stream of "this device is alive" signals.

## Quick Start

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Documentation

See the [Developer Guide](doc/developer-guide.md) for the full architecture reference,
timing-behavior diagrams, build options, and a runnable example.
