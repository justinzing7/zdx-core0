// ZDX CORE-0 — event bus.
//
// Modules talk to each other ONLY through events (plus each module's own
// public API). Events are small PODs copied by value into a static queue.
// Handlers run in loop context, never in an ISR.
#pragma once
#include "types.h"

namespace core0 {

// Event categories. Each module owns one category and defines its own `code`
// enum in its header (e.g. InputCode, PowerCode). App events start at App.
enum class EventType : uint8_t {
  None = 0,
  System,    // SystemCode  (boot, init done, safe mode)
  Timer,     // code = user tag, arg = TimerId
  Input,     // InputCode   (input.h)
  Power,     // PowerCode   (power.h)
  Battery,   // BatteryCode (battery.h)
  Audio,     // AudioCode   (audio.h)
  Display,   // DisplayCode (display.h)
  Ui,        // UiCode      (ui.h)
  Clock,     // ClockCode   (clock.h)
  Settings,  // SettingsCode (settings.h)
  Storage,   // StorageCode (storage.h)
  Net,       // NetCode     (net.h)
  Sensor,    // project-defined codes, arg = sensor id
  Fault,     // FaultCode   (fault.h)
  App = 128, // App + n : project-defined categories
};

// 8 bytes. Meaning of code/arg/data is defined by the owning category.
struct Event {
  EventType type;
  uint8_t   code;
  uint16_t  arg;
  uint32_t  data;
};

inline Event make_event(EventType t, uint8_t code, uint16_t arg = 0, uint32_t data = 0) {
  Event e = {t, code, arg, data};
  return e;
}

enum class SystemCode : uint8_t {
  Boot,        // data = hal::ResetReason
  InitDone,    // all modules initialised
  SafeMode,    // booted into safe mode (fault.h)
  Shutdown,    // about to power off / reset
};

namespace bus {

typedef void (*Handler)(const Event& e, void* ctx);

// Subscribe a handler to one category (EventType::None = all events).
// Call during init only. Returns NoSpace when CORE0_MAX_SUBSCRIBERS exceeded.
Status subscribe(EventType type, Handler fn, void* ctx = nullptr);

// Queue an event. Safe from loop context AND from ISRs (uses a critical
// section internally; multiple producers allowed). Returns NoSpace if the
// queue is full — the event is dropped and a drop counter incremented.
Status post(const Event& e);

// Dispatch up to `budget` queued events to subscribers (called by the core loop).
// Returns number dispatched.
uint8_t dispatch(uint8_t budget = CORE0_DISPATCH_BUDGET);

bool     pending();        // queue non-empty (used to decide whether to sleep)
uint32_t dropped_count();  // diagnostics

}  // namespace bus
}  // namespace core0
