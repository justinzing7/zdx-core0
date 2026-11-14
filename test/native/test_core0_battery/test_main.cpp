// T063 — battery service: curve, IIR seed, monotonic sweep, report step, hysteresis, noise,
// audio deferral, sample_now (battery.md acceptance).
#include <unity.h>
#include "test_helpers.h"
#include "mock_audio.h"
using namespace core0;

namespace core0 {
namespace test {
void reset_battery();
}
}  // namespace core0

// Scripted ADC: the pin voltage the next read returns (Vbat / 2 with a 2:1 divider).
static uint16_t g_pin_mv = 2000;
static int      g_reads = 0;
static uint16_t read_mv() { ++g_reads; return g_pin_mv; }
static void set_vbat(uint16_t mv) { g_pin_mv = static_cast<uint16_t>(mv / 2); }

static MockAudioOut g_out;

static BatteryConfig cfg() {
  BatteryConfig c = {read_mv, 2, 1, 0xFF, 0, 8, 3, 10000, 0, kCurveLiPo1S,
                     static_cast<uint8_t>(count_of(kCurveLiPo1S)), 3500, 3350, 50, 5, nullptr, nullptr};
  return c;
}

static void boot(uint16_t vbat, bool with_audio = false) {
  reset_all();
  test::reset_battery();
  g_reads = 0;
  set_vbat(vbat);
  g_out.reset();
  if (with_audio) {
    audio::attach_out(g_out);
    sys::register_module(&audio::kModule);
  }
  battery::configure(cfg());
  sys::register_module(&battery::kModule);
  rec_attach(EventType::Battery);
  sys::begin();
  bus::dispatch();
}

void setUp() {}
void tearDown() {}

// Settle the IIR at `vbat` with forced samples (each sample_now() = one filtered reading).
static void settle(uint16_t vbat, int n = 40) {
  set_vbat(vbat);
  for (int i = 0; i < n; ++i) battery::sample_now();
  bus::dispatch();
}

static void test_curve_points_and_midpoints() {
  const uint8_t n = static_cast<uint8_t>(count_of(kCurveLiPo1S));
  TEST_ASSERT_EQUAL_UINT8(100, battery::mv_to_percent(4200, kCurveLiPo1S, n));
  TEST_ASSERT_EQUAL_UINT8(100, battery::mv_to_percent(4180, kCurveLiPo1S, n));
  TEST_ASSERT_EQUAL_UINT8(81, battery::mv_to_percent(4000, kCurveLiPo1S, n));
  TEST_ASSERT_EQUAL_UINT8(9, battery::mv_to_percent(3500, kCurveLiPo1S, n));
  TEST_ASSERT_EQUAL_UINT8(0, battery::mv_to_percent(3300, kCurveLiPo1S, n));
  TEST_ASSERT_EQUAL_UINT8(0, battery::mv_to_percent(3000, kCurveLiPo1S, n));
  TEST_ASSERT_EQUAL_UINT8(75, battery::mv_to_percent(3950, kCurveLiPo1S, n));  // 69..81
  TEST_ASSERT_EQUAL_UINT8(41, battery::mv_to_percent(3725, kCurveLiPo1S, n));  // 36..46
}

static void test_first_sample_seeds_filter() {
  boot(3900);
  TEST_ASSERT_EQUAL_UINT16(3900, battery::mv());
  TEST_ASSERT_EQUAL_UINT8(69, battery::percent());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BatteryState::Normal), static_cast<int>(battery::state()));
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Battery, U(BatteryCode::StateChanged)));
  const Event& e = g_rec.ev[rec_find(EventType::Battery, U(BatteryCode::StateChanged))];
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(BatteryState::Normal), e.arg);
  TEST_ASSERT_EQUAL_UINT32((3900u << 8) | 69u, e.data);
}

static void test_averages_samples_and_scales_divider() {
  boot(4000);
  TEST_ASSERT_EQUAL_INT(8, g_reads);  // one reading = `samples` ADC reads
  TEST_ASSERT_EQUAL_UINT16(4000, battery::mv());
}

static void test_periodic_sampling() {
  boot(4000);
  const int r0 = g_reads;
  pump(9990);
  TEST_ASSERT_EQUAL_INT(r0, g_reads);
  pump(20);
  TEST_ASSERT_EQUAL_INT(r0 + 8, g_reads);
}

static void test_sweep_monotonic() {
  boot(4200);
  uint8_t prev = battery::percent();
  for (uint16_t mv = 4200; mv >= 3300; mv = static_cast<uint16_t>(mv - 10)) {
    set_vbat(mv);
    for (int i = 0; i < 3; ++i) battery::sample_now();
    TEST_ASSERT_TRUE(battery::percent() <= prev);
    prev = battery::percent();
  }
}

static void test_percent_changed_only_on_step() {
  boot(4000);  // 81 %
  g_rec.n = 0;
  settle(3990);  // ~80 %: below the 5-point step
  TEST_ASSERT_EQUAL_INT(0, rec_count(EventType::Battery, U(BatteryCode::PercentChanged)));
  settle(3950);  // 75 %: moved 6 from the last report
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Battery, U(BatteryCode::PercentChanged)));
  const Event& e = g_rec.ev[rec_find(EventType::Battery, U(BatteryCode::PercentChanged))];
  TEST_ASSERT_TRUE((e.data & 0xFF) <= 76);
}

static BatteryState st() { return battery::state(); }
#define ASSERT_STATE(s) TEST_ASSERT_EQUAL_INT(static_cast<int>(BatteryState::s), static_cast<int>(st()))

static void test_hysteresis_thresholds() {
  boot(3700);
  ASSERT_STATE(Normal);
  settle(3510);
  ASSERT_STATE(Normal);
  settle(3500);
  ASSERT_STATE(Low);
  settle(3540);
  ASSERT_STATE(Low);  // needs +50 to recover
  settle(3550);
  ASSERT_STATE(Normal);
  settle(3500);
  ASSERT_STATE(Low);
  settle(3360);
  ASSERT_STATE(Low);
  settle(3350);
  ASSERT_STATE(Critical);
  settle(3390);
  ASSERT_STATE(Critical);
  settle(3400);
  ASSERT_STATE(Low);
}

static void test_boot_straight_into_low_or_critical() {
  boot(3450);
  ASSERT_STATE(Low);
  boot(3300);
  ASSERT_STATE(Critical);
}

static void test_noise_no_flapping() {
  boot(3500);
  g_rec.n = 0;
  // 10 min of 10 s samples with +-10 mV noise around the Low threshold.
  for (int i = 0; i < 60; ++i) {
    set_vbat(static_cast<uint16_t>((i & 1) ? 3510 : 3490));
    pump(10000);
  }
  TEST_ASSERT_TRUE(rec_count(EventType::Battery, U(BatteryCode::StateChanged)) <= 1);
}

static const Note kLong[] = {{440, 30000}};
static const Melody kLongMelody = {kLong, 1, 0};

static void test_sample_deferred_during_alarm_audio() {
  boot(4000, true);
  audio::play(kLongMelody, AudioPriority::Alarm);
  bus::dispatch();
  const int r0 = g_reads;
  pump(10010);  // period elapsed, but an Alarm is playing
  TEST_ASSERT_EQUAL_INT(r0, g_reads);
  pump(10000);  // deferred for a full period: sample anyway
  TEST_ASSERT_EQUAL_INT(r0 + 8, g_reads);
}

static void test_sample_not_deferred_for_notify() {
  boot(4000, true);
  audio::play(kLongMelody, AudioPriority::Notify);
  const int r0 = g_reads;
  pump(10010);
  TEST_ASSERT_EQUAL_INT(r0 + 8, g_reads);
}

static void test_sample_now_forces_reading() {
  boot(4000);
  const int r0 = g_reads;
  battery::sample_now();
  TEST_ASSERT_EQUAL_INT(r0 + 8, g_reads);
}

static void test_switched_divider_settles_before_sampling() {
  reset_all();
  g_reads = 0;
  set_vbat(4000);
  BatteryConfig c = cfg();
  c.enable_pin = 5;
  c.settle_ms = 10;
  battery::configure(c);
  sys::register_module(&battery::kModule);
  sys::begin();
  TEST_ASSERT_EQUAL_INT(0, g_reads);  // boot reading waits for the divider to settle
  pump(9);
  TEST_ASSERT_EQUAL_INT(0, g_reads);
  pump(2);
  TEST_ASSERT_EQUAL_INT(8, g_reads);
  TEST_ASSERT_EQUAL_UINT16(4000, battery::mv());
  battery::sample_now();
  TEST_ASSERT_EQUAL_INT(8, g_reads);
  pump(11);
  TEST_ASSERT_EQUAL_INT(16, g_reads);
}

static void test_no_timer_wakes_in_sleep_then_sampling_resumes() {
  boot(4000);
  battery::kModule.on_suspend(PowerState::Sleep);
  ms_t t;
  TEST_ASSERT_FALSE(timers::next_deadline(t));  // period_sleep_ms = 0: nothing may wake the device
  const int r0 = g_reads;
  pump(30000);
  TEST_ASSERT_EQUAL_INT(r0, g_reads);
  battery::kModule.on_resume(PowerState::Sleep, hal::kWakeButton);
  TEST_ASSERT_EQUAL_INT(r0 + 8, g_reads);  // on_resume samples at once
  pump(10010);
  TEST_ASSERT_EQUAL_INT(r0 + 16, g_reads);  // and the period runs again
}

static void test_cli_bat_line() {
  boot(3987);
  port_native::console_clear();
  char line[] = "bat";
  cli::exec(line);
  // cli not registered here; the battery module adds `bat` itself.
  TEST_ASSERT_NOT_NULL(strstr(port_native::console_capture(), "mV"));
  TEST_ASSERT_NOT_NULL(strstr(port_native::console_capture(), "Normal"));
  TEST_ASSERT_NOT_NULL(strstr(port_native::console_capture(), "raw 1993"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_curve_points_and_midpoints);
  RUN_TEST(test_first_sample_seeds_filter);
  RUN_TEST(test_averages_samples_and_scales_divider);
  RUN_TEST(test_periodic_sampling);
  RUN_TEST(test_sweep_monotonic);
  RUN_TEST(test_percent_changed_only_on_step);
  RUN_TEST(test_hysteresis_thresholds);
  RUN_TEST(test_boot_straight_into_low_or_critical);
  RUN_TEST(test_noise_no_flapping);
  RUN_TEST(test_sample_deferred_during_alarm_audio);
  RUN_TEST(test_sample_not_deferred_for_notify);
  RUN_TEST(test_sample_now_forces_reading);
  RUN_TEST(test_switched_divider_settles_before_sampling);
  RUN_TEST(test_no_timer_wakes_in_sleep_then_sampling_resumes);
  RUN_TEST(test_cli_bat_line);
  return UNITY_END();
}
