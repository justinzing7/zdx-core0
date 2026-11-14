// T010 — module registry, init failure isolation, tick overrun (core.md acceptance 1–2).
#include <unity.h>
#include "test_helpers.h"
using namespace core0;

static int g_ticks_ok, g_ticks_bad, g_inits;
static Status init_ok() { ++g_inits; return Status::Ok; }
static Status init_bad() { ++g_inits; return Status::IoError; }
static void tick_ok(ms_t) { ++g_ticks_ok; }
static void tick_bad(ms_t) { ++g_ticks_bad; }
static void tick_slow(ms_t) { port_native::advance_ms(6); }

static const Module kOk = {"ok", init_ok, tick_ok, nullptr, nullptr, nullptr, nullptr};
static const Module kBad = {"bad", init_bad, tick_bad, nullptr, nullptr, nullptr, nullptr};
static const Module kSlow = {"slow", nullptr, tick_slow, nullptr, nullptr, nullptr, nullptr};

void setUp() {
  reset_all();
  g_ticks_ok = g_ticks_bad = g_inits = 0;
}
void tearDown() {}

static void test_registry_overflow_returns_nospace_and_rest_run() {
  for (int i = 0; i < CORE0_MAX_MODULES; ++i) TEST_ASSERT_EQUAL(U(Status::Ok), U(sys::register_module(&kOk)));
  TEST_ASSERT_EQUAL(U(Status::NoSpace), U(sys::register_module(&kOk)));
  sys::begin();
  TEST_ASSERT_EQUAL_INT(CORE0_MAX_MODULES, g_inits);
  TEST_ASSERT_EQUAL_UINT8(CORE0_MAX_MODULES, sys::module_count());
}

static void test_failed_init_is_skipped_and_reported() {
  sys::register_module(&kOk);
  sys::register_module(&kBad);
  sys::register_module(&kOk);
  rec_attach();
  sys::begin();
  TEST_ASSERT_TRUE(sys::module_healthy(0));
  TEST_ASSERT_FALSE(sys::module_healthy(1));
  TEST_ASSERT_TRUE(sys::module_healthy(2));
  pump(3);
  TEST_ASSERT_EQUAL_INT(6, g_ticks_ok);
  TEST_ASSERT_EQUAL_INT(0, g_ticks_bad);
  const int i = rec_find(EventType::Fault, U(FaultCode::ModuleInitFailed));
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_EQUAL_UINT16(1, g_rec.ev[i].arg);
  TEST_ASSERT_TRUE(rec_find(EventType::System, U(SystemCode::InitDone)) >= 0);
}

static void test_tick_overrun_posts_fault() {
  sys::register_module(&kOk);
  sys::register_module(&kSlow);
  rec_attach();
  sys::begin();
  pump(1);
  pump(1);  // dispatch
  const int i = rec_find(EventType::Fault, U(FaultCode::ModuleOverrun));
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_EQUAL_UINT16(1, g_rec.ev[i].arg);
  TEST_ASSERT_EQUAL_UINT32(6, g_rec.ev[i].data);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_registry_overflow_returns_nospace_and_rest_run);
  RUN_TEST(test_failed_init_is_skipped_and_reported);
  RUN_TEST(test_tick_overrun_posts_fault);
  return UNITY_END();
}
