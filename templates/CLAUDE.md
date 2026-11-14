# <PRODUCT NAME> firmware — agent guide

Zingcore Dynamics firmware, built with PlatformIO + Arduino C++ on the **ZDX CORE-0** foundation.

## Read before writing code
- `.specify/memory/constitution.md` contains the non-negotiable rules (non-blocking, no heap after init, layer boundaries, power contracts).
- `zdx-core0/spec/00-architecture.md` covers layers, the loop, execution profiles, boot, and bring-up order.
- `zdx-core0/spec/01-conventions.md` covers naming, the module file pattern, events, and budgets.
- `zdx-core0/spec/modules/<module>.md` holds the contract and acceptance criteria for any module you touch.
- `zdx-core0/lib/core0/src/core0/*.h` holds the foundation API. `zdx-core0/` is a git submodule pinned to a release.
  Change it only as a foundation change: see "Changing the foundation from a product" in `zdx-core0/README.md`.

## Where things go
| What | Where |
|---|---|
| Pins, features, sizing | `include/board_config.h` (only here) |
| Product features (app modules) | `src/app/` (namespace `app`) |
| Screens | `src/app/screens/` |
| Hardware drivers (implement core0 interfaces) | `src/drivers/` |
| MCU port (`hal::`) | `src/port/port_<mcu>.cpp` |
| Generated assets | `src/assets/gen/` (from `assets/src` via `tools/img2c.py`) |
| Tests | `test/native/` (host), `test/target/` (device) |

## Commands
- Build: `pio run -e <env>`
- Upload + monitor: `pio run -e <env> -t upload && pio device monitor`
- Host tests: `pio test -e native`
- Foundation tests (after changing the submodule): `pio test -e native_core0 -d zdx-core0`
- Size report: `pio run -e <env> -t size`

## Working rules
- Never add `delay()`, blocking loops, `new`/`malloc`, or `String` concatenation in runtime paths.
- New behavior with more than two states is an explicit `enum class` state machine advanced in `tick()`.
- Every new module needs: a power contract, init logging, a self-test, and a CLI view.
- Write native tests for host-testable acceptance criteria before or alongside the implementation.
- Spec Kit flow per feature: `/speckit-specify` → `/speckit-plan` → `/speckit-tasks` → `/speckit-implement` → `/speckit-converge`.
