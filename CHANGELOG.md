# Changelog

Versions follow `CORE0_VERSION_*` in `lib/core0/src/core0/config.h`. While the major version is 0, a minor release may
change an API, and its notes say what a product must change.

## 0.1.0 (2026-11-14)

First release with working services. Built and validated in the pilot product, zingcore_remtrak feature 001 (FireBeetle
2 ESP32-E on carrier rev A, RTOS profile). The full record is in that repo's
`specs/001-foundation-bringup/upstream-notes.md` and `checklists/hardware.md`.

### Added
- The service implementations, 18 files in `lib/core0/src/core0/`: sys, bus, timers, log, fault, cli, power, input,
  display, ui, splash_screen, menu_screen, settings, audio, feedback, battery, clock, selftest.
- The host test kit, `lib/core0_testkit/`: the native port, `test_helpers.h` and generic mocks. Host builds only.
- 21 foundation suites in `test/native/` (192 tests), run with `pio test -e native_core0`.

### Changed headers (against the 0.1.0 skeleton)
- `fault.h`: `FaultRecord` keeps a copy of the crashing module's name (`module_name[12]`), and `note_module()` takes an
  optional name. The retained-record magic changed. Report `module_name`, not a lookup of `module_index`: safe mode
  registers a different module list.
- `config.h`: includes `<esp_attr.h>` on ESP32 (`CORE0_RETAINED` and `CORE0_ISR_ATTR` failed to compile without it).
- `anim.h`: `SplashScreen` reports its own deadline, so the loop cannot sleep past `min_ms` or `max_ms`.
- `selftest.h`, `timer.h`: comments only (the self-test step contract, `requested_at_boot()` timing, `stop()` frees the
  slot).

### Behaviour to know
- A timer-only wake from Sleep resumes every module and reports Idle. Display and LED stay off, and the device returns
  to sleep after the due timers run. `request(Active)` turns the screen on. A product `on_resume` that powers
  user-facing hardware must leave it off when the wake bits are only `kWakeRtc`.
- A person setting the time (`ClockSource::Manual` or `Host`) is never refused.
- Menus move Up/Down on `Press` and `Repeat`. Select and Back act on release.
- `power::set_wake_at()` only stores the time in this release.

### Validation
- Host: 192/192 foundation tests. Every `.cpp` compiles warning-free with `-std=gnu++11 -Wall -Wextra -pedantic` and
  only `-Ilib/core0/src`, for defaults, every optional feature off, and another board (superloop, encoder, 3 keys).
- Target: the pilot's full quickstart run, including key-to-screen 6.4 to 8.4 ms, key-to-click 12.4 to 13.9 ms, wake
  from Sleep 7.4 to 8.8 ms, three panics to safe mode, and a 60 min heap soak with no change.

### Not in this release
Planned with the first product that needs them: a per-screen opt-out of the wake-press swallow, a public
`hal::wake_loop()`, a battery divider hook that does not use Arduino calls, CLI subcommands for built-ins, an amp
keep-warm hook on `AudioOut`, `set_wake_at` as a sleep limit, and a power lock for battery samples during a background
wake.
