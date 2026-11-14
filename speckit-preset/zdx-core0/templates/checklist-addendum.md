
---

## ZDX CORE-0 Firmware Quality

- [ ] CHK-C0-01 No `delay()`/busy-wait in new code paths (grep before merge)
- [ ] CHK-C0-02 No heap use after init (no `new`, `malloc`, `String +=`, STL containers)
- [ ] CHK-C0-03 All `millis()` comparisons use wrap-safe helpers
- [ ] CHK-C0-04 ISRs only capture + post/push
- [ ] CHK-C0-05 Every new module has init logging, a self-test, and a CLI view
- [ ] CHK-C0-06 Power hooks implemented; PowerLocks released on every exit path (incl. errors)
- [ ] CHK-C0-07 Settings version bumped + migration for any struct change
- [ ] CHK-C0-08 Native tests pass (`pio test -e native`)
- [ ] CHK-C0-09 Target builds pass for every env in platformio.ini
- [ ] CHK-C0-10 Measured: input latency, tick() worst case, currents per state (values recorded)
- [ ] CHK-C0-11 Failure paths exercised: hardware absent, storage full, battery critical mid-feature
- [ ] CHK-C0-12 Release build: log level ≤ WARN, no warnings, BuildInfo shows git SHA
