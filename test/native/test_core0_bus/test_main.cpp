// T011 — event bus (core.md "Event bus", acceptance 3).
#include <unity.h>
#include "test_helpers.h"
using namespace core0;

static int g_order[16], g_order_n;
static void h_a(const Event& e, void*) { if (g_order_n < 16) g_order[g_order_n++] = 100 + e.code; }
static void h_b(const Event& e, void*) { if (g_order_n < 16) g_order[g_order_n++] = 200 + e.code; }
static int g_reposted;
static void h_repost(const Event& e, void*) {
  if (e.code == 1) { bus::post(make_event(EventType::Sensor, 2)); ++g_reposted; }
}

void setUp() {
  reset_all();
  g_order_n = 0;
  g_reposted = 0;
}
void tearDown() {}

static void test_fifo_and_subscription_order() {
  bus::subscribe(EventType::Sensor, h_a);
  bus::subscribe(EventType::Sensor, h_b);
  bus::post(make_event(EventType::Sensor, 1));
  bus::post(make_event(EventType::Sensor, 2));
  bus::dispatch();
  TEST_ASSERT_EQUAL_INT(4, g_order_n);
  TEST_ASSERT_EQUAL_INT(101, g_order[0]);
  TEST_ASSERT_EQUAL_INT(201, g_order[1]);
  TEST_ASSERT_EQUAL_INT(102, g_order[2]);
  TEST_ASSERT_EQUAL_INT(202, g_order[3]);
}

static void test_none_receives_everything() {
  rec_attach(EventType::None);
  bus::post(make_event(EventType::Sensor, 1));
  bus::post(make_event(EventType::Ui, 2));
  bus::dispatch();
  TEST_ASSERT_EQUAL_INT(2, g_rec.n);
}

static void test_post_inside_handler_is_not_recursive() {
  bus::subscribe(EventType::Sensor, h_repost);
  rec_attach(EventType::Sensor);
  bus::post(make_event(EventType::Sensor, 1));
  TEST_ASSERT_EQUAL_UINT8(1, bus::dispatch());
  TEST_ASSERT_EQUAL_INT(1, g_rec.n);
  TEST_ASSERT_TRUE(bus::pending());
  bus::dispatch();
  TEST_ASSERT_EQUAL_INT(2, g_rec.n);
  TEST_ASSERT_EQUAL_UINT8(2, g_rec.ev[1].code);
}

static void tmr_post(TimerId, void*) { bus::post(make_event(EventType::Sensor, 7)); }
static int g_delivered;
static void h_count(const Event&, void*) { ++g_delivered; }

static void test_1000_posts_at_1khz_no_drops() {
  g_delivered = 0;
  bus::subscribe(EventType::Sensor, h_count);
  sys::begin();
  timers::start(1, TimerMode::Periodic, tmr_post);
  pump(1000);
  pump(2);
  TEST_ASSERT_EQUAL_UINT32(0, bus::dropped_count());
  TEST_ASSERT_TRUE(g_delivered >= 1000);  // one post per ms, all delivered in order
}

static void test_overflow_counts_drops_and_reports() {
  for (int i = 0; i < CORE0_EVENT_QUEUE_LEN; ++i) TEST_ASSERT_EQUAL(U(Status::Ok), U(bus::post(make_event(EventType::Sensor, 1))));
  TEST_ASSERT_EQUAL(U(Status::NoSpace), U(bus::post(make_event(EventType::Sensor, 1))));
  TEST_ASSERT_EQUAL_UINT32(1, bus::dropped_count());
  rec_attach(EventType::Fault);
  for (int i = 0; i < 20 && bus::pending(); ++i) bus::dispatch(CORE0_EVENT_QUEUE_LEN);
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Fault, U(FaultCode::EventDropped)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fifo_and_subscription_order);
  RUN_TEST(test_none_receives_everything);
  RUN_TEST(test_post_inside_handler_is_not_recursive);
  RUN_TEST(test_1000_posts_at_1khz_no_drops);
  RUN_TEST(test_overflow_counts_drops_and_reports);
  return UNITY_END();
}
