// ZDX CORE-0 — fault handling: reset reason, crash records, safe mode, asserts.
//
// Boot sequence (fault::init is the FIRST thing in setup()):
//   1. read hal::reset_reason()
//   2. if abnormal (Watchdog/Panic/Brownout): ++retained.crash_count
//   3. crash_count >= CORE0_SAFE_MODE_RESETS -> safe mode
//   4. after CORE0_STABLE_RUN_MS of uptime, crash_count = 0
//
// Safe mode = minimal module set (display, input, power, battery, cli), app
// features disabled, a "SAFE MODE" screen with the last FaultRecord. It exists
// so a bad settings blob or a crashing driver can never brick a device.
#pragma once
#include "event.h"
#include "module.h"
#include "port.h"

namespace core0 {

struct FaultRecord {
  uint32_t         magic;        // validity marker for retained RAM
  hal::ResetReason reason;
  uint8_t          crash_count;
  uint8_t          module_index; // module running when it died (0xFF = unknown)
  uint8_t          reserved;
  uint32_t         code;         // panic code / assert line
  uint32_t         uptime_ms;
  Event            last_event;   // last event dispatched
  char             what[24];     // short reason / file name
  char             module_name[12];  // copy of the module's name ("" = unknown). Use this to report
                                     // a crash: module_index is only valid for the boot that crashed,
                                     // and safe mode registers a different module list.
};

// Event codes for EventType::Fault.
enum class FaultCode : uint8_t {
  ModuleInitFailed,   // arg = module index
  ModuleOverrun,      // tick took > 5 ms, arg = module index, data = ms
  EventDropped,       // bus queue full
  Recovered,          // previous boot crashed, data = reason
};

namespace fault {

void init();                       // call first in setup()
bool safe_mode();
const FaultRecord& last();         // record from the previous crash (if any)
void clear();

// Record and reset. Never returns.
void panic(const char* what, uint32_t code)
#if defined(__GNUC__)
    __attribute__((noreturn))
#endif
    ;

// Bookkeeping hooks used by sys::run_once (cheap; store into retained RAM).
void note_module(uint8_t index, const char* name = nullptr);
void note_event(const Event& e);
void service(ms_t now);            // clears crash counter after stable run

}  // namespace fault
}  // namespace core0

#define C0_ASSERT(cond)                                         \
  do {                                                          \
    if (!(cond)) ::core0::fault::panic(__FILE__, __LINE__);     \
  } while (0)
