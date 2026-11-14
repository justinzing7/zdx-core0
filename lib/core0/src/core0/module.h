// ZDX CORE-0 — module contract and core loop.
//
// Everything (foundation services, drivers with state, app features) is a
// Module: a const table of optional hooks. Tables live in flash; the registry
// is a static array of pointers. Registration order = init/tick order;
// sleep preparation runs in REVERSE order, resume in forward order.
#pragma once
#include "types.h"

namespace core0 {

// System power states, lowest power last (power.h owns the policy).
enum class PowerState : uint8_t {
  Active,  // full UI, display on
  Dim,     // display dimmed, everything else running
  Idle,    // display off/static, MCU idles between events, wake is instant
  Sleep,   // light sleep: RAM retained, wake on button/RTC
  Deep,    // deep sleep / standby: may reset on wake
  Off,     // soft power-off (wake only by power button / charger)
};

struct Module {
  const char* name;

  // Called once at boot, in registration order. Must not block > 50 ms;
  // long hardware bring-up is split across tick() as a state machine.
  Status (*init)();

  // Called every loop pass while the system is awake. Must return quickly
  // (target < 1 ms, hard limit 5 ms). `now` is hal::millis() for this pass.
  void (*tick)(ms_t now);

  // Earliest time this module needs its next tick (for idle sleep sizing).
  // Return `now + large` if event-driven only. nullptr = no constraint.
  ms_t (*next_deadline)(ms_t now);

  // Veto: return false to keep the system out of `target` (e.g. audio playing).
  bool (*can_enter)(PowerState target);

  // Quiesce hardware before entering `target` (reverse order).
  void (*on_suspend)(PowerState target);

  // Restore after waking from `from` (forward order). `wake` = hal WakeSource bits.
  void (*on_resume)(PowerState from, uint32_t wake);
};

namespace sys {

// Register during setup(), before sys::begin(). Returns NoSpace past CORE0_MAX_MODULES.
Status register_module(const Module* m);

// Init all modules, start watchdog, post SystemCode::InitDone.
// Modules whose init() fails are marked faulted and skipped (logged), the rest run.
Status begin();

// One pass of the cooperative loop (call from Arduino loop()):
//   1. now = hal::millis()
//   2. timers::service(now)
//   3. tick every healthy module
//   4. bus::dispatch()
//   5. power::service(now)  — may sleep until the next deadline / wake source
//   6. hal::watchdog_feed() — the ONLY feed point
void run_once();

// Module queries (CLI, diagnostics)
uint8_t       module_count();
const Module* module_at(uint8_t index);
bool          module_healthy(uint8_t index);

// Earliest next_deadline across modules + timers (used by power service).
ms_t next_deadline(ms_t now);

}  // namespace sys
}  // namespace core0
