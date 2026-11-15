// ZDX CORE-0 — serial CLI (cli.h, spec/modules/diagnostics.md "CLI").
// Line-oriented: cli::print() terminates each call with "\n". Contract:
// specs/001-foundation-bringup/contracts/serial-console.md §3.
// Power contract: none (the port drains UART TX before sleep; Sleep loses RX bytes by design).
#include "config.h"
#if CORE0_FEAT_CLI  // feature off = zero code (constitution IX)

#include "cli.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "event.h"
#include "fault.h"
#if CORE0_FEAT_INPUT
#  include "input.h"
#endif
#include "log.h"
#include "port.h"
#include "version.h"
#if CORE0_FEAT_POWER
#  include "power.h"
#endif

namespace core0 {
namespace sys {  // library-internal (sys.cpp)
uint32_t    tick_max_us(uint8_t index);
Status      init_status(uint8_t index);
const char* status_str(Status s);
}  // namespace sys

#if CORE0_FEAT_SELFTEST
namespace selftest {  // library-internal (selftest.cpp)
Status run_named(const char* only);
}  // namespace selftest
#endif

namespace cli {
namespace {

const size_t  kLineMax = 96;
const uint8_t kMaxArgs = 8;
const int     kBytesPerTick = 64;
const ms_t    kSecretTimeoutMs = 60000;

void cmd_help(uint8_t, char**);
void cmd_ver(uint8_t, char**);
void cmd_mods(uint8_t, char**);
void cmd_ev(uint8_t, char**);
#if CORE0_FEAT_INPUT
void cmd_key(uint8_t, char**);
#endif
void cmd_log(uint8_t, char**);
void cmd_dmesg(uint8_t, char**);
void cmd_fault(uint8_t, char**);
void cmd_reset(uint8_t, char**);
#if CORE0_FEAT_SELFTEST
void cmd_test(uint8_t, char**);
#endif

const CliCommand kBuiltins[] = {
    {"help", "list commands", cmd_help},
    {"ver", "build info", cmd_ver},
    {"mods", "modules + health", cmd_mods},
    {"ev", "ev <type> <code> [arg] [data]: inject event", cmd_ev},
#if CORE0_FEAT_INPUT
    {"key", "key <up|down|select|back|...> <press|release|click|long|repeat>", cmd_key},
#endif
    {"log", "log <0-5>: runtime log level", cmd_log},
    {"dmesg", "recent log lines", cmd_dmesg},
    {"fault", "last fault record", cmd_fault},
    {"reset", "software reset", cmd_reset},
#if CORE0_FEAT_SELFTEST
    {"test", "test [name]: run self-tests", cmd_test},
#endif
};

CliCommand cmds[CORE0_MAX_CLI_COMMANDS];
uint8_t    ncmds = 0;
char       line[kLineMax];
size_t     pos = 0;
bool       last_cr = false;
SecretFn   secret_fn = nullptr;  // set: the next line is a secret (read_secret)
ms_t       secret_at = 0;        // last input while a secret is pending

void wipe_line() {
  volatile char* p = line;
  for (size_t i = 0; i < kLineMax; ++i) p[i] = 0;
  pos = 0;
}

#if CORE0_FEAT_INPUT
const char* const kKeyNames[] = {"up", "down", "left", "right", "select", "back",
                                 "menu", "power", "aux1", "aux2", "aux3"};
#endif
const char* const kResetNames[] = {"PowerOn", "Brownout", "Watchdog", "Software",
                                   "ExternalPin", "Panic", "DeepSleepWake", "Unknown"};

void out(const char* s) { hal::console_write(s, strlen(s)); }
void prompt() { out("> "); }

void cmd_help(uint8_t, char**) {
  for (size_t i = 0; i < count_of(kBuiltins); ++i) print("%s - %s", kBuiltins[i].name, kBuiltins[i].help);
  for (uint8_t i = 0; i < ncmds; ++i) print("%s - %s", cmds[i].name, cmds[i].help);
}

void cmd_ver(uint8_t, char**) {
  print("==== %s %s ====", kBuildInfo.product, kBuildInfo.model);
  print("fw %s  board %s  git %s  core0 %s", kBuildInfo.fw_version, kBuildInfo.board_rev, kBuildInfo.git_sha,
        kBuildInfo.core_version);
  print("built %s", kBuildInfo.build_date);
}

void cmd_mods(uint8_t, char**) {
  for (uint8_t i = 0; i < sys::module_count(); ++i) {
    const Module* m = sys::module_at(i);
    const bool h = sys::module_healthy(i);
    print("%2u %-10s %-5s %6lu us  %s", static_cast<unsigned>(i), m->name, h ? "OK" : "FAULT",
          static_cast<unsigned long>(sys::tick_max_us(i)), h ? "" : sys::status_str(sys::init_status(i)));
  }
}

void cmd_ev(uint8_t argc, char** argv) {
  if (argc < 3) { print("usage: ev <type> <code> [arg] [data]"); return; }
  Event e = make_event(static_cast<EventType>(strtoul(argv[1], nullptr, 0)),
                       static_cast<uint8_t>(strtoul(argv[2], nullptr, 0)),
                       argc > 3 ? static_cast<uint16_t>(strtoul(argv[3], nullptr, 0)) : 0,
                       argc > 4 ? static_cast<uint32_t>(strtoul(argv[4], nullptr, 0)) : 0);
  print(ok(bus::post(e)) ? "ok" : "queue full");
}

#if CORE0_FEAT_INPUT
void cmd_key(uint8_t argc, char** argv) {
  if (argc < 3) { print("usage: key <name> <press|release|click|long|repeat>"); return; }
  int key = -1;
  for (size_t i = 0; i < count_of(kKeyNames); ++i)
    if (strcmp(argv[1], kKeyNames[i]) == 0) key = static_cast<int>(i);
  InputCode code;
  if (strcmp(argv[2], "press") == 0) code = InputCode::Press;
  else if (strcmp(argv[2], "release") == 0) code = InputCode::Release;
  else if (strcmp(argv[2], "click") == 0) code = InputCode::Click;
  else if (strcmp(argv[2], "long") == 0) code = InputCode::LongPress;
  else if (strcmp(argv[2], "repeat") == 0) code = InputCode::Repeat;
  else key = -1;
  if (key < 0) { print("? key %s %s", argv[1], argv[2]); return; }
#if CORE0_FEAT_POWER
  power::activity(hal::millis());
#endif
  bus::post(make_event(EventType::Input, static_cast<uint8_t>(code), static_cast<uint16_t>(key),
                       code == InputCode::Repeat ? 1u : 0u));
}

#endif

void cmd_log(uint8_t argc, char** argv) {
  if (argc > 1) logger::set_level(static_cast<uint8_t>(atoi(argv[1])));
  print("log level %u", static_cast<unsigned>(logger::level()));
}

void cmd_dmesg(uint8_t, char**) { logger::dump_recent(); }

void cmd_fault(uint8_t, char**) {
  const FaultRecord& r = fault::last();
  const unsigned reason = static_cast<unsigned>(r.reason);
  print("reason %s  crashes %u  module %s  code %lu  what %s  uptime %lu ms",
        reason < count_of(kResetNames) ? kResetNames[reason] : "?", static_cast<unsigned>(r.crash_count),
        r.module_name[0] ? r.module_name : "?", static_cast<unsigned long>(r.code), r.what[0] ? r.what : "-",
        static_cast<unsigned long>(r.uptime_ms));
}

void cmd_reset(uint8_t, char**) {
  print("resetting");
  hal::system_reset();
}

#if CORE0_FEAT_SELFTEST
void cmd_test(uint8_t argc, char** argv) {
  const Status s = selftest::run_named(argc > 1 ? argv[1] : nullptr);
  if (s == Status::NotFound) print("? test %s", argv[1]);
  else if (!ok(s)) print("test: %s", sys::status_str(s));
}
#endif

const CliCommand* find(const char* name) {
  for (size_t i = 0; i < count_of(kBuiltins); ++i)
    if (strcmp(name, kBuiltins[i].name) == 0) return &kBuiltins[i];
  for (uint8_t i = 0; i < ncmds; ++i)
    if (strcmp(name, cmds[i].name) == 0) return &cmds[i];
  return nullptr;
}

Status init() {
  C0_LOGI("cli", "init ok (%u commands)", static_cast<unsigned>(count_of(kBuiltins) + ncmds));
  prompt();
  return Status::Ok;
}

void tick(ms_t now) {
  if (secret_fn && time_since(now, secret_at) >= kSecretTimeoutMs) {
    secret_fn = nullptr;
    wipe_line();
    out("\r\n");
    print("(cancelled)");
    prompt();
  }
  for (int n = 0; n < kBytesPerTick; ++n) {
    const int c = hal::console_read();
    if (c < 0) return;
    if (secret_fn) secret_at = now;
    if (c == '\r' || c == '\n') {
      if (c == '\n' && last_cr) { last_cr = false; continue; }
      last_cr = c == '\r';
      out("\r\n");
      if (secret_fn) {
        SecretFn fn = secret_fn;
        secret_fn = nullptr;
        line[pos] = 0;
        fn(line, static_cast<uint8_t>(pos));
        wipe_line();
      } else if (pos > 0) {
        line[pos] = 0;
        exec(line);
        pos = 0;
      }
      if (!secret_fn) prompt();  // a command that asked for a secret has printed its own prompt
    } else if (c == 0x08 || c == 0x7F) {
      last_cr = false;
      if (pos > 0) { --pos; out("\b \b"); }
    } else if (c >= 0x20 && c < 0x7F && pos < kLineMax - 1) {
      last_cr = false;
      line[pos++] = static_cast<char>(c);
      const char ch[2] = {secret_fn ? '*' : static_cast<char>(c), 0};
      out(ch);
    }
  }
}

ms_t next_deadline(ms_t now) { return now + 50; }  // poll console at 20 Hz while awake

}  // namespace

Status read_secret(const char* prompt_text, SecretFn done) {
  if (!done) return Status::Invalid;
  if (secret_fn) return Status::Busy;
  secret_fn = done;
  secret_at = hal::millis();
  if (prompt_text) out(prompt_text);
  return Status::Ok;
}

Status add(const CliCommand& cmd) {
  if (ncmds >= CORE0_MAX_CLI_COMMANDS) {
    C0_LOGE("cli", "table full (%u): '%s' not added, raise CORE0_MAX_CLI_COMMANDS", static_cast<unsigned>(CORE0_MAX_CLI_COMMANDS),
            cmd.name);
    return Status::NoSpace;
  }
  cmds[ncmds++] = cmd;
  return Status::Ok;
}

void print(const char* fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (static_cast<size_t>(n) > sizeof(buf) - 2) n = sizeof(buf) - 2;
  buf[n++] = '\n';
  hal::console_write(buf, static_cast<size_t>(n));
}

void exec(char* s) {
  char* argv[kMaxArgs];
  uint8_t argc = 0;
  char* p = s;
  while (*p && argc < kMaxArgs) {
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) break;
    argv[argc++] = p;
    while (*p && *p != ' ' && *p != '\t') ++p;
    if (*p) *p++ = 0;
  }
  if (argc == 0) return;
  const CliCommand* c = find(argv[0]);
  if (!c) {
    print("? %s (try help)", argv[0]);
    return;
  }
  c->run(argc, argv);
}

const Module kModule = {"cli", init, tick, next_deadline, nullptr, nullptr, nullptr};

}  // namespace cli

#if defined(CORE0_NATIVE)
namespace test {
void reset_cli() {
  cli::ncmds = 0;
  cli::pos = 0;
  cli::last_cr = false;
  cli::secret_fn = nullptr;
  cli::secret_at = 0;
}
const char* cli_line() { return cli::line; }
}  // namespace test
#endif

}  // namespace core0

#endif  // CORE0_FEAT_CLI
