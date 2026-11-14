// ZDX CORE-0 — wall-clock time (clock.h, spec/modules/clock.md).
//
// Between RTC reads the time is extrapolated from millis(); the RTC is re-read every minute and on
// resume, so a bus RTC sees little traffic. Boundary events follow local time, and only ever move
// forward: a small step back from an RTC correction does not fire the same minute twice.
// Power contract: owns no hardware; the RtcSource keeps running across Sleep and Off. It does not
// wake the device: in Sleep, Minute fires on the next wake (a wake per minute is for feature 003).
// CLI view: `time [epoch]`.
#include "config.h"
#if CORE0_FEAT_CLOCK  // feature off = zero code (constitution IX)

#include "clock.h"

#include <stdio.h>
#include <stdlib.h>

#include "log.h"
#include "port.h"
#if CORE0_FEAT_CLI
#  include "cli.h"
#endif

namespace core0 {
namespace clock {
namespace {

const ms_t     kReReadMs = 60000;
const ms_t     kPrecedenceMs = 3600000;  // a better source is protected for this long
const ms_t     kIdleDeadlineMs = 60000;
const int32_t  kDaysTo2000 = 10957;      // days from 1970-01-01 to 2000-01-01
const uint32_t kSecPerDay = 86400;
const uint32_t kBackStepResyncS = 60;    // a larger step back is a correction, not jitter

RtcSource*  rtc = nullptr;
epoch_t     base_epoch = 0;
ms_t        base_ms = 0;
ms_t        last_read_ms = 0;
bool        valid_ = false;
ClockSource source_ = ClockSource::None;
bool        protected_ = false;  // set() was called: precedence applies (a boot-time RTC read is not a set)
ms_t        set_at_ms = 0;
int16_t     tz_min = 0;
uint8_t     dst_rule = 0;
bool        second_events = false;
epoch_t     last_local = 0;

uint8_t rank(ClockSource s) {
  switch (s) {
    case ClockSource::Ntp:
    case ClockSource::Gps: return 3;
    case ClockSource::Rtc: return 2;
    case ClockSource::Manual:
    case ClockSource::Host: return 1;
    default: return 0;
  }
}

int32_t days_from_civil(int32_t y, uint32_t m, uint32_t d) {
  y -= m <= 2 ? 1 : 0;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = static_cast<uint32_t>(y - era * 400);
  const uint32_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

void post(ClockCode c, uint16_t arg, uint32_t data) {
  bus::post(make_event(EventType::Clock, static_cast<uint8_t>(c), arg, data));
}

// Epoch of the n-th (1-based) Sunday of a month; n = 0 means the last one. Midnight UTC.
epoch_t sunday_of(uint16_t year, uint8_t month, uint8_t n) {
  DateTime dt = {year, month, 1, 0, 0, 0, 0};
  if (n == 0) {
    dt.month = static_cast<uint8_t>(month == 12 ? 1 : month + 1);
    dt.year = static_cast<uint16_t>(month == 12 ? year + 1 : year);
    const epoch_t next_first = from_datetime(dt);
    const uint32_t days = next_first / kSecPerDay;
    const uint32_t wd = (days + 5) % 7;  // weekday of the 1st of next month, 0 = Monday
    return next_first - ((wd + 1) % 7 == 0 ? 7 : (wd + 1)) * kSecPerDay;
  }
  const epoch_t first = from_datetime(dt);
  const uint32_t wd = (first / kSecPerDay + 5) % 7;
  return first + ((6 - wd) + (n - 1) * 7u) * kSecPerDay;
}

bool dst_active(epoch_t utc) {
  if (dst_rule == 0) return false;
  DateTime dt;
  to_datetime(utc, dt);
  if (dst_rule == 1) {  // EU: last Sunday of March to last Sunday of October, at 01:00 UTC
    return utc >= sunday_of(dt.year, 3, 0) + 3600 && utc < sunday_of(dt.year, 10, 0) + 3600;
  }
  // US: second Sunday of March 02:00 standard to first Sunday of November 02:00 daylight (= 01:00
  // standard). Written as local standard time converted to UTC.
  const int32_t offset = static_cast<int32_t>(tz_min) * 60;
  const int64_t start = static_cast<int64_t>(sunday_of(dt.year, 3, 2)) + 7200 - offset;
  const int64_t end = static_cast<int64_t>(sunday_of(dt.year, 11, 1)) + 3600 - offset;
  return utc >= start && utc < end;
}

void sync_boundaries() { last_local = local_now(); }

void invalidate(const char* why) {
  valid_ = false;
  source_ = ClockSource::None;
  protected_ = false;
  C0_LOGW("clk", "time invalid: %s", why);
  post(ClockCode::Invalid, 0, 0);
}

void re_read(ms_t now) {
  last_read_ms = now;
  if (!rtc) return;
  if (rtc->lost_power()) {
    invalidate("rtc lost power");
    return;
  }
  epoch_t t;
  if (!ok(rtc->read(t))) return;  // keep extrapolating: one failed read is not lost time
  base_epoch = t;
  base_ms = now;
}

// Keeps the elapsed time well inside ms_t range when there is no RTC to rebase on.
void roll_base(ms_t now) {
  const uint32_t secs = time_since(now, base_ms) / 1000;
  base_epoch += secs;
  base_ms += secs * 1000;
}

void emit_boundaries(epoch_t local) {
  if (second_events) post(ClockCode::Second, 0, local);
  if (local / 60 != last_local / 60) post(ClockCode::Minute, 0, local);
  if (local / 3600 != last_local / 3600) post(ClockCode::Hour, 0, local);
  if (local / kSecPerDay != last_local / kSecPerDay) post(ClockCode::DayChanged, 0, local);
}

#if CORE0_FEAT_CLI
void cmd_time(uint8_t argc, char** argv) {
  static const char* const kSources[] = {"None", "Manual", "Rtc", "Ntp", "Gps", "Host"};
  if (argc >= 2) {
    const Status s = set(static_cast<epoch_t>(strtoul(argv[1], nullptr, 0)), ClockSource::Host);
    if (!ok(s)) {
      cli::print("time refused (status %u)", static_cast<unsigned>(s));
      return;
    }
  }
  if (!valid_) {
    cli::print("time --:-- (not set; `time <seconds since 2000-01-01>`)");
    return;
  }
  DateTime u, l;
  to_datetime(now(), u);
  to_datetime(local_now(), l);
  cli::print("time %04u-%02u-%02u %02u:%02u:%02u UTC  epoch %lu  source %s", u.year, u.month, u.day, u.hour, u.minute,
             u.second, static_cast<unsigned long>(now()), kSources[static_cast<unsigned>(source_)]);
  cli::print("local %04u-%02u-%02u %02u:%02u:%02u  tz %d min  dst rule %u", l.year, l.month, l.day, l.hour, l.minute,
             l.second, tz_min, dst_rule);
}
const CliCommand kCmdTime = {"time", "time [epoch]: show or set the clock (seconds since 2000)", cmd_time};
#endif

Status init() {
#if CORE0_FEAT_CLI
  cli::add(kCmdTime);
#endif
  const ms_t t = hal::millis();
  base_ms = last_read_ms = t;
  if (!rtc) {
    C0_LOGW("clk", "init ok, no RTC attached: time invalid until set");
    post(ClockCode::Invalid, 0, 0);
    return Status::Ok;
  }
  const Status s = rtc->begin();
  if (!ok(s)) {
    C0_LOGE("clk", "init failed: rtc begin (%u)", static_cast<unsigned>(s));
    return s;
  }
  epoch_t e = 0;
  if (rtc->lost_power() || !ok(rtc->read(e))) {
    invalidate("rtc lost power");
  } else {
    base_epoch = e;
    valid_ = true;
    source_ = ClockSource::Rtc;
    sync_boundaries();
  }
  C0_LOGI("clk", "init ok (%s)", valid_ ? "time from RTC" : "time not set");
  return Status::Ok;
}

void tick(ms_t t) {
  if (!valid_) return;
  if (time_since(t, last_read_ms) >= kReReadMs) {
    re_read(t);
    if (!valid_) return;
  }
  if (!rtc) roll_base(t);
  const epoch_t local = local_now();
  if (local == last_local) return;
  if (local < last_local) {  // stepped back: wait for the clock to catch up, unless it is a real jump
    if (last_local - local > kBackStepResyncS) last_local = local;
    return;
  }
  emit_boundaries(local);
  last_local = local;
}

ms_t next_deadline(ms_t t) {
  if (!valid_) return t + kIdleDeadlineMs;
  const ms_t to_second = 1000 - time_since(t, base_ms) % 1000;
  if (second_events) return t + to_second;
  return t + to_second + (59 - local_now() % 60) * 1000;
}

void on_resume(PowerState, uint32_t) {
  if (valid_) re_read(hal::millis());
}

}  // namespace

Status attach(RtcSource& r) {
  rtc = &r;
  return Status::Ok;
}

epoch_t now() { return base_epoch + time_since(hal::millis(), base_ms) / 1000; }

epoch_t local_now() {
  const epoch_t utc = now();
  int64_t local = static_cast<int64_t>(utc) + static_cast<int32_t>(tz_min) * 60;
  if (dst_active(utc)) local += 3600;
  return local < 0 ? 0 : static_cast<epoch_t>(local);
}

bool valid() { return valid_; }

Status set(epoch_t utc, ClockSource src) {
  if (src == ClockSource::None) return Status::Invalid;
  const ms_t t = hal::millis();
  // A person setting the time (Manual, or Host from the CLI) always wins. Between automatic
  // sources, a worse one never overwrites a better one within the hour (clock.md).
  const bool by_person = src == ClockSource::Manual || src == ClockSource::Host;
  if (!by_person && valid_ && protected_ && rank(src) < rank(source_) && time_since(t, set_at_ms) < kPrecedenceMs)
    return Status::Busy;
  if (rtc && src != ClockSource::Rtc && !ok(rtc->write(utc))) C0_LOGW("clk", "rtc write failed");
  base_epoch = utc;
  base_ms = last_read_ms = t;
  valid_ = true;
  source_ = src;
  protected_ = true;
  set_at_ms = t;
  sync_boundaries();
  C0_LOGI("clk", "time set: %lu (source %u)", static_cast<unsigned long>(utc), static_cast<unsigned>(src));
  post(ClockCode::TimeSet, static_cast<uint16_t>(src), utc);
  return Status::Ok;
}

ClockSource source() { return source_; }

void set_tz_offset_min(int16_t minutes) {
  tz_min = minutes;
  sync_boundaries();
}

void set_eu_us_dst(uint8_t rule) {
  dst_rule = rule;
  sync_boundaries();
}

void enable_second_events(bool on) { second_events = on; }

void to_datetime(epoch_t t, DateTime& out) {
  const uint32_t days = t / kSecPerDay;
  const uint32_t rem = t % kSecPerDay;
  out.hour = static_cast<uint8_t>(rem / 3600);
  out.minute = static_cast<uint8_t>((rem % 3600) / 60);
  out.second = static_cast<uint8_t>(rem % 60);
  out.weekday = static_cast<uint8_t>((days + 5) % 7);  // 2000-01-01 was a Saturday
  // civil_from_days, on days since 1970.
  const int32_t z = static_cast<int32_t>(days) + kDaysTo2000 + 719468;
  const int32_t era = z / 146097;
  const uint32_t doe = static_cast<uint32_t>(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  out.day = static_cast<uint8_t>(doy - (153 * mp + 2) / 5 + 1);
  out.month = static_cast<uint8_t>(mp < 10 ? mp + 3 : mp - 9);
  out.year = static_cast<uint16_t>(static_cast<int32_t>(yoe) + era * 400 + (out.month <= 2 ? 1 : 0));
}

epoch_t from_datetime(const DateTime& dt) {
  const int32_t days = days_from_civil(dt.year, dt.month, dt.day) - kDaysTo2000;
  return static_cast<epoch_t>(days) * kSecPerDay + dt.hour * 3600u + dt.minute * 60u + dt.second;
}

const Module kModule = {"clock", init, tick, next_deadline, nullptr, nullptr, on_resume};

}  // namespace clock

#if defined(CORE0_NATIVE)
namespace test {
void reset_clock() {
  clock::rtc = nullptr;
  clock::base_epoch = 0;
  clock::base_ms = 0;
  clock::last_read_ms = 0;
  clock::valid_ = false;
  clock::source_ = ClockSource::None;
  clock::protected_ = false;
  clock::set_at_ms = 0;
  clock::tz_min = 0;
  clock::dst_rule = 0;
  clock::second_events = false;
  clock::last_local = 0;
}
}  // namespace test
#endif

}  // namespace core0

#endif  // CORE0_FEAT_CLOCK
