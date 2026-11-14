// T019 — CLI parser and built-ins (diagnostics.md "CLI", acceptance 3).
#include <unity.h>
#include "test_helpers.h"
using namespace core0;

static uint8_t g_argc;
static char g_arg7[16];
static void cmd_probe(uint8_t argc, char** argv) {
  g_argc = argc;
  g_arg7[0] = 0;
  if (argc > 7) strncpy(g_arg7, argv[7], sizeof(g_arg7) - 1);
}
static const CliCommand kProbe = {"probe", "test command", cmd_probe};

static Status init_ok() { return Status::Ok; }
static Status init_bad() { return Status::IoError; }
static const Module kGood = {"good", init_ok, nullptr, nullptr, nullptr, nullptr, nullptr};
static const Module kBroken = {"broken", init_bad, nullptr, nullptr, nullptr, nullptr, nullptr};

static void run(const char* s) {
  char buf[96];
  strncpy(buf, s, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  cli::exec(buf);
}

void setUp() {
  reset_all();
  g_argc = 0;
}
void tearDown() {}

static void test_tokenizer_up_to_8_args() {
  cli::add(kProbe);
  run("probe a b   c d e f g");
  TEST_ASSERT_EQUAL_UINT8(8, g_argc);
  TEST_ASSERT_EQUAL_STRING("g", g_arg7);
}

static void test_unknown_command() {
  port_native::console_clear();
  run("x");
  TEST_ASSERT_NOT_NULL(strstr(port_native::console_capture(), "? x (try help)"));
}

static void test_help_lists_commands() {
  cli::add(kProbe);
  port_native::console_clear();
  run("help");
  TEST_ASSERT_NOT_NULL(strstr(port_native::console_capture(), "probe - test command"));
  TEST_ASSERT_NOT_NULL(strstr(port_native::console_capture(), "mods"));
}

static void test_ev_posts_event() {
  rec_attach(EventType::Input);
  run("ev 3 2 5 0");
  bus::dispatch();
  TEST_ASSERT_EQUAL_INT(1, g_rec.n);
  TEST_ASSERT_EQUAL_UINT8(2, g_rec.ev[0].code);
  TEST_ASSERT_EQUAL_UINT16(5, g_rec.ev[0].arg);
}

static void test_key_select_click_matches_input_event() {
  rec_attach(EventType::Input);
  run("key select click");
  bus::dispatch();
  TEST_ASSERT_EQUAL_INT(1, g_rec.n);
  TEST_ASSERT_EQUAL_UINT8(U(InputCode::Click), g_rec.ev[0].code);
  TEST_ASSERT_EQUAL_UINT16(U(Key::Select), g_rec.ev[0].arg);
}

static void test_mods_lists_health() {
  sys::register_module(&kGood);
  sys::register_module(&kBroken);
  sys::begin();
  port_native::console_clear();
  run("mods");
  const char* out = port_native::console_capture();
  TEST_ASSERT_NOT_NULL(strstr(out, "good"));
  TEST_ASSERT_NOT_NULL(strstr(out, "OK"));
  TEST_ASSERT_NOT_NULL(strstr(out, "broken"));
  TEST_ASSERT_NOT_NULL(strstr(out, "FAULT"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_tokenizer_up_to_8_args);
  RUN_TEST(test_unknown_command);
  RUN_TEST(test_help_lists_commands);
  RUN_TEST(test_ev_posts_event);
  RUN_TEST(test_key_select_click_matches_input_event);
  RUN_TEST(test_mods_lists_health);
  return UNITY_END();
}
