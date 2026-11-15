// ZDX CORE-0 — wall-clock time.
//
// Internal time is `epoch_t`: seconds since 2000-01-01 00:00:00 UTC (uint32,
// valid to 2136). Local time = UTC + tz offset (+ DST if enabled). The clock
// keeps running across Sleep/Deep via an RtcSource; time "validity" tracks
// whether it has ever been set since power loss.
#pragma once
#include "event.h"
#include "module.h"

namespace core0 {

typedef uint32_t epoch_t;

struct DateTime {
  uint16_t year;    // 2000..2135
  uint8_t  month;   // 1..12
  uint8_t  day;     // 1..31
  uint8_t  hour, minute, second;
  uint8_t  weekday; // 0 = Monday .. 6 = Sunday
};

enum class ClockSource : uint8_t { None, Manual, Rtc, Ntp, Gps, Host /* CLI */ };

// Time source / keeper (MCU RTC, DS3231, PCF8563, ESP32 RTC timer...).
class RtcSource {
 public:
  virtual ~RtcSource() {}
  virtual Status begin() = 0;
  virtual Status read(epoch_t& out) = 0;
  virtual Status write(epoch_t t) = 0;
  virtual bool   lost_power() = 0;                      // oscillator stop flag
  virtual Status set_wake_alarm(epoch_t t) { (void)t; return Status::Unsupported; }  // 0 = clear
};

// Event codes for EventType::Clock.
enum class ClockCode : uint8_t {
  Second,       // only if enable_second_events(true) — costs power
  Minute,       // data = local epoch
  Hour,
  DayChanged,
  TimeSet,      // arg = ClockSource
  Invalid,      // RTC lost power: time needs setting
};

namespace clock {

Status   attach(RtcSource& rtc);
epoch_t  now();                 // UTC
epoch_t  local_now();           // == local_from_utc(now())
// Conversions with the current tz offset and DST rule.
epoch_t  local_from_utc(epoch_t utc);
// Inverse: a local time that occurs twice (DST fall-back) gives the first, DST occurrence; a local
// time that never occurs (spring-forward gap) is taken as standard time, so it shows an hour later.
epoch_t  utc_from_local(epoch_t local);
bool     valid();
Status   set(epoch_t utc, ClockSource src);
ClockSource source();

void     set_tz_offset_min(int16_t minutes);
void     set_eu_us_dst(uint8_t rule);   // 0 = none, 1 = EU, 2 = US
void     enable_second_events(bool on);

// Pure helpers (days-from-civil algorithm, no libc time.h needed).
void     to_datetime(epoch_t t, DateTime& out);
epoch_t  from_datetime(const DateTime& dt);

extern const Module kModule;

}  // namespace clock
}  // namespace core0
