# Project Documentation Conventions (TickGuard)

Reference doc, not a Claude Code Skill — no auto-invocation, just the rules to follow
when touching documentation in this repo.

## Where things go

- **`README.md`** — landing page only. Short project intro (what it is, why it
  exists), a minimal Quick Start (build + test, a handful of commands), and a
  link to the developer guide. Do not add deep architecture material here —
  move it to the developer guide instead.
- **`doc/developer-guide.md`** — the full architecture reference: component
  diagrams, the `EventDescriptor` debounce/heartbeat state machine, timing
  behavior, lifecycle flow, the runnable example, and the detailed build/test
  section (CMake options, Catch2 `FetchContent` notes, test-injection
  patterns). Mermaid diagrams belong here, not in the README.
- **`doc/adr/NNNN-title.md`** — one Architecture Decision Record per
  significant, durable decision (language standard, tooling choices, protocol
  changes, etc.), numbered sequentially starting at `0001`. Use the
  Context / Decision / Consequences shape (see `doc/adr/0001-cpp20-linting-testing.md`
  for the canonical example). An ADR records *why*, not *how* — don't restate
  rules that already live in `AGENTS.md`, `.clang-tidy`, or `.clang-format`;
  cross-reference them instead.
- **`AGENTS.md`** — condensed, code-facing conventions (naming, lifecycle
  order, thread-safety rules, test patterns) meant to be read quickly before
  writing code. Keep it terse; move rationale/history to an ADR.

## Rule of thumb

If a change to documentation explains *what the code currently does*, it goes
in the developer guide or `AGENTS.md`. If it explains *why a decision was
made and what it should protect against changing carelessly*, it goes in an
ADR. The README should not need to change when internal architecture details
change — only when the project's overall purpose or quick-start steps change.

## Link hygiene (required)

Before finishing any change that touches a Markdown doc, verify every link
in the file(s) you edited:

- **Links to code** (`include/tickguard/...`, `tests/...`, `examples/...`,
  specific symbols/anchors) must point at a path and, where a line/symbol is
  referenced, a target that still exists after your change. If you renamed
  or moved a file/symbol, grep the docs for the old path and update every
  reference.
- **Links to other docs** (README ↔ developer guide ↔ ADRs ↔ AGENTS.md)
  must resolve to an existing file and, if a heading anchor is used, an
  existing heading.
- **External links** should be checked for obvious staleness (wrong repo
  org/name, dead redirects) when you touch the line they're on — you are not
  required to proactively crawl links you didn't touch.

Dead links are prohibited — do not merge/commit a doc change that leaves a
link pointing at a path, heading, or symbol that no longer exists. If a
target is being removed intentionally, update or delete the link in the same
change rather than leaving it dangling.
