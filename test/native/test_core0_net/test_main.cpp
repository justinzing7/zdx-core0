// test_core0_net — net service: reference-counted radio, timeouts, Sleep veto, CLI view
// (spec/modules/storage-net.md "Connectivity"; 0.2.0 F1).
#include <unity.h>
#include "test_helpers.h"
#include "mock_net.h"
using namespace core0;

static MockNet g_net;

static uint8_t NS(NetState s) { return static_cast<uint8_t>(s); }

// Number of StateChanged events with this state (and this data, unless data_any).
static int changes(NetState s, bool data_any = true, uint32_t data = 0) {
  int c = 0;
  for (int i = 0; i < g_rec.n; ++i) {
    const Event& e = g_rec.ev[i];
    if (e.type == EventType::Net && e.code == U(NetCode::StateChanged) && e.arg == NS(s) &&
        (data_any || e.data == data))
      ++c;
  }
  return c;
}

void setUp() {
  reset_all();
  g_net.reset();
  net::attach(g_net);
  sys::register_module(&net::kModule);
  rec_attach(EventType::Net);
  sys::begin();
  bus::dispatch();
  g_rec.n = 0;
}
void tearDown() {}

static void test_init_begins_driver() {
  TEST_ASSERT_EQUAL_INT(1, g_net.begins);
  TEST_ASSERT_EQUAL_INT(0, g_net.connects);
  TEST_ASSERT_EQUAL_UINT8(NS(NetState::Off), NS(net::state()));
}

static void test_first_acquire_connects_once() {
  net::NetUser a = net::acquire("a", 5000);
  net::NetUser b = net::acquire("b", 5000);
  TEST_ASSERT_TRUE(a >= 0);
  TEST_ASSERT_TRUE(b >= 0);
  TEST_ASSERT_TRUE(a != b);
  TEST_ASSERT_EQUAL_INT(1, g_net.connects);
  TEST_ASSERT_EQUAL_UINT8(2, net::users());
}

static void test_slots_run_out() {
  net::acquire("a", 5000);
  net::acquire("b", 5000);
  TEST_ASSERT_EQUAL_INT8(-1, net::acquire("c", 5000));
  TEST_ASSERT_EQUAL_UINT8(2, net::users());
}

static void test_state_change_posts_once() {
  net::acquire("a", 5000);
  pump(3);
  TEST_ASSERT_EQUAL_INT(1, changes(NetState::Connecting));
  g_net.st = NetState::Up;
  pump(3);
  TEST_ASSERT_EQUAL_INT(1, changes(NetState::Up));
  TEST_ASSERT_EQUAL_UINT8(NS(NetState::Up), NS(net::state()));
}

static void test_newcomer_hears_up_again() {
  net::acquire("a", 5000);
  g_net.st = NetState::Up;
  pump(3);
  g_rec.n = 0;
  net::NetUser b = net::acquire("b", 5000);
  pump(1);
  TEST_ASSERT_EQUAL_INT(1, changes(NetState::Up, false, static_cast<uint32_t>(b)));
  TEST_ASSERT_EQUAL_INT(1, g_net.connects);
}

static void test_last_release_disconnects() {
  net::NetUser a = net::acquire("a", 5000);
  net::NetUser b = net::acquire("b", 5000);
  net::release(a);
  TEST_ASSERT_EQUAL_INT8(-1, a);
  TEST_ASSERT_EQUAL_INT(0, g_net.disconnects);
  net::release(b);
  TEST_ASSERT_EQUAL_INT(1, g_net.disconnects);
  TEST_ASSERT_EQUAL_UINT8(0, net::users());
}

static void test_double_release_is_a_no_op() {
  net::NetUser a = net::acquire("a", 5000);
  net::NetUser copy = a;
  net::release(a);
  net::release(a);     // already -1
  net::release(copy);  // stale id: slot already free
  TEST_ASSERT_EQUAL_INT8(-1, a);
  TEST_ASSERT_EQUAL_INT(1, g_net.disconnects);
}

static void test_timeout_posts_error_to_that_user_and_keeps_the_slot() {
  net::NetUser a = net::acquire("a", 1000);
  pump(990);
  TEST_ASSERT_EQUAL_INT(0, changes(NetState::Error));
  pump(20);
  TEST_ASSERT_EQUAL_INT(1, changes(NetState::Error, false, static_cast<uint32_t>(a)));
  pump(500);
  TEST_ASSERT_EQUAL_INT(1, changes(NetState::Error));  // once only
  TEST_ASSERT_EQUAL_UINT8(1, net::users());             // the user still decides when to release
  TEST_ASSERT_EQUAL_INT(0, g_net.disconnects);
}

static void test_no_timeout_once_up() {
  net::acquire("a", 1000);
  g_net.st = NetState::Up;
  pump(3000);
  TEST_ASSERT_EQUAL_INT(0, changes(NetState::Error));
}

static void test_driver_error_is_broadcast() {
  net::acquire("a", 5000);
  pump(2);
  g_net.st = NetState::Error;
  pump(2);
  TEST_ASSERT_EQUAL_INT(1, changes(NetState::Error, false, net::kAllUsers));
}

static void test_sleep_is_vetoed_while_held_or_shutting_down() {
  TEST_ASSERT_TRUE(net::kModule.can_enter(PowerState::Sleep));
  net::NetUser a = net::acquire("a", 5000);
  TEST_ASSERT_FALSE(net::kModule.can_enter(PowerState::Sleep));
  TEST_ASSERT_FALSE(net::kModule.can_enter(PowerState::Off));
  TEST_ASSERT_TRUE(net::kModule.can_enter(PowerState::Idle));  // only Sleep and below
  g_net.off_on_disconnect = false;                              // radio takes time to stop
  net::release(a);
  pump(2);
  TEST_ASSERT_FALSE(net::kModule.can_enter(PowerState::Sleep));
  g_net.st = NetState::Off;
  pump(2);
  TEST_ASSERT_TRUE(net::kModule.can_enter(PowerState::Sleep));
}

static void test_state_names() {
  TEST_ASSERT_EQUAL_STRING("Off", net::state_name(NetState::Off));
  TEST_ASSERT_EQUAL_STRING("Connecting", net::state_name(NetState::Connecting));
  TEST_ASSERT_EQUAL_STRING("Up", net::state_name(NetState::Up));
  TEST_ASSERT_EQUAL_STRING("Error", net::state_name(NetState::Error));
}

static void test_cli_view() {
  g_net.rssi_ = -61;
  g_net.desc = "MockNet stack free 2312";
  net::acquire("sync", 5000);
  g_net.st = NetState::Up;
  pump(2);
  port_native::console_clear();
  char cmd[] = "net";
  cli::exec(cmd);
  const char* out = port_native::console_capture();
  TEST_ASSERT_NOT_NULL(strstr(out, "net Up"));
  TEST_ASSERT_NOT_NULL(strstr(out, "users 1 (sync)"));
  TEST_ASSERT_NOT_NULL(strstr(out, "rssi -61"));
  TEST_ASSERT_NOT_NULL(strstr(out, "MockNet stack free 2312"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_init_begins_driver);
  RUN_TEST(test_first_acquire_connects_once);
  RUN_TEST(test_slots_run_out);
  RUN_TEST(test_state_change_posts_once);
  RUN_TEST(test_newcomer_hears_up_again);
  RUN_TEST(test_last_release_disconnects);
  RUN_TEST(test_double_release_is_a_no_op);
  RUN_TEST(test_timeout_posts_error_to_that_user_and_keeps_the_slot);
  RUN_TEST(test_no_timeout_once_up);
  RUN_TEST(test_driver_error_is_broadcast);
  RUN_TEST(test_sleep_is_vetoed_while_held_or_shutting_down);
  RUN_TEST(test_state_names);
  RUN_TEST(test_cli_view);
  return UNITY_END();
}
