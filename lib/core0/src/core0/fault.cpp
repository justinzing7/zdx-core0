// ZDX CORE-0 — fault handling (fault.h, spec/modules/diagnostics.md "Fault handling").
//
// The working FaultRecord lives in retained RAM and is updated while running (module being
// ticked, last event, uptime) so a watchdog reset can name the culprit. At boot, init()
// snapshots it into `prev`, which is what last() returns. `reserved` bit 0 marks a pending
// panic (panic() resets via software, so the reset reason alone would say Software).
// Power contract: none (no hardware).
#include "fault.h"

#include <string.h>

#include "log.h"

namespace core0 {
namespace fault {
namespace {

const uint32_t kMagic = 0x5A434632u;  // "ZCF2": change it whenever the FaultRecord layout changes
const uint8_t  kPendingPanic = 0x01;

CORE0_RETAINED FaultRecord rec;
FaultRecord prev;
bool        safe = false;
bool        stable_cleared = false;

bool abnormal(hal::ResetReason r) {
  return r == hal::ResetReason::Watchdog || r == hal::ResetReason::Panic || r == hal::ResetReason::Brownout;
}

const char* basename_of(const char* path) {
  const char* b = path;
  for (const char* p = path; *p; ++p)
    if (*p == '/' || *p == '\\') b = p + 1;
  return b;
}

}  // namespace

void init() {
  hal::ResetReason reason = hal::reset_reason();
  if (rec.magic != kMagic) {
    memset(&rec, 0, sizeof(rec));
    rec.magic = kMagic;
    rec.module_index = 0xFF;
  }
  if (rec.reserved & kPendingPanic) reason = hal::ResetReason::Panic;
  rec.reason = reason;
  const bool bad = abnormal(reason);
  if (bad) {
    if (rec.crash_count < 0xFF) ++rec.crash_count;
  }
  rec.reserved &= static_cast<uint8_t>(~kPendingPanic);
  prev = rec;
  safe = rec.crash_count >= CORE0_SAFE_MODE_RESETS;
  stable_cleared = false;
  if (bad) {
    bus::post(make_event(EventType::Fault, static_cast<uint8_t>(FaultCode::Recovered), 0, static_cast<uint32_t>(reason)));
  }
  if (safe) bus::post(make_event(EventType::System, static_cast<uint8_t>(SystemCode::SafeMode)));
  // Fresh working record for this boot (crash_count carries over).
  rec.module_index = 0xFF;
  rec.code = 0;
  rec.uptime_ms = 0;
  rec.what[0] = 0;
  rec.module_name[0] = 0;
  memset(&rec.last_event, 0, sizeof(rec.last_event));
}

bool safe_mode() { return safe; }

const FaultRecord& last() { return prev; }

void clear() {
  rec.crash_count = 0;
  prev.crash_count = 0;
  prev.what[0] = 0;
  prev.code = 0;
  safe = false;
}

void panic(const char* what, uint32_t code) {
  const char* b = basename_of(what ? what : "?");
  strncpy(rec.what, b, sizeof(rec.what) - 1);
  rec.what[sizeof(rec.what) - 1] = 0;
  rec.code = code;
  rec.uptime_ms = hal::millis();
  rec.reason = hal::ResetReason::Panic;
  rec.reserved |= kPendingPanic;
  C0_LOGE("flt", "panic %s:%lu", rec.what, static_cast<unsigned long>(code));
  hal::system_reset();
  for (;;) {
  }
}

// A copy, not a pointer: a watchdog reset leaves no chance to resolve the name later, and a
// pointer into flash would dangle after a firmware update (retained RAM outlives the image).
void note_module(uint8_t index, const char* name) {
  rec.module_index = index;
  uint8_t n = 0;
  if (name) {
    for (; name[n] && n < sizeof(rec.module_name) - 1; ++n) rec.module_name[n] = name[n];
  }
  rec.module_name[n] = 0;
}
void note_event(const Event& e) { rec.last_event = e; }

void service(ms_t now) {
  rec.uptime_ms = now;
  if (!stable_cleared && now >= CORE0_STABLE_RUN_MS) {
    stable_cleared = true;
    if (rec.crash_count) C0_LOGI("flt", "stable run: crash counter cleared");
    rec.crash_count = 0;
  }
}

}  // namespace fault

#if defined(CORE0_NATIVE)
namespace test {
void reset_fault() {
  memset(&fault::rec, 0, sizeof(fault::rec));
  memset(&fault::prev, 0, sizeof(fault::prev));
  fault::safe = false;
  fault::stable_cleared = false;
}
// Simulate a reboot that keeps retained RAM (native has no real reset).
void reboot_keep_retained() {
  fault::safe = false;
  fault::stable_cleared = false;
}
}  // namespace test
#endif

}  // namespace core0
