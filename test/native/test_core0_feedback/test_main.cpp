// T053 — status LED patterns, Base/Overlay layers, brightness cap, sleep (feedback.md acceptance).
#include <unity.h>
#include "test_helpers.h"
#include "mock_led.h"
using namespace core0;

namespace core0 {
namespace test {
void reset_feedback();
}
}  // namespace core0

static MockLed g_led;

static void boot() {
  reset_all();
  test::reset_feedback();
  g_led.reset();
  feedback::attach_led(g_led);
  sys::register_module(&feedback::kModule);
  sys::begin();
}

void setUp() { boot(); }
void tearDown() {}

static bool is(const Rgb& c, uint8_t r, uint8_t g, uint8_t b) { return c.r == r && c.g == g && c.b == b; }

static void test_solid() {
  feedback::led(LedPattern::Solid, colors::kGreen);
  pump(2);
  TEST_ASSERT_TRUE(is(g_led.last, 0, 255, 0));
}

static void test_blink_50_percent_within_one_tick() {
  feedback::led(LedPattern::Blink, colors::kRed, 1000);
  pump(1);
  TEST_ASSERT_TRUE(is(g_led.last, 255, 0, 0));
  pump(497);  // t = 498
  TEST_ASSERT_TRUE(is(g_led.last, 255, 0, 0));
  pump(3);    // t = 501
  TEST_ASSERT_TRUE(is(g_led.last, 0, 0, 0));
  pump(500);  // t = 1001: next period
  TEST_ASSERT_TRUE(is(g_led.last, 255, 0, 0));
}

static void test_pulse_breathes() {
  feedback::led(LedPattern::Pulse, colors::kWhite, 2000);
  pump(1);
  const uint8_t low = g_led.last.r;
  pump(999);  // half period: peak
  const uint8_t peak = g_led.last.r;
  pump(999);
  const uint8_t low2 = g_led.last.r;
  TEST_ASSERT_TRUE(peak > 200);
  TEST_ASSERT_TRUE(low < 20);
  TEST_ASSERT_TRUE(low2 < 20);
}

static Rgb pulse_colour_at(ms_t t, bool with_flash) {
  boot();
  feedback::led(LedPattern::Pulse, colors::kAmber, 2000);
  pump(300);
  if (with_flash) feedback::led(LedPattern::Flash, colors::kGreen, 80, feedback::Layer::Overlay);
  pump(t - 300);
  return g_led.last;
}

static void test_flash_overlay_returns_to_base_with_phase() {
  const Rgb with = pulse_colour_at(700, true);
  const Rgb without = pulse_colour_at(700, false);
  TEST_ASSERT_TRUE(is(with, without.r, without.g, without.b));
  // during the flash the overlay shows
  boot();
  feedback::led(LedPattern::Pulse, colors::kAmber, 2000);
  pump(300);
  feedback::led(LedPattern::Flash, colors::kGreen, 80, feedback::Layer::Overlay);
  pump(40);
  TEST_ASSERT_TRUE(is(g_led.last, 0, 255, 0));
  pump(45);
  TEST_ASSERT_FALSE(is(g_led.last, 0, 255, 0));
}

static void test_brightness_cap() {
  feedback::led_brightness(64);
  feedback::led(LedPattern::Solid, colors::kWhite);
  pump(2);
  TEST_ASSERT_EQUAL_UINT8(64, g_led.last.r);
  TEST_ASSERT_EQUAL_UINT8(64, g_led.last.g);
  TEST_ASSERT_EQUAL_UINT8(64, g_led.last.b);
  for (int i = 0; i < g_led.n; ++i) TEST_ASSERT_TRUE(g_led.hist[i].r <= 64);
}

static void test_off_and_unpowered_in_sleep() {
  feedback::led(LedPattern::Solid, colors::kRed);
  pump(2);
  feedback::kModule.on_suspend(PowerState::Sleep);
  TEST_ASSERT_TRUE(is(g_led.last, 0, 0, 0));
  TEST_ASSERT_FALSE(g_led.powered);
  feedback::kModule.on_resume(PowerState::Sleep, hal::kWakeButton);
  pump(2);
  TEST_ASSERT_TRUE(g_led.powered);
  TEST_ASSERT_TRUE(is(g_led.last, 255, 0, 0));
}

static void test_background_wake_keeps_led_off() {
  feedback::led(LedPattern::Solid, colors::kRed);
  pump(2);
  feedback::kModule.on_suspend(PowerState::Sleep);
  feedback::kModule.on_resume(PowerState::Sleep, hal::kWakeRtc);
  pump(2);
  TEST_ASSERT_FALSE(g_led.powered);
  TEST_ASSERT_TRUE(is(g_led.last, 0, 0, 0));
  feedback::kModule.on_resume(PowerState::Idle, hal::kWakeOther);  // request(Active) later
  pump(2);
  TEST_ASSERT_TRUE(g_led.powered);
  TEST_ASSERT_TRUE(is(g_led.last, 255, 0, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_solid);
  RUN_TEST(test_blink_50_percent_within_one_tick);
  RUN_TEST(test_pulse_breathes);
  RUN_TEST(test_flash_overlay_returns_to_base_with_phase);
  RUN_TEST(test_brightness_cap);
  RUN_TEST(test_off_and_unpowered_in_sleep);
  RUN_TEST(test_background_wake_keeps_led_off);
  return UNITY_END();
}
