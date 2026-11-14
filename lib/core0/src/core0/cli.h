// ZDX CORE-0 — serial debug console.
//
// Line-based, non-blocking (bytes polled in tick). Built-in commands:
//   help              list commands
//   ver               BuildInfo + foundation version
//   mods              modules + health
//   ev <type> <code> <arg> <data>   inject an event (drive the UI without hardware)
//   key <name> <gesture>            shorthand: "key select click", "key up long"
//   pwr [state]       show / request power state
//   bat               battery mV, %, state
//   time [epoch]      show / set clock (ClockSource::Host)
//   set [dump|reset]  settings
//   log <0-5>         runtime log level
//   test [name]       run self-tests          reset   software reset
//   fault             last FaultRecord        dmesg   recent log lines
// Projects add their own with cli::add(). Compile out with CORE0_FEAT_CLI=0.
#pragma once
#include "module.h"

namespace core0 {

struct CliCommand {
  const char* name;
  const char* help;
  void (*run)(uint8_t argc, char** argv);  // argv[0] = name
};

namespace cli {

Status add(const CliCommand& cmd);
void   print(const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
void   exec(char* line);   // parse + run (also used by tests)

extern const Module kModule;

}  // namespace cli
}  // namespace core0
