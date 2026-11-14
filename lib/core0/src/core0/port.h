// ZDX CORE-0 — port / HAL contract.
//
// The ONLY MCU-specific surface. One implementation per MCU family lives in
// the project (src/port/port_<mcu>.cpp) or a shared ports library; a mock
// implementation backs the native (host) build.
//
// Rule: services and app code never call vendor sleep/watchdog/NVM APIs
// directly — only these functions. Plain GPIO/SPI/I2C via Arduino is fine
// inside *drivers*; the port covers what Arduino does not abstract.
#pragma once
#include "types.h"

namespace core0 {
namespace hal {

// ---- Time -----------------------------------------------------------------
ms_t     millis();
uint32_t micros();

// ---- Critical sections (nestable) ------------------------------------------
// Superloop: disable IRQs. RTOS/dual-core: spinlock/portENTER_CRITICAL.
void critical_enter();
void critical_exit();

class CriticalSection {  // RAII guard
 public:
  CriticalSection() { critical_enter(); }
  ~CriticalSection() { critical_exit(); }
 private:
  CriticalSection(const CriticalSection&);
  CriticalSection& operator=(const CriticalSection&);
};

// ---- Sleep ----------------------------------------------------------------
enum class SleepDepth : uint8_t {
  Idle,   // CPU halt (WFI / light idle) — peripherals & RAM alive, fast wake
  Light,  // clocks reduced, RAM retained, wake on IRQ/RTC (ESP32 light sleep, SAMD STANDBY)
  Deep,   // lowest retained state; may RESET on wake (ESP32 deep sleep, STM32 STANDBY)
};

// Wake source bitmask. Board config decides which are physically wired.
enum WakeSource : uint32_t {
  kWakeNone    = 0,
  kWakeButton  = 1u << 0,
  kWakeEncoder = 1u << 1,
  kWakeRtc     = 1u << 2,   // timer / RTC alarm
  kWakeUsb     = 1u << 3,   // VBUS present
  kWakeCharger = 1u << 4,
  kWakeSensor  = 1u << 5,   // sensor INT pin
  kWakeNet     = 1u << 6,
  kWakeOther   = 1u << 31,
};

// Enter sleep for at most `max_ms` (0 = until a wake source fires).
// Returns the WakeSource bits that caused the wake. For Deep sleep on parts
// that reset, this never returns; wake reason is reported via reset_reason().
uint32_t sleep(SleepDepth depth, ms_t max_ms, uint32_t wake_mask);

// ---- Reset / watchdog -----------------------------------------------------
enum class ResetReason : uint8_t {
  PowerOn, Brownout, Watchdog, Software, ExternalPin, Panic, DeepSleepWake, Unknown
};
ResetReason reset_reason();
void        system_reset();          // software reset
void        watchdog_start(ms_t timeout_ms);
void        watchdog_feed();         // called ONLY by the core loop

// ---- Non-volatile memory (settings backend) -------------------------------
// A small linear region (EEPROM, emulated EEPROM, NVS blob, flash page).
// The settings service handles layout, CRC and A/B slots; the port only moves bytes.
size_t nvm_size();
Status nvm_read(size_t offset, void* dst, size_t len);
Status nvm_write(size_t offset, const void* src, size_t len);
Status nvm_commit();                 // flush emulation buffers (no-op if N/A)

// ---- Console (log + CLI transport, usually Serial) ------------------------
void console_write(const char* data, size_t len);
int  console_read();                 // -1 if no byte available (never blocks)

// ---- Identity -------------------------------------------------------------
uint32_t chip_id32();                // short unique id (serials, BLE names)

// ---- Dual-core profile only -----------------------------------------------
#if CORE0_PROFILE == CORE0_PROFILE_DUALCORE
// Start `entry` on the second core. Core 1 may only touch modules declared
// core-1-owned (audio stream, display flush) and talk via SPSC rings.
Status start_core1(void (*entry)());
#endif

}  // namespace hal
}  // namespace core0
