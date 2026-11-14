// ZDX CORE-0 — status LED + haptics (feedback.h, spec/modules/feedback.md).
//
// Two layers: Base (persistent status) and Overlay (momentary, e.g. an 80 ms confirm Flash).
// Patterns are computed from (now - layer start), so a Base pulse keeps its phase while an
// Overlay covers it. A global cap scales every channel (night mode). In Sleep and below the
// LED is off and its supply switched off (no sleep-safe patterns in this version).
#include "config.h"
#if CORE0_FEAT_FEEDBACK  // feature off = zero code (constitution IX)

#include "feedback.h"

#include "anim.h"
#include "log.h"
#include "port.h"

namespace core0 {
namespace feedback {
namespace {

const ms_t kIdleDeadlineMs = 60000;
const ms_t kPulseFrameMs = 20;
const uint8_t kMaxHaptic = 8;

struct LayerState {
  LedPattern pattern;
  Rgb        color;
  uint16_t   period;
  ms_t       start;
};

LedDriver*    led_drv = nullptr;
HapticDriver* hap_drv = nullptr;
LayerState    base = {LedPattern::Off, {0, 0, 0}, 1000, 0};
LayerState    over = {LedPattern::Off, {0, 0, 0}, 1000, 0};
uint8_t       cap = 255;
Rgb           shown = {0, 0, 0};
bool          shown_valid = false;
bool          suspended = false;

uint16_t      hap_steps[kMaxHaptic];
uint8_t       hap_n = 0, hap_i = 0, hap_strength = 0;
ms_t          hap_next = 0;
bool          hap_active = false;

Rgb scale(const Rgb& c, uint16_t level /* 0..256 */) {
  Rgb r = {static_cast<uint8_t>(c.r * level / 256), static_cast<uint8_t>(c.g * level / 256),
           static_cast<uint8_t>(c.b * level / 256)};
  return r;
}

// Colour of a layer at `now`; returns false when the layer contributes nothing (Off / ended Flash).
bool layer_colour(const LayerState& l, ms_t now, Rgb& out) {
  const uint16_t period = l.period ? l.period : 1;
  const ms_t el = time_since(now, l.start);
  switch (l.pattern) {
    case LedPattern::Off: return false;
    case LedPattern::Solid: out = l.color; return true;
    case LedPattern::Blink: {
      const ms_t ph = el % period;
      out = ph < period / 2u ? l.color : Rgb{0, 0, 0};
      return true;
    }
    case LedPattern::Pulse: {
      const ms_t ph = el % period;
      // Triangle 0 -> 256 -> 0 over the period, eased (breathe).
      const uint32_t t = static_cast<uint32_t>(ph) * 512u / period;
      const q8_t tri = static_cast<q8_t>(t < 256 ? t : 511 - t);
      out = scale(l.color, ease(Ease::InOutQuad, tri));
      return true;
    }
    case LedPattern::Flash:
      if (el >= period) return false;
      out = l.color;
      return true;
  }
  return false;
}

void render(ms_t now) {
  if (!led_drv || suspended) return;
  Rgb c = {0, 0, 0};
  if (!layer_colour(over, now, c)) {
    if (over.pattern == LedPattern::Flash) over.pattern = LedPattern::Off;  // Flash ended
    if (!layer_colour(base, now, c)) c = Rgb{0, 0, 0};
  }
  c = Rgb{static_cast<uint8_t>(c.r * cap / 255), static_cast<uint8_t>(c.g * cap / 255),
          static_cast<uint8_t>(c.b * cap / 255)};
  if (!shown_valid || c.r != shown.r || c.g != shown.g || c.b != shown.b) {
    led_drv->set(c);
    shown = c;
    shown_valid = true;
  }
}

ms_t layer_deadline(const LayerState& l, ms_t now) {
  const uint16_t period = l.period ? l.period : 1;
  const ms_t el = time_since(now, l.start);
  switch (l.pattern) {
    case LedPattern::Blink: {
      const ms_t ph = el % period;
      return now + (ph < period / 2u ? period / 2u - ph : period - ph);
    }
    case LedPattern::Pulse: return now + kPulseFrameMs;
    case LedPattern::Flash: return el < period ? now + (period - el) : now + kIdleDeadlineMs;
    default: return now + kIdleDeadlineMs;
  }
}

Status init() {
  if (led_drv) {
    const Status s = led_drv->begin();
    if (!ok(s)) {
      C0_LOGE("fb", "init failed: led begin (%u)", static_cast<unsigned>(s));
      return s;
    }
  }
  if (hap_drv) hap_drv->begin();
  C0_LOGI("fb", "init ok (led %s, haptic %s)", led_drv ? "yes" : "no", hap_drv ? "yes" : "no");
  render(hal::millis());
  return Status::Ok;
}

void tick(ms_t now) {
  render(now);
  if (hap_active && time_reached(now, hap_next)) {
    if (++hap_i >= hap_n) {
      hap_active = false;
      if (hap_drv) hap_drv->set(0);
    } else {
      if (hap_drv) hap_drv->set((hap_i & 1) ? 0 : hap_strength);  // even = ON, odd = OFF
      hap_next = now + hap_steps[hap_i];
    }
  }
}

ms_t next_deadline(ms_t now) {
  if (suspended) return now + kIdleDeadlineMs;
  ms_t best = layer_deadline(over.pattern != LedPattern::Off ? over : base, now);
  if (over.pattern == LedPattern::Flash) {
    const ms_t b = layer_deadline(base, now);
    if (time_until(best, b) < 0) best = b;
  }
  if (hap_active && time_until(best, hap_next) < 0) best = hap_next;
  return best;
}

void on_suspend(PowerState target) {
  if (target < PowerState::Sleep || !led_drv) return;
  led_drv->set(Rgb{0, 0, 0});
  led_drv->set_power(false);
  shown_valid = false;
  suspended = true;
}

void on_resume(PowerState, uint32_t wake) {
  if (!suspended || wake == hal::kWakeRtc) return;  // background wake (power.md): the LED stays off
  suspended = false;
  if (led_drv) led_drv->set_power(true);
  render(hal::millis());
}

}  // namespace

Status attach_led(LedDriver& d) {
  led_drv = &d;
  return Status::Ok;
}
Status attach_haptic(HapticDriver& d) {
  hap_drv = &d;
  return Status::Ok;
}

void led(LedPattern p, const Rgb& c, uint16_t period_ms, Layer layer) {
  LayerState& l = layer == Layer::Overlay ? over : base;
  l.pattern = p;
  l.color = c;
  l.period = period_ms;
  l.start = hal::millis();
  render(l.start);
}

void led_brightness(uint8_t max_level) {
  cap = max_level;
  shown_valid = false;
  render(hal::millis());
}

void haptic_pulse(uint16_t ms, uint8_t strength) { haptic_pattern(&ms, 1, strength); }

void haptic_pattern(const uint16_t* on_off_ms, uint8_t count, uint8_t strength) {
  if (!hap_drv || count == 0) return;
  hap_n = count > kMaxHaptic ? kMaxHaptic : count;
  for (uint8_t i = 0; i < hap_n; ++i) hap_steps[i] = on_off_ms[i];
  hap_i = 0;
  hap_strength = strength;
  hap_active = true;
  hap_drv->set(strength);
  hap_next = hal::millis() + hap_steps[0];
}

const Module kModule = {"feedback", init, tick, next_deadline, nullptr, on_suspend, on_resume};

}  // namespace feedback

#if defined(CORE0_NATIVE)
namespace test {
void reset_feedback() {
  feedback::led_drv = nullptr;
  feedback::hap_drv = nullptr;
  feedback::base.pattern = feedback::over.pattern = LedPattern::Off;
  feedback::cap = 255;
  feedback::shown_valid = false;
  feedback::suspended = false;
  feedback::hap_active = false;
}
}  // namespace test
#endif

}  // namespace core0

#endif  // CORE0_FEAT_FEEDBACK
