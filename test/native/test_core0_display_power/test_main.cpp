// T078 — display power contract (display.md acceptance 4) through real power transitions.
#include <unity.h>
#include "test_helpers.h"
#include "mock_panel.h"
using namespace core0;

static MockPanel g_panel;
static int g_draws = 0;
static void draw_fn(Gfx&, const Rect&) { ++g_draws; }

static const PowerPolicy kPolicy = {20000, 60000, 120000, 0, hal::kWakeButton | hal::kWakeRtc};

void setUp() {
  reset_all();
  g_panel.reset();
  g_draws = 0;
  display::attach(g_panel);
  display::set_draw_fn(draw_fn);
  display::set_brightness(200);
  sys::register_module(&power::kModule);
  sys::register_module(&display::kModule);
  power::set_policy(kPolicy);
  port_native::set_wake_bits(hal::kWakeRtc);
  sys::begin();
  pump(50);
}
void tearDown() {}

static void wake() {
  power::activity(port_native::now());
  pump(1);
}

static void test_dim_lowers_brightness_panel_stays_on() {
  pump(20000);
  TEST_ASSERT_EQUAL_UINT8(8, g_panel.brightness);
  TEST_ASSERT_TRUE(g_panel.power_on);
}

static void test_dim_never_brighter_than_user_level() {
  display::set_brightness(2);
  pump(20000);
  TEST_ASSERT_EQUAL_UINT8(2, g_panel.brightness);
}

static void test_brightness_edit_while_dimmed_is_applied_on_resume() {
  pump(20000);
  display::set_brightness(100);
  TEST_ASSERT_EQUAL_UINT8(8, g_panel.brightness);
  wake();
  TEST_ASSERT_EQUAL_UINT8(100, g_panel.brightness);
}

static void test_idle_turns_panel_off() {
  pump(60000);
  TEST_ASSERT_FALSE(g_panel.power_on);
}

static void test_no_drawing_while_panel_off() {
  pump(60000);
  const int draws = g_draws, flushes = g_panel.flush_calls;
  display::invalidate_all();
  pump(500);
  TEST_ASSERT_EQUAL_INT(draws, g_draws);
  TEST_ASSERT_EQUAL_INT(flushes, g_panel.flush_calls);
  const ms_t now = port_native::now();
  TEST_ASSERT_TRUE(time_until(now, sys::next_deadline(now)) > 16);  // dirty must not spin the loop
}

static void test_resume_restores_power_brightness_and_one_flush() {
  pump(60000);
  const int flushes = g_panel.flush_calls;
  wake();
  TEST_ASSERT_TRUE(g_panel.power_on);
  TEST_ASSERT_EQUAL_UINT8(200, g_panel.brightness);
  pump(50);
  TEST_ASSERT_EQUAL_INT(flushes + 1, g_panel.flush_calls);
}

static void test_rtc_wake_keeps_panel_off_until_active() {
  timers::start(130000, TimerMode::OneShot, [](TimerId, void*) {});
  pump(125000);  // asleep since 120 s; the timer wakes it in the background at 130 s
  TEST_ASSERT_FALSE(g_panel.power_on);
  pump(5100);
  TEST_ASSERT_FALSE(g_panel.power_on);  // background wake: the panel stayed off
  wake();
  TEST_ASSERT_TRUE(g_panel.power_on);
  TEST_ASSERT_EQUAL_UINT8(200, g_panel.brightness);
}

static void test_sleep_waits_while_panel_busy() {
  pump(60000);
  g_panel.is_busy = true;
  pump(60100);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PowerState::Idle), static_cast<int>(power::state()));
  TEST_ASSERT_EQUAL_UINT32(0, port_native::sleep_calls(hal::SleepDepth::Light));
  g_panel.is_busy = false;
  pump(1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PowerState::Sleep), static_cast<int>(power::state()));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_dim_lowers_brightness_panel_stays_on);
  RUN_TEST(test_dim_never_brighter_than_user_level);
  RUN_TEST(test_brightness_edit_while_dimmed_is_applied_on_resume);
  RUN_TEST(test_idle_turns_panel_off);
  RUN_TEST(test_no_drawing_while_panel_off);
  RUN_TEST(test_resume_restores_power_brightness_and_one_flush);
  RUN_TEST(test_rtc_wake_keeps_panel_off_until_active);
  RUN_TEST(test_sleep_waits_while_panel_busy);
  return UNITY_END();
}
