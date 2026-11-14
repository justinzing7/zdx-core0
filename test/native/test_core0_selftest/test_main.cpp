// T086 — self-test service (diagnostics.md acceptance 4, FR-040..043, contracts/serial-console.md §4).
#include <stdio.h>
#include <unity.h>
#include "test_helpers.h"
using namespace core0;

namespace core0 {
namespace test {
void reset_selftest();
}
}  // namespace core0

class BaseScreen : public Screen {
 public:
  const char* name() const override { return "base"; }
  void draw(Gfx&, const Rect&) override {}
};
static BaseScreen s_base;

static int g_aborts = 0;
static int g_starts = 0;
static int g_presses = 0;

static TestResult step_pass(ms_t, const Event&, char*, uint8_t) { return TestResult::Pass; }
static TestResult step_fail(ms_t, const Event&, char* msg, uint8_t n) {
  if (n) snprintf(msg, n, "bad");
  return TestResult::Fail;
}
static TestResult step_detail(ms_t, const Event&, char* msg, uint8_t n) {
  if (n) snprintf(msg, n, "3987mV");
  return TestResult::Pass;
}
// Yes/no question: the service maps the answer. Shows the first-call and abort contract.
static TestResult step_ask(ms_t, const Event&, char* msg, uint8_t n) {
  if (n == 0) {
    ++g_aborts;
    return TestResult::Fail;
  }
  if (msg[0] == 0) {
    ++g_starts;
    snprintf(msg, n, "Looks OK?");
  }
  return TestResult::Running;
}
// Raw keys (like the `keys` test): passes on the second Press.
static TestResult step_raw(ms_t, const Event& e, char* msg, uint8_t n) {
  if (n == 0) return TestResult::Fail;
  if (msg[0] == 0) {
    g_presses = 0;
    snprintf(msg, n, "Press twice");
  }
  if (e.type == EventType::Input && e.code == U(InputCode::Press) && ++g_presses == 2) {
    msg[0] = 0;
    return TestResult::Pass;
  }
  return TestResult::Running;
}

static const SelfTest kPass = {"good", false, step_pass};
static const SelfTest kFail = {"broken", false, step_fail};
static const SelfTest kDetail = {"detail", false, step_detail};
static const SelfTest kAsk = {"ask", true, step_ask};
static const SelfTest kRaw = {"raw", false, step_raw};

static const PowerPolicy kPolicy = {20000, 60000, 120000, 0, 0};
static const Key kCombo[] = CORE0_FACTORY_COMBO;

static void boot() {
  reset_all();
  test::reset_selftest();
  g_aborts = g_starts = g_presses = 0;
  power::set_policy(kPolicy);
  sys::register_module(&power::kModule);
  sys::register_module(&ui::kModule);
  sys::register_module(&cli::kModule);
  sys::begin();
  ui::home(s_base);
  pump(1);
  port_native::console_clear();
}

void setUp() { boot(); }
void tearDown() {}

static const char* out() { return port_native::console_capture(); }
static void assert_out(const char* s) {
  if (!strstr(out(), s)) TEST_FAIL_MESSAGE(s);
}
static int count_out(const char* s) {
  int c = 0;
  for (const char* p = strstr(out(), s); p; p = strstr(p + 1, s)) ++c;
  return c;
}
static void send(InputCode c, Key k) {
  bus::post(make_event(EventType::Input, U(c), U(k)));
  pump(1);
}
static void run() {
  TEST_ASSERT_EQUAL_INT(U(Status::Ok), U(selftest::run_all()));
}

static void test_register_and_count() {
  TEST_ASSERT_EQUAL_UINT8(0, selftest::count());
  selftest::register_test(kPass);
  TEST_ASSERT_EQUAL_UINT8(1, selftest::count());
  for (int i = 1; i < CORE0_MAX_SELFTESTS; ++i) selftest::register_test(kPass);
  TEST_ASSERT_EQUAL_INT(U(Status::NoSpace), U(selftest::register_test(kPass)));
}

static void test_console_contract_and_failure_does_not_stop_later_tests() {
  selftest::register_test(kPass);
  selftest::register_test(kFail);
  selftest::register_test(kDetail);
  run();
  pump(20);
  assert_out("SELFTEST BEGIN 3\n");
  assert_out("ST good PASS\n");
  assert_out("ST broken FAIL bad\n");
  assert_out("ST detail PASS 3987mV\n");
  assert_out("SELFTEST FAIL 2/3\n");
  TEST_ASSERT_EQUAL_INT(1, count_out("SELFTEST FAIL"));
  TEST_ASSERT_EQUAL_INT(0, count_out("SELFTEST PASS"));
}

static void test_all_pass_summary_appears_once() {
  selftest::register_test(kPass);
  selftest::register_test(kDetail);
  run();
  pump(6000);
  assert_out("SELFTEST PASS 2/2\n");
  TEST_ASSERT_EQUAL_INT(1, count_out("SELFTEST PASS"));
}

static void test_interactive_times_out_at_30s_and_is_aborted() {
  selftest::register_test(kAsk);
  run();
  pump(29900);
  TEST_ASSERT_EQUAL_INT(0, count_out("ST ask"));
  TEST_ASSERT_EQUAL_INT(0, g_aborts);
  pump(200);
  assert_out("ST ask FAIL timeout\n");
  assert_out("SELFTEST FAIL 0/1\n");
  TEST_ASSERT_EQUAL_INT(1, g_aborts);
}

static void answer_gives(Key k, const char* line) {
  boot();
  selftest::register_test(kAsk);
  run();
  pump(5);
  send(InputCode::Click, k);
  pump(5);
  assert_out(line);
}

static void test_answer_keys() {
  answer_gives(Key::Up, "ST ask PASS\n");
  answer_gives(Key::Select, "ST ask PASS\n");
  answer_gives(Key::Down, "ST ask FAIL\n");
  answer_gives(Key::Back, "ST ask FAIL\n");
  TEST_ASSERT_EQUAL_INT(1, g_aborts);  // an answered test still gets to clean up
}

static void test_press_alone_does_not_answer() {
  selftest::register_test(kAsk);
  run();
  pump(5);
  send(InputCode::Press, Key::Select);
  send(InputCode::Release, Key::Select);
  pump(5);
  TEST_ASSERT_EQUAL_INT(0, count_out("ST ask"));
}

static void test_raw_key_test_is_not_answered_by_clicks_and_sees_presses() {
  selftest::register_test(kRaw);
  run();
  pump(5);
  send(InputCode::Click, Key::Select);
  send(InputCode::Press, Key::Down);
  TEST_ASSERT_EQUAL_INT(0, count_out("ST raw"));
  send(InputCode::Press, Key::Up);
  pump(5);
  assert_out("ST raw PASS\n");
}

static void test_input_extends_the_timeout() {
  selftest::register_test(kRaw);
  run();
  pump(25000);
  send(InputCode::Release, Key::Up);
  pump(25000);
  TEST_ASSERT_EQUAL_INT(0, count_out("ST raw"));
  pump(5100);
  assert_out("ST raw FAIL timeout\n");
}

static void test_power_lock_held_during_run_then_released() {
  selftest::register_test(kAsk);
  run();
  pump(25000);
  TEST_ASSERT_EQUAL_INT(U(PowerState::Active), U(power::state()));
  send(InputCode::Click, Key::Select);
  pump(20010);
  TEST_ASSERT_EQUAL_INT(U(PowerState::Dim), U(power::state()));
}

static void test_screen_stays_until_back_then_pops_to_screen_below() {
  selftest::register_test(kPass);
  run();
  TEST_ASSERT_TRUE(ui::top() != &s_base);
  pump(20);
  TEST_ASSERT_TRUE(ui::top() != &s_base);
  send(InputCode::Click, Key::Down);
  TEST_ASSERT_TRUE(ui::top() != &s_base);
  send(InputCode::Click, Key::Back);
  pump(1);
  TEST_ASSERT_TRUE(ui::top() == &s_base);
}

static void test_screen_auto_pops_after_5s() {
  selftest::register_test(kPass);
  run();
  pump(4900);
  TEST_ASSERT_TRUE(ui::top() != &s_base);
  pump(200);
  TEST_ASSERT_TRUE(ui::top() == &s_base);
}

static void test_back_during_a_run_never_leaves_the_screen() {
  selftest::register_test(kRaw);
  run();
  pump(5);
  send(InputCode::Click, Key::Back);
  send(InputCode::LongPress, Key::Back);
  TEST_ASSERT_TRUE(ui::top() != &s_base);
}

static void test_screen_taken_away_ends_run_with_one_summary_and_no_lock() {
  selftest::register_test(kAsk);
  selftest::register_test(kPass);
  run();
  pump(5);
  ui::home(s_base);  // what the battery-empty path does
  pump(5);
  assert_out("SELFTEST FAIL 0/2\n");
  TEST_ASSERT_EQUAL_INT(1, count_out("SELFTEST FAIL"));
  TEST_ASSERT_EQUAL_INT(1, g_aborts);
  pump(20010);
  TEST_ASSERT_EQUAL_INT(U(PowerState::Dim), U(power::state()));
}

static void test_second_run_restarts_tests_and_a_run_in_progress_is_refused() {
  selftest::register_test(kAsk);
  run();
  pump(5);
  TEST_ASSERT_EQUAL_INT(U(Status::Busy), U(selftest::run_all()));
  send(InputCode::Click, Key::Select);
  pump(5);
  send(InputCode::Click, Key::Back);
  pump(2);
  run();
  pump(5);
  TEST_ASSERT_EQUAL_INT(2, g_starts);
}

static void test_cli_test_runs_all_or_one() {
  selftest::register_test(kPass);
  selftest::register_test(kDetail);
  char all[] = "test";
  cli::exec(all);
  pump(20);
  assert_out("SELFTEST PASS 2/2\n");
  send(InputCode::Click, Key::Back);
  pump(2);
  port_native::console_clear();
  char one[] = "test detail";
  cli::exec(one);
  pump(20);
  assert_out("ST detail PASS 3987mV\n");
  assert_out("SELFTEST PASS 1/1\n");
  TEST_ASSERT_EQUAL_INT(0, count_out("ST good"));
  send(InputCode::Click, Key::Back);
  pump(2);
  port_native::console_clear();
  char none[] = "test nosuch";
  cli::exec(none);
  pump(5);
  TEST_ASSERT_EQUAL_INT(0, count_out("SELFTEST"));
  assert_out("nosuch");
}

static void test_requested_at_boot_needs_every_combo_key() {
  KeyConfig keys[count_of(kCombo)];
  for (size_t i = 0; i < count_of(kCombo); ++i) {
    keys[i].key = kCombo[i];
    keys[i].pin = 0xFF;
    keys[i].flags = kKeyActiveLow;
  }
  input::configure(keys, count_of(kCombo), kDefaultInputTiming);
  sys::register_module(&input::kModule);
  input::kModule.init();
  TEST_ASSERT_FALSE(selftest::requested_at_boot());
  input::feed_key(kCombo[0], true, port_native::now());
  input::kModule.tick(port_native::now());
  port_native::advance_ms(kDefaultInputTiming.debounce_ms + 1);
  input::kModule.tick(port_native::now());
  TEST_ASSERT_FALSE(selftest::requested_at_boot());
  input::feed_key(kCombo[1], true, port_native::now());
  input::kModule.tick(port_native::now());
  port_native::advance_ms(kDefaultInputTiming.debounce_ms + 1);
  input::kModule.tick(port_native::now());
  TEST_ASSERT_TRUE(selftest::requested_at_boot());
}

// Bench 2026-11-06: a test that passes on Press finishes with the key still down, and its release
// must not act on what comes next (it dismissed the summary at once).
static TestResult step_press_back(ms_t, const Event& e, char* msg, uint8_t n) {
  if (n == 0) return TestResult::Fail;
  if (msg[0] == 0) snprintf(msg, n, "Press BACK");
  if (e.type == EventType::Input && e.code == U(InputCode::Press) && static_cast<Key>(e.arg) == Key::Back) {
    msg[0] = 0;
    return TestResult::Pass;
  }
  return TestResult::Running;
}
static const SelfTest kPressBack = {"pressback", false, step_press_back};

static void boot_with_real_keys() {
  reset_all();
  test::reset_selftest();
  static const KeyConfig keys[] = {{Key::Back, 0xFF, kKeyActiveLow},
                                   {Key::Select, 0xFF, kKeyActiveLow}};
  input::configure(keys, 2, kDefaultInputTiming);
  sys::register_module(&input::kModule);
  sys::register_module(&ui::kModule);
  sys::begin();
  ui::home(s_base);
  pump(1);
  port_native::console_clear();
}

static void test_release_of_a_key_that_ended_a_test_does_not_dismiss_the_summary() {
  boot_with_real_keys();
  selftest::register_test(kPressBack);
  run();
  pump(5);
  input::feed_key(Key::Back, true, port_native::now());
  pump(50);  // debounced: the test passes, the summary is up, the key is still down
  assert_out("SELFTEST PASS 1/1\n");
  input::feed_key(Key::Back, false, port_native::now());
  pump(100);
  TEST_ASSERT_TRUE(ui::top() != &s_base);  // still on the summary
  pump(5000);
  TEST_ASSERT_TRUE(ui::top() == &s_base);  // until the 5 s are up
}

static void test_a_fresh_back_click_still_dismisses_the_summary() {
  boot_with_real_keys();
  selftest::register_test(kPressBack);
  run();
  pump(5);
  input::feed_key(Key::Back, true, port_native::now());
  pump(50);
  input::feed_key(Key::Back, false, port_native::now());
  pump(100);
  input::feed_key(Key::Back, true, port_native::now());
  pump(50);
  input::feed_key(Key::Back, false, port_native::now());
  pump(50);
  TEST_ASSERT_TRUE(ui::top() == &s_base);
}

// ---- Foundation tests that register themselves in their module's init -----------------------------
struct Blob {
  int16_t v[4];
};
static void blob_defaults(Blob& b) { memset(&b, 0, sizeof(b)); }
static Settings<Blob, 1> g_blob(blob_defaults);

static uint16_t g_batt_mv = 2000;  // at the pin: Vbat / 2
static uint16_t read_batt() { return g_batt_mv; }

static void boot_with_services() {
  reset_all();
  test::reset_selftest();
  g_blob.bind();
  BatteryConfig c = {read_batt, 2, 1, 0xFF, 0, 8, 3, 10000, 0, kCurveLiPo1S,
                     static_cast<uint8_t>(count_of(kCurveLiPo1S)), 3500, 3350, 50, 5, nullptr, nullptr};
  battery::configure(c);
  sys::register_module(&settings::kModule);
  sys::register_module(&battery::kModule);
  sys::register_module(&ui::kModule);
  sys::begin();
  ui::home(s_base);
  pump(1);
  port_native::console_clear();
}

static void test_nvm_test_commits_through_the_settings_path_and_passes() {
  g_batt_mv = 2000;
  boot_with_services();
  TEST_ASSERT_EQUAL_UINT8(2, selftest::count());
  const uint32_t commits = port_native::nvm_commit_count();
  uint8_t before[4];
  hal::nvm_read(hal::nvm_size() - 4, before, 4);
  TEST_ASSERT_EQUAL_INT(U(Status::Ok), U(selftest::run_all()));
  pump(50);
  assert_out("ST nvm PASS\n");
  TEST_ASSERT_EQUAL_UINT32(commits + 1, port_native::nvm_commit_count());
  uint8_t after[4];
  hal::nvm_read(hal::nvm_size() - 4, after, 4);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(before, after, 4);
  TEST_ASSERT_FALSE(settings::dirty());
}

static void test_battery_test_reports_a_plausible_reading() {
  g_batt_mv = 1995;
  boot_with_services();
  selftest::run_all();
  pump(50);
  assert_out("ST battery PASS 3990mV\n");
}

static void test_battery_test_fails_out_of_range_with_the_value() {
  g_batt_mv = 1000;
  boot_with_services();
  selftest::run_all();
  pump(50);
  assert_out("ST battery FAIL 2000mV\n");
  assert_out("SELFTEST FAIL 1/2\n");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_register_and_count);
  RUN_TEST(test_console_contract_and_failure_does_not_stop_later_tests);
  RUN_TEST(test_all_pass_summary_appears_once);
  RUN_TEST(test_interactive_times_out_at_30s_and_is_aborted);
  RUN_TEST(test_answer_keys);
  RUN_TEST(test_press_alone_does_not_answer);
  RUN_TEST(test_raw_key_test_is_not_answered_by_clicks_and_sees_presses);
  RUN_TEST(test_input_extends_the_timeout);
  RUN_TEST(test_power_lock_held_during_run_then_released);
  RUN_TEST(test_screen_stays_until_back_then_pops_to_screen_below);
  RUN_TEST(test_screen_auto_pops_after_5s);
  RUN_TEST(test_back_during_a_run_never_leaves_the_screen);
  RUN_TEST(test_screen_taken_away_ends_run_with_one_summary_and_no_lock);
  RUN_TEST(test_second_run_restarts_tests_and_a_run_in_progress_is_refused);
  RUN_TEST(test_cli_test_runs_all_or_one);
  RUN_TEST(test_requested_at_boot_needs_every_combo_key);
  RUN_TEST(test_release_of_a_key_that_ended_a_test_does_not_dismiss_the_summary);
  RUN_TEST(test_a_fresh_back_click_still_dismisses_the_summary);
  RUN_TEST(test_nvm_test_commits_through_the_settings_path_and_passes);
  RUN_TEST(test_battery_test_reports_a_plausible_reading);
  RUN_TEST(test_battery_test_fails_out_of_range_with_the_value);
  return UNITY_END();
}
