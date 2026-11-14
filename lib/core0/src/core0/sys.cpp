// ZDX CORE-0 — module registry and the cooperative loop (module.h, spec/modules/core.md).
#include "module.h"

#include "event.h"
#include "fault.h"
#include "log.h"
#include "port.h"
#include "timer.h"
#if CORE0_FEAT_POWER
#  include "power.h"
#endif

namespace core0 {
namespace sys {
namespace {

const uint32_t kOverrunUs = 5000;
const ms_t     kOverrunReportMs = 1000;
const ms_t     kMaxIdleMs = 60000;

const Module* mods[CORE0_MAX_MODULES];
bool          healthy[CORE0_MAX_MODULES];
Status        last_status[CORE0_MAX_MODULES];
uint32_t      tick_max[CORE0_MAX_MODULES];
ms_t          overrun_at[CORE0_MAX_MODULES];
bool          overrun_seen[CORE0_MAX_MODULES];
uint8_t       count = 0;

const char* status_name(Status s) {
  switch (s) {
    case Status::Ok: return "Ok";
    case Status::Busy: return "Busy";
    case Status::Timeout: return "Timeout";
    case Status::NotFound: return "NotFound";
    case Status::NoSpace: return "NoSpace";
    case Status::Invalid: return "Invalid";
    case Status::Unsupported: return "Unsupported";
    case Status::IoError: return "IoError";
    case Status::Corrupt: return "Corrupt";
  }
  return "?";
}

}  // namespace

Status register_module(const Module* m) {
  if (!m) return Status::Invalid;
  if (count >= CORE0_MAX_MODULES) {
    C0_LOGW("sys", "register %s: registry full", m->name);
    return Status::NoSpace;
  }
  mods[count] = m;
  healthy[count] = false;
  last_status[count] = Status::Ok;
  tick_max[count] = 0;
  overrun_seen[count] = false;
  ++count;
  return Status::Ok;
}

Status begin() {
  uint8_t ok_count = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const Status s = mods[i]->init ? mods[i]->init() : Status::Ok;
    last_status[i] = s;
    healthy[i] = ok(s);
    if (healthy[i]) {
      ++ok_count;
    } else {
      C0_LOGE("sys", "%s init failed: %s (module disabled)", mods[i]->name, status_name(s));
      bus::post(make_event(EventType::Fault, static_cast<uint8_t>(FaultCode::ModuleInitFailed), i));
    }
  }
  C0_LOGI("sys", "init done, %u/%u modules healthy", static_cast<unsigned>(ok_count), static_cast<unsigned>(count));
  hal::watchdog_start(CORE0_WATCHDOG_MS);
  bus::post(make_event(EventType::System, static_cast<uint8_t>(SystemCode::InitDone)));
  return ok_count == count ? Status::Ok : Status::IoError;
}

void run_once() {
  const ms_t now = hal::millis();
  timers::service(now);
  for (uint8_t i = 0; i < count; ++i) {
    if (!healthy[i] || !mods[i]->tick) continue;
    fault::note_module(i, mods[i]->name);
    const uint32_t t0 = hal::micros();
    mods[i]->tick(now);
    const uint32_t dt = hal::micros() - t0;
    if (dt > tick_max[i]) tick_max[i] = dt;
    if (dt > kOverrunUs && (!overrun_seen[i] || time_since(now, overrun_at[i]) >= kOverrunReportMs)) {
      overrun_seen[i] = true;
      overrun_at[i] = now;
      bus::post(make_event(EventType::Fault, static_cast<uint8_t>(FaultCode::ModuleOverrun), i, dt / 1000));
    }
  }
  fault::note_module(0xFF);
  bus::dispatch();
#if CORE0_FEAT_POWER
  power::service(hal::millis());
#else
  if (!bus::pending()) {
    const ms_t t = hal::millis();
    const int32_t wait = time_until(t, next_deadline(t));
    if (wait > 0) hal::sleep(hal::SleepDepth::Idle, static_cast<ms_t>(wait), 0);
  }
#endif
  fault::service(hal::millis());
  hal::watchdog_feed();
}

uint8_t       module_count() { return count; }
const Module* module_at(uint8_t index) { return index < count ? mods[index] : nullptr; }
bool          module_healthy(uint8_t index) { return index < count && healthy[index]; }

ms_t next_deadline(ms_t now) {
  ms_t best = now + kMaxIdleMs;
  for (uint8_t i = 0; i < count; ++i) {
    if (!healthy[i] || !mods[i]->next_deadline) continue;
    const ms_t d = mods[i]->next_deadline(now);
    if (time_until(best, d) < 0) best = d;
  }
  ms_t t;
  if (timers::next_deadline(t) && time_until(best, t) < 0) best = t;
  return best;
}

// Library-internal diagnostics (used by cli.cpp; not part of the public header contract).
uint32_t tick_max_us(uint8_t index) { return index < count ? tick_max[index] : 0; }
Status   init_status(uint8_t index) { return index < count ? last_status[index] : Status::NotFound; }
const char* status_str(Status s) { return status_name(s); }

}  // namespace sys

#if defined(CORE0_NATIVE)
namespace test {
void reset_sys() { sys::count = 0; }
}  // namespace test
#endif

}  // namespace core0
