# Battery

Header: `battery.h` · Event type: `EventType::Battery` · Flag: `CORE0_FEAT_BATTERY`

## Measurement

- `BatteryConfig.read_mv()` is supplied by the driver and returns the **pin** voltage in mV, already calibrated. Examples: `analogReadMilliVolts()` on ESP32, or reading a VREF/bandgap channel on SAMD21/STM32.
- The service scales by `divider_num/den`.
- If `enable_pin` is wired, the service drives it, waits `settle_ms`, takes `samples` readings, averages them, and disables the pin. A permanently connected divider wastes current; a 100k/100k divider draws about 20 µA.
- An IIR filter (`filter_shift`) smooths load noise. The first reading after boot seeds the filter directly.
- Sampling period is `period_active_ms` while awake. While asleep, it is `period_sleep_ms` using an RTC wake; 0 means "only sample when awake", and the period timer is then stopped in `on_suspend` so it cannot wake the device. It restarts on resume.
- Avoid sampling during audio playback or radio TX. The service defers a sample while audio plays at `Alarm` priority or higher (`audio::busy() && audio::current() >= AudioPriority::Alarm`) or a net user is active, up to one period.

## Percent and state

- `mv_to_percent()` interpolates over a descending `VoltPct` curve. `kCurveLiPo1S` and `kCurveAlkaline2S` ship by default. A product with a measured cell replaces the curve; this is recommended for anything that shows a percentage.
- State machine, evaluated with hysteresis (`hysteresis_mv`):
  - `External` / `Charging` / `Full` when `is_external()` / `is_charging()` say so.
  - `Normal` → `Low` at `low_mv`, then → `Critical` at `critical_mv`.
  - Recovery requires +hysteresis, so the state never flaps at a threshold: Critical → Low needs `critical_mv + hysteresis_mv`, and Critical or Low → Normal needs `low_mv + hysteresis_mv`.
- Events: `StateChanged` on every state change, including the first reading after boot (Unknown → the first state). `PercentChanged` only when the percent moves by at least `report_step_pct` (default 5), so the UI icon doesn't flicker.
- `percent()` is the live filtered value. Events carry the last reported value.
- A forced Off is not cancelled if the voltage recovers during the battery-empty face: a sagging cell reads higher once the load drops.

## UI conventions

- The status bar battery icon has 4 or 5 bars.
- Low: the icon blinks and plays one notification tone when entering Low.
- Critical: see [power §Low battery](power.md#low-battery).
- Charging: an animated icon, and the LED pulses amber (see [feedback](feedback.md)).

## Acceptance

- [ ] With a lab supply sweep from 4.2→3.3 V, reported % is monotonic and within ±5% of the curve.
- [ ] Holding a supply at exactly `low_mv` ± noise causes no state flapping (at most 1 event per 10 min).
- [ ] The divider enable pin is high only during measurement (verify with a scope or logic analyzer).
- [ ] A Critical event leads to an orderly shutdown with settings flushed.
