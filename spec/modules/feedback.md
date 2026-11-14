# Feedback: status LED and haptics

Header: `feedback.h` · Flag: `CORE0_FEAT_FEEDBACK`

## LED

- Drivers implement `LedDriver::set(Rgb)`. A mono LED uses `max(r,g,b)` as its level through PWM; WS2812/SK6812 LEDs take the full colour.
- The service renders patterns in `tick()`: `Solid`, `Blink` (50% duty), `Pulse` (eased breathe), and `Flash` (single, then returns to Base).
- Two layers: **Base** holds a persistent status (charging, recording); **Overlay** holds momentary feedback (a confirmation flash). The overlay ends and the base resumes automatically.
- `led_brightness(max)` sets a global cap, used for night mode and as an alarm-clock bedside courtesy.
- **Power:**
  - In Sleep and below, only `Solid` and slow `Blink` (≥ 2 s period) patterns are allowed, and only if the product explicitly requests them. Otherwise the LED is off.
  - WS2812 parts draw about 1 mA even when dark. Products should switch their supply (`set_power(false)`) when the LED is off.

### Zingcore LED convention

| Meaning | Pattern |
|---|---|
| Charging | amber pulse, 2 s |
| Charged | green solid (or off after 1 min) |
| Low battery | red blink, 2 s, short duty |
| Recording / live | red solid |
| Confirm | white or green flash, 80 ms (overlay) |
| Fault / safe mode | red fast blink, 250 ms |
| Pairing / net busy | blue pulse |

## Haptic

`haptic_pulse(ms, strength)` and `haptic_pattern(on_off[], n)` are rendered by the service. The driver's `set(strength)` maps to PWM on the motor driver. A haptic pulse doesn't hold a power lock; patterns are short.

## Acceptance

- [ ] A Flash overlay during a charging pulse returns to the pulse afterwards, preserving its phase.
- [ ] The LED supply is off in Sleep unless a pattern was explicitly requested.
- [ ] Pattern timing is within ±1 tick of spec.
