// ZDX CORE-0 — battery monitor.
//
// Sample the divider at a slow rate, filter, map to % with a per-chemistry
// curve, and emit events only on state changes (with hysteresis) or when the
// percentage moves by >= report_step. Never sample during heavy transients
// (audio playback, radio TX) unless the project calibrates for load.
#pragma once
#include "event.h"
#include "module.h"

namespace core0 {

struct VoltPct {
  uint16_t mv;
  uint8_t  pct;
};

// Single-cell LiPo, light load, 25 °C. Descending mV. Replace per cell if measured.
static const VoltPct kCurveLiPo1S[] = {
  {4180, 100}, {4100, 92}, {4000, 81}, {3900, 69}, {3800, 55}, {3750, 46},
  {3700, 36},  {3650, 26}, {3600, 18}, {3500, 9},  {3400, 4},  {3300, 0},
};
// 2x AA alkaline (series), light load.
static const VoltPct kCurveAlkaline2S[] = {
  {3100, 100}, {2900, 80}, {2700, 55}, {2500, 30}, {2300, 12}, {2100, 3}, {2000, 0},
};

enum class BatteryState : uint8_t { Unknown, Normal, Low, Critical, Charging, Full, External };

struct BatteryConfig {
  // Measurement: read_mv returns the ADC pin voltage in mV (driver handles
  // calibration, e.g. esp_adc_cal / analogReadMilliVolts). Divider scales up.
  uint16_t (*read_mv)();
  uint16_t divider_num, divider_den;    // Vbat = pin_mv * num / den
  uint8_t  enable_pin;                  // divider high-side switch, 0xFF = none
  uint8_t  settle_ms;                   // after enabling divider
  uint8_t  samples;                     // averaged per reading (oversample)
  uint8_t  filter_shift;                // IIR: y += (x - y) >> shift  (2..4)
  ms_t     period_active_ms;            // e.g. 10000
  ms_t     period_sleep_ms;             // e.g. 300000 (0 = don't wake for battery)
  const VoltPct* curve;
  uint8_t  curve_len;
  uint16_t low_mv, critical_mv, hysteresis_mv;
  uint8_t  report_step_pct;             // emit PercentChanged when moved >= this
  bool (*is_charging)();                // nullptr if not wired
  bool (*is_external)();                // USB / VBUS present, nullptr if not wired
};

// Event codes for EventType::Battery.  arg = BatteryState, data = mV << 8 | pct
enum class BatteryCode : uint8_t { StateChanged, PercentChanged };

namespace battery {

Status       configure(const BatteryConfig& cfg);
uint16_t     mv();
uint8_t      percent();
BatteryState state();
void         sample_now();   // force a reading (e.g. after wake)

// Pure helper: curve lookup with linear interpolation.
inline uint8_t mv_to_percent(uint16_t mv, const VoltPct* curve, uint8_t n) {
  if (n == 0) return 0;
  if (mv >= curve[0].mv) return curve[0].pct;
  for (uint8_t i = 1; i < n; ++i) {
    if (mv >= curve[i].mv) {
      const uint16_t span_mv = curve[i - 1].mv - curve[i].mv;
      const uint8_t  span_p  = curve[i - 1].pct - curve[i].pct;
      return static_cast<uint8_t>(curve[i].pct + (static_cast<uint32_t>(mv - curve[i].mv) * span_p) / span_mv);
    }
  }
  return curve[n - 1].pct;
}

extern const Module kModule;

}  // namespace battery
}  // namespace core0
