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

namespace core0 {
namespace test {
const char* cli_line();  // cli.cpp test seam: the raw line buffer
}
}  // namespace core0

static char g_secret[96];
static uint8_t g_secret_len;
static int g_secret_calls;
static void on_secret(char* line, uint8_t len) {
  ++g_secret_calls;
  g_secret_len = len;
  memcpy(g_secret, line, len);
  g_secret[len] = 0;
}

static void type(const char* s) {
  port_native::console_inject(s);
  cli::kModule.tick(hal::millis());
}

static void run(const char* s) {
  char buf[96];
  strncpy(buf, s, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  cli::exec(buf);
}

void setUp() {
  reset_all();
  g_argc = 0;
  g_secret_calls = 0;
  g_secret_len = 0;
  g_secret[0] = 0;
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

// A full table refuses the command and says so: a missing command must not be silent.
static void test_full_table_is_no_space_and_logged() {
  for (int i = 0; i < CORE0_MAX_CLI_COMMANDS; ++i) TEST_ASSERT_TRUE(cli::add(kProbe) == Status::Ok);
  port_native::console_clear();
  TEST_ASSERT_TRUE(cli::add(kProbe) == Status::NoSpace);
  TEST_ASSERT_NOT_NULL(strstr(port_native::console_capture(), "'probe' not added"));
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

static void test_secret_echoes_stars_and_hands_over_the_line() {
  port_native::console_clear();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::Ok), static_cast<uint8_t>(cli::read_secret("password: ", on_secret)));
  type("secret\r");
  const char* out = port_native::console_capture();
  TEST_ASSERT_NOT_NULL(strstr(out, "password: ******"));
  TEST_ASSERT_NULL(strstr(out, "secret"));
  TEST_ASSERT_EQUAL_INT(1, g_secret_calls);
  TEST_ASSERT_EQUAL_UINT8(6, g_secret_len);
  TEST_ASSERT_EQUAL_STRING("secret", g_secret);
}

static void test_secret_buffer_is_wiped_after_use() {
  cli::read_secret("password: ", on_secret);
  type("hunter22\r");
  const char* raw = test::cli_line();
  for (int i = 0; i < 96; ++i) TEST_ASSERT_EQUAL_HEX8(0, static_cast<uint8_t>(raw[i]));
}

static void test_secret_backspace_removes_a_star() {
  cli::read_secret("pw: ", on_secret);
  port_native::console_clear();
  type("abX\b");
  type("c\r");
  TEST_ASSERT_EQUAL_STRING("abc", g_secret);
  TEST_ASSERT_NOT_NULL(strstr(port_native::console_capture(), "***\b \b*"));
}

static void test_secret_empty_line_is_still_handed_over() {
  cli::read_secret("pw: ", on_secret);
  type("\r");
  TEST_ASSERT_EQUAL_INT(1, g_secret_calls);
  TEST_ASSERT_EQUAL_UINT8(0, g_secret_len);
}

static void test_second_secret_request_is_busy() {
  cli::read_secret("pw: ", on_secret);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::Busy), static_cast<uint8_t>(cli::read_secret("pw: ", on_secret)));
}

static void test_secret_times_out_after_60s_of_silence() {
  cli::read_secret("pw: ", on_secret);
  type("ab");
  port_native::advance_ms(59000);
  cli::kModule.tick(hal::millis());
  type("c");  // input restarts the timeout
  port_native::advance_ms(59000);
  cli::kModule.tick(hal::millis());
  TEST_ASSERT_NULL(strstr(port_native::console_capture(), "(cancelled)"));
  port_native::advance_ms(1500);
  cli::kModule.tick(hal::millis());
  TEST_ASSERT_NOT_NULL(strstr(port_native::console_capture(), "(cancelled)"));
  type("\r");
  TEST_ASSERT_EQUAL_INT(0, g_secret_calls);
}

static void test_next_line_after_a_secret_is_a_command() {
  cli::add(kProbe);
  cli::read_secret("pw: ", on_secret);
  type("x y z\r");
  TEST_ASSERT_EQUAL_UINT8(0, g_argc);  // the secret was not parsed as a command
  type("probe 1 2\r");
  TEST_ASSERT_EQUAL_UINT8(3, g_argc);
}

static int g_chain_calls;
static void cmd_ask(uint8_t, char**) { cli::read_secret("pw: ", on_secret); }
static const CliCommand kAsk = {"ask", "asks for a secret", cmd_ask};

static void test_command_can_ask_for_a_secret_without_a_stray_prompt() {
  (void)g_chain_calls;
  cli::add(kAsk);
  port_native::console_clear();
  type("ask\r");
  const char* out = port_native::console_capture();
  TEST_ASSERT_NOT_NULL(strstr(out, "pw: "));
  TEST_ASSERT_NULL(strstr(strstr(out, "pw: "), "> "));  // no command prompt after the secret prompt
  type("topsecret\r");
  TEST_ASSERT_EQUAL_STRING("topsecret", g_secret);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_tokenizer_up_to_8_args);
  RUN_TEST(test_unknown_command);
  RUN_TEST(test_help_lists_commands);
  RUN_TEST(test_full_table_is_no_space_and_logged);
  RUN_TEST(test_ev_posts_event);
  RUN_TEST(test_key_select_click_matches_input_event);
  RUN_TEST(test_mods_lists_health);
  RUN_TEST(test_secret_echoes_stars_and_hands_over_the_line);
  RUN_TEST(test_secret_buffer_is_wiped_after_use);
  RUN_TEST(test_secret_backspace_removes_a_star);
  RUN_TEST(test_secret_empty_line_is_still_handed_over);
  RUN_TEST(test_second_secret_request_is_busy);
  RUN_TEST(test_secret_times_out_after_60s_of_silence);
  RUN_TEST(test_next_line_after_a_secret_is_a_command);
  RUN_TEST(test_command_can_ask_for_a_secret_without_a_stray_prompt);
  return UNITY_END();
}
