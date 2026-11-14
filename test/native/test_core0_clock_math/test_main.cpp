// T096 — clock: date math, events, RTC handling, source precedence, DST (clock.md acceptance).
#include <unity.h>
#include "test_helpers.h"
#include "mock_rtc.h"
using namespace core0;

static MockRtc g_rtc;

static epoch_t at(uint16_t y, uint8_t mo, uint8_t d, uint8_t h = 0, uint8_t mi = 0, uint8_t s = 0) {
  const DateTime dt = {y, mo, d, h, mi, s, 0};
  return clock::from_datetime(dt);
}

static void boot(epoch_t start = 1000000, bool lost = false, bool attach = true) {
  reset_all();
  g_rtc.reset();
  g_rtc.base = start;
  g_rtc.lost = lost;
  if (attach) clock::attach(g_rtc);
  sys::register_module(&clock::kModule);
  rec_attach(EventType::Clock);
  sys::begin();
  bus::dispatch();
}

void setUp() { boot(); }
void tearDown() {}

#define ASSERT_DT(dt_, y_, mo_, d_, h_, mi_, s_)  \
  do {                                            \
    TEST_ASSERT_EQUAL_UINT16(y_, (dt_).year);     \
    TEST_ASSERT_EQUAL_UINT8(mo_, (dt_).month);    \
    TEST_ASSERT_EQUAL_UINT8(d_, (dt_).day);       \
    TEST_ASSERT_EQUAL_UINT8(h_, (dt_).hour);      \
    TEST_ASSERT_EQUAL_UINT8(mi_, (dt_).minute);   \
    TEST_ASSERT_EQUAL_UINT8(s_, (dt_).second);    \
  } while (0)

static bool leap(uint16_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static uint8_t days_in(uint16_t y, uint8_t m) {
  static const uint8_t k[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return static_cast<uint8_t>(m == 2 && leap(y) ? 29 : k[m - 1]);
}

// ---- Math -------------------------------------------------------------------------------------
static void test_round_trip_every_day_2000_to_2135() {
  epoch_t expect = 0;
  uint8_t weekday = 5;  // 2000-01-01 was a Saturday
  for (uint16_t y = 2000; y <= 2135; ++y) {
    for (uint8_t m = 1; m <= 12; ++m) {
      for (uint8_t d = 1; d <= days_in(y, m); ++d) {
        const epoch_t t0 = at(y, m, d);
        TEST_ASSERT_EQUAL_UINT32(expect, t0);
        DateTime out;
        clock::to_datetime(t0, out);
        ASSERT_DT(out, y, m, d, 0, 0, 0);
        TEST_ASSERT_EQUAL_UINT8(weekday, out.weekday);
        const epoch_t t1 = at(y, m, d, 23, 59, 59);
        TEST_ASSERT_EQUAL_UINT32(expect + 86399, t1);
        clock::to_datetime(t1, out);
        ASSERT_DT(out, y, m, d, 23, 59, 59);
        expect += 86400;
        weekday = static_cast<uint8_t>((weekday + 1) % 7);
      }
    }
  }
}

static void test_known_dates() {
  DateTime out;
  clock::to_datetime(0, out);
  ASSERT_DT(out, 2000, 1, 1, 0, 0, 0);
  TEST_ASSERT_EQUAL_UINT8(5, out.weekday);
  clock::to_datetime(at(2024, 2, 29, 12, 30, 15), out);
  ASSERT_DT(out, 2024, 2, 29, 12, 30, 15);
  TEST_ASSERT_EQUAL_UINT8(3, out.weekday);  // a Thursday
  TEST_ASSERT_EQUAL_UINT32(at(2000, 3, 1), at(2000, 2, 29) + 86400);  // 2000 is a leap year
  TEST_ASSERT_EQUAL_UINT32(at(2100, 3, 1), at(2100, 2, 28) + 86400);  // 2100 is not
}

// ---- RTC at boot -------------------------------------------------------------------------------
static void test_live_rtc_makes_time_valid_at_boot() {
  TEST_ASSERT_TRUE(clock::valid());
  TEST_ASSERT_EQUAL_INT(U(ClockSource::Rtc), U(clock::source()));
  TEST_ASSERT_EQUAL_UINT32(1000000, clock::now());
  TEST_ASSERT_EQUAL_INT(0, rec_count(EventType::Clock, U(ClockCode::Invalid)));
}

static void test_dead_rtc_is_invalid_and_posts_invalid() {
  boot(1000000, true);
  TEST_ASSERT_FALSE(clock::valid());
  TEST_ASSERT_EQUAL_INT(U(ClockSource::None), U(clock::source()));
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Clock, U(ClockCode::Invalid)));
}

static void test_no_minute_events_while_invalid() {
  boot(1000000, true);
  pump(130000);
  TEST_ASSERT_EQUAL_INT(0, rec_count(EventType::Clock, U(ClockCode::Minute)));
}

static void test_no_rtc_attached_still_runs_once_set() {
  boot(0, false, false);
  TEST_ASSERT_FALSE(clock::valid());
  TEST_ASSERT_EQUAL_INT(U(Status::Ok), U(clock::set(5000, ClockSource::Host)));
  TEST_ASSERT_TRUE(clock::valid());
  pump(3000);
  TEST_ASSERT_EQUAL_UINT32(5003, clock::now());
}

// ---- Events ------------------------------------------------------------------------------------
static void test_minute_event_at_the_boundary_with_local_epoch() {
  boot(at(2026, 1, 1, 10, 0, 58));
  g_rec.n = 0;
  pump(1900);
  TEST_ASSERT_EQUAL_INT(0, rec_count(EventType::Clock, U(ClockCode::Minute)));
  pump(200);
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Clock, U(ClockCode::Minute)));
  const Event& e = g_rec.ev[rec_find(EventType::Clock, U(ClockCode::Minute))];
  TEST_ASSERT_EQUAL_UINT32(at(2026, 1, 1, 10, 1, 0), e.data);
  pump(60000);
  TEST_ASSERT_EQUAL_INT(2, rec_count(EventType::Clock, U(ClockCode::Minute)));
}

static void test_hour_and_day_events() {
  boot(at(2026, 1, 1, 23, 59, 59));
  g_rec.n = 0;
  pump(1100);
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Clock, U(ClockCode::Minute)));
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Clock, U(ClockCode::Hour)));
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Clock, U(ClockCode::DayChanged)));
}

static void test_day_follows_local_time_not_utc() {
  boot(at(2026, 1, 1, 22, 59, 59));
  clock::set_tz_offset_min(60);
  g_rec.n = 0;
  pump(1100);
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Clock, U(ClockCode::DayChanged)));
}

static void test_second_events_are_opt_in() {
  g_rec.n = 0;
  pump(3100);
  TEST_ASSERT_EQUAL_INT(0, rec_count(EventType::Clock, U(ClockCode::Second)));
  clock::enable_second_events(true);
  pump(3000);
  TEST_ASSERT_EQUAL_INT(3, rec_count(EventType::Clock, U(ClockCode::Second)));
}

static void test_next_deadline_lets_an_idle_loop_sleep_to_the_minute() {
  boot(at(2026, 1, 1, 10, 0, 30));
  const ms_t now = hal::millis();
  const ms_t d = clock::kModule.next_deadline(now);
  TEST_ASSERT_EQUAL_INT32(30000, time_until(now, d));
  clock::enable_second_events(true);
  TEST_ASSERT_EQUAL_INT32(1000, time_until(now, clock::kModule.next_deadline(now)));
}

static void test_setting_the_time_fires_no_false_boundary() {
  g_rec.n = 0;
  clock::set(at(2030, 6, 6, 6, 6, 6), ClockSource::Host);
  pump(1000);
  TEST_ASSERT_EQUAL_INT(0, rec_count(EventType::Clock, U(ClockCode::Minute)));
  TEST_ASSERT_EQUAL_INT(0, rec_count(EventType::Clock, U(ClockCode::DayChanged)));
}

// ---- RTC re-read -------------------------------------------------------------------------------
static void test_rtc_is_re_read_every_60s_and_corrects_drift() {
  pump(1000);
  g_rtc.base += 5;  // the RTC is 5 s ahead of the extrapolation
  pump(30000);
  TEST_ASSERT_EQUAL_UINT32(1000031, clock::now());  // not read yet
  pump(30000);
  TEST_ASSERT_EQUAL_UINT32(1000066, clock::now());
}

static void test_rtc_is_re_read_on_resume_and_minute_is_correct_after_sleep() {
  boot(at(2026, 1, 1, 10, 0, 0));
  g_rec.n = 0;
  g_rtc.base += 600;  // 10 minutes that the extrapolation did not see
  clock::kModule.on_resume(PowerState::Sleep, hal::kWakeButton);
  TEST_ASSERT_EQUAL_UINT32(at(2026, 1, 1, 10, 10, 0), clock::now());
  pump(2);
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Clock, U(ClockCode::Minute)));
  const Event& e = g_rec.ev[rec_find(EventType::Clock, U(ClockCode::Minute))];
  TEST_ASSERT_EQUAL_UINT32(at(2026, 1, 1, 10, 10, 0), e.data);
}

static void test_a_small_step_back_does_not_repeat_the_minute() {
  boot(at(2026, 1, 1, 10, 0, 59));
  g_rec.n = 0;
  pump(1100);
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Clock, U(ClockCode::Minute)));
  g_rtc.base -= 2;  // now just before the same minute boundary
  clock::kModule.on_resume(PowerState::Sleep, 0);
  pump(3000);
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Clock, U(ClockCode::Minute)));
}

static void test_rtc_losing_power_at_runtime_invalidates() {
  g_rec.n = 0;
  g_rtc.lost = true;
  pump(60100);
  TEST_ASSERT_FALSE(clock::valid());
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Clock, U(ClockCode::Invalid)));
}

// ---- set() and precedence ------------------------------------------------------------------------
static void test_set_writes_rtc_and_posts_time_set() {
  g_rec.n = 0;
  TEST_ASSERT_EQUAL_INT(U(Status::Ok), U(clock::set(2000000, ClockSource::Host)));
  bus::dispatch();
  TEST_ASSERT_EQUAL_UINT32(2000000, clock::now());
  TEST_ASSERT_EQUAL_UINT32(2000000, g_rtc.base);
  TEST_ASSERT_EQUAL_INT(U(ClockSource::Host), U(clock::source()));
  const int i = rec_find(EventType::Clock, U(ClockCode::TimeSet));
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_EQUAL_UINT16(U(ClockSource::Host), g_rec.ev[i].arg);
}

static void test_set_fixes_a_dead_rtc() {
  boot(0, true);
  clock::set(3000000, ClockSource::Manual);
  TEST_ASSERT_TRUE(clock::valid());
  TEST_ASSERT_FALSE(g_rtc.lost);
}

static void test_source_none_is_refused() {
  TEST_ASSERT_EQUAL_INT(U(Status::Invalid), U(clock::set(1, ClockSource::None)));
}

static void test_worse_automatic_source_cannot_overwrite_a_better_one_within_the_hour() {
  TEST_ASSERT_EQUAL_INT(U(Status::Ok), U(clock::set(2000000, ClockSource::Ntp)));
  TEST_ASSERT_EQUAL_INT(U(Status::Busy), U(clock::set(9, ClockSource::Rtc)));
  TEST_ASSERT_EQUAL_UINT32(2000000, clock::now());
  TEST_ASSERT_EQUAL_INT(U(Status::Ok), U(clock::set(2000100, ClockSource::Gps)));  // same rank
  port_native::advance_ms(3600000);
  TEST_ASSERT_EQUAL_INT(U(Status::Ok), U(clock::set(5000000, ClockSource::Rtc)));
  TEST_ASSERT_EQUAL_UINT32(5000000, clock::now());
}

// A person setting the time is never refused, even right after NTP or GPS set it.
static void test_person_setting_the_time_always_wins() {
  TEST_ASSERT_EQUAL_INT(U(Status::Ok), U(clock::set(2000000, ClockSource::Ntp)));
  TEST_ASSERT_EQUAL_INT(U(Status::Ok), U(clock::set(3000000, ClockSource::Manual)));
  TEST_ASSERT_EQUAL_UINT32(3000000, clock::now());
  TEST_ASSERT_EQUAL_INT(U(Status::Ok), U(clock::set(2000000, ClockSource::Gps)));
  TEST_ASSERT_EQUAL_INT(U(Status::Ok), U(clock::set(4000000, ClockSource::Host)));
  TEST_ASSERT_EQUAL_UINT32(4000000, clock::now());
}

// An RTC read at boot is not a "set": it must not lock a person out of setting the time.
static void test_manual_set_works_right_after_a_boot_from_rtc() {
  TEST_ASSERT_EQUAL_INT(U(ClockSource::Rtc), U(clock::source()));
  TEST_ASSERT_EQUAL_INT(U(Status::Ok), U(clock::set(2000000, ClockSource::Host)));
}

// ---- Local time and DST ----------------------------------------------------------------------------
static void test_fixed_offset() {
  clock::set(at(2026, 1, 15, 12, 0, 0), ClockSource::Host);
  clock::set_tz_offset_min(-360);
  TEST_ASSERT_EQUAL_UINT32(at(2026, 1, 15, 6, 0, 0), clock::local_now());
  clock::set_tz_offset_min(330);
  TEST_ASSERT_EQUAL_UINT32(at(2026, 1, 15, 17, 30, 0), clock::local_now());
}

static epoch_t local_at(epoch_t utc) {
  clock::set(utc, ClockSource::Host);
  return clock::local_now();
}

static void test_eu_dst_changes_at_0100_utc_on_last_sundays() {
  clock::set_tz_offset_min(60);
  clock::set_eu_us_dst(1);
  TEST_ASSERT_EQUAL_UINT32(at(2026, 3, 29, 1, 59, 59), local_at(at(2026, 3, 29, 0, 59, 59)));
  TEST_ASSERT_EQUAL_UINT32(at(2026, 3, 29, 3, 0, 0), local_at(at(2026, 3, 29, 1, 0, 0)));
  TEST_ASSERT_EQUAL_UINT32(at(2026, 10, 25, 2, 59, 59), local_at(at(2026, 10, 25, 0, 59, 59)));
  TEST_ASSERT_EQUAL_UINT32(at(2026, 10, 25, 2, 0, 0), local_at(at(2026, 10, 25, 1, 0, 0)));
}

static void test_us_dst_changes_at_0200_local_on_second_and_first_sundays() {
  clock::set_tz_offset_min(-360);  // Central
  clock::set_eu_us_dst(2);
  TEST_ASSERT_EQUAL_UINT32(at(2026, 3, 8, 1, 59, 59), local_at(at(2026, 3, 8, 7, 59, 59)));
  TEST_ASSERT_EQUAL_UINT32(at(2026, 3, 8, 3, 0, 0), local_at(at(2026, 3, 8, 8, 0, 0)));
  TEST_ASSERT_EQUAL_UINT32(at(2026, 11, 1, 1, 59, 59), local_at(at(2026, 11, 1, 6, 59, 59)));
  TEST_ASSERT_EQUAL_UINT32(at(2026, 11, 1, 1, 0, 0), local_at(at(2026, 11, 1, 7, 0, 0)));
}

static void test_no_dst_rule_means_no_shift() {
  clock::set_tz_offset_min(60);
  clock::set_eu_us_dst(0);
  TEST_ASSERT_EQUAL_UINT32(at(2026, 7, 1, 13, 0, 0), local_at(at(2026, 7, 1, 12, 0, 0)));
}

// ---- CLI -----------------------------------------------------------------------------------------
static void test_cli_time_shows_and_sets() {
  port_native::console_clear();
  char show[] = "time";
  cli::exec(show);
  TEST_ASSERT_NOT_NULL(strstr(port_native::console_capture(), "Rtc"));
  port_native::console_clear();
  char set[] = "time 835000000";
  cli::exec(set);
  TEST_ASSERT_EQUAL_UINT32(835000000, clock::now());
  TEST_ASSERT_EQUAL_INT(U(ClockSource::Host), U(clock::source()));
  TEST_ASSERT_NOT_NULL(strstr(port_native::console_capture(), "2026-06-17"));
}

static void test_cli_time_shows_dashes_when_invalid() {
  boot(0, true);
  port_native::console_clear();
  char show[] = "time";
  cli::exec(show);
  TEST_ASSERT_NOT_NULL(strstr(port_native::console_capture(), "--:--"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_round_trip_every_day_2000_to_2135);
  RUN_TEST(test_known_dates);
  RUN_TEST(test_live_rtc_makes_time_valid_at_boot);
  RUN_TEST(test_dead_rtc_is_invalid_and_posts_invalid);
  RUN_TEST(test_no_minute_events_while_invalid);
  RUN_TEST(test_no_rtc_attached_still_runs_once_set);
  RUN_TEST(test_minute_event_at_the_boundary_with_local_epoch);
  RUN_TEST(test_hour_and_day_events);
  RUN_TEST(test_day_follows_local_time_not_utc);
  RUN_TEST(test_second_events_are_opt_in);
  RUN_TEST(test_next_deadline_lets_an_idle_loop_sleep_to_the_minute);
  RUN_TEST(test_setting_the_time_fires_no_false_boundary);
  RUN_TEST(test_rtc_is_re_read_every_60s_and_corrects_drift);
  RUN_TEST(test_rtc_is_re_read_on_resume_and_minute_is_correct_after_sleep);
  RUN_TEST(test_a_small_step_back_does_not_repeat_the_minute);
  RUN_TEST(test_rtc_losing_power_at_runtime_invalidates);
  RUN_TEST(test_set_writes_rtc_and_posts_time_set);
  RUN_TEST(test_set_fixes_a_dead_rtc);
  RUN_TEST(test_source_none_is_refused);
  RUN_TEST(test_worse_automatic_source_cannot_overwrite_a_better_one_within_the_hour);
  RUN_TEST(test_person_setting_the_time_always_wins);
  RUN_TEST(test_manual_set_works_right_after_a_boot_from_rtc);
  RUN_TEST(test_fixed_offset);
  RUN_TEST(test_eu_dst_changes_at_0100_utc_on_last_sundays);
  RUN_TEST(test_us_dst_changes_at_0200_local_on_second_and_first_sundays);
  RUN_TEST(test_no_dst_rule_means_no_shift);
  RUN_TEST(test_cli_time_shows_and_sets);
  RUN_TEST(test_cli_time_shows_dashes_when_invalid);
  return UNITY_END();
}
