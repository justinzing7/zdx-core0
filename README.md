# ZDX CORE-0

**Zingcore Dynamics firmware foundation** for handheld embedded devices: MP3 players, alarm clocks and dream recorders, focus timers, weather displays.

PlatformIO + Arduino C++. Targets ESP32, ATSAMD21, STM32, and RP2040 (ATmega328 with modules trimmed).

> Working name: **ZDX CORE-0**. The code namespace is `core0`, so renaming the platform only touches the docs.

## What's here

```
zdx-core0/
├─ QUICKSTART.md                 ← start here: get it into a PlatformIO project + Claude Code
├─ CHANGELOG.md                  what each release changed, and what a product must do about it
├─ spec/
│  ├─ 00-architecture.md         layers, loop, profiles, the 10 rules, boot, bring-up order
│  ├─ 01-conventions.md          C++ subset, naming, module pattern, events, budgets
│  └─ modules/                   one contract per module, each with acceptance criteria
│     core · input · display · ui · power · battery · audio · feedback
│     settings · clock · diagnostics · storage-net · assets · testing
├─ lib/core0/                    the foundation (PlatformIO library)
│  └─ src/core0/                 25 headers + 18 service implementations
├─ lib/core0_testkit/            host builds only: native port, test_helpers.h, mocks
├─ test/native/test_core0_*      foundation suites (pio test -e native_core0)
├─ kit/                          board_config.h and BuildInfo for running the suites here
├─ templates/                    files copied into each product repo
│  ├─ platformio.ini             esp32 / samd21 / stm32 / rp2040 / native envs
│  ├─ include/board_config.h     pins, features, sizing
│  ├─ src/main.cpp               boot sequence + settings + data-driven menu example
│  ├─ tools/git_rev.py           injects git SHA into BuildInfo
│  ├─ tools/img2c.py             PNG / frame folders → Bitmap / Animation headers
│  └─ CLAUDE.md                  tells Claude Code where the rules and spec live
├─ speckit-preset/zdx-core0/     GitHub Spec Kit preset (constitution + spec/plan/tasks/checklist additions)
└─ pilot/dream-recorder/         pilot mapping, feature sequence, open questions, board_config.h
```

## Status

| Part | State |
|---|---|
| Release | **0.1.0** (see `CHANGELOG.md`) |
| Spec | 0.1.0, matching the code |
| Headers | Complete interfaces. Compile clean as C++11 and C++17 with `-Wall -Wextra -pedantic` |
| Service implementations (`.cpp`) | All 18, validated on the pilot board (ESP32, RTOS profile). Other MCUs need their port |
| Tests | 192 host tests in 21 suites: `pio test -e native_core0` |
| Spec Kit preset | Verified to install with `specify` 1.1.x. Templates compose correctly |

## Using it in a product

Add this repo as a git submodule at `zdx-core0/` in the product root, pinned to a release tag, and point PlatformIO at
its libraries with `lib_extra_dirs = zdx-core0/lib`. `QUICKSTART.md` has the steps.

## Changing the foundation from a product

A change belongs here only if any board could use it with nothing but its `board_config.h`: no pins, product logic or
board names. If only part of a feature is generic, put that part here and keep the rest in the product.

1. In the product, make the change inside the `zdx-core0` submodule on a branch, with the spec update and a
   `test_core0_*` test. Run `pio test -e native_core0` in the submodule and the product's own builds and tests.
2. Log every header or spec change in the product feature's upstream notes, with the reason.
3. Push the submodule branch, open a pull request here, and add the change to `CHANGELOG.md`.
4. After merging, tag a release (patch for fixes, minor for new or changed APIs) and bump `CORE0_VERSION_*` in
   `config.h` and the two `library.json` files.
5. Move the product's submodule to the new tag.

## Design in one paragraph

A cooperative superloop runs a static event queue. Each part of the system is a `Module`: a const table of `init`/`tick`/power hooks. Modules talk through 8-byte events. ISRs only capture data. Nothing blocks, and nothing allocates after init. Screens sit on a fixed-depth stack and draw through a thin `Gfx` interface, while a render service decides when and how to refresh, which matters for e-ink. Power is a state machine (Active→Dim→Idle→Sleep→Deep→Off) with locks and vetoes; the MCU idles between events in every awake state. Settings persist as a versioned POD struct in A/B NVM slots with CRC. Every MCU-specific detail sits behind `port.h`, and a native port runs the logic and UI tests on a PC.
