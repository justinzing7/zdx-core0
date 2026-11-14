// ZDX CORE-0 — software timers.
//
// Fixed pool of CORE0_MAX_TIMERS. Callbacks and events fire in loop context
// (from sys::run_once), never in an ISR, so they may touch any module state.
// Resolution = loop rate; for sub-ms work use a hardware timer in a driver.
#pragma once
#include "event.h"

namespace core0 {

typedef int8_t TimerId;
static const TimerId kNoTimer = -1;

enum class TimerMode : uint8_t { OneShot, Periodic };

typedef void (*TimerCallback)(TimerId id, void* ctx);

namespace timers {

// Start a timer that calls `cb`. Periodic timers re-arm from their previous
// deadline (no drift); if the loop falls > 1 period behind they skip, not burst.
TimerId start(ms_t period_ms, TimerMode mode, TimerCallback cb, void* ctx = nullptr);

// Start a timer that posts an event instead (preferred for cross-module use).
// The posted event is `ev` with ev.arg overwritten by the TimerId.
TimerId start_event(ms_t period_ms, TimerMode mode, const Event& ev);

Status stop(TimerId id);                        // frees the slot: the id is invalid afterwards
Status restart(TimerId id);                     // re-arm from now, same period
Status change_period(TimerId id, ms_t period_ms);
bool   active(TimerId id);
ms_t   remaining(TimerId id, ms_t now);

// Earliest pending deadline (power service uses it to size sleeps).
// Returns false if no timer is armed.
bool next_deadline(ms_t& out);

// Called by sys::run_once — not by app code.
void service(ms_t now);

// After waking from Light/Deep sleep, the port reports elapsed time so timers
// fire correctly even if millis() stopped during sleep.
void compensate_sleep(ms_t slept_ms);

}  // namespace timers
}  // namespace core0
