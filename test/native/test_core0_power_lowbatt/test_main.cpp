// T064 — power low-battery Off path (power.md "Low battery"): Critical -> LowBatteryForce ->
// 2 s -> on_suspend(Off) in reverse order (locks ignored) -> settings flushed -> hal::sleep(Deep).
#include <unity.h>
#include "test_helpers.h"
using namespace core0;

// Records the order of on_suspend calls across three dummy modules.
static char g_order[8];
static int  g_norder = 0;
static PowerState g_target = PowerState::Active;
static bool g_veto = false;
static void sus_a(PowerState t) { g_target = t; if (g_norder < 7) g_order[g_norder++] = 'a'; }
static void sus_b(PowerState) { if (g_norder < 7) g_order[g_norder++] = 'b'; }
static void sus_c(PowerState) { if (g_norder < 7) g_order[g_norder++] = 'c'; }
static bool veto(PowerState) { return !g_veto; }
static const Module kA = {"a", nullptr, nullptr, nullptr, nullptr, sus_a, nullptr};
static const Module kB = {"b", nullptr, nullptr, nullptr, veto, sus_b, nullptr};
static const Module kC = {"c", nullptr, nullptr, nullptr, nullptr, sus_c, nullptr};

struct Blob {
  int16_t v[4];
};
static void defaults(Blob& b) { memset(&b, 0, sizeof(b)); }
static Settings<Blob, 1> g_blob(defaults);

static void boot() {
  reset_all();
  g_norder = 0;
  memset(g_order, 0, sizeof(g_order));
  g_target = PowerState::Active;
  g_veto = false;
  sys::register_module(&power::kModule);
  sys::register_module(&kA);
  sys::register_module(&settings::kModule);
  sys::register_module(&kB);
  sys::register_module(&kC);
  g_blob.bind();
  rec_attach(EventType::Power);
  sys::begin();
  pump(1);
}

void setUp() { boot(); }
void tearDown() {}

static void post_battery(BatteryState s) {
  bus::post(make_event(EventType::Battery, U(BatteryCode::StateChanged), static_cast<uint16_t>(s), (3340u << 8)));
}

static void test_critical_posts_force_then_off_after_2s() {
  PowerLock l = power::lock(PowerState::Active, "audio");  // ignored by the force path
  g_blob->v[0] = 42;
  settings::mark_dirty();
  const uint32_t commits0 = port_native::nvm_commit_count();
  post_battery(BatteryState::Critical);
  pump(2);
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Power, U(PowerCode::LowBatteryForce)));
  TEST_ASSERT_EQUAL_UINT32(0, port_native::sleep_calls(hal::SleepDepth::Deep));
  pump(1990);
  TEST_ASSERT_EQUAL_UINT32(0, port_native::sleep_calls(hal::SleepDepth::Deep));
  pump(20);
  TEST_ASSERT_EQUAL_UINT32(1, port_native::sleep_calls(hal::SleepDepth::Deep));
  TEST_ASSERT_EQUAL_STRING("cba", g_order);  // reverse registration order
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PowerState::Off), static_cast<int>(g_target));
  TEST_ASSERT_EQUAL_UINT32(commits0 + 1, port_native::nvm_commit_count());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PowerState::Off), static_cast<int>(power::state()));
  power::unlock(l);
}

static void test_force_ignores_vetoes() {
  g_veto = true;
  post_battery(BatteryState::Critical);
  pump(2200);  // 2 s face + the bounded wait for the veto (ignored after that)
  TEST_ASSERT_EQUAL_UINT32(1, port_native::sleep_calls(hal::SleepDepth::Deep));
}

static void test_low_does_not_force() {
  post_battery(BatteryState::Low);
  pump(3000);
  TEST_ASSERT_EQUAL_INT(0, rec_count(EventType::Power, U(PowerCode::LowBatteryForce)));
  TEST_ASSERT_EQUAL_UINT32(0, port_native::sleep_calls(hal::SleepDepth::Deep));
}

static void test_request_off_waits_for_vetoes_only() {
  PowerLock l = power::lock(PowerState::Active, "ui");
  g_veto = true;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Busy), static_cast<int>(power::request(PowerState::Off)));
  TEST_ASSERT_EQUAL_UINT32(0, port_native::sleep_calls(hal::SleepDepth::Deep));
  g_veto = false;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok), static_cast<int>(power::request(PowerState::Off)));
  pump(2);  // deep sleep on a later pass, once queued driver work has drained
  TEST_ASSERT_EQUAL_UINT32(1, port_native::sleep_calls(hal::SleepDepth::Deep));
  TEST_ASSERT_EQUAL_STRING("cba", g_order);
  power::unlock(l);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_critical_posts_force_then_off_after_2s);
  RUN_TEST(test_force_ignores_vetoes);
  RUN_TEST(test_low_does_not_force);
  RUN_TEST(test_request_off_waits_for_vetoes_only);
  return UNITY_END();
}
