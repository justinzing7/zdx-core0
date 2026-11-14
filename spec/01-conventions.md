# ZDX CORE-0 — Code Conventions

## Language

- **Language level:** C++11 is the floor, because several Arduino cores still default to `gnu++11`. Foundation headers must compile with `-std=gnu++11 -Wall -Wextra -pedantic`. Product code may use C++17 where its toolchain supports it.
- **Allowed:** classes, virtual interfaces (one vtable per driver type), `enum class`, `constexpr`, templates for containers and typed wrappers, namespaces, RAII guards.
- **Not allowed:**
  - Exceptions and RTTI.
  - STL containers that allocate (`std::vector`, `std::string`, `std::map`).
  - Arduino `String` in steady state.
  - Floating point in ISRs.
  - Floating point in hot paths on MCUs without an FPU (SAMD21, RP2040). Use Q8/Q16 fixed point instead (see `anim.h`).
- **Globals:** permitted only as `static` objects at file scope (screens, drivers, settings). Every module keeps its state in an anonymous namespace inside its `.cpp`.

## Naming

| Thing | Style | Example |
|---|---|---|
| Namespaces | lower | `core0::power`, `app::recorder` |
| Types, enums | `PascalCase` | `PowerState`, `MenuItem` |
| Enum values | `PascalCase` | `PowerState::Idle` |
| Functions | `snake_case` | `power::request()` |
| Constants | `kPascalCase` | `kDefaultPowerPolicy` |
| Macros | `CORE0_*` (foundation), `C0_*` (helpers), `PIN_*`/`BOARD_*` (board) | `CORE0_FEAT_AUDIO` |
| Member data | `snake_` (trailing underscore) | `sel_` |
| Files | `snake_case.h/.cpp` | `home_screen.h` |

Product code uses `namespace app` (or `app::<feature>`). It never adds symbols to `core0`.

## Module file pattern

```cpp
// src/app/recorder.cpp
#include "recorder.h"
namespace app { namespace recorder {
namespace {                     // private state
  enum class St : uint8_t { Off, Arming, Recording, Saving };
  St    st = St::Off;
  core0::PowerLock lock = core0::kNoLock;
  core0::Status init() { ...; return core0::Status::Ok; }
  void tick(core0::ms_t now) { switch (st) { ... } }
  bool can_enter(core0::PowerState t) { return st == St::Off || t <= core0::PowerState::Idle; }
}
const core0::Module kModule = { "recorder", init, tick, nullptr, can_enter, nullptr, nullptr };
}}  // namespace app::recorder
```

- Behavior with more than two states is written as an **explicit state machine** (`enum class` + `switch` in `tick()`). There are no implicit states hidden in boolean flags.
- A public API returns `Status`. It never calls `panic` for recoverable conditions.
- `init()` must not block longer than 50 ms. Slower hardware (e-ink, SD, Wi-Fi) starts in `init()` and finishes through `tick()`.

## Events

- Each module owns one `EventType` and defines its `...Code` enum in its own header.
- App events use `EventType::App + n`. The list of app event types for a product lives in `src/app/app_events.h`.
- Payloads fit in `code` / `arg` / `data` (8 bytes). For anything larger, the event signals "data ready" and the receiver pulls the data through the owner's API.
- Handlers must be idempotent and fast. A handler may post further events, but it never dispatches recursively.

## Timing budgets (defaults; a product may tighten them)

| Budget | Target |
|---|---|
| `tick()` per module | < 1 ms (hard 5 ms → `FaultCode::ModuleOverrun`) |
| Key press → visual response | ≤ 30 ms OLED, ≤ 300 ms e-ink partial |
| Key press → click sound | ≤ 15 ms |
| Encoder detent → menu move | next frame (≤ 33 ms) |
| Boot → first pixel (splash) | ≤ 500 ms |
| Boot → usable home screen | ≤ 2.5 s |

## Memory budgets

Each product records its RAM and flash use per module in its plan. This is the plan template's *Resource Budget* table. Foundation defaults:

| Item | RAM |
|---|---|
| Event queue | 8 B × `CORE0_EVENT_QUEUE_LEN` (32) = 256 B |
| Timers | ~12 B × 16 |
| Module registry | 4 B × 24 |
| Log ring | configurable, 512 B default |
| Audio stream ring | 2 B × 1024 = 2 KB (only with `FEAT_AUDIO_STREAM`) |

## Error handling

- `Status` codes propagate up to the module boundary, where the module logs them once.
- Recoverable driver errors (I2C NAK, SD missing) degrade the feature, emit the module's error event, and the UI explains what happened. The device keeps running.
- Unrecoverable conditions (corrupted internal invariant) use `C0_ASSERT` / `fault::panic`, which records a crash and resets. Safe mode prevents reboot loops.

## Logging

- Tags are short lowercase module names: `"pwr"`, `"bat"`, `"ui"`, `"rec"`.
- Levels:
  - **E:** something failed and a feature degraded.
  - **W:** an unexpected but handled condition.
  - **I:** state changes, at most a few per minute in steady state.
  - **D/T:** development detail only.
- Never log inside a per-frame or per-sample path above Debug level.

## Documentation

Each product feature is specified with Spec Kit (`specs/NNN-feature/`). Each driver gets a short header comment covering the part number, bus, address, timing quirks, and power draw in each state.
