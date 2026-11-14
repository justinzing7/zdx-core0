# Clock (wall time)

Header: `clock.h` · Event type: `EventType::Clock` · Flag: `CORE0_FEAT_CLOCK`

## Model

- `epoch_t` is seconds since 2000-01-01 UTC, as a uint32 (valid until 2136). This avoids 64-bit `time_t` math on small MCUs.
- An `RtcSource` driver keeps time across sleep. Examples:
  - ESP32 RTC timer (survives deep sleep, drifts ~5%/day on the internal RC; use the 32 kHz crystal if fitted).
  - SAMD21/STM32 internal RTC with a 32 kHz crystal.
  - DS3231/PCF8563 over I2C, for best accuracy and survival through power loss with a coin cell.
- Between RTC reads the service extrapolates from `millis()` and re-reads the RTC every minute and on resume. This avoids I2C traffic every second.
- `valid()` is false until time has been set since the last power loss (`lost_power()`). The UI must show "--:--" and prompt the user to set the time rather than display a wrong time.
- Sources, from best to worst: Ntp/Gps > Rtc > Manual/Host.
  - A person setting the time (Manual, or Host from the CLI) is never refused.
  - Between automatic sources, a worse one never overwrites time set by a better one within the same hour.
  - Reading the RTC at boot is not a set, so it protects nothing.

## Events

- `Minute` is posted on every minute boundary (local time). This is the main event for clock faces and alarm checks: one wake per minute, not per second.
  - The clock has no timer behind it, so it does not wake the device from Sleep. `Minute` fires once on the next wake, with the correct time. A wake at a set time needs `power::set_wake_at`.
- `Hour` and `DayChanged` follow the same pattern.
- `Second` is opt-in via `enable_second_events`, and costs power. Use it only while a seconds display is visible.
- `TimeSet`, `Invalid`.

## Wake alarms

`power::set_wake_at(epoch)` → `RtcSource::set_wake_alarm`. Products with alarms (the alarm clock, a Pomodoro timer in Sleep) compute their next wake and set it in their `on_suspend`.

In 0.1.0, `set_wake_at` only stores the time: it does not limit the sleep yet. It is implemented with the first product that needs a timed wake.

## Time zone

The time zone is a fixed offset in minutes, plus an optional EU or US DST rule, stored in settings. Full tz database support is out of scope.

## Acceptance

- [ ] `to_datetime(from_datetime(x)) == x` across 2000–2135, including leap days (native test).
- [ ] A `Minute` event fires within 1 s of the boundary while awake, and on the correct minute after waking from Sleep.
- [ ] After 24 h in Sleep, drift is within the RTC source's spec (record it in the plan).
- [ ] A cold boot with a dead RTC shows `--:--` and posts `Invalid`.
