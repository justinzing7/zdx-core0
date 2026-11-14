// T026 — screen stack, lifecycle, Back handling, forwarding (ui.md "Screen stack", acceptance 3).
#include <unity.h>
#include "test_helpers.h"
#include "mock_panel.h"
using namespace core0;

static char g_log[256];
static void logf(const char* s) { strncat(g_log, s, sizeof(g_log) - strlen(g_log) - 1); }

class TScreen : public Screen {
 public:
  explicit TScreen(const char* n) : n_(n), battery_events(0) {}
  const char* name() const override { return n_; }
  void on_enter(const void*) override { logf("+"); logf(n_); }
  void on_exit() override { logf("-"); logf(n_); }
  void on_cover() override { logf("c"); logf(n_); }
  void on_uncover() override { logf("u"); logf(n_); }
  bool on_event(const Event& e) override {
    if (e.type == EventType::Battery) { ++battery_events; return true; }
    return false;
  }
  void draw(Gfx&, const Rect&) override {}
  const char* n_;
  int battery_events;
};

static TScreen A("A"), B("B"), C("C"), D("D"), E("E"), F("F"), G("G");

void setUp() {
  reset_all();
  g_log[0] = 0;
  A.battery_events = B.battery_events = 0;
  sys::register_module(&ui::kModule);
  sys::begin();
}
void tearDown() {}

static void send(InputCode c, Key k) {
  bus::post(make_event(EventType::Input, U(c), U(k)));
  bus::dispatch();
}

static void test_depth_limit() {
  TScreen* s[] = {&A, &B, &C, &D, &E, &F};
  for (int i = 0; i < CORE0_SCREEN_STACK_DEPTH; ++i) TEST_ASSERT_EQUAL(U(Status::Ok), U(ui::push(*s[i % 6])));
  TEST_ASSERT_EQUAL(U(Status::NoSpace), U(ui::push(G)));
  TEST_ASSERT_EQUAL_UINT8(CORE0_SCREEN_STACK_DEPTH, ui::depth());
  TEST_ASSERT_EQUAL_PTR(s[(CORE0_SCREEN_STACK_DEPTH - 1) % 6], ui::top());
}

static void test_lifecycle_order() {
  ui::home(A);
  ui::push(B);
  ui::pop();
  TEST_ASSERT_EQUAL_STRING("+AcA+B-BuA", g_log);
}

static void test_unhandled_back_click_pops() {
  ui::home(A);
  ui::push(B);
  send(InputCode::Click, Key::Back);
  TEST_ASSERT_EQUAL_PTR(&A, ui::top());
  send(InputCode::Click, Key::Back);  // root never pops
  TEST_ASSERT_EQUAL_PTR(&A, ui::top());
  TEST_ASSERT_EQUAL_UINT8(1, ui::depth());
}

static void test_back_long_press_unwinds_to_root() {
  ui::home(A);
  ui::push(B);
  ui::push(C);
  ui::push(D);
  send(InputCode::LongPress, Key::Back);
  TEST_ASSERT_EQUAL_UINT8(1, ui::depth());
  TEST_ASSERT_EQUAL_PTR(&A, ui::top());
}

static void test_forward_battery_to_top() {
  ui::home(A);
  bus::post(make_event(EventType::Battery, 0));
  bus::dispatch();
  TEST_ASSERT_EQUAL_INT(0, A.battery_events);
  ui::forward(EventType::Battery);
  bus::post(make_event(EventType::Battery, 0));
  bus::dispatch();
  TEST_ASSERT_EQUAL_INT(1, A.battery_events);
  ui::push(B);  // B did not ask for Battery
  bus::post(make_event(EventType::Battery, 0));
  bus::dispatch();
  TEST_ASSERT_EQUAL_INT(0, B.battery_events);
  ui::pop();  // A's forward survives cover/uncover
  bus::post(make_event(EventType::Battery, 0));
  bus::dispatch();
  TEST_ASSERT_EQUAL_INT(2, A.battery_events);
}

static void test_home_clears_stack() {
  ui::home(A);
  ui::push(B);
  ui::push(C);
  g_log[0] = 0;
  ui::home(D);
  TEST_ASSERT_EQUAL_UINT8(1, ui::depth());
  TEST_ASSERT_EQUAL_PTR(&D, ui::top());
  TEST_ASSERT_EQUAL_STRING("-C-B-A+D", g_log);
}

// Regression (bench 2026-10-31): the pop (SlideRight) wipe was inverted — it started on the new
// screen and ended on the old one, then snapped back = visible flash. A transition must move
// monotonically from the old screen to the new one.
class FillScreen : public Screen {
 public:
  const char* name() const override { return "fill"; }
  void draw(Gfx& g, const Rect&) override {
    Rect all = {0, 0, g.width(), g.height()};
    g.rect(all, Color::White, true);
  }
};
class BlankScreen : public Screen {
 public:
  const char* name() const override { return "blank"; }
  void draw(Gfx&, const Rect&) override {}
};
static FillScreen s_fill;
static BlankScreen s_blank;
static MockPanel g_panel;

// The display module is attached after sys::begin() in this suite, so drive its tick here.
static void pump_with_display(ms_t ms) {
  for (ms_t i = 0; i < ms; ++i) {
    port_native::advance_ms(1);
    sys::run_once();
    display::kModule.tick(hal::millis());
  }
}
static int lit_after(ms_t ms) {
  pump_with_display(ms);
  return g_panel.g.count_set();
}

static void run_transition_monotonic(bool pop_case) {
  g_panel.reset();
  display::attach(g_panel);
  display::kModule.init();
  // pop: fill (new) is uncovered as blank (old) leaves; push: fill (new) is pushed over blank.
  if (pop_case) {
    ui::home(s_fill);
    ui::push(s_blank, nullptr, Transition::None);
    pump_with_display(40);
    ui::pop(Transition::SlideRight);
  } else {
    ui::home(s_blank);
    pump_with_display(40);
    ui::push(s_fill, nullptr, Transition::SlideLeft);
  }
  const int total = 128 * 64;
  int prev = -1;
  for (int step = 0; step < 6; ++step) {
    const int lit = lit_after(30);
    TEST_ASSERT_TRUE_MESSAGE(lit >= prev, "transition went backwards (flash)");
    prev = lit;
  }
  TEST_ASSERT_EQUAL_INT(total, prev);  // ends fully on the new screen
}

static void test_pop_transition_reveals_new_screen_monotonically() { run_transition_monotonic(true); }
static void test_push_transition_reveals_new_screen_monotonically() { run_transition_monotonic(false); }

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_depth_limit);
  RUN_TEST(test_lifecycle_order);
  RUN_TEST(test_unhandled_back_click_pops);
  RUN_TEST(test_back_long_press_unwinds_to_root);
  RUN_TEST(test_forward_battery_to_top);
  RUN_TEST(test_home_clears_stack);
  RUN_TEST(test_pop_transition_reveals_new_screen_monotonically);
  RUN_TEST(test_push_transition_reveals_new_screen_monotonically);
  return UNITY_END();
}
