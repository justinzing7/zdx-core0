// T013 — logging format, level filter, recent-lines ring (diagnostics.md "Log").
#include <unity.h>
#include "test_helpers.h"
using namespace core0;

void setUp() { reset_all(); }
void tearDown() {}

static void test_format() {
  port_native::set_ms(12345);
  C0_LOGI("bat", "%u mV", 3912u);
  TEST_ASSERT_EQUAL_STRING("[   12.345] I bat: 3912 mV\n", port_native::console_capture());
}

static void test_runtime_level_filter() {
  logger::set_level(CORE0_LOG_WARN);
  C0_LOGI("x", "hidden");
  C0_LOGW("x", "shown");
  TEST_ASSERT_NULL(strstr(port_native::console_capture(), "hidden"));
  TEST_ASSERT_NOT_NULL(strstr(port_native::console_capture(), "W x: shown"));
  logger::set_level(CORE0_LOG_TRACE + 3);
  TEST_ASSERT_TRUE(logger::level() <= CORE0_LOG_LEVEL);
}

static void test_ring_keeps_recent_lines() {
  for (int i = 0; i < 40; ++i) C0_LOGI("t", "line %02d", i);
  port_native::console_clear();
  logger::dump_recent();
  const char* out = port_native::console_capture();
  TEST_ASSERT_NOT_NULL(strstr(out, "line 39"));
  TEST_ASSERT_NULL(strstr(out, "line 00"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_format);
  RUN_TEST(test_runtime_level_filter);
  RUN_TEST(test_ring_keeps_recent_lines);
  return UNITY_END();
}
