// T009 — wrap-safe time helpers (core.md acceptance 5).
#include <unity.h>
#include "test_helpers.h"
using namespace core0;

void setUp() {}
void tearDown() {}

static void test_reached_across_wrap() {
  const ms_t before = 0xFFFFFFF0u, after = 0x10u;
  TEST_ASSERT_TRUE(time_reached(after, before));
  TEST_ASSERT_FALSE(time_reached(before, after));
  TEST_ASSERT_TRUE(time_reached(after, after));
}

static void test_since_across_wrap() {
  TEST_ASSERT_EQUAL_UINT32(0x20u, time_since(0x10u, 0xFFFFFFF0u));
}

static void test_until_across_wrap() {
  TEST_ASSERT_EQUAL_INT32(0x20, time_until(0xFFFFFFF0u, 0x10u));
  TEST_ASSERT_EQUAL_INT32(-0x20, time_until(0x10u, 0xFFFFFFF0u));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reached_across_wrap);
  RUN_TEST(test_since_across_wrap);
  RUN_TEST(test_until_across_wrap);
  return UNITY_END();
}
