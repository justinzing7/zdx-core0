// test_helpers.h — ZDX CORE-0 foundation test kit (generic: no product includes).
// Include once per test_main.cpp. Product suites use app_test_helpers.h on top of this.
//
// Foundation modules expose test-only reset seams (compiled only with CORE0_NATIVE):
// core0::test::reset_<module>(). They are declared here, not in the foundation headers.
#pragma once
#include <string.h>
#include <unity.h>
#include <core0/core0.h>
#include "port/port_native.h"

namespace core0 {
namespace test {
void reset_sys();
void reset_bus();
void reset_timers();
void reset_log();
void reset_fault();
void reset_cli();
void reset_power();
void reset_input();
void reset_display();
void reset_ui();
void reset_settings();
void reset_audio();
void reset_feedback();
void reset_battery();
void reset_selftest();
void reset_clock();
}  // namespace test
}  // namespace core0

// Reinitialise the native port and every foundation module between tests.
inline void reset_all() {
  port_native::reset();
  core0::test::reset_sys();
  core0::test::reset_bus();
  core0::test::reset_timers();
  core0::test::reset_log();
  core0::test::reset_fault();
  core0::test::reset_cli();
  core0::test::reset_power();
  core0::test::reset_input();
  core0::test::reset_display();
  core0::test::reset_ui();
  core0::test::reset_settings();
  core0::test::reset_audio();
  core0::test::reset_feedback();
  core0::test::reset_battery();
  core0::test::reset_selftest();
  core0::test::reset_clock();
}

// Run the loop for `ms` milliseconds of mock time, one pass per millisecond.
inline void pump(core0::ms_t ms) {
  for (core0::ms_t i = 0; i < ms; ++i) {
    port_native::advance_ms(1);
    core0::sys::run_once();
  }
}

// Event recorder: subscribe with rec_attach(type) after reset_all().
struct EventLog {
  enum { kMax = 512 };
  core0::Event ev[kMax];
  core0::ms_t at[kMax];
  int n;
};
static EventLog g_rec;
inline void rec_handler(const core0::Event& e, void*) {
  if (g_rec.n < EventLog::kMax) { g_rec.ev[g_rec.n] = e; g_rec.at[g_rec.n] = core0::hal::millis(); ++g_rec.n; }
}
inline void rec_attach(core0::EventType t = core0::EventType::None) {
  g_rec.n = 0;
  core0::bus::subscribe(t, rec_handler, nullptr);
}
inline int rec_count(core0::EventType t, uint8_t code) {
  int c = 0;
  for (int i = 0; i < g_rec.n; ++i) c += (g_rec.ev[i].type == t && g_rec.ev[i].code == code);
  return c;
}
inline int rec_find(core0::EventType t, uint8_t code, int from = 0) {
  for (int i = from; i < g_rec.n; ++i)
    if (g_rec.ev[i].type == t && g_rec.ev[i].code == code) return i;
  return -1;
}
template <typename C>
inline uint8_t U(C c) { return static_cast<uint8_t>(c); }
