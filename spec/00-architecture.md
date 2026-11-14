# ZDX CORE-0 — Architecture

**Foundation version:** 0.1.0 · **Owner:** Zingcore Dynamics · **Status:** draft (pilot: dream recorder)

ZDX CORE-0 is the firmware foundation for Zingcore handheld devices: battery-powered gadgets with buttons or a knob, a small OLED or e-ink display, menus, sounds, and sleep. It defines **structure and contracts**, not finished products. Every product starts from CORE-0 and adds its own features on top.

The foundation is written for PlatformIO + Arduino C++. Primary targets are ATSAMD21, ESP32, STM32, and RP2040. ATmega328 should still compile with most modules disabled, but it is not a design target.

---

## 1. Layers

```
┌─────────────────────────────────────────────────────────────────┐
│ APP        screens, device features (player, recorder, timer)   │  src/app/
├─────────────────────────────────────────────────────────────────┤
│ SERVICES   event bus · timers · input · display · ui · power    │  lib/core0/
│            battery · audio · feedback · settings · clock        │
│            log · cli · fault · selftest · [storage] · [net]     │
├─────────────────────────────────────────────────────────────────┤
│ DRIVERS    panel, amp, mic, RTC, LED, sensors (implement the    │  src/drivers/
│            service interfaces: Panel, AudioOut, RtcSource ...)  │
├─────────────────────────────────────────────────────────────────┤
│ PORT       millis, critical sections, sleep, watchdog, reset    │  src/port/
│            reason, NVM bytes, console  (hal:: in port.h)        │
└─────────────────────────────────────────────────────────────────┘
          board_config.h: pins, features, sizing (include/)
```

Dependencies only point downward. The app depends on services. Services depend on driver **interfaces** and the port, never on concrete drivers. Drivers may use Arduino and vendor libraries freely.

## 2. Runtime model

Execution is a **cooperative superloop with an event queue**. Arduino's `loop()` calls `sys::run_once()`, and nothing else:

```
run_once():
  now = hal::millis()
  timers::service(now)          fire due soft timers (callbacks / events)
  for m in modules: m.tick(now) each returns in < 1 ms (hard limit 5 ms)
  bus::dispatch(budget)         deliver queued events to subscribers
  power::service(now)           maybe dim / idle-sleep until next deadline
  hal::watchdog_feed()          the ONLY feed point
```

- **ISRs** do not run logic. They capture raw data (a key edge, a DMA half-buffer, an RTC alarm) and either push into an SPSC ring or call `bus::post()`.
- **Modules** communicate through events (`event.h`) and through each other's small public APIs. They never reach into another module's internals or drivers.
- **Idle is the default.** When no module has work before its next deadline, `power::service` puts the MCU into `SleepDepth::Idle` until the earliest deadline or an interrupt. Battery life comes mainly from this, not from deep sleep.

## 3. Execution profiles

The app code is identical across profiles. Only the scheduling glue in the port and `sys` changes.

| Profile | Targets | How services run | Cross-context communication |
|---|---|---|---|
| `SUPERLOOP` | SAMD21, STM32, ATmega | Everything runs in `loop()` | ISR → `bus::post` / `SpscRing` |
| `RTOS` | ESP32 (Arduino runs on FreeRTOS) | Still one loop task by default. A service *may* move to its own task (e.g. I2S audio pump) if its spec allows it | Task → `SpscRing` or `bus::post` |
| `DUALCORE` | RP2040, ESP32 (opt-in) | Core 0 runs the superloop. Core 1 runs only **core-1-owned** work: audio stream pump, display flush, capture | `SpscRing` only. No shared mutable state, no bus calls from core 1 except `bus::post` |

Rule: a module declares in its spec which profile-specific placement it supports. Default placement is the superloop.

## 4. Non-negotiable rules

These rules are mirrored in the Spec Kit constitution (`speckit-preset/`). Any deviation must be recorded in a plan's *Complexity Tracking* table.

1. **Nothing blocks.** No `delay()`, busy-wait, or blocking bus transaction longer than 1 ms in a module, screen, or handler. Long operations (e-ink refresh, SD mount, Wi-Fi connect) are state machines driven by `tick()`.
2. **No dynamic allocation after `sys::begin()`.** Use static objects, fixed pools, and rings. No `new`, `malloc`, or growing `String` in steady state.
3. **Layer boundaries hold.** Services see only driver interfaces and `hal::`. Screens see only service APIs. Pins exist only in `board_config.h` and drivers.
4. **Every module has a power contract.** It implements `can_enter`, `on_suspend`, and `on_resume`, or documents why each is not needed.
5. **Time is wrap-safe.** Use `time_reached()`, `time_since()`, and `time_until()`. Never compare `millis()` values with `<` or `>`.
6. **ISRs only capture.** No logging, no allocation, no driver calls, no floating point in ISRs.
7. **Everything compiles out.** Each optional module sits behind a `CORE0_FEAT_*` flag. Disabled means zero code and zero RAM.
8. **One source of truth per fact.** Pins → `board_config.h`. Persisted state → the settings struct. Build identity → `BuildInfo`. UI strings for a screen → that screen.
9. **Observable by default.** Every module logs init success/failure, registers a self-test, and exposes its state through a CLI command.
10. **Host-testable logic.** Logic that does not touch hardware (gestures, menus, curves, settings migration, time math) must build and pass tests in `env:native`.

## 5. Project layout (every product)

```
<product>/
├─ platformio.ini
├─ CLAUDE.md                  points the agent at docs/core0 + .specify
├─ include/
│  └─ board_config.h          pins, features, sizing
├─ lib/core0/                 the foundation (vendored or git submodule)
├─ src/
│  ├─ main.cpp                setup(): fault::init, register modules, bind config, sys::begin
│  ├─ app/                    app module(s) + screens/
│  ├─ drivers/                Panel, AudioOut, RtcSource ... implementations
│  └─ port/                   port_esp32.cpp | port_samd21.cpp | port_native.cpp ...
├─ assets/                    source art (PNG, fonts) → generated headers (assets.md)
├─ test/                      Unity tests (native first)
├─ docs/core0/                copy of this spec (read-only in product repos)
└─ .specify/                  Spec Kit (constitution, templates, specs/)
```

## 6. Boot sequence

1. `fault::init()` reads the reset reason, updates the crash counter, and decides whether to enter safe mode.
2. Register modules in dependency order. The usual order is power → input → battery → settings → clock → audio → feedback → display → ui → cli → storage/net → app. A product may change it where one module needs another first, and records why in its plan. For example, a board module that feeds the keys registers before input, and battery follows settings so a boot-time low-battery tone uses the stored volume.
3. Bind configuration: `input::configure`, `battery::configure`, `display::attach`, `settings.bind()`, and so on.
4. `sys::begin()` inits each module. Failures are logged and that module is skipped; the rest still run. Then it starts the watchdog and posts `SystemCode::InitDone`.
5. Push the first screen: `SplashScreen` → home. In safe mode, push the safe-mode screen. If the factory key combo is held, push the self-test screen.

The splash screen appears as soon as display init finishes. It covers the remaining bring-up (settings load, RTC read, SD mount) instead of extending boot time.

## 7. Module index

| Module | Header | Spec | Default |
|---|---|---|---|
| Core: registry, loop, events, timers, profiles | `module.h` `event.h` `timer.h` `ring.h` | [core](modules/core.md) | always |
| Port / HAL | `port.h` | [core §Port](modules/core.md#port) | always |
| Input | `input.h` | [input](modules/input.md) | on |
| Display | `display.h` | [display](modules/display.md) | on |
| UI: screens, menus, animation, splash | `ui.h` `anim.h` | [ui](modules/ui.md) | on |
| Power | `power.h` | [power](modules/power.md) | on |
| Battery | `battery.h` | [battery](modules/battery.md) | on |
| Audio (tone / stream / capture) | `audio.h` | [audio](modules/audio.md) | on (tone) |
| Feedback: LED, haptic | `feedback.h` | [feedback](modules/feedback.md) | on |
| Settings | `settings.h` | [settings](modules/settings.md) | on |
| Clock / RTC | `clock.h` | [clock](modules/clock.md) | on |
| Log, CLI, fault, version, self-test | `log.h` `cli.h` `fault.h` `version.h` `selftest.h` | [diagnostics](modules/diagnostics.md) | on |
| Storage, connectivity | `storage.h` `net.h` | [storage-net](modules/storage-net.md) | off |
| Assets (bitmaps, fonts, animations) | `display.h` (`Bitmap`) | [assets](modules/assets.md) | — |
| Testing (native build, mock port) | — | [testing](modules/testing.md) | — |

## 8. Bring-up order (new board)

This is the order the Spec Kit tasks template enforces. Each step has a visible checkpoint.

1. **Port + console.** `hal::millis`, console, reset reason. *Checkpoint:* boot banner shows BuildInfo and reset reason.
2. **Core loop + timers + CLI.** *Checkpoint:* `mods`, `ver`, and `ev` commands work.
3. **Input.** *Checkpoint:* key events are logged with correct gestures.
4. **Display + UI.** *Checkpoint:* splash, then the settings menu, navigable with keys or `key` CLI commands.
5. **Settings + clock.** *Checkpoint:* settings survive a reset, and time survives sleep.
6. **Audio + feedback.** *Checkpoint:* key clicks play, a melody plays, and the LED shows patterns.
7. **Battery + power.** *Checkpoint:* the device dims, idles, sleeps, and wakes on a key. Measured current is recorded in the board's power budget.
8. **Self-test + fault.** *Checkpoint:* factory mode passes, and a forced panic leads to safe mode after three resets.
9. **Product features** (the app), one Spec Kit feature at a time.

## 9. Versioning

The foundation uses semver: `CORE0_VERSION_*` in `config.h`. A MAJOR bump means a header contract changed. Product repos pin a foundation version, either as a submodule tag or a vendored copy with its version noted. Changes discovered during a product build are upstreamed into CORE-0 rather than forked silently.
