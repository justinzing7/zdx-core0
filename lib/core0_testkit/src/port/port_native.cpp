// port_native.cpp — hal:: for the host build (env:native): controllable clock, NVM array
// with power-cut injection, console on stdout + capture buffer. Never sleeps for real.
#include "port/port_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
const size_t kNvmSize = 256;
const size_t kCaptureSize = 16384;
const size_t kInjectSize = 512;

core0::ms_t      g_now = 0;
bool             g_auto_advance = false;
core0::ms_t      g_last_sleep = 0;
uint32_t         g_sleep_calls[3] = {0, 0, 0};
uint32_t         g_wake_bits = core0::hal::kWakeButton;
int              g_crit_depth = 0;

uint8_t          g_nvm[kNvmSize];
long             g_fail_after = -1;
bool             g_cut = false;
uint32_t         g_commits = 0;

char             g_capture[kCaptureSize];
size_t           g_capture_len = 0;
char             g_inject[kInjectSize];
size_t           g_inject_len = 0, g_inject_pos = 0;

core0::hal::ResetReason g_reason = core0::hal::ResetReason::PowerOn;
bool             g_reset_requested = false;
bool             g_reset_armed = false;
jmp_buf          g_reset_jmp;
}  // namespace

namespace port_native {
void reset() {
  g_now = 0;
  g_auto_advance = false;
  g_last_sleep = 0;
  memset(g_sleep_calls, 0, sizeof(g_sleep_calls));
  g_wake_bits = core0::hal::kWakeButton;
  g_crit_depth = 0;
  nvm_wipe();
  g_fail_after = -1;
  g_cut = false;
  g_commits = 0;
  console_clear();
  g_inject_len = g_inject_pos = 0;
  g_reason = core0::hal::ResetReason::PowerOn;
  g_reset_requested = false;
  g_reset_armed = false;
}
void        set_ms(core0::ms_t t) { g_now = t; }
void        advance_ms(core0::ms_t n) { g_now += n; }
core0::ms_t now() { return g_now; }
void        set_auto_advance(bool on) { g_auto_advance = on; }
core0::ms_t last_sleep_request() { return g_last_sleep; }
uint32_t    sleep_calls(core0::hal::SleepDepth d) { return g_sleep_calls[static_cast<int>(d)]; }
void        set_wake_bits(uint32_t bits) { g_wake_bits = bits; }

void     nvm_fail_after(long n) { g_fail_after = n; g_cut = false; }
bool     nvm_power_cut() { return g_cut; }
void     nvm_wipe() { memset(g_nvm, 0xFF, sizeof(g_nvm)); }
uint32_t nvm_commit_count() { return g_commits; }
uint8_t* nvm_raw() { return g_nvm; }

const char* console_capture() { return g_capture; }
void        console_clear() { g_capture_len = 0; g_capture[0] = 0; }
void        console_inject(const char* s) {
  const size_t n = strlen(s);
  if (g_inject_pos == g_inject_len) g_inject_pos = g_inject_len = 0;
  for (size_t i = 0; i < n && g_inject_len < kInjectSize; ++i) g_inject[g_inject_len++] = s[i];
}

void     set_reset_reason(core0::hal::ResetReason r) { g_reason = r; }
bool     reset_requested() { return g_reset_requested; }
jmp_buf& reset_point() { return g_reset_jmp; }
void     arm_reset_point(bool on) { g_reset_armed = on; }
}  // namespace port_native

namespace core0 {
namespace hal {

ms_t     millis() { return g_now; }
uint32_t micros() { return g_now * 1000u; }

void critical_enter() { ++g_crit_depth; }
void critical_exit() { if (g_crit_depth > 0) --g_crit_depth; }

uint32_t sleep(SleepDepth depth, ms_t max_ms, uint32_t wake_mask) {
  (void)wake_mask;
  ++g_sleep_calls[static_cast<int>(depth)];
  g_last_sleep = max_ms;
  if (depth == SleepDepth::Idle) {
    if (g_auto_advance) g_now += max_ms;
    return kWakeNone;
  }
  // A timer wake means the sleep limit elapsed, as on the hardware (the clock moves to it).
  if (g_auto_advance || (depth == SleepDepth::Light && g_wake_bits == kWakeRtc)) g_now += max_ms;
  if (depth == SleepDepth::Deep) {
    g_reset_requested = true;
    if (g_reset_armed) longjmp(g_reset_jmp, 2);
    return kWakeNone;
  }
  return g_wake_bits;
}

ResetReason reset_reason() { return g_reason; }
void system_reset() {
  g_reset_requested = true;
  if (g_reset_armed) longjmp(g_reset_jmp, 1);
}
void watchdog_start(ms_t) {}
void watchdog_feed() {}

size_t nvm_size() { return kNvmSize; }
Status nvm_read(size_t offset, void* dst, size_t len) {
  if (offset + len > kNvmSize) return Status::Invalid;
  memcpy(dst, g_nvm + offset, len);
  return Status::Ok;
}
Status nvm_write(size_t offset, const void* src, size_t len) {
  if (offset + len > kNvmSize) return Status::Invalid;
  const uint8_t* s = static_cast<const uint8_t*>(src);
  for (size_t i = 0; i < len; ++i) {
    if (g_cut) return Status::IoError;
    if (g_fail_after == 0) { g_cut = true; return Status::IoError; }
    if (g_fail_after > 0) --g_fail_after;
    g_nvm[offset + i] = s[i];
  }
  return Status::Ok;
}
Status nvm_commit() {
  if (g_cut) return Status::IoError;
  ++g_commits;
  return Status::Ok;
}

void console_write(const char* data, size_t len) {
  fwrite(data, 1, len, stdout);
  for (size_t i = 0; i < len && g_capture_len + 1 < kCaptureSize; ++i) g_capture[g_capture_len++] = data[i];
  g_capture[g_capture_len] = 0;
}
int console_read() {
  if (g_inject_pos >= g_inject_len) return -1;
  return static_cast<unsigned char>(g_inject[g_inject_pos++]);
}

uint32_t chip_id32() { return 0xC0DE0001u; }

}  // namespace hal
}  // namespace core0
