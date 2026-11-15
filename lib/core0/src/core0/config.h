// ZDX CORE-0 — compile-time configuration.
// Every value here is a default. Override in the project's board_config.h
// (found automatically if it is on the include path) or via build_flags.
#pragma once

#if defined(__has_include)
#  if __has_include("board_config.h")
#    include "board_config.h"
#  endif
#endif

// ---------------------------------------------------------------------------
// Foundation version (semver of the spec + skeleton, not the product)
// ---------------------------------------------------------------------------
#define CORE0_VERSION_MAJOR 0
#define CORE0_VERSION_MINOR 2
#define CORE0_VERSION_PATCH 0

// ---------------------------------------------------------------------------
// Execution profile  (spec/modules/core.md §Execution profiles)
// ---------------------------------------------------------------------------
#define CORE0_PROFILE_SUPERLOOP 0  // SAMD21, STM32, ATmega: one loop, ISRs post events
#define CORE0_PROFILE_RTOS      1  // ESP32: services may run as FreeRTOS tasks
#define CORE0_PROFILE_DUALCORE  2  // RP2040 / ESP32: core 1 owns streaming work

#ifndef CORE0_PROFILE
#  define CORE0_PROFILE CORE0_PROFILE_SUPERLOOP
#endif

// ---------------------------------------------------------------------------
// Feature flags: 1 = compiled in, 0 = compiled out (zero code, zero RAM)
// ---------------------------------------------------------------------------
#ifndef CORE0_FEAT_INPUT
#  define CORE0_FEAT_INPUT 1
#endif
#ifndef CORE0_FEAT_ENCODER
#  define CORE0_FEAT_ENCODER 0
#endif
#ifndef CORE0_FEAT_DISPLAY
#  define CORE0_FEAT_DISPLAY 1
#endif
#ifndef CORE0_FEAT_UI
#  define CORE0_FEAT_UI CORE0_FEAT_DISPLAY
#endif
#ifndef CORE0_FEAT_POWER
#  define CORE0_FEAT_POWER 1
#endif
#ifndef CORE0_FEAT_BATTERY
#  define CORE0_FEAT_BATTERY 1
#endif
#ifndef CORE0_FEAT_AUDIO
#  define CORE0_FEAT_AUDIO 1
#endif
#ifndef CORE0_FEAT_AUDIO_STREAM   // DAC / I2S sample streaming (buzzer-only boards: 0)
#  define CORE0_FEAT_AUDIO_STREAM 0
#endif
#ifndef CORE0_FEAT_AUDIO_IN       // microphone capture
#  define CORE0_FEAT_AUDIO_IN 0
#endif
#ifndef CORE0_FEAT_FEEDBACK       // status LED / haptic
#  define CORE0_FEAT_FEEDBACK 1
#endif
#ifndef CORE0_FEAT_SETTINGS
#  define CORE0_FEAT_SETTINGS 1
#endif
#ifndef CORE0_FEAT_CLOCK
#  define CORE0_FEAT_CLOCK 1
#endif
#ifndef CORE0_FEAT_CLI
#  define CORE0_FEAT_CLI 1
#endif
#ifndef CORE0_FEAT_SELFTEST
#  define CORE0_FEAT_SELFTEST 1
#endif
#ifndef CORE0_FEAT_STORAGE
#  define CORE0_FEAT_STORAGE 0
#endif
#ifndef CORE0_FEAT_NET
#  define CORE0_FEAT_NET 0
#endif

// ---------------------------------------------------------------------------
// Static sizing (no heap after init)
// ---------------------------------------------------------------------------
#ifndef CORE0_MAX_MODULES
#  define CORE0_MAX_MODULES 24
#endif
#ifndef CORE0_EVENT_QUEUE_LEN      // must be a power of two
#  define CORE0_EVENT_QUEUE_LEN 32
#endif
#ifndef CORE0_MAX_SUBSCRIBERS
#  define CORE0_MAX_SUBSCRIBERS 24
#endif
#ifndef CORE0_DISPATCH_BUDGET      // max events handled per loop pass
#  define CORE0_DISPATCH_BUDGET 8
#endif
#ifndef CORE0_MAX_TIMERS
#  define CORE0_MAX_TIMERS 16
#endif
#ifndef CORE0_MAX_KEYS
#  define CORE0_MAX_KEYS 8
#endif
#ifndef CORE0_SCREEN_STACK_DEPTH
#  define CORE0_SCREEN_STACK_DEPTH 6
#endif
#ifndef CORE0_MAX_POWER_LOCKS
#  define CORE0_MAX_POWER_LOCKS 8
#endif
#ifndef CORE0_MAX_CLI_COMMANDS
#  define CORE0_MAX_CLI_COMMANDS 24
#endif
#ifndef CORE0_MAX_SELFTESTS
#  define CORE0_MAX_SELFTESTS 16
#endif
#ifndef CORE0_NET_MAX_USERS        // concurrent net::acquire() holders
#  define CORE0_NET_MAX_USERS 2
#endif
#ifndef CORE0_INPUT_POLL_GPIO      // input module polls configured key pins itself
#  define CORE0_INPUT_POLL_GPIO 1
#endif
#ifndef CORE0_FACTORY_COMBO        // keys held at boot -> self-test (selftest.h)
#  define CORE0_FACTORY_COMBO { ::core0::Key::Up, ::core0::Key::Down }
#endif
#ifndef CORE0_AUDIO_RING_SAMPLES   // power of two
#  define CORE0_AUDIO_RING_SAMPLES 1024
#endif

// ---------------------------------------------------------------------------
// Timing defaults (ms)
// ---------------------------------------------------------------------------
#ifndef CORE0_SERVICE_TICK_MS      // max loop sleep when idle-but-awake
#  define CORE0_SERVICE_TICK_MS 10
#endif
#ifndef CORE0_WATCHDOG_MS
#  define CORE0_WATCHDOG_MS 4000
#endif
#ifndef CORE0_STABLE_RUN_MS        // uptime after which crash counter clears
#  define CORE0_STABLE_RUN_MS 60000
#endif
#ifndef CORE0_SAFE_MODE_RESETS     // consecutive abnormal resets -> safe mode
#  define CORE0_SAFE_MODE_RESETS 3
#endif

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
#define CORE0_LOG_NONE  0
#define CORE0_LOG_ERROR 1
#define CORE0_LOG_WARN  2
#define CORE0_LOG_INFO  3
#define CORE0_LOG_DEBUG 4
#define CORE0_LOG_TRACE 5
#ifndef CORE0_LOG_LEVEL
#  define CORE0_LOG_LEVEL CORE0_LOG_INFO
#endif

// ---------------------------------------------------------------------------
// Platform attributes (see port.h for the rationale)
// ---------------------------------------------------------------------------
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#  include <esp_attr.h>            // IRAM_ATTR, RTC_NOINIT_ATTR used below
#endif
#ifndef CORE0_ISR_ATTR
#  if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#    define CORE0_ISR_ATTR IRAM_ATTR
#  else
#    define CORE0_ISR_ATTR
#  endif
#endif

#ifndef CORE0_RETAINED             // survives warm reset (and ESP32 deep sleep)
#  if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#    define CORE0_RETAINED RTC_NOINIT_ATTR
#  elif defined(CORE0_NATIVE)
#    define CORE0_RETAINED
#  else
#    define CORE0_RETAINED __attribute__((section(".noinit")))
#  endif
#endif

#ifndef CORE0_MEMORY_BARRIER
#  define CORE0_MEMORY_BARRIER() __sync_synchronize()
#endif
