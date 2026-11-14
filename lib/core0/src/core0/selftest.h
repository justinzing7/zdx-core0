// ZDX CORE-0 — factory / self-test mode.
//
// Entry: hold CORE0_FACTORY_COMBO keys during boot, or CLI "test".
// Runs registered tests in order and shows PASS/FAIL per line on the display
// (and over the console). Interactive tests prompt the operator ("Press UP",
// "Did you hear the tone? SELECT=yes BACK=no").
// Every driver should register at least a presence test (I2C ACK, chip ID).
#pragma once
#include "event.h"

namespace core0 {

enum class TestResult : uint8_t { Pass, Fail, Skip, Running };

struct SelfTest {
  const char* name;
  bool        interactive;  // a yes/no question: the service maps Click Up/Select = Pass, Down/Back = Fail
  // Called repeatedly (non-blocking) until it returns != Running.
  // `e` is an input event, or type None if none; every test gets them, so a test that reads keys
  // itself (not `interactive`) sees each one. Any input restarts the 30 s timeout.
  // msg: empty on the first call of a run, which is how a test knows to start. While Running it is
  // the prompt; when finished it is the detail, so clear it if there is none.
  // msg_len == 0: the run is abandoning this test (answered, timed out, or the screen went away).
  // Release what it holds and write nothing; the result is ignored.
  TestResult (*step)(ms_t now, const Event& e, char* msg, uint8_t msg_len);
};

namespace selftest {

Status register_test(const SelfTest& t);
// Checks the combo keys. Call it from the loop once debounce_ms has passed (e.g. during the splash),
// not straight after sys::begin(): keys read as up until then. It starts a swallow, so releasing
// the combo does not answer the first test.
bool   requested_at_boot();
Status run_all();             // pushes the factory screen
uint8_t count();

}  // namespace selftest
}  // namespace core0
