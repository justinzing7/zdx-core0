# Testing

## Levels

| Level | Where | What | Tooling |
|---|---|---|---|
| **Unit** | `env:native` | pure logic: gestures, timers, menus, settings slots/migration, curves, time math, easing | PlatformIO `test/` + Unity, mock port |
| **Screen** | `env:native` | screens draw into a mock framebuffer → PBM/PNG golden images | mock `Panel`/`Gfx` |
| **On-target** | device | drivers, timing budgets, power states | `pio test -e <target>`, CLI scripts |
| **HIL / jig** | device + serial | CLI `key`/`ev` injection, `SELFTEST PASS n/n` parsing | small Python script |
| **Power** | device + meter | current per state (fills the power budget table) | PPK2 / µCurrent / INA219 |

## Native build

- `src/port/port_native.cpp` implements `hal::` using a **controllable clock**: tests advance time explicitly, with no real sleeping. It provides an NVM file or array with fault injection (cut power at byte N), and `console_*` on stdout/stdin.
- `main()` for native calls `setup()` and then loops `run_once()`, or tests drive `run_once()` directly.
- Drivers are excluded from native through `build_src_filter`. Mock `Panel`, `AudioOut`, and `RtcSource` live in `test/mocks/`.

## Rules (also in the constitution)

- Every foundation module has native unit tests for its acceptance criteria wherever they don't need hardware.
- Every product feature spec lists acceptance criteria, and `/speckit-tasks` produces a test task for each criterion that can run natively.
- Bugs get a regression test at the lowest level that reproduces them.
- On-target acceptance results (timings, currents) are recorded in the feature's `specs/NNN/checklists/` or the plan's tables, not just "it worked".
