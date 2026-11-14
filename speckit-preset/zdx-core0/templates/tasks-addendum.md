
---

## ZDX CORE-0 Task Rules *(apply when generating the phases above)*

**Ordering.** Within the Setup/Foundational phases, order work by the CORE-0 bring-up
sequence. Skip steps that already exist and pass their checkpoints:

1. Port + console → boot banner shows BuildInfo and reset reason
2. Core loop + timers + CLI → `mods`, `ver`, `ev` work
3. Input → key gestures logged correctly
4. Display + UI → splash → menu navigable (keys and CLI `key`)
5. Settings + clock → values survive reset; time survives sleep
6. Audio + feedback → click, melody, LED patterns
7. Battery + power → dim/idle/sleep/wake; currents recorded
8. Self-test + fault → factory mode passes; safe mode after forced crashes
9. Feature user stories (phases 3+)

**Task shape.** Each implementation task names:
- the module (`app::<name>` or the foundation module being extended),
- its files (`src/app/…`, `src/drivers/…`, `include/board_config.h`, `test/…`),
- its state-machine states, events posted or consumed, and power hooks.

**Tests first where possible.** For each acceptance criterion that can run on the host,
add a `test/native/test_<area>.cpp` task BEFORE the implementation task, marked `[P]`
when independent. Use the mock port's controllable clock — never real sleeps.

**Hardware verification tasks.** For each `HW-xxx` criterion in the spec, add a final
task: "Measure <criterion> on target; record value + method in checklists/hardware.md".

**Mandatory closing tasks for every feature:**
- [ ] Register self-test(s) for any new driver
- [ ] Add CLI command(s) for inspecting the new state
- [ ] Update the plan's Resource Budget with real numbers from `pio run -t size`
- [ ] Update the Power Impact table with measured currents
- [ ] Verify release build (`CORE0_LOG_LEVEL` ≤ WARN) compiles without warnings
