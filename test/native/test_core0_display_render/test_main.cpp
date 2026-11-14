// T025 — render service (display.md acceptance 1–2, busy handling, FrameDone).
#include <unity.h>
#include "test_helpers.h"
#include "mock_panel.h"
using namespace core0;

static MockPanel g_panel;
static int g_draws;
static Rect g_last_dirty;
static void draw_fn(Gfx&, const Rect& dirty) { ++g_draws; g_last_dirty = dirty; }

void setUp() {
  reset_all();
  g_panel.reset();
  g_draws = 0;
  display::attach(g_panel);
  display::set_draw_fn(draw_fn);
  sys::register_module(&display::kModule);
  rec_attach(EventType::Display);
  sys::begin();
  pump(50);  // initial full frame
  g_panel.flush_calls = 0;
  g_draws = 0;
  g_rec.n = 0;
}
void tearDown() {}

static void test_no_invalidation_no_flush() {
  pump(1000);
  TEST_ASSERT_EQUAL_INT(0, g_panel.flush_calls);
}

static void test_ten_invalidations_one_flush() {
  for (int i = 0; i < 10; ++i) {
    Rect r = {static_cast<int16_t>(i * 4), 0, 4, 4};
    display::invalidate(r);
  }
  pump(50);
  TEST_ASSERT_EQUAL_INT(1, g_panel.flush_calls);
  TEST_ASSERT_EQUAL_INT(1, g_draws);
  TEST_ASSERT_EQUAL_INT16(0, g_last_dirty.x);
  TEST_ASSERT_EQUAL_INT16(40, g_last_dirty.w);
}

static void test_busy_defers_and_accumulates() {
  g_panel.is_busy = true;
  Rect a = {0, 0, 8, 8};
  display::invalidate(a);
  pump(100);
  TEST_ASSERT_EQUAL_INT(0, g_panel.flush_calls);
  TEST_ASSERT_EQUAL_INT(0, g_draws);
  Rect b = {100, 50, 8, 8};
  display::invalidate(b);
  g_panel.is_busy = false;
  pump(20);
  TEST_ASSERT_EQUAL_INT(1, g_panel.flush_calls);
  TEST_ASSERT_EQUAL_INT16(0, g_last_dirty.x);
  TEST_ASSERT_EQUAL_INT16(0, g_last_dirty.y);
  TEST_ASSERT_EQUAL_INT16(108, g_last_dirty.w);
  TEST_ASSERT_EQUAL_INT16(58, g_last_dirty.h);
}

static void test_frame_done_posted() {
  display::invalidate_all();
  pump(20);
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Display, U(DisplayCode::FrameDone)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_invalidation_no_flush);
  RUN_TEST(test_ten_invalidations_one_flush);
  RUN_TEST(test_busy_defers_and_accumulates);
  RUN_TEST(test_frame_done_posted);
  return UNITY_END();
}
