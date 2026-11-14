// T076 — power policy transitions (power.md acceptance, FR-033..037): timed steps, activity,
// locks, vetoes, suspend/resume order, request(), live policy change, `pwr` CLI.
#include <unity.h>
#include "test_helpers.h"
using namespace core0;

static char g_sus[32], g_res[32];
static int  g_nsus = 0, g_nres = 0;
static bool g_veto_sleep = false;
static void rec_c(char* b, int& n, char c) { if (n < 31) { b[n++] = c; b[n] = 0; } }
static void sus_a(PowerState) { rec_c(g_sus, g_nsus, 'a'); }
static void sus_b(PowerState) { rec_c(g_sus, g_nsus, 'b'); }
static void res_a(PowerState, uint32_t) { rec_c(g_res, g_nres, 'a'); }
static void res_b(PowerState, uint32_t) { rec_c(g_res, g_nres, 'b'); }
static bool can_b(PowerState t) { return !(g_veto_sleep && t >= PowerState::Sleep); }
static const Module kA = {"a", nullptr, nullptr, nullptr, nullptr, sus_a, res_a};
static const Module kB = {"b", nullptr, nullptr, nullptr, can_b, sus_b, res_b};

static const PowerPolicy kPolicy = {20000, 60000, 120000, 0, hal::kWakeButton | hal::kWakeRtc};

void setUp() {
  reset_all();
  g_nsus = g_nres = 0;
  g_sus[0] = g_res[0] = 0;
  g_veto_sleep = false;
  sys::register_module(&power::kModule);
  sys::register_module(&kA);
  sys::register_module(&kB);
  power::set_policy(kPolicy);
  rec_attach(EventType::Power);
  port_native::set_wake_bits(hal::kWakeRtc);  // stay asleep unless a test wakes it
  sys::begin();
}
void tearDown() {}

#define ASSERT_ST(s) TEST_ASSERT_EQUAL_INT(static_cast<int>(PowerState::s), static_cast<int>(power::state()))

static void pump_until(ms_t t) { pump(t - port_native::now()); }

static void test_steps_at_configured_times() {
  pump_until(19998);
  ASSERT_ST(Active);
  pump_until(20002);
  ASSERT_ST(Dim);
  pump_until(59998);
  ASSERT_ST(Dim);
  pump_until(60002);
  ASSERT_ST(Idle);
  pump_until(119998);
  ASSERT_ST(Idle);
  TEST_ASSERT_EQUAL_UINT32(0, port_native::sleep_calls(hal::SleepDepth::Light));
  pump_until(120002);
  ASSERT_ST(Sleep);
  TEST_ASSERT_TRUE(port_native::sleep_calls(hal::SleepDepth::Light) >= 1);
  TEST_ASSERT_EQUAL_INT(3, rec_count(EventType::Power, U(PowerCode::StateChanged)));
}

static void test_next_deadline_includes_policy_step() {
  pump(100);
  const ms_t now = port_native::now();
  TEST_ASSERT_TRUE(time_until(now, sys::next_deadline(now)) <= 20000 - 100);
}

static void test_activity_returns_to_active() {
  pump_until(20002);
  ASSERT_ST(Dim);
  g_rec.n = 0;
  power::activity(port_native::now());
  pump(1);
  ASSERT_ST(Active);
  pump(1);
  const int i = rec_find(EventType::Power, U(PowerCode::StateChanged));
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_EQUAL_UINT16(U(PowerState::Active), g_rec.ev[i].arg);
  TEST_ASSERT_EQUAL_UINT32(U(PowerState::Dim), g_rec.ev[i].data);
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Power, U(PowerCode::Resumed)));
  pump(19990);
  ASSERT_ST(Active);  // the timers restart from the activity
  pump(20);
  ASSERT_ST(Dim);
}

static void test_lock_holds_floor_then_sleeps_within_one_pass() {
  PowerLock l = power::lock(PowerState::Idle, "audio");
  pump_until(130000);
  ASSERT_ST(Idle);  // may go down to the floor, not below
  TEST_ASSERT_EQUAL_UINT32(0, port_native::sleep_calls(hal::SleepDepth::Light));
  power::unlock(l);
  pump(1);
  ASSERT_ST(Sleep);
}

static void test_veto_stays_and_retries() {
  g_veto_sleep = true;
  pump_until(130000);
  ASSERT_ST(Idle);
  g_veto_sleep = false;
  pump(1);
  ASSERT_ST(Sleep);
}

static void test_blocked_step_asks_for_a_retry() {
  g_veto_sleep = true;
  pump_until(130000);
  const ms_t now = port_native::now();
  TEST_ASSERT_TRUE(time_until(now, sys::next_deadline(now)) <= 1000);
}

static void test_suspend_reverse_resume_forward() {
  pump_until(20002);
  TEST_ASSERT_EQUAL_STRING("ba", g_sus);
  power::activity(port_native::now());
  pump(1);
  TEST_ASSERT_EQUAL_STRING("ab", g_res);
}

static void test_button_wake_from_sleep() {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok), static_cast<int>(power::request(PowerState::Sleep)));
  pump(2);
  ASSERT_ST(Sleep);
  port_native::set_wake_bits(hal::kWakeButton);
  pump(2);
  ASSERT_ST(Active);
  pump(1);
  const int i = rec_find(EventType::Power, U(PowerCode::Resumed));
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_EQUAL_UINT32(hal::kWakeButton, g_rec.ev[i].data);
}

static int g_timer_fired = 0;
static void on_timer(TimerId, void*) { ++g_timer_fired; }

static void test_rtc_wake_resumes_runs_timers_and_sleeps_again() {
  g_timer_fired = 0;
  const ms_t t0 = port_native::now();
  timers::start(5000, TimerMode::OneShot, on_timer);
  power::request(PowerState::Sleep);
  const int resumes = g_nres;
  g_rec.n = 0;
  pump(100);  // the native clock moves to the timer deadline during the sleep, as on the hardware
  TEST_ASSERT_EQUAL_INT(1, g_timer_fired);
  ASSERT_ST(Sleep);
  TEST_ASSERT_EQUAL_INT(resumes + 2, g_nres);  // on_resume ran once for each of the two modules
  const int s = rec_find(EventType::Power, U(PowerCode::StateChanged));
  TEST_ASSERT_TRUE(s >= 0);
  TEST_ASSERT_EQUAL_UINT16(U(PowerState::Sleep), g_rec.ev[s].arg);  // asleep first, then woken
  const int i = rec_find(EventType::Power, U(PowerCode::Resumed));
  TEST_ASSERT_TRUE(i > s);
  TEST_ASSERT_UINT32_WITHIN(2, t0 + 5000, g_rec.at[i]);  // woken by the timer, not before
  TEST_ASSERT_EQUAL_UINT16(U(PowerState::Idle), g_rec.ev[i].arg);  // background: Idle, not Active
  TEST_ASSERT_EQUAL_UINT32(hal::kWakeRtc, g_rec.ev[i].data);
}

static void test_rtc_wake_after_sleep_now_returns_to_sleep() {
  power::set_policy(PowerPolicy{0, 0, 0, 0, hal::kWakeButton | hal::kWakeRtc});  // no idle steps
  timers::start(5000, TimerMode::OneShot, on_timer);
  power::request(PowerState::Sleep);
  pump(5100);
  ASSERT_ST(Sleep);  // the policy alone would leave it in Idle
}

// Something woken in the background (an alarm) asks for the user: full resume to Active.
static void on_timer_wants_user(TimerId, void*) { power::request(PowerState::Active); }

static void test_rtc_wake_then_request_active_goes_active() {
  timers::start(5000, TimerMode::OneShot, on_timer_wants_user);
  power::request(PowerState::Sleep);
  pump(5100);
  ASSERT_ST(Active);
}

static void test_rtc_bits_with_no_timer_armed_are_ignored() {
  power::request(PowerState::Sleep);
  pump(2);
  const int resumes = g_nres;
  pump(500);  // the native port reports kWakeRtc on every sleep, but no timer is due
  ASSERT_ST(Sleep);
  TEST_ASSERT_EQUAL_INT(resumes, g_nres);
}

// Background work that spans passes holds a lock from the timer that woke the device.
static PowerLock g_bg_lock = kNoLock;
static void on_timer_takes_lock(TimerId, void*) { g_bg_lock = power::lock(PowerState::Idle, "bg"); }

static void test_lock_holds_idle_after_rtc_wake() {
  g_bg_lock = kNoLock;
  timers::start(5000, TimerMode::OneShot, on_timer_takes_lock);
  power::request(PowerState::Sleep);
  pump(200);
  TEST_ASSERT_TRUE(g_bg_lock != kNoLock);
  ASSERT_ST(Idle);
  power::unlock(g_bg_lock);
  pump(5);
  ASSERT_ST(Sleep);
}

static void test_request_goes_immediately_subject_to_locks() {
  PowerLock l = power::lock(PowerState::Idle, "audio");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Busy), static_cast<int>(power::request(PowerState::Sleep)));
  pump(2);
  ASSERT_ST(Active);
  power::unlock(l);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok), static_cast<int>(power::request(PowerState::Sleep)));
  pump(1);
  ASSERT_ST(Sleep);
}

static void test_request_vetoed() {
  g_veto_sleep = true;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Busy), static_cast<int>(power::request(PowerState::Sleep)));
  pump(2);
  ASSERT_ST(Active);
}

static void test_set_policy_applies_live() {
  pump(5000);
  const PowerPolicy p = {6000, 60000, 120000, 0, hal::kWakeButton};
  power::set_policy(p);
  pump(900);
  ASSERT_ST(Active);
  pump(200);
  ASSERT_ST(Dim);
}

static void test_zero_timeout_means_never() {
  const PowerPolicy p = {0, 0, 0, 0, hal::kWakeButton};
  power::set_policy(p);
  pump(300000);
  ASSERT_ST(Active);
}

static void test_pwr_cli() {
  PowerLock l = power::lock(PowerState::Idle, "audio");
  pump(1500);
  port_native::console_clear();
  char line[] = "pwr";
  cli::exec(line);
  const char* out = port_native::console_capture();
  TEST_ASSERT_NOT_NULL(strstr(out, "state Active"));
  TEST_ASSERT_NOT_NULL(strstr(out, "idle_ms 1500"));
  TEST_ASSERT_NOT_NULL(strstr(out, "audio@Idle"));
  power::unlock(l);
  char req[] = "pwr dim";
  cli::exec(req);
  pump(1);
  ASSERT_ST(Dim);
}

static void test_unregistered_power_never_transitions() {
  reset_all();
  sys::begin();
  pump(150000);
  ASSERT_ST(Active);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_steps_at_configured_times);
  RUN_TEST(test_next_deadline_includes_policy_step);
  RUN_TEST(test_activity_returns_to_active);
  RUN_TEST(test_lock_holds_floor_then_sleeps_within_one_pass);
  RUN_TEST(test_veto_stays_and_retries);
  RUN_TEST(test_blocked_step_asks_for_a_retry);
  RUN_TEST(test_suspend_reverse_resume_forward);
  RUN_TEST(test_button_wake_from_sleep);
  RUN_TEST(test_rtc_wake_resumes_runs_timers_and_sleeps_again);
  RUN_TEST(test_rtc_wake_after_sleep_now_returns_to_sleep);
  RUN_TEST(test_rtc_wake_then_request_active_goes_active);
  RUN_TEST(test_rtc_bits_with_no_timer_armed_are_ignored);
  RUN_TEST(test_lock_holds_idle_after_rtc_wake);
  RUN_TEST(test_request_goes_immediately_subject_to_locks);
  RUN_TEST(test_request_vetoed);
  RUN_TEST(test_set_policy_applies_live);
  RUN_TEST(test_zero_timeout_means_never);
  RUN_TEST(test_pwr_cli);
  RUN_TEST(test_unregistered_power_never_transitions);
  return UNITY_END();
}
