# [PROJECT_NAME] Constitution
<!-- preset:zdx-core0 — Zingcore Dynamics firmware built on ZDX CORE-0 -->

[PROJECT_NAME] is Zingcore Dynamics handheld firmware built on the **ZDX CORE-0** foundation (version [CORE0_VERSION]).
The foundation specification lives in `zdx-core0/spec/`; the foundation code lives in `zdx-core0/lib/core0/` (a git submodule pinned to a release).
Where this constitution and the foundation spec disagree, this constitution wins and the conflict is
raised upstream.

Target hardware: [MCU_AND_BOARD] · Execution profile: [SUPERLOOP|RTOS|DUALCORE] · Power source: [BATTERY]

## Core Principles

### I. Nothing Blocks (NON-NEGOTIABLE)
No `delay()`, busy-wait or blocking transaction > 1 ms in any module `tick()`, screen, or event
handler. Slow operations (display refresh, storage mount, radio connect, sensor conversion) are
explicit state machines advanced by `tick()`. Module `tick()` targets < 1 ms (hard limit 5 ms).

### II. Static Memory After Init (NON-NEGOTIABLE)
No heap allocation after `sys::begin()`: no `new`/`malloc`, no allocating STL containers, no
growing Arduino `String`. Use static objects, fixed pools and `SpscRing`. Every plan states RAM
and flash usage per new module.

### III. Layer Boundaries
App → services → driver interfaces → port (`hal::`). Screens use service APIs only. Services
never include concrete drivers. Pins and wiring exist only in `include/board_config.h` and
`src/drivers/`. Cross-module communication is via the event bus or a module's public API.

### IV. Power Is a Feature
Every module defines its power contract (`can_enter`, `on_suspend`, `on_resume`) or documents why
none is needed. Activities that must not be interrupted hold a `PowerLock`. Every feature states
its impact on the power budget, and on-target current is measured, not assumed.

### V. Responsive Input
Key press → visible response ≤ 30 ms (OLED) / ≤ 300 ms (e-ink); key → click sound ≤ 15 ms.
Screens react to logical keys and gestures, never to pins. A press that wakes the device never
also triggers an action unless the feature spec says so explicitly.

### VI. Wrap-Safe Time, Capture-Only ISRs
All `millis()` comparisons use `time_reached`/`time_since`/`time_until`. ISRs only capture data and
post events or push to a ring: no logging, allocation, driver calls or floating point.

### VII. Host-Testable Logic
Logic that does not touch hardware builds and passes Unity tests in `env:native` with the mock
port. Every acceptance criterion that can run natively has a test written before the
implementation is considered done.

### VIII. Observable and Recoverable
Every module logs init result and state changes, registers a self-test, and is inspectable via the
CLI. Failures degrade a feature, never the device. Crash loops land in safe mode, never a brick.
Persisted data uses the settings service (A/B slots, CRC, versioned migration).

### IX. Compile-Time Configuration
Optional capability sits behind a `CORE0_FEAT_*` (foundation) or `APP_FEAT_*` (product) flag.
Disabled means zero code and zero RAM. Board revisions differ only in `board_config.h`.

### X. Simplicity
Prefer the foundation's existing mechanism over a new one. A new abstraction, thread/task, core-1
placement or dependency must be justified in the plan's Complexity Tracking table.

## Engineering Standards

- **Toolchain**: PlatformIO + Arduino C++. Foundation headers are C++11-clean; product code may use
  the toolchain's newest supported standard.
- **Conventions**: `zdx-core0/spec/01-conventions.md` (naming, module file pattern, events,
  logging, budgets).
- **Bring-up order** for new hardware: port → loop/CLI → input → display/UI → settings/clock →
  audio/feedback → battery/power → self-test/fault → features (`zdx-core0/spec/00-architecture.md` §8).
- **Release builds**: `CORE0_LOG_LEVEL` ≤ WARN, watchdog enabled, BuildInfo populated from git.

## Development Workflow

- Each feature is specified with Spec Kit (`/speckit-specify` → `/speckit-plan` → `/speckit-tasks`
  → `/speckit-implement` → `/speckit-converge`).
- A plan is not ready until its Foundation Mapping, Resource Budget and Power Impact tables are
  complete.
- Hardware acceptance results (latency, current, drift) are recorded in the feature's checklist,
  with the measuring method.
- Changes needed in `zdx-core0/lib/core0/` are made as foundation changes (spec + header + version bump),
  never as silent product-local forks.

## Governance

This constitution supersedes ad-hoc practice in this repository. Amendments require: the reason,
the principle affected, and a version bump below. Principles I and II cannot be waived for a
feature; other principles may be waived per feature only through a documented Complexity Tracking
entry approved by [OWNER].

**Version**: [CONSTITUTION_VERSION] | **Ratified**: [RATIFICATION_DATE] | **Last Amended**: [LAST_AMENDED_DATE]
