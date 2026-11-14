// ZDX CORE-0 — soft timers (spec/modules/core.md "Soft timers").
//
// One-shot timers stay allocated after firing (restart() re-arms them) until stop().
#include "timer.h"

#include "port.h"

namespace core0 {
namespace timers {
namespace {

struct Slot {
  bool          used;
  bool          armed;
  bool          is_event;
  TimerMode     mode;
  ms_t          period;
  ms_t          deadline;
  TimerCallback cb;
  void*         ctx;
  Event         ev;
};

Slot slots[CORE0_MAX_TIMERS];

bool valid(TimerId id) { return id >= 0 && id < CORE0_MAX_TIMERS && slots[id].used; }

TimerId alloc(ms_t period, TimerMode mode) {
  for (int i = 0; i < CORE0_MAX_TIMERS; ++i) {
    if (!slots[i].used) {
      Slot& s = slots[i];
      s.used = true;
      s.armed = true;
      s.mode = mode;
      s.period = period;
      s.deadline = hal::millis() + period;
      s.cb = nullptr;
      s.ctx = nullptr;
      s.is_event = false;
      return static_cast<TimerId>(i);
    }
  }
  return kNoTimer;
}

}  // namespace

TimerId start(ms_t period_ms, TimerMode mode, TimerCallback cb, void* ctx) {
  if (!cb) return kNoTimer;
  const TimerId id = alloc(period_ms, mode);
  if (id != kNoTimer) {
    slots[id].cb = cb;
    slots[id].ctx = ctx;
  }
  return id;
}

TimerId start_event(ms_t period_ms, TimerMode mode, const Event& ev) {
  const TimerId id = alloc(period_ms, mode);
  if (id != kNoTimer) {
    slots[id].is_event = true;
    slots[id].ev = ev;
  }
  return id;
}

Status stop(TimerId id) {
  if (!valid(id)) return Status::NotFound;
  slots[id].used = false;
  slots[id].armed = false;
  return Status::Ok;
}

Status restart(TimerId id) {
  if (!valid(id)) return Status::NotFound;
  slots[id].deadline = hal::millis() + slots[id].period;
  slots[id].armed = true;
  return Status::Ok;
}

Status change_period(TimerId id, ms_t period_ms) {
  if (!valid(id)) return Status::NotFound;
  slots[id].period = period_ms;
  return restart(id);
}

bool active(TimerId id) { return valid(id) && slots[id].armed; }

ms_t remaining(TimerId id, ms_t now) {
  if (!active(id)) return 0;
  const int32_t r = time_until(now, slots[id].deadline);
  return r > 0 ? static_cast<ms_t>(r) : 0;
}

bool next_deadline(ms_t& out) {
  bool any = false;
  for (int i = 0; i < CORE0_MAX_TIMERS; ++i) {
    const Slot& s = slots[i];
    if (!s.used || !s.armed) continue;
    if (!any || time_until(out, s.deadline) < 0) out = s.deadline;
    any = true;
  }
  return any;
}

void service(ms_t now) {
  for (int i = 0; i < CORE0_MAX_TIMERS; ++i) {
    Slot& s = slots[i];
    if (!s.used || !s.armed || !time_reached(now, s.deadline)) continue;
    // Re-arm before firing so the callback may restart/stop/change this timer.
    if (s.mode == TimerMode::Periodic && s.period > 0) {
      do { s.deadline += s.period; } while (time_reached(now, s.deadline));  // skip, don't burst
    } else {
      s.armed = false;
    }
    if (s.is_event) {
      Event e = s.ev;
      e.arg = static_cast<uint16_t>(i);
      bus::post(e);
    } else if (s.cb) {
      s.cb(static_cast<TimerId>(i), s.ctx);
    }
  }
}

void compensate_sleep(ms_t slept_ms) {
  for (int i = 0; i < CORE0_MAX_TIMERS; ++i)
    if (slots[i].used && slots[i].armed) slots[i].deadline -= slept_ms;
}

}  // namespace timers

#if defined(CORE0_NATIVE)
namespace test {
void reset_timers() {
  for (int i = 0; i < CORE0_MAX_TIMERS; ++i) timers::slots[i].used = timers::slots[i].armed = false;
}
}  // namespace test
#endif

}  // namespace core0
