// ZDX CORE-0 — battery monitor (battery.h, spec/modules/battery.md).
//
// A periodic soft timer marks a reading due; tick() runs the settle state machine (enable the
// divider, wait settle_ms, average `samples` reads, disable). Readings feed an IIR kept as a
// fixed-point accumulator (exact steady state, no truncation bias). Percent comes from the curve;
// the state machine applies hysteresis_mv on recovery. A due reading waits while an Alarm/Media
// sound plays, for at most one period.
#include "config.h"
#if CORE0_FEAT_BATTERY  // feature off = zero code (constitution IX)

#include "battery.h"

#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "cli.h"
#include "log.h"
#include "port.h"
#include "timer.h"
#if CORE0_FEAT_SELFTEST
#  include "selftest.h"
#endif

#if defined(ARDUINO)
#  include <Arduino.h>  // enable_pin only: the HAL has no GPIO (upstream-notes §5)
#endif

namespace core0 {
namespace battery {
namespace {

enum class St : uint8_t { Idle, Settling, Sampling };

const ms_t kIdleDeadlineMs = 60000;
const ms_t kDeferPollMs = 100;

BatteryConfig cfg;
bool          configured = false;
St            st = St::Idle;
TimerId       period_timer = kNoTimer;
bool          due = false;
bool          deferring = false;
ms_t          defer_since = 0;
ms_t          settle_until = 0;

bool          seeded = false;
uint32_t      acc = 0;        // IIR accumulator = mV << filter_shift
uint16_t      filt_mv = 0;
uint16_t      pin_mv = 0;     // last averaged pin voltage (CLI "raw")
uint8_t       pct = 0;
uint8_t       reported_pct = 0;
BatteryState  bstate = BatteryState::Unknown;

const char* const kStateNames[] = {"Unknown", "Normal", "Low", "Critical", "Charging", "Full", "External"};

void set_enable(bool on) {
  if (cfg.enable_pin == 0xFF) return;
#if defined(ARDUINO)
  digitalWrite(cfg.enable_pin, on ? HIGH : LOW);
#else
  (void)on;
#endif
}

uint32_t pack() { return (static_cast<uint32_t>(filt_mv) << 8) | pct; }

BatteryState evaluate(uint16_t mv) {
  if (cfg.is_charging && cfg.is_charging()) return BatteryState::Charging;
  if (cfg.is_external && cfg.is_external()) return pct >= 100 ? BatteryState::Full : BatteryState::External;
  const uint16_t low_up = static_cast<uint16_t>(cfg.low_mv + cfg.hysteresis_mv);
  const uint16_t crit_up = static_cast<uint16_t>(cfg.critical_mv + cfg.hysteresis_mv);
  switch (bstate) {
    case BatteryState::Low:
      if (mv <= cfg.critical_mv) return BatteryState::Critical;
      return mv >= low_up ? BatteryState::Normal : BatteryState::Low;
    case BatteryState::Critical:
      if (mv >= low_up) return BatteryState::Normal;
      return mv >= crit_up ? BatteryState::Low : BatteryState::Critical;
    default:  // Unknown / Normal / external states: plain thresholds
      if (mv <= cfg.critical_mv) return BatteryState::Critical;
      if (mv <= cfg.low_mv) return BatteryState::Low;
      return BatteryState::Normal;
  }
}

// One complete reading: average, scale, filter, map, evaluate, report.
void take_reading() {
  const uint8_t n = cfg.samples ? cfg.samples : 1;
  uint32_t sum = 0;
  for (uint8_t i = 0; i < n; ++i) sum += cfg.read_mv();
  set_enable(false);
  pin_mv = static_cast<uint16_t>(sum / n);
  const uint32_t den = cfg.divider_den ? cfg.divider_den : 1;
  const uint32_t vbat = static_cast<uint32_t>(pin_mv) * cfg.divider_num / den;
  const uint8_t sh = cfg.filter_shift;
  if (!seeded) {
    acc = vbat << sh;  // the first reading seeds the filter
    seeded = true;
  } else {
    acc = acc + vbat - (acc >> sh);
  }
  filt_mv = static_cast<uint16_t>(acc >> sh);
  pct = mv_to_percent(filt_mv, cfg.curve, cfg.curve_len);

  const BatteryState next = evaluate(filt_mv);
  if (next != bstate) {
    bstate = next;
    reported_pct = pct;
    bus::post(make_event(EventType::Battery, static_cast<uint8_t>(BatteryCode::StateChanged),
                         static_cast<uint16_t>(bstate), pack()));
  } else {
    const int d = static_cast<int>(pct) - static_cast<int>(reported_pct);
    if ((d < 0 ? -d : d) >= (cfg.report_step_pct ? cfg.report_step_pct : 1)) {
      reported_pct = pct;
      bus::post(make_event(EventType::Battery, static_cast<uint8_t>(BatteryCode::PercentChanged),
                           static_cast<uint16_t>(bstate), pack()));
    }
  }
  st = St::Idle;
  due = false;
  deferring = false;
}

// Start a reading now (settle first if the divider is switched).
void begin_reading(ms_t now) {
  if (cfg.enable_pin != 0xFF && cfg.settle_ms) {
    set_enable(true);
    settle_until = now + cfg.settle_ms;
    st = St::Settling;
    return;
  }
  set_enable(true);
  st = St::Sampling;
  take_reading();
}

bool audio_heavy() {
#if CORE0_FEAT_AUDIO
  return audio::busy() && audio::current() >= AudioPriority::Alarm;
#else
  return false;
#endif
}

void on_period(TimerId, void*) { due = true; }

void cmd_bat(uint8_t, char**) {
  const unsigned s = static_cast<unsigned>(bstate);
  cli::print("bat %u mV  %u%%  %s  raw %u", static_cast<unsigned>(filt_mv), static_cast<unsigned>(pct),
             s < count_of(kStateNames) ? kStateNames[s] : "?", static_cast<unsigned>(pin_mv));
}
const CliCommand kCmdBat = {"bat", "battery mV, %, state", cmd_bat};

#if CORE0_FEAT_SELFTEST
// `battery`: a fresh reading must land in the plausible range for a 1S cell (data-model §8).
const uint16_t kTestMinMv = 3000;
const uint16_t kTestMaxMv = 4400;
const ms_t     kTestReadingMs = 1000;
ms_t           test_started = 0;

TestResult selftest_step(ms_t now, const Event&, char* msg, uint8_t n) {
  if (n == 0) return TestResult::Fail;
  if (msg[0] == 0) {
    test_started = now;
    snprintf(msg, n, "Reading");
    sample_now();
  }
  if (due || st != St::Idle || bstate == BatteryState::Unknown) {
    if (time_since(now, test_started) < kTestReadingMs) return TestResult::Running;
    snprintf(msg, n, "no-reading");
    return TestResult::Fail;
  }
  snprintf(msg, n, "%umV", static_cast<unsigned>(filt_mv));
  return filt_mv >= kTestMinMv && filt_mv <= kTestMaxMv ? TestResult::Pass : TestResult::Fail;
}
const SelfTest kSelfTest = {"battery", false, selftest_step};
#endif

Status init() {
  if (!configured || !cfg.read_mv || !cfg.curve || cfg.curve_len == 0) {
    C0_LOGE("bat", "init failed: not configured");
    return Status::Invalid;
  }
#if defined(ARDUINO)
  if (cfg.enable_pin != 0xFF) {
    pinMode(cfg.enable_pin, OUTPUT);
    digitalWrite(cfg.enable_pin, LOW);
  }
#endif
  if (cfg.period_active_ms) {
    period_timer = timers::start(cfg.period_active_ms, TimerMode::Periodic, on_period);
    if (period_timer == kNoTimer) {
      C0_LOGE("bat", "init failed: no timer");
      return Status::NoSpace;
    }
  }
#if CORE0_FEAT_CLI
  cli::add(kCmdBat);
#if CORE0_FEAT_SELFTEST
  selftest::register_test(kSelfTest);
#endif
#endif
  begin_reading(hal::millis());  // with a switched divider the seed lands settle_ms later, in tick()
  C0_LOGI("bat", "init ok (%u mV %u%% %s, every %lus)", static_cast<unsigned>(filt_mv), static_cast<unsigned>(pct),
          kStateNames[static_cast<unsigned>(bstate)], static_cast<unsigned long>(cfg.period_active_ms / 1000));
  if (!cfg.is_charging && !cfg.is_external) C0_LOGW("bat", "charge/external sense not wired");
  return Status::Ok;
}

void tick(ms_t now) {
  switch (st) {
    case St::Idle:
      if (!due) return;
      if (audio_heavy()) {
        if (!deferring) {
          deferring = true;
          defer_since = now;
        }
        if (time_since(now, defer_since) < cfg.period_active_ms) return;  // defer at most one period
      }
      begin_reading(now);
      return;
    case St::Settling:
      if (time_reached(now, settle_until)) {
        st = St::Sampling;
        take_reading();
      }
      return;
    case St::Sampling:
      take_reading();
      return;
  }
}

ms_t next_deadline(ms_t now) {
  if (st == St::Settling) return settle_until;
  if (st == St::Sampling) return now;
  if (deferring) return now + kDeferPollMs;
  return now + kIdleDeadlineMs;  // the period timer drives readings
}

void on_suspend(PowerState target) {
  if (target < PowerState::Sleep) return;
  set_enable(false);  // never leave the divider powered in sleep
  st = St::Idle;
  if (period_timer == kNoTimer) return;
  // period_sleep_ms = 0: no sampling, so the timer must not wake the device every period.
  if (cfg.period_sleep_ms) timers::change_period(period_timer, cfg.period_sleep_ms);
  else {
    timers::stop(period_timer);  // frees the slot: on_resume starts a new one
    period_timer = kNoTimer;
  }
}

void on_resume(PowerState from, uint32_t) {
  if (from < PowerState::Sleep) return;
  if (period_timer != kNoTimer) {
    timers::change_period(period_timer, cfg.period_active_ms);
  } else if (cfg.period_active_ms) {
    period_timer = timers::start(cfg.period_active_ms, TimerMode::Periodic, on_period);
  }
  sample_now();
}

}  // namespace

Status configure(const BatteryConfig& c) {
  cfg = c;
  configured = true;
  return Status::Ok;
}

uint16_t     mv() { return filt_mv; }
uint8_t      percent() { return pct; }
BatteryState state() { return bstate; }

void sample_now() {
  if (!configured || !cfg.read_mv) return;
  set_enable(true);
  if (cfg.enable_pin != 0xFF && cfg.settle_ms) {
    // Forced reading: settle via the state machine, the reading lands in a later tick.
    settle_until = hal::millis() + cfg.settle_ms;
    st = St::Settling;
    return;
  }
  take_reading();
}

// Power contract: switches the divider off and pauses (or slows) the timer before Sleep; resamples on
// resume from Sleep/Deep. No veto.
const Module kModule = {"battery", init, tick, next_deadline, nullptr, on_suspend, on_resume};

}  // namespace battery

#if defined(CORE0_NATIVE)
namespace test {
void reset_battery() {
  if (battery::period_timer != kNoTimer) timers::stop(battery::period_timer);
  battery::period_timer = kNoTimer;
  memset(&battery::cfg, 0, sizeof(battery::cfg));
  battery::configured = false;
  battery::st = battery::St::Idle;
  battery::due = battery::deferring = false;
  battery::seeded = false;
  battery::acc = 0;
  battery::filt_mv = battery::pin_mv = 0;
  battery::pct = battery::reported_pct = 0;
  battery::bstate = BatteryState::Unknown;
}
}  // namespace test
#endif

}  // namespace core0

#endif  // CORE0_FEAT_BATTERY
