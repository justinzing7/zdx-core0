// ZDX CORE-0 — factory self-test service (selftest.h, spec/modules/diagnostics.md "Self-test").
//
// run_all() pushes FactoryScreen, whose tick() and on_event() drive the run, so this service needs
// no module of its own: the Active power lock keeps it on screen, and leaving the screen ends the
// run. One test finishes per pass at most. Console contract: serial-console.md §4.
// Power contract: PowerLock(Active, "selftest") for the length of a run; nothing owned otherwise.
// CLI view: `test [name]` (cli.cpp). Without CORE0_FEAT_UI there is no screen to run on.
#include "config.h"
#if CORE0_FEAT_SELFTEST  // feature off = zero code (constitution IX)

#include "selftest.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "port.h"
#if CORE0_FEAT_UI
#  include "ui.h"
#endif
#if CORE0_FEAT_POWER
#  include "power.h"
#endif

namespace core0 {
namespace selftest {

Status run_named(const char* only);  // library-internal (cli.cpp): nullptr = every test

namespace {

SelfTest tests[CORE0_MAX_SELFTESTS];
uint8_t  ntests = 0;

#if CORE0_FEAT_UI

const ms_t    kAnswerTimeoutMs = 30000;  // FR-042
const ms_t    kAutoExitMs = 5000;        // contracts/ui-navigation.md §6
const ms_t    kPollMs = 10;
const uint8_t kMsgLen = 24;
const uint8_t kLeaderChars = 14;

enum class St : uint8_t { Idle, Starting, Running, Next, Summary, Done };

struct Result {
  const char* name;
  TestResult  result;
  char        msg[kMsgLen];
};

Result      results[CORE0_MAX_SELFTESTS];
uint8_t     nresults = 0;
St          st = St::Idle;
const char* filter = nullptr;
char        filter_name[kMsgLen];
int16_t     index = -1;       // running test
uint8_t     total = 0;
uint8_t     passed = 0;
uint8_t     skipped = 0;       // Skip = not applicable here (nothing fitted / configured): not a failure
char        current[kMsgLen];  // prompt while running, detail once finished
ms_t        last_input = 0;
ms_t        done_at = 0;
#if CORE0_FEAT_POWER
PowerLock   lock_id = kNoLock;
#endif

void say(const char* fmt, ...) {
  char buf[64];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n > static_cast<int>(sizeof(buf) - 2)) n = sizeof(buf) - 2;
  buf[n++] = '\n';
  hal::console_write(buf, static_cast<size_t>(n));
}

bool selected(uint8_t i) { return !filter || strcmp(tests[i].name, filter) == 0; }

const char* result_name(TestResult r) {
  return r == TestResult::Pass ? "PASS" : r == TestResult::Skip ? "SKIP" : "FAIL";
}

// A finished test's last chance to release what it holds (selftest.h).
void abort_test(ms_t now) {
  Event none = {};
  tests[index].step(now, none, current, 0);
}

void finish(TestResult r, const char* detail) {
  if (detail) snprintf(current, sizeof(current), "%s", detail);
  if (r == TestResult::Pass) ++passed;
  if (r == TestResult::Skip) ++skipped;
  Result& out = results[nresults++];
  out.name = tests[index].name;
  out.result = r;
  snprintf(out.msg, sizeof(out.msg), "%s", current);
  if (out.msg[0]) say("ST %s %s %s", out.name, result_name(r), out.msg);
  else say("ST %s %s", out.name, result_name(r));
  st = St::Next;
#if CORE0_FEAT_INPUT
  // A test can finish on a Press. The release, and its Click, must not answer the next test or
  // dismiss the summary (bench 2026-11-06). With no key down this ends after a debounce time.
  input::swallow_until_release();
#endif
  display::invalidate_all();
}

void step_current(ms_t now, const Event& e) {
  char before[kMsgLen];
  memcpy(before, current, sizeof(before));
  const TestResult r = tests[index].step(now, e, current, kMsgLen);
  if (r != TestResult::Running) {
    finish(r, nullptr);
    return;
  }
  if (memcmp(before, current, sizeof(before)) != 0) display::invalidate_all();
  if (tests[index].interactive && e.type == EventType::Input && e.code == static_cast<uint8_t>(InputCode::Click)) {
    const Key k = static_cast<Key>(e.arg);
    const bool yes = k == Key::Up || k == Key::Select;   // research R17
    const bool no = k == Key::Down || k == Key::Back;
    if (yes || no) {
      abort_test(now);
      finish(yes ? TestResult::Pass : TestResult::Fail, "");
      return;
    }
  }
  if (time_since(now, last_input) >= kAnswerTimeoutMs) {
    abort_test(now);
    finish(TestResult::Fail, "timeout");
  }
}

void start_next(ms_t now) {
  do {
    ++index;
  } while (index < ntests && !selected(static_cast<uint8_t>(index)));
  if (index >= ntests) {
    st = St::Summary;
    return;
  }
  current[0] = 0;
  last_input = now;
  st = St::Running;
  display::invalidate_all();
}

void release_power(ms_t now) {
#if CORE0_FEAT_POWER
  power::unlock(lock_id);
  power::activity(now);  // the idle timeout counts from the end of the run, not from its start
#else
  (void)now;
#endif
}

bool all_ok() { return passed + skipped == total; }

void print_summary() {
  if (skipped) {
    say("SELFTEST %s %u/%u (%u skipped)", all_ok() ? "PASS" : "FAIL", static_cast<unsigned>(passed),
        static_cast<unsigned>(total), static_cast<unsigned>(skipped));
  } else {
    say("SELFTEST %s %u/%u", all_ok() ? "PASS" : "FAIL", static_cast<unsigned>(passed), static_cast<unsigned>(total));
  }
}

class FactoryScreen : public Screen {
 public:
  const char* name() const override { return "factory"; }

  void on_exit() override {
    const ms_t now = hal::millis();
    const St was = st;
    st = St::Idle;
    if (was == St::Done || was == St::Idle) return;
    // Taken away mid-run (battery empty): still exactly one summary, and nothing left held.
    if (was == St::Running) abort_test(now);
    print_summary();
    release_power(now);
  }

  bool on_event(const Event& e) override {
    if (e.type != EventType::Input) return false;
    const ms_t now = hal::millis();
    if (st == St::Running) {
      last_input = now;
      step_current(now, e);
    } else if (st == St::Done) {
      done_at = now;
      if (e.code == static_cast<uint8_t>(InputCode::Click) && static_cast<Key>(e.arg) == Key::Back) ui::pop();
    }
    return true;  // Back never leaves a run
  }

  void tick(ms_t now) override {
    switch (st) {
      case St::Starting:
        index = -1;
        start_next(now);
        break;
      case St::Running: {
        Event none = {};
        step_current(now, none);
        break;
      }
      case St::Next:
        start_next(now);
        break;
      case St::Summary:
        print_summary();
        release_power(now);
        done_at = now;
        st = St::Done;
        display::invalidate_all();
        break;
      case St::Done:
        if (time_since(now, done_at) >= kAutoExitMs) ui::pop();
        break;
      case St::Idle:
        break;
    }
  }

  ms_t next_deadline(ms_t now) override {
    if (st == St::Done) return done_at + kAutoExitMs;
    return st == St::Running ? now + kPollMs : now;
  }

  void draw(Gfx& g, const Rect&) override {
    g.set_font(nullptr);
    const int16_t w = g.width(), fh = g.font_height();
    const int16_t rows = static_cast<int16_t>(g.height() / fh);
    const int16_t list_rows = rows > 2 ? static_cast<int16_t>(rows - 2) : 1;
    const bool running = st == St::Running;
    const int16_t shown = static_cast<int16_t>(nresults + (running ? 1 : 0));
    const int16_t first = shown > list_rows ? static_cast<int16_t>(shown - list_rows) : 0;
    for (int16_t i = first; i < shown; ++i) {
      const int16_t y = static_cast<int16_t>((i - first + 1) * fh - 3);
      const bool live = i >= nresults;
      char row[kLeaderChars + 1];
      memset(row, '.', kLeaderChars);
      row[kLeaderChars] = 0;
      const char* nm = live ? tests[index].name : results[i].name;
      const size_t n = strlen(nm);
      memcpy(row, nm, n < kLeaderChars ? n : kLeaderChars);
      if (n < kLeaderChars) row[n] = ' ';
      g.text(0, y, row, Color::White, Align::Left);
      if (!live) g.text(w, y, result_name(results[i].result), Color::White, Align::Right);
    }
    const int16_t y1 = static_cast<int16_t>((rows - 1) * fh - 3), y2 = static_cast<int16_t>(rows * fh - 3);
    if (running) {
      g.text(0, y1, current, Color::White, Align::Left);
      if (tests[index].interactive) g.text(0, y2, "Up/Sel=YES Dn/Bk=NO", Color::White, Align::Left);
    } else if (st == St::Done) {
      char summary[24];
      snprintf(summary, sizeof(summary), "%s %u/%u", all_ok() ? "PASS" : "FAIL", static_cast<unsigned>(passed),
               static_cast<unsigned>(total));
      g.text(0, y1, summary, Color::White, Align::Left);
      g.text(0, y2, "Back: exit", Color::White, Align::Left);
    }
  }
};

FactoryScreen s_factory;

#endif  // CORE0_FEAT_UI

}  // namespace

Status register_test(const SelfTest& t) {
  if (ntests >= CORE0_MAX_SELFTESTS) return Status::NoSpace;
  tests[ntests++] = t;
  return Status::Ok;
}

uint8_t count() { return ntests; }

bool requested_at_boot() {
#if CORE0_FEAT_INPUT && CORE0_FEAT_UI
  const Key combo[] = CORE0_FACTORY_COMBO;
  for (size_t i = 0; i < count_of(combo); ++i)
    if (!input::is_down(combo[i])) return false;
  input::swallow_until_release();  // letting go of the combo must not answer the first test
  return true;
#else
  return false;
#endif
}

Status run_named(const char* only) {
#if CORE0_FEAT_UI
  if (st != St::Idle) return Status::Busy;
  filter = nullptr;
  if (only) {
    snprintf(filter_name, sizeof(filter_name), "%s", only);
    filter = filter_name;
  }
  total = 0;
  for (uint8_t i = 0; i < ntests; ++i)
    if (selected(i)) ++total;
  if (only && total == 0) return Status::NotFound;
  // Before the push: the screen may take a pass to appear, but the run owns the state from here.
  st = St::Starting;
  const Status s = ui::push(s_factory);
  if (!ok(s)) {
    st = St::Idle;
    return s;
  }
  nresults = 0;
  passed = 0;
  skipped = 0;
  say("SELFTEST BEGIN %u", static_cast<unsigned>(total));
#if CORE0_FEAT_POWER
  lock_id = power::lock(PowerState::Active, "selftest");
#endif
  return Status::Ok;
#else
  (void)only;
  return Status::Unsupported;
#endif
}

Status run_all() { return run_named(nullptr); }

}  // namespace selftest

#if defined(CORE0_NATIVE)
namespace test {
void reset_selftest() {
  selftest::ntests = 0;
#if CORE0_FEAT_UI
  selftest::st = selftest::St::Idle;
  selftest::nresults = 0;
  selftest::filter = nullptr;
#if CORE0_FEAT_POWER
  selftest::lock_id = kNoLock;
#endif
#endif
}
}  // namespace test
#endif

}  // namespace core0

#endif  // CORE0_FEAT_SELFTEST
