// T012 — soft timers (core.md "Soft timers", acceptance 4).
#include <unity.h>
#include "test_helpers.h"
using namespace core0;

static int g_fires;
static ms_t g_fire_at[8000];
static void cb(TimerId, void*) {
  if (g_fires < 8000) g_fire_at[g_fires] = hal::millis();
  ++g_fires;
}

void setUp() {
  reset_all();
  g_fires = 0;
}
void tearDown() {}

static void test_periodic_no_drift_over_10_minutes() {
  timers::start(100, TimerMode::Periodic, cb);
  for (ms_t t = 0; t < 600000; ++t) {
    port_native::advance_ms(1);
    timers::service(hal::millis());
  }
  TEST_ASSERT_EQUAL_INT(6000, g_fires);
  TEST_ASSERT_EQUAL_UINT32(600000, g_fire_at[5999]);
}

static void test_skip_not_burst_after_stall() {
  timers::start(100, TimerMode::Periodic, cb);
  port_native::advance_ms(100);
  timers::service(hal::millis());
  TEST_ASSERT_EQUAL_INT(1, g_fires);
  port_native::advance_ms(350);  // stall: deadlines 200, 300, 400 missed
  timers::service(hal::millis());
  TEST_ASSERT_EQUAL_INT(2, g_fires);
  port_native::advance_ms(49);  // t = 499
  timers::service(hal::millis());
  TEST_ASSERT_EQUAL_INT(2, g_fires);
  port_native::advance_ms(1);  // t = 500: phase kept
  timers::service(hal::millis());
  TEST_ASSERT_EQUAL_INT(3, g_fires);
}

static void test_start_event_posts_with_timer_id() {
  rec_attach(EventType::Timer);
  const TimerId id = timers::start_event(50, TimerMode::OneShot, make_event(EventType::Timer, 9));
  port_native::advance_ms(50);
  timers::service(hal::millis());
  bus::dispatch();
  TEST_ASSERT_EQUAL_INT(1, g_rec.n);
  TEST_ASSERT_EQUAL_UINT8(9, g_rec.ev[0].code);
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(id), g_rec.ev[0].arg);
}

static void test_next_deadline_and_compensate() {
  ms_t d = 0;
  TEST_ASSERT_FALSE(timers::next_deadline(d));
  const TimerId a = timers::start(1000, TimerMode::OneShot, cb);
  timers::start(300, TimerMode::OneShot, cb);
  TEST_ASSERT_TRUE(timers::next_deadline(d));
  TEST_ASSERT_EQUAL_UINT32(300, d);
  timers::compensate_sleep(500);
  TEST_ASSERT_EQUAL_UINT32(500, timers::remaining(a, hal::millis()));
}

static void test_stop_restart_change_period() {
  const TimerId id = timers::start(100, TimerMode::OneShot, cb);
  TEST_ASSERT_TRUE(timers::active(id));
  timers::stop(id);
  TEST_ASSERT_FALSE(timers::active(id));
  port_native::advance_ms(200);
  timers::service(hal::millis());
  TEST_ASSERT_EQUAL_INT(0, g_fires);
  const TimerId id2 = timers::start(100, TimerMode::OneShot, cb);
  port_native::advance_ms(60);
  timers::restart(id2);
  port_native::advance_ms(60);
  timers::service(hal::millis());
  TEST_ASSERT_EQUAL_INT(0, g_fires);
  port_native::advance_ms(40);
  timers::service(hal::millis());
  TEST_ASSERT_EQUAL_INT(1, g_fires);
  timers::change_period(id2, 10);
  port_native::advance_ms(10);
  timers::service(hal::millis());
  TEST_ASSERT_EQUAL_INT(2, g_fires);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_periodic_no_drift_over_10_minutes);
  RUN_TEST(test_skip_not_burst_after_stall);
  RUN_TEST(test_start_event_posts_with_timer_id);
  RUN_TEST(test_next_deadline_and_compensate);
  RUN_TEST(test_stop_restart_change_period);
  return UNITY_END();
}
