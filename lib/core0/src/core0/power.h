// ZDX CORE-0 — power manager.
//
// State machine:  Active -> Dim -> Idle -> Sleep -> Deep (-> Off)
// driven by an inactivity policy. Any user activity returns to Active.
//
// A transition to a lower state happens only when:
//   1. no PowerLock is held that forbids it, AND
//   2. every module's can_enter(target) returns true.
// Then: on_suspend() in reverse registration order -> hal::sleep() ->
//       timers::compensate_sleep() -> on_resume() in forward order ->
//       PowerCode::Resumed event.
//
// Idle is the workhorse: the loop sleeps (hal SleepDepth::Idle) until the
// next deadline (sys::next_deadline) or an interrupt. Most battery life comes
// from making Idle the common case, not from Deep.
#pragma once
#include "event.h"
#include "module.h"
#include "port.h"

namespace core0 {

struct PowerPolicy {
  ms_t     dim_after_ms;    // inactivity -> Dim   (0 = never)
  ms_t     idle_after_ms;   // inactivity -> Idle  (0 = never)
  ms_t     sleep_after_ms;  // inactivity -> Sleep (0 = never)
  ms_t     deep_after_ms;   // inactivity -> Deep  (0 = never)
  uint32_t wake_mask;       // hal::WakeSource bits armed in Sleep/Deep
};

static const PowerPolicy kDefaultPowerPolicy = {
  15000, 30000, 120000, 0, hal::kWakeButton | hal::kWakeRtc | hal::kWakeUsb,
};

// Event codes for EventType::Power.  arg = new PowerState, data as noted.
enum class PowerCode : uint8_t {
  StateChanged,    // data = previous state
  Resumed,         // data = WakeSource bits
  LowBatteryForce, // battery critical: forcing Off / Deep
};

// Lock = "do not go below this state" (e.g. recording holds Idle; a
// visible countdown holds Dim). Handles are small integers from a static pool.
typedef int8_t PowerLock;
static const PowerLock kNoLock = -1;

namespace power {

Status set_policy(const PowerPolicy& p);
const PowerPolicy& policy();

PowerState state();

// User activity (input module calls this automatically on every key event).
void activity(ms_t now);

// Request a state now (menu "Sleep" item, power key long press). Still subject
// to locks/vetoes, except Off which only waits for vetoes.
Status request(PowerState target);

PowerLock lock(PowerState floor, const char* owner);
void      unlock(PowerLock& l);   // sets l = kNoLock

// RTC wake time for Sleep/Deep (e.g. next alarm). 0 = none.
void set_wake_at(uint32_t epoch_seconds);

// Called by sys::run_once.
void service(ms_t now);

extern const Module kModule;

}  // namespace power
}  // namespace core0
