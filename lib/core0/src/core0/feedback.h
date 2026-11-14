// ZDX CORE-0 — non-audio feedback: status LED (mono or RGB) and haptics.
//
// Patterns are rendered by the service in tick(); drivers only set levels.
// Status LED convention (keep consistent across products):
//   charging = amber pulse, full = green solid, low battery = red blink,
//   recording = red solid, fault/safe mode = red fast blink.
#pragma once
#include "event.h"
#include "module.h"

namespace core0 {

struct Rgb {
  uint8_t r, g, b;
};

namespace colors {
static const Rgb kOff   = {0, 0, 0};
static const Rgb kRed   = {255, 0, 0};
static const Rgb kGreen = {0, 255, 0};
static const Rgb kBlue  = {0, 0, 255};
static const Rgb kAmber = {255, 120, 0};
static const Rgb kWhite = {255, 255, 255};
}  // namespace colors

enum class LedPattern : uint8_t { Off, Solid, Blink, Pulse /* breathe */, Flash /* once */ };

class LedDriver {
 public:
  virtual ~LedDriver() {}
  virtual Status begin() = 0;
  virtual void   set(const Rgb& c) = 0;   // mono LEDs use max(r,g,b)
  virtual void   set_power(bool on) = 0;  // e.g. WS2812 supply switch
};

class HapticDriver {
 public:
  virtual ~HapticDriver() {}
  virtual Status begin() = 0;
  virtual void   set(uint8_t strength) = 0;  // 0 = off
};

namespace feedback {

Status attach_led(LedDriver& d);
Status attach_haptic(HapticDriver& d);

// Layered: an Overlay pattern (e.g. a Flash) temporarily covers the Base
// pattern (e.g. charging pulse) and then the base resumes.
enum class Layer : uint8_t { Base, Overlay };
void led(LedPattern p, const Rgb& c, uint16_t period_ms = 1000, Layer layer = Layer::Base);
void led_brightness(uint8_t max_level);   // global cap (night mode)

void haptic_pulse(uint16_t ms, uint8_t strength = 255);
// Alternating on/off durations, starting with ON. Max 8 entries.
void haptic_pattern(const uint16_t* on_off_ms, uint8_t count, uint8_t strength = 255);

extern const Module kModule;

}  // namespace feedback
}  // namespace core0
