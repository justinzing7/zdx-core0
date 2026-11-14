# ZDX CORE-0 Quick Start

How to get the foundation spec, the header skeleton, and the Spec Kit preset into a PlatformIO project you work on with the Claude Code extension.

## "Can I just drop the plan into chat?"

You can, but the context only lasts for that session. Put the files **in the repo** instead and point Claude Code at them with a `CLAUDE.md`. Claude Code reads `CLAUDE.md` at the start of every session, so every conversation already knows the rules, the spec location, and the folder layout. Spec Kit then turns each feature into a spec, a plan, and tasks that also live in the repo.

## Prerequisites (one time)

- VS Code with the **PlatformIO IDE** and **Claude Code** extensions
- git
- Python 3.11+ and [uv](https://docs.astral.sh/uv/) (for Spec Kit)
- Spec Kit CLI:
  ```bash
  uv tool install specify-cli
  ```

## 1. Create the PlatformIO project

Create the project in PlatformIO Home → **New Project**: pick your board, use the Arduino framework, and open the folder in VS Code. Alternatively, start from `templates/platformio.ini`.

## 2. Add the foundation

Add this repo as a submodule, pinned to a release:

```bash
git submodule add https://github.com/justinzing7/zdx-core0 zdx-core0
git -C zdx-core0 checkout v0.1.0
git add zdx-core0 && git commit -m "Add ZDX CORE-0 0.1.0"
```

A fresh clone of the product then needs `git clone --recurse-submodules`, or `git submodule update --init` afterwards.
The foundation library is used from `zdx-core0/lib/` (`lib_extra_dirs`) and the spec is read at `zdx-core0/spec/`.

Then copy the starting files from `zdx-core0/templates/`:

| Copy from (zdx-core0/) | To (your project/) |
|---|---|
| `templates/include/board_config.h` | `include/board_config.h` |
| `templates/src/main.cpp` | `src/main.cpp` (replace the default) |
| `templates/tools/` | `tools/` |
| `templates/CLAUDE.md` | `CLAUDE.md` |
| `templates/platformio.ini` | merge the `[env]` section and the envs you need |

From the project root:

```bash
mkdir -p include src tools
cp zdx-core0/templates/include/board_config.h include/ && cp zdx-core0/templates/src/main.cpp src/
cp -r zdx-core0/templates/tools/* tools/ && cp zdx-core0/templates/CLAUDE.md .
```

To take a newer release later: `git -C zdx-core0 fetch --tags && git -C zdx-core0 checkout v<x.y.z>`, read its
`CHANGELOG.md` entry, then commit the submodule change.

## 3. Add Spec Kit + the CORE-0 preset

In the project root terminal:

```bash
git init                      # if not already a repo (Spec Kit uses feature branches)
specify init --here --force --integration claude
specify preset add --dev ~/zingcore/zdx-core0/speckit-preset/zdx-core0
specify preset list           # should show "ZDX CORE-0 Embedded Foundation — enabled"
```

This adds `.specify/` (templates, scripts, memory) and `.claude/skills/speckit-*`. Those skills are what the `/speckit-…` commands invoke in Claude Code.

## 4. Drive it from Claude Code

Open the Claude Code panel in VS Code and run these one at a time, reviewing the output between steps.

```text
/speckit-constitution Project: <product name>. MCU/board: <e.g. FireBeetle 2 ESP32-E>.
  Profile: RTOS. Power: 1S LiPo. Owner: <you>. Foundation version 0.1.0.

/speckit-specify <what the feature does for the user — no tech details>

/speckit-plan PlatformIO + Arduino on <board>. Follow zdx-core0/spec. <libraries you want,
  e.g. U8g2 for the SSD1306, ESP32 I2S driver for MAX98357A + SPH0655>

/speckit-tasks
/speckit-implement
/speckit-converge            # repeat implement → converge until "Converged"
```

Optional quality gates: `/speckit-clarify` before plan, `/speckit-analyze` after tasks, `/speckit-checklist`.

## 5. First feature for any new product: foundation bring-up

The services are implemented (0.1.0), so a new product's first feature brings them up on its own board: the drivers
and the MCU port, plus the product's screens and settings. For example:

```text
/speckit-specify Bring up the ZDX CORE-0 foundation on this board: boot banner over serial,
  splash screen, three-button navigation of a settings menu that persists across reboot,
  key-click sounds, status LED, battery readout, and dim/idle/sleep with wake on button.
```

The native port comes from the submodule's test kit. An ESP32 product can start from the pilot's
`src/port/port_esp32.cpp`; ports for other MCUs are written per product (see `spec/modules/core.md` "Port").

## Without Spec Kit

`CLAUDE.md` plus `zdx-core0/spec/` is enough for ordinary chat. For example: *"Add a gesture per zdx-core0/spec/modules/input.md, with native tests for each acceptance criterion."* Spec Kit adds the durable spec, plan, and tasks per feature, and the constitution gate.

## Checking the setup

```bash
specify preset resolve plan-template   # shows the CORE-0 sections appended
pio run -e esp32                       # compiles and links once board drivers and the port exist
pio test -e native                     # product host tests, with the foundation test kit
```
