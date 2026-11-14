// T077 — the wake press never acts (input.md "Interaction with power", FR-011, SC-006).
#include <unity.h>
#include "test_helpers.h"
using namespace core0;

static const KeyConfig kKeys[] = {
    {Key::Select, 0xFF, kKeyActiveLow | kKeyWake},
    {Key::Back, 0xFF, kKeyActiveLow | kKeyLongPress | kKeyWake},
};
static const PowerPolicy kPolicy = {20000, 60000, 120000, 0, hal::kWakeButton};

void setUp() {
  reset_all();
  input::configure(kKeys, 2, kDefaultInputTiming);
  sys::register_module(&power::kModule);
  sys::register_module(&input::kModule);
  power::set_policy(kPolicy);
  rec_attach(EventType::Input);
  sys::begin();
}
void tearDown() {}

#define ASSERT_ST(s) TEST_ASSERT_EQUAL_INT(static_cast<int>(PowerState::s), static_cast<int>(power::state()))

static void press(Key k) { input::feed_key(k, true, port_native::now()); }
static void release(Key k) { input::feed_key(k, false, port_native::now()); }
static int input_events() { return g_rec.n; }

// The key goes down, hal::sleep(Light) returns kWakeButton in the same pass (native port).
static void sleep_and_wake_with(Key k) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok), static_cast<int>(power::request(PowerState::Sleep)));
  pump(1);
  ASSERT_ST(Sleep);
  press(k);
  pump(1);
  ASSERT_ST(Active);
}

static void test_sleep_wake_press_is_swallowed() {
  sleep_and_wake_with(Key::Select);
  pump(50);
  release(Key::Select);
  pump(50);
  TEST_ASSERT_EQUAL_INT(0, input_events());
}

static void test_next_press_after_wake_works() {
  sleep_and_wake_with(Key::Select);
  pump(50);
  release(Key::Select);
  pump(50);
  press(Key::Select);
  pump(50);
  release(Key::Select);
  pump(50);
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Input, U(InputCode::Press)));
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Input, U(InputCode::Click)));
}

static void test_long_hold_wake_fires_no_long_press() {
  sleep_and_wake_with(Key::Back);
  pump(2000);
  release(Key::Back);
  pump(50);
  TEST_ASSERT_EQUAL_INT(0, input_events());
}

static void test_press_in_dim_is_swallowed_and_wakes() {
  pump(20010);
  ASSERT_ST(Dim);
  press(Key::Select);
  pump(50);
  ASSERT_ST(Active);
  release(Key::Select);
  pump(50);
  TEST_ASSERT_EQUAL_INT(0, input_events());
}

static void test_press_in_display_off_is_swallowed_and_wakes() {
  pump(60010);
  ASSERT_ST(Idle);
  press(Key::Back);
  pump(50);
  ASSERT_ST(Active);
  release(Key::Back);
  pump(50);
  TEST_ASSERT_EQUAL_INT(0, input_events());
}

static void test_key_held_through_sleep_transition_is_wake_only() {
  press(Key::Select);
  pump(50);
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Input, U(InputCode::Press)));
  g_rec.n = 0;
  power::request(PowerState::Sleep);
  pump(2);  // goes down; the held key wakes it straight back up
  ASSERT_ST(Active);
  release(Key::Select);
  pump(50);
  TEST_ASSERT_EQUAL_INT(0, input_events());
}

static void test_50_wakes_0_actions() {
  for (int i = 0; i < 50; ++i) {
    sleep_and_wake_with(i & 1 ? Key::Select : Key::Back);
    pump(30);
    release(i & 1 ? Key::Select : Key::Back);
    pump(30);
  }
  TEST_ASSERT_EQUAL_INT(0, input_events());
}

// The wake fires on the contact's first low, but the next loop pass can read it bouncing high.
static void test_bounce_high_on_first_read_after_wake_is_still_swallowed() {
  power::request(PowerState::Sleep);
  pump(1);
  ASSERT_ST(Sleep);
  pump(1);  // woke; the key reads released on this pass
  ASSERT_ST(Active);
  pump(1);
  press(Key::Select);  // the contact settles low, inside the debounce time
  pump(50);
  release(Key::Select);
  pump(50);
  TEST_ASSERT_EQUAL_INT(0, input_events());
}

static void test_swallow_ends_without_a_press_and_next_press_works() {
  power::request(PowerState::Sleep);
  pump(1);
  pump(1);  // woke with no key seen at all (a tap shorter than the wake)
  ASSERT_ST(Active);
  pump(kDefaultInputTiming.debounce_ms + 2);
  press(Key::Select);
  pump(50);
  release(Key::Select);
  pump(50);
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Input, U(InputCode::Click)));
}

static void test_press_while_active_is_not_swallowed() {
  press(Key::Select);
  pump(50);
  release(Key::Select);
  pump(50);
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Input, U(InputCode::Click)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_sleep_wake_press_is_swallowed);
  RUN_TEST(test_next_press_after_wake_works);
  RUN_TEST(test_long_hold_wake_fires_no_long_press);
  RUN_TEST(test_press_in_dim_is_swallowed_and_wakes);
  RUN_TEST(test_press_in_display_off_is_swallowed_and_wakes);
  RUN_TEST(test_key_held_through_sleep_transition_is_wake_only);
  RUN_TEST(test_50_wakes_0_actions);
  RUN_TEST(test_bounce_high_on_first_read_after_wake_is_still_swallowed);
  RUN_TEST(test_swallow_ends_without_a_press_and_next_press_works);
  RUN_TEST(test_press_while_active_is_not_swallowed);
  return UNITY_END();
}
