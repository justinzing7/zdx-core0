// T033 — crash counter, safe mode, stable-run clear, panic record (diagnostics.md acceptance 1–2).
#include <unity.h>
#include "test_helpers.h"
using namespace core0;

namespace core0 {
namespace test {
void reboot_keep_retained();  // fault.cpp (CORE0_NATIVE): simulate a reset that keeps RTC RAM
}
}  // namespace core0

static void boot(hal::ResetReason r) {
  test::reboot_keep_retained();
  port_native::set_reset_reason(r);
  fault::init();
}

void setUp() { reset_all(); }
void tearDown() {}

static void test_three_watchdog_resets_enter_safe_mode() {
  boot(hal::ResetReason::PowerOn);
  TEST_ASSERT_FALSE(fault::safe_mode());
  boot(hal::ResetReason::Watchdog);
  boot(hal::ResetReason::Watchdog);
  TEST_ASSERT_FALSE(fault::safe_mode());
  boot(hal::ResetReason::Watchdog);
  TEST_ASSERT_TRUE(fault::safe_mode());
  TEST_ASSERT_EQUAL_UINT8(3, fault::last().crash_count);
}

static void test_power_on_does_not_count() {
  boot(hal::ResetReason::Watchdog);
  boot(hal::ResetReason::PowerOn);
  boot(hal::ResetReason::Software);
  TEST_ASSERT_EQUAL_UINT8(1, fault::last().crash_count);
  TEST_ASSERT_FALSE(fault::safe_mode());
}

static void test_stable_run_clears_counter() {
  boot(hal::ResetReason::Watchdog);
  boot(hal::ResetReason::Watchdog);
  fault::service(CORE0_STABLE_RUN_MS - 1);
  fault::service(CORE0_STABLE_RUN_MS);
  boot(hal::ResetReason::Watchdog);
  TEST_ASSERT_EQUAL_UINT8(1, fault::last().crash_count);
}

static void test_panic_records_file_and_line_then_counts() {
  boot(hal::ResetReason::PowerOn);
  port_native::set_ms(1234);
  port_native::arm_reset_point(true);
  if (setjmp(port_native::reset_point()) == 0) {
    fault::panic("src/app/x.cpp", 42);
    TEST_FAIL_MESSAGE("panic returned");
  }
  port_native::arm_reset_point(false);
  TEST_ASSERT_TRUE(port_native::reset_requested());
  boot(hal::ResetReason::Software);  // esp_restart() reports Software; the pending flag says Panic
  const FaultRecord& r = fault::last();
  TEST_ASSERT_EQUAL_STRING("x.cpp", r.what);
  TEST_ASSERT_EQUAL_UINT32(42, r.code);
  TEST_ASSERT_EQUAL_UINT32(1234, r.uptime_ms);
  TEST_ASSERT_EQUAL(U(hal::ResetReason::Panic), U(r.reason));
  TEST_ASSERT_EQUAL_UINT8(1, r.crash_count);
}

static void test_module_index_survives_reset() {
  boot(hal::ResetReason::PowerOn);
  fault::note_module(3);
  boot(hal::ResetReason::Watchdog);  // watchdog fired while module 3 was ticking
  TEST_ASSERT_EQUAL_UINT8(3, fault::last().module_index);
}

static void test_module_name_survives_reset() {
  boot(hal::ResetReason::PowerOn);
  fault::note_module(3, "settings");
  boot(hal::ResetReason::Watchdog);
  TEST_ASSERT_EQUAL_STRING("settings", fault::last().module_name);
}

static void test_long_module_name_is_truncated_and_terminated() {
  boot(hal::ResetReason::PowerOn);
  fault::note_module(1, "a-very-long-module-name");
  boot(hal::ResetReason::Watchdog);
  TEST_ASSERT_EQUAL_STRING("a-very-long", fault::last().module_name);
}

static void test_no_module_means_empty_name() {
  boot(hal::ResetReason::PowerOn);
  fault::note_module(3, "settings");
  fault::note_module(0xFF);
  boot(hal::ResetReason::Watchdog);
  TEST_ASSERT_EQUAL_STRING("", fault::last().module_name);
}

static void test_name_does_not_carry_into_the_next_boot() {
  boot(hal::ResetReason::PowerOn);
  fault::note_module(3, "settings");
  boot(hal::ResetReason::Watchdog);
  boot(hal::ResetReason::PowerOn);
  TEST_ASSERT_EQUAL_STRING("", fault::last().module_name);
}

// The reset happens "inside" the tick, as a watchdog would: the record must name that module.
static void reboot_in_tick(ms_t) { boot(hal::ResetReason::Watchdog); }
static const Module kHung = {"hung", nullptr, reboot_in_tick, nullptr, nullptr, nullptr, nullptr};

static void test_run_once_names_the_ticking_module() {
  boot(hal::ResetReason::PowerOn);
  sys::register_module(&kHung);
  sys::begin();
  sys::run_once();
  TEST_ASSERT_EQUAL_STRING("hung", fault::last().module_name);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_three_watchdog_resets_enter_safe_mode);
  RUN_TEST(test_power_on_does_not_count);
  RUN_TEST(test_stable_run_clears_counter);
  RUN_TEST(test_panic_records_file_and_line_then_counts);
  RUN_TEST(test_module_index_survives_reset);
  RUN_TEST(test_module_name_survives_reset);
  RUN_TEST(test_long_module_name_is_truncated_and_terminated);
  RUN_TEST(test_no_module_means_empty_name);
  RUN_TEST(test_name_does_not_carry_into_the_next_boot);
  RUN_TEST(test_run_once_names_the_ticking_module);
  return UNITY_END();
}
