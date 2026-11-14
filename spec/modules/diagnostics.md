# Diagnostics: log, CLI, fault, watchdog, version, self-test

Headers: `log.h`, `cli.h`, `fault.h`, `version.h`, `selftest.h`

## Log

- Use the macros `C0_LOGE/W/I/D/T(tag, fmt, ...)`. Levels above `CORE0_LOG_LEVEL` compile to nothing, and their arguments aren't evaluated.
- Output format: `[sssss.mmm] L tag: message` via `hal::console_write`. A RAM ring (default 512 B) keeps the most recent lines for `dmesg` and for the safe-mode screen.
- Release builds use `CORE0_LOG_LEVEL=CORE0_LOG_WARN`.

## CLI

- The CLI is a non-blocking line reader on the console. It provides the built-ins listed in `cli.h`; products add commands with `cli::add`.
- The `ev` and `key` commands inject events, so UI flows can be driven and scripted over serial without touching buttons. This is the fastest way to iterate on screens, and it lets automated hardware-in-the-loop tests work.
- The CLI is compiled out with `CORE0_FEAT_CLI=0`. Products may keep it in release builds behind a key combo.

## Fault handling

- `fault::init()` runs first in `setup()`. See the header for the crash counter and safe-mode rules.
- **Watchdog:**
  - Started in `sys::begin()` with `CORE0_WATCHDOG_MS`, and fed **only** at the end of `run_once()`.
  - A hung module therefore always trips the watchdog.
  - Modules never feed the watchdog themselves. Long operations must be state machines.
  - During Sleep/Deep the port suspends or reconfigures the watchdog.
- **`FaultRecord`** lives in `CORE0_RETAINED` RAM. It records the reset reason, panic code and location, uptime, the module that was ticking (its index and a copy of its name, up to 11 characters), and the last event dispatched. Report the name: the index is only meaningful in the boot that crashed, because safe mode registers a different module list. Change the record's magic whenever its layout changes. It is shown on the safe-mode screen, by the CLI command `fault`, and logged at boot.
- **Safe mode** runs only the minimal module set: display, input, power, battery, feedback, cli. Feedback is in the set for the red fast blink that [feedback](feedback.md) requires in safe mode. It shows the fault record and offers:
  - "Reset settings" (factory reset),
  - "Reboot",
  - "Continue anyway".
- Brownout: enable the MCU's brownout detector where available. Repeated brownout resets on a low battery go to Off, not safe mode.

## Version

`C0_DEFINE_BUILD_INFO(product, model)` in `main.cpp` combines `FW_VERSION`/`BOARD_REV` from build flags with the git SHA (from `tools/git_rev.py`; a `+` suffix means a dirty tree). It is shown on the boot log banner, the About screen (settings → About), and the CLI command `ver`.

## Self-test (factory mode)

- **Entry:** hold `CORE0_FACTORY_COMBO` keys during boot (default Up+Down), or run the CLI command `test`.
  - Call `selftest::requested_at_boot()` from the loop once `debounce_ms` has passed, not straight after `sys::begin()`, when every key still reads as up. Polling it during the splash makes the combo easy to hit.
  - It starts a swallow, so releasing the combo does not answer the first test.
- The run is driven by the factory screen. There is no self-test module, so `run_all()` needs the UI, and a product without a display gets `Unsupported`.
- Each driver registers at least one `SelfTest`:
  - Presence: I2C ACK and chip ID.
  - Functional: tone played, mic level above threshold, display checkerboard.
- Interactive tests prompt the operator and read answers from input events.
- Results appear as a PASS/FAIL list on the display and over the console, with a final summary line that a test jig can parse: `SELFTEST PASS 12/12`.
- Finishing a run starts a swallow, so releasing the key that answered the last test does not dismiss the summary.
- The `nvm` test uses the normal commit path (`save_now`), so each run costs one real settings save. Comparing bytes read back cannot prove the flash, because the port may read from its RAM copy.

## Acceptance

- [ ] A forced `C0_ASSERT(false)` three times in a row leads to safe mode, and the safe-mode screen shows the file and line.
- [ ] After `CORE0_STABLE_RUN_MS` of normal run, the crash counter is 0.
- [ ] `key select click` over the CLI has the same effect as pressing the button.
- [ ] Factory mode lists every registered test and prints a parsable summary line.
