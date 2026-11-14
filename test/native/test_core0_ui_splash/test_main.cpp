// T032 — SplashScreen timing (ui.md "Splash", acceptance 4).
#include <unity.h>
#include "test_helpers.h"
using namespace core0;

class NextScreen : public Screen {
 public:
  const char* name() const override { return "next"; }
  void draw(Gfx&, const Rect&) override {}
};
static NextScreen s_next;
static const SplashConfig kCfg = {nullptr, nullptr, "CAPTION", 1200, 3000, &s_next};
static SplashScreen s_splash(kCfg);

void setUp() {
  reset_all();
  sys::register_module(&ui::kModule);
  sys::begin();   // posts InitDone; tests that need "no InitDone" drain it before pushing
}
void tearDown() {}

static void drain() { while (bus::pending()) bus::dispatch(); }

static ms_t run_until_next(ms_t limit) {
  for (ms_t t = 0; t < limit; ++t) {
    if (ui::top() == &s_next) return hal::millis();
    pump(1);
  }
  return 0xFFFFFFFFu;
}

static void test_min_hold_then_next_after_init_done() {
  drain();  // InitDone from begin() delivered before the splash exists: ignored
  ui::home(s_splash);
  pump(200);
  bus::post(make_event(EventType::System, U(SystemCode::InitDone)));
  const ms_t at = run_until_next(5000);
  TEST_ASSERT_UINT32_WITHIN(16, 1200, at);
}

static void test_max_without_init_done_logs_warning() {
  drain();
  ui::home(s_splash);
  port_native::console_clear();
  const ms_t at = run_until_next(5000);
  TEST_ASSERT_UINT32_WITHIN(16, 3000, at);
  TEST_ASSERT_TRUE(at <= 3000 + 16);
  TEST_ASSERT_NOT_NULL(strstr(port_native::console_capture(), "W ui: splash"));
}

static void test_key_before_min_does_not_skip() {
  drain();
  ui::home(s_splash);
  pump(200);
  bus::post(make_event(EventType::System, U(SystemCode::InitDone)));
  pump(400);
  bus::post(make_event(EventType::Input, U(InputCode::Press), U(Key::Select)));
  pump(500);  // t = 1100
  TEST_ASSERT_EQUAL_PTR(&s_splash, ui::top());
  const ms_t at = run_until_next(5000);
  TEST_ASSERT_TRUE(at >= 1200);
}

static void test_init_done_already_queued_at_enter() {
  // main.cpp order: sys::begin() posts InitDone, then ui::home(splash) before the first pass.
  ui::home(s_splash);
  const ms_t at = run_until_next(5000);
  TEST_ASSERT_UINT32_WITHIN(16, 1200, at);
}

// The loop may sleep until sys::next_deadline, so the splash must report its own deadlines.
static void test_deadline_covers_min_hold() {
  drain();
  ui::home(s_splash);
  bus::post(make_event(EventType::System, U(SystemCode::InitDone)));
  pump(1100);
  const ms_t now = hal::millis();
  TEST_ASSERT_TRUE(time_until(now, sys::next_deadline(now)) <= 101);
}

static void test_deadline_covers_max_hold() {
  drain();
  ui::home(s_splash);
  pump(2500);
  const ms_t now = hal::millis();
  TEST_ASSERT_TRUE(time_until(now, sys::next_deadline(now)) <= 501);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_min_hold_then_next_after_init_done);
  RUN_TEST(test_max_without_init_done_logs_warning);
  RUN_TEST(test_key_before_min_does_not_skip);
  RUN_TEST(test_init_done_already_queued_at_enter);
  RUN_TEST(test_deadline_covers_min_hold);
  RUN_TEST(test_deadline_covers_max_hold);
  return UNITY_END();
}
