# 1. C++20 baseline, linting/formatting tooling, and test strategy

## Status

Accepted

## Context

TickGuard is a header-only C++ library intended for embedded-adjacent targets
(no dynamic threading frameworks, minimal heap use). It needs a consistent,
low-friction toolchain so that contributors and CI reviewers judge code
against the same bar, without requiring bespoke build-system integration for
every tool.

## Decision

- **Language standard**: target C++20 exclusively (see `CMakeLists.txt`,
  `CXX_STANDARD 20`). Newer standards are not adopted until the project
  explicitly revisits this decision.
- **Formatting**: `.clang-format` is Google-based but deliberately diverges
  from stock Google style — Allman braces, 4-space indent, 120-column limit,
  `ReflowComments: false`. Do not "fix" the codebase back to stock Google
  style; the divergences are intentional.
- **Static analysis**: `.clang-tidy` enables the broad `*` ruleset with a
  short, explicitly-documented list of disabled checks and rationale for each
  (see the file's own header comments — e.g. `-llvm-header-guard` because the
  project uses `#pragma once` everywhere by design, not `#ifndef` guards).
  A small `WarningsAsErrors` allowlist (`modernize-use-nullptr`,
  `cppcoreguidelines-no-malloc`, etc.) is treated as a hard gate. Naming
  conventions (camelBack methods/variables, `m_`-prefixed CamelCase members)
  are enforced via `readability-identifier-naming` `CheckOptions` — see
  `AGENTS.md` for the human-readable version of the same rules.
- **Linting is external to the build, by design**: `CMakeLists.txt` only sets
  `CMAKE_EXPORT_COMPILE_COMMANDS ON` to produce a `compile_commands.json`.
  There is intentionally no `add_custom_target` wrapping clang-tidy or
  clang-format into `cmake --build`. Lint/format checks run manually or in CI
  against the generated compile database, keeping the local build fast and
  tool-version-independent.
- **Testing**: Catch2 v2.13.10, fetched automatically via `FetchContent` in
  `tests/CMakeLists.txt` (no manual install step), run through `ctest`. Tests
  inject time manually (`ev.tick(Xms)`) instead of sleeping wherever possible,
  reserving real `sleep_for`-based tests for the small surface that actually
  exercises the live worker thread (`EventSupervisorTests.cpp`).

## Consequences

- Contributors run clang-tidy/clang-format against `compile_commands.json`
  before submitting; neither is enforced automatically by `cmake --build`.
- Enabling a new clang-tidy check or disabling an existing one requires
  updating the rationale comments at the top of `.clang-tidy` — undocumented
  suppressions should be treated as review blockers.
- No parallel or competing lint/format configuration should be introduced;
  `.clang-tidy` and `.clang-format` are the single source of truth, and
  `AGENTS.md` restates their naming rules for quick human/agent reference
  rather than redefining them.
- Bumping the language standard past C++20, or replacing Catch2, is a new
  decision and should get its own ADR rather than a silent change to
  `CMakeLists.txt`.
