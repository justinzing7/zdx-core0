// ZDX CORE-0 — power manager (power.h, spec/modules/power.md).
//
// service() runs at the end of every loop pass:
//   activity or request(Active) while below Active -> on_resume forward -> Resumed + StateChanged.
//   Target from inactivity or a pending request(), clamped by the lock floors. Going down: vetoes
//   -> on_suspend in reverse -> StateChanged. Sleep/Deep/Off enter hal::sleep on a LATER pass, once
//   the events are dispatched and the drivers have drained what on_suspend queued (panel off).
//   A timer-only wake from Sleep (wake bits == kWakeRtc) is a background wake: on_resume runs for
//   every module, Resumed + StateChanged report Idle, and modules facing the user (display, LED)
//   stay off. The due timers run on the next pass, then the device returns to the state it woke
//   from, unless activity or request(Active) brings it to Active first (power.md).
//   Otherwise: Idle-depth sleep until sys::next_deadline.
// Off path: battery Critical -> LowBatteryForce -> 2 s for the "battery empty" face -> on_suspend(Off)
// (locks and vetoes ignored) -> hal::sleep(Deep). request(Off) is the same but honours vetoes.
// Power contract: the power module itself owns no hardware. CLI view: `pwr`.
#include "config.h"
#if CORE0_FEAT_POWER  // feature off = zero code (constitution IX)

#include "power.h"

#include <stdio.h>
#include <string.h>

#include "battery.h"
#include "cli.h"
#include "event.h"
#include "log.h"
#include "timer.h"

namespace core0 {
namespace power {
namespace {

struct LockSlot {
  bool        used;
  PowerState  floor;
  const char* owner;
};

const ms_t kIdleDeadlineMs = 60000;
const ms_t kRetryMs = 100;        // a blocked step is retried at least this often
const ms_t kOffQuiesceMs = 100;   // Off ignores vetoes after this (drivers get to drain first)

const char* const kStateNames[] = {"Active", "Dim", "Idle", "Sleep", "Deep", "Off"};

PowerPolicy pol = kDefaultPowerPolicy;
PowerState  st = PowerState::Active;
ms_t        last_activity = 0;
uint32_t    wake_at = 0;          // stored for feature 003: needs the clock to become a sleep limit
LockSlot    locks[CORE0_MAX_POWER_LOCKS];
bool        started = false;      // service() does no transitions unless this module is registered
bool        wake_pending = false;
bool        has_req = false;
PowerState  req = PowerState::Active;
bool        blocked = false;      // a step down is waiting on a veto
ms_t        off_at = 0;
bool        off_slept = false;

const char* state_name(PowerState s) {
  const unsigned i = static_cast<unsigned>(s);
  return i < count_of(kStateNames) ? kStateNames[i] : "?";
}

bool vetoed(PowerState target) {
  for (uint8_t i = 0; i < sys::module_count(); ++i) {
    const Module* m = sys::module_at(i);
    if (m && m->can_enter && sys::module_healthy(i) && !m->can_enter(target)) return true;
  }
  return false;
}

// Lock floors clamp how low the device may go.
PowerState lock_floor(PowerState target) {
  for (int i = 0; i < CORE0_MAX_POWER_LOCKS; ++i)
    if (locks[i].used && locks[i].floor < target) target = locks[i].floor;
  return target;
}

PowerState policy_target(ms_t now) {
  const ms_t el = time_since(now, last_activity);
  PowerState t = PowerState::Active;
  if (pol.dim_after_ms && el >= pol.dim_after_ms) t = PowerState::Dim;
  if (pol.idle_after_ms && el >= pol.idle_after_ms) t = PowerState::Idle;
  if (pol.sleep_after_ms && el >= pol.sleep_after_ms) t = PowerState::Sleep;
  if (pol.deep_after_ms && el >= pol.deep_after_ms) t = PowerState::Deep;
  return t;
}

void suspend_all(PowerState target) {
  for (uint8_t i = sys::module_count(); i-- > 0;) {
    const Module* m = sys::module_at(i);
    if (m && m->on_suspend && sys::module_healthy(i)) m->on_suspend(target);
  }
}

void set_state(PowerState next) {
  const PowerState prev = st;
  st = next;
  C0_LOGI("pwr", "%s -> %s", state_name(prev), state_name(next));
  bus::post(make_event(EventType::Power, static_cast<uint8_t>(PowerCode::StateChanged), static_cast<uint16_t>(next),
                       static_cast<uint32_t>(prev)));
}

void go_down(PowerState target) {
  suspend_all(target);
  set_state(target);
}

void resume_all(PowerState from, uint32_t bits, PowerState to) {
  if (from >= PowerState::Sleep) timers::compensate_sleep(0);  // millis keeps running in Light sleep
  for (uint8_t i = 0; i < sys::module_count(); ++i) {
    const Module* m = sys::module_at(i);
    if (m && m->on_resume && sys::module_healthy(i)) m->on_resume(from, bits);
  }
  bus::post(make_event(EventType::Power, static_cast<uint8_t>(PowerCode::Resumed), static_cast<uint16_t>(to), bits));
  set_state(to);
}

void wake(uint32_t bits) {
  wake_pending = false;
  has_req = false;
  blocked = false;
  last_activity = hal::millis();
  resume_all(st, bits, PowerState::Active);
}

// Background wake: last_activity is left alone, and a request for the state we woke from takes the
// device back down once the due timers have run. Activity or request(Active) clears it.
void background_wake(uint32_t bits) {
  const PowerState from = st;
  blocked = false;
  resume_all(from, bits, PowerState::Idle);
  req = from;
  has_req = true;
}

// Quiesce every module and switch off on a later pass. Wakes only by the button, through a reset.
void enter_off() {
  off_at = hal::millis();
  off_slept = false;
  go_down(PowerState::Off);
}

void off_pass(ms_t now) {
  if (off_slept) return;
  if (bus::pending()) return;
  if (vetoed(PowerState::Off) && time_since(now, off_at) < kOffQuiesceMs) return;
  off_slept = true;
  hal::sleep(hal::SleepDepth::Deep, 0, hal::kWakeButton);  // does not return on parts that reset
}

// In Sleep/Deep: everything is suspended, so wait for a wake source. The next timer limits the
// sleep when RTC wake is armed. What on_suspend queued must have drained first (can_enter).
void sleep_pass(ms_t now) {
  if (vetoed(st)) {
    hal::sleep(hal::SleepDepth::Idle, kRetryMs, 0);
    return;
  }
  ms_t max_ms = 0, t;
  if ((pol.wake_mask & hal::kWakeRtc) && timers::next_deadline(t)) {
    const int32_t w = time_until(now, t);
    if (w <= 0) {  // a timer is due: wake in the background, so it runs with the modules resumed
      background_wake(hal::kWakeRtc);
      return;
    }
    max_ms = static_cast<ms_t>(w);
  }
  const hal::SleepDepth depth = st == PowerState::Deep ? hal::SleepDepth::Deep : hal::SleepDepth::Light;
  const uint32_t bits = hal::sleep(depth, max_ms, pol.wake_mask);
  if (bits == hal::kWakeRtc) {
    // Counts only once a timer is due: an early or spurious timer wake just sleeps again.
    ms_t due;
    if (timers::next_deadline(due) && time_until(hal::millis(), due) <= 0) background_wake(bits);
    return;
  }
  if (bits) wake(bits);
}

// One step towards the policy/request target. True if the state changed.
bool step_down(ms_t now) {
  PowerState target = policy_target(now);
  if (has_req && req > target) target = req;
  target = lock_floor(target);
  blocked = false;
  if (has_req && st >= req) has_req = false;
  if (target <= st) return false;
  if (vetoed(target)) {
    blocked = true;
    return false;
  }
  go_down(target);
  return true;
}

void idle_wait(ms_t now) {
  const int32_t wait = time_until(now, sys::next_deadline(now));
  if (wait > 0) hal::sleep(hal::SleepDepth::Idle, static_cast<ms_t>(wait), 0);
}

#if CORE0_FEAT_BATTERY
TimerId    force_timer = kNoTimer;
const ms_t kLowBatteryFaceMs = 2000;  // power.md: "battery empty" face for about 2 s

void on_force_timer(TimerId, void*) {
  force_timer = kNoTimer;
  enter_off();
}

void on_battery(const Event& e, void*) {
  if (e.code != static_cast<uint8_t>(BatteryCode::StateChanged)) return;
  if (static_cast<BatteryState>(e.arg) != BatteryState::Critical || force_timer != kNoTimer) return;
  if (st == PowerState::Off) return;
  C0_LOGW("pwr", "battery critical (%lu mV): off in %lus", static_cast<unsigned long>(e.data >> 8),
          static_cast<unsigned long>(kLowBatteryFaceMs / 1000));
  bus::post(make_event(EventType::Power, static_cast<uint8_t>(PowerCode::LowBatteryForce),
                       static_cast<uint16_t>(PowerState::Off), e.data));
  // No cancel on recovery: a sagging cell often reads higher once the load drops.
  force_timer = timers::start(kLowBatteryFaceMs, TimerMode::OneShot, on_force_timer);
  if (force_timer == kNoTimer) enter_off();  // no timer slot: skip the face, never skip the shutdown
}
#endif

#if CORE0_FEAT_CLI
void cmd_pwr(uint8_t argc, char** argv) {
  if (argc >= 2) {
    static const char* const kNames[] = {"active", "dim", "idle", "sleep", "deep", "off"};
    for (uint8_t i = 0; i < count_of(kNames); ++i) {
      if (strcmp(argv[1], kNames[i]) != 0) continue;
      const Status s = request(static_cast<PowerState>(i));
      cli::print(ok(s) ? "pwr %s ok" : "pwr %s refused (lock or veto)", argv[1]);
      return;
    }
    cli::print("? pwr %s", argv[1]);
    return;
  }
  char list[96];
  size_t n = 0;
  list[0] = 0;
  for (int i = 0; i < CORE0_MAX_POWER_LOCKS && n < sizeof(list); ++i) {
    if (!locks[i].used) continue;
    const int w = snprintf(list + n, sizeof(list) - n, " %s@%s", locks[i].owner, state_name(locks[i].floor));
    if (w < 0) break;
    n += static_cast<size_t>(w);
  }
  cli::print("state %s  idle_ms %lu  locks:%s", state_name(st),
             static_cast<unsigned long>(time_since(hal::millis(), last_activity)), list[0] ? list : " none");
}
const CliCommand kCmdPwr = {"pwr", "pwr [active|dim|idle|sleep|deep|off]: state, locks, request", cmd_pwr};
#endif

Status init() {
#if CORE0_FEAT_BATTERY
  bus::subscribe(EventType::Battery, on_battery);
#endif
#if CORE0_FEAT_CLI
  cli::add(kCmdPwr);
#endif
  started = true;
  last_activity = hal::millis();
  C0_LOGI("pwr", "init ok (dim %lus off %lus sleep %lus)", static_cast<unsigned long>(pol.dim_after_ms / 1000),
          static_cast<unsigned long>(pol.idle_after_ms / 1000), static_cast<unsigned long>(pol.sleep_after_ms / 1000));
  return Status::Ok;
}

// The next policy step, so an idle loop wakes on time for it.
ms_t next_deadline(ms_t now) {
  if (wake_pending || has_req) return now;
  if (st == PowerState::Off) return off_slept ? now + kIdleDeadlineMs : now;
  if (blocked) return now + kRetryMs;
  const ms_t el = time_since(now, last_activity);
  const ms_t steps[] = {pol.dim_after_ms, pol.idle_after_ms, pol.sleep_after_ms, pol.deep_after_ms};
  ms_t best = now + kIdleDeadlineMs;
  for (size_t i = 0; i < count_of(steps); ++i) {
    if (!steps[i] || steps[i] <= el) continue;
    const ms_t d = last_activity + steps[i];
    if (time_until(best, d) < 0) best = d;
  }
  return best;
}

}  // namespace

Status set_policy(const PowerPolicy& p) {
  pol = p;
  return Status::Ok;
}
const PowerPolicy& policy() { return pol; }

PowerState state() { return st; }

void activity(ms_t now) {
  last_activity = now;
  has_req = false;
  if (st != PowerState::Active && st != PowerState::Off) wake_pending = true;
}

Status request(PowerState target) {
  if (target == PowerState::Off) {
    if (vetoed(PowerState::Off)) return Status::Busy;  // Off waits for vetoes only, not locks
    enter_off();
    return Status::Ok;
  }
  if (target == PowerState::Active) {
    activity(hal::millis());
    return Status::Ok;
  }
  if (target <= st) return Status::Ok;
  if (lock_floor(target) < target || vetoed(target)) return Status::Busy;
  req = target;
  has_req = true;
  return Status::Ok;
}

PowerLock lock(PowerState floor, const char* owner) {
  for (int i = 0; i < CORE0_MAX_POWER_LOCKS; ++i) {
    if (!locks[i].used) {
      locks[i].used = true;
      locks[i].floor = floor;
      locks[i].owner = owner ? owner : "?";
      return static_cast<PowerLock>(i);
    }
  }
  C0_LOGW("pwr", "lock pool full (%s)", owner ? owner : "?");
  return kNoLock;
}

void unlock(PowerLock& l) {
  if (l >= 0 && l < CORE0_MAX_POWER_LOCKS) locks[l].used = false;
  l = kNoLock;
}

void set_wake_at(uint32_t epoch_seconds) { wake_at = epoch_seconds; }

void service(ms_t now) {
  if (!started) {
    if (!bus::pending()) idle_wait(now);
    return;
  }
  if (st == PowerState::Off) {
    off_pass(now);
    return;
  }
  if (wake_pending) wake(hal::kWakeOther);  // activity below Active: a key, or the CLI
  if (bus::pending()) return;
  if (st >= PowerState::Sleep) {
    sleep_pass(now);
    return;
  }
  if (!step_down(now)) idle_wait(now);
}

const Module kModule = {"power", init, nullptr, next_deadline, nullptr, nullptr, nullptr};

}  // namespace power

#if defined(CORE0_NATIVE)
namespace test {
void reset_power() {
  power::pol = kDefaultPowerPolicy;
  power::st = PowerState::Active;
  power::last_activity = 0;
  power::wake_at = 0;
  memset(power::locks, 0, sizeof(power::locks));
  power::started = false;
  power::wake_pending = false;
  power::has_req = false;
  power::blocked = false;
  power::off_slept = false;
#if CORE0_FEAT_BATTERY
  power::force_timer = kNoTimer;
#endif
}
}  // namespace test
#endif

}  // namespace core0

#endif  // CORE0_FEAT_POWER
