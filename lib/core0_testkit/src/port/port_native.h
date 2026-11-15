// port_native.h — controls for the native (host) port. Test code only.
#pragma once
#include <setjmp.h>
#include <core0/core0.h>

namespace port_native {

void reset();                       // clock 0, NVM erased (0xFF), console cleared, flags cleared

// Clock
void        set_ms(core0::ms_t t);
void        advance_ms(core0::ms_t n);
core0::ms_t now();
// When true, hal::sleep(Idle, max) advances the clock by max (simulation runs).
// Default false: tests advance time explicitly; sleep only records the request.
void        set_auto_advance(bool on);
core0::ms_t last_sleep_request();   // max_ms of the most recent hal::sleep call
uint32_t    sleep_calls(core0::hal::SleepDepth depth);
void        set_wake_bits(uint32_t bits);  // returned by the next Light sleep

// NVM (CORE0_NATIVE_NVM_SIZE bytes, default 256)
void     nvm_fail_after(long n);    // next writes cut power after n bytes total (-1 = off)
bool     nvm_power_cut();           // true once the injected cut happened (further writes dropped)
void     nvm_wipe();                // all 0xFF
uint32_t nvm_commit_count();
uint8_t* nvm_raw();

// Console
const char* console_capture();      // everything written since the last clear
void        console_clear();
void        console_inject(const char* s);

// Reset / identity
void                     set_reset_reason(core0::hal::ResetReason r);
bool                     reset_requested();
// If armed, hal::system_reset() longjmps here (so noreturn paths can be tested).
jmp_buf&                 reset_point();
void                     arm_reset_point(bool on);

}  // namespace port_native
