// T022 — debounce + gestures (input.md acceptance 1–3, 6).
#include <unity.h>
#include "test_helpers.h"
using namespace core0;

static const KeyConfig kKeys[] = {
    {Key::Up, 0xFF, kKeyActiveLow | kKeyRepeat | kKeyWake},
    {Key::Down, 0xFF, kKeyActiveLow | kKeyRepeat | kKeyWake},
    {Key::Select, 0xFF, kKeyActiveLow | kKeyWake},
    {Key::Back, 0xFF, kKeyActiveLow | kKeyLongPress | kKeyWake},
};
static const InputTiming kTiming = {5, 600, 400, 80, 250};

void setUp() {
  reset_all();
  input::configure(kKeys, 4, kTiming);
  sys::register_module(&input::kModule);
  rec_attach(EventType::Input);
  sys::begin();
}
void tearDown() {}

static void press(Key k) { input::feed_key(k, true, hal::millis()); }
static void release(Key k) { input::feed_key(k, false, hal::millis()); }

static void test_chatter_gives_one_press_one_release() {
  pump(10);
  for (int i = 0; i < 5; ++i) { press(Key::Select); pump(1); release(Key::Select); pump(1); }
  press(Key::Select);
  pump(50);
  for (int i = 0; i < 5; ++i) { release(Key::Select); pump(1); press(Key::Select); pump(1); }
  release(Key::Select);
  pump(50);
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Input, U(InputCode::Press)));
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Input, U(InputCode::Release)));
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Input, U(InputCode::Click)));
}

static void test_press_latency_within_debounce_plus_tick() {
  pump(10);
  const ms_t edge = hal::millis();
  press(Key::Up);
  pump(20);
  const int i = rec_find(EventType::Input, U(InputCode::Press));
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_TRUE(time_since(g_rec.at[i], edge) <= 5 + 1);
}

static void test_repeat_count_for_2s_hold() {
  pump(10);
  press(Key::Up);
  pump(5);                // debounce -> Press at ~t+5
  pump(2000);
  release(Key::Up);
  pump(20);
  const int n = rec_count(EventType::Input, U(InputCode::Repeat));
  const int expected = (2000 - 400) / 80 + 1;
  TEST_ASSERT_INT_WITHIN(1, expected, n);
}

// Regression (bench 2026-10-31): releasing a held Repeat key emitted a Click, which would
// move a menu one extra step. Click = release before long_ms (input.md gesture table).
static void test_no_click_after_hold_on_repeat_key() {
  pump(10);
  press(Key::Down);
  pump(1100);
  release(Key::Down);
  pump(20);
  TEST_ASSERT_TRUE(rec_count(EventType::Input, U(InputCode::Repeat)) > 0);
  TEST_ASSERT_EQUAL_INT(0, rec_count(EventType::Input, U(InputCode::Click)));
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Input, U(InputCode::Release)));
}

static void test_long_press_once_and_no_click() {
  pump(10);
  press(Key::Back);
  pump(700);
  release(Key::Back);
  pump(20);
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Input, U(InputCode::LongPress)));
  TEST_ASSERT_EQUAL_INT(0, rec_count(EventType::Input, U(InputCode::Click)));
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Input, U(InputCode::Release)));
}

static void test_click_on_release() {
  pump(10);
  press(Key::Select);
  pump(100);
  TEST_ASSERT_EQUAL_INT(0, rec_count(EventType::Input, U(InputCode::Click)));
  release(Key::Select);
  pump(10);
  const int i = rec_find(EventType::Input, U(InputCode::Click));
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_EQUAL_UINT16(U(Key::Select), g_rec.ev[i].arg);
}

static void test_combo_up_select_suppresses_clicks() {
  pump(10);
  press(Key::Up);
  pump(30);
  press(Key::Select);
  pump(100);
  release(Key::Up);
  release(Key::Select);
  pump(20);
  const int i = rec_find(EventType::Input, U(InputCode::Combo));
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_EQUAL_UINT16((U(Key::Up) << 8) | U(Key::Select), g_rec.ev[i].arg);
  TEST_ASSERT_EQUAL_INT(0, rec_count(EventType::Input, U(InputCode::Click)));
}

static void test_is_down_and_held_ms() {
  pump(10);
  press(Key::Down);
  pump(105);
  TEST_ASSERT_TRUE(input::is_down(Key::Down));
  TEST_ASSERT_UINT32_WITHIN(2, 100, input::held_ms(Key::Down, hal::millis()));
  release(Key::Down);
  pump(10);
  TEST_ASSERT_FALSE(input::is_down(Key::Down));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_chatter_gives_one_press_one_release);
  RUN_TEST(test_press_latency_within_debounce_plus_tick);
  RUN_TEST(test_repeat_count_for_2s_hold);
  RUN_TEST(test_no_click_after_hold_on_repeat_key);
  RUN_TEST(test_long_press_once_and_no_click);
  RUN_TEST(test_click_on_release);
  RUN_TEST(test_combo_up_select_suppresses_clicks);
  RUN_TEST(test_is_down_and_held_ms);
  return UNITY_END();
}
