
---

## ZDX CORE-0 Foundation Mapping *(mandatory)*

<!--
  Read zdx-core0/spec/00-architecture.md and the module specs before filling this in.
  One row per foundation module. "Extend" means new screens/events/drivers on top of it;
  "Change" means zdx-core0/lib/core0 must change (requires a foundation version bump + upstream note).
-->

**Foundation version**: [e.g., 0.1.0] · **Execution profile**: [SUPERLOOP | RTOS | DUALCORE]

| Module | Use / Extend / Change / Off | Driver or implementation | Notes |
|--------|-----------------------------|--------------------------|-------|
| core (loop, bus, timers) | Use | — | |
| port | Use | src/port/port_[mcu].cpp | |
| input | | | |
| display | | [Panel adapter: e.g., U8g2 SSD1306] | |
| ui / anim | | | |
| power | | | |
| battery | | | |
| audio (tone / stream / capture) | | [AudioOut / AudioIn drivers] | |
| feedback | | | |
| settings | | | [fields added + version bump?] |
| clock | | [RtcSource] | |
| diagnostics (log/cli/fault/selftest) | Use | | [new CLI commands, self-tests] |
| storage | | | |
| net | | | |

### New Modules, Screens and Events

- **App modules**: [name — responsibility — state machine states]
- **Screens**: [name — RefreshClass — entered from]
- **App events** (`EventType::App + n`): [name — code/arg/data meaning]
- **Settings fields**: [field — type — default — range]  → settings version [old → new]
- **CLI commands / self-tests**: [...]

### Board & Pin Changes

<!-- Only include/board_config.h may change. List every new/changed define. -->

| Define | Value | Reason |
|--------|-------|--------|
| | | |

### Resource Budget

| Item | RAM (bytes) | Flash (bytes) | Measured after build? |
|------|-------------|---------------|-----------------------|
| [module/asset] | | | |
| **Total delta** | | | |
| **Remaining headroom** | | | |

### Power Impact

| State | Before (mA) | After (mA) | How measured |
|-------|-------------|------------|--------------|
| Active | | | |
| Idle | | | |
| Sleep | | | |
| Feature running | | | |

- **PowerLocks held**: [owner — floor state — when]
- **New wake sources**: [...]
- **can_enter vetoes**: [...]

### Timing Budget

| Path | Budget | Mechanism |
|------|--------|-----------|
| [key → response] | [≤ 30 ms] | [Press event → …] |
| [tick() worst case] | [< 1 ms] | |

### Constitution Gate (ZDX CORE-0)

- [ ] I. No blocking calls introduced (list any slow ops and their state machines)
- [ ] II. No heap after init (list pools/rings and sizes)
- [ ] III. Layer boundaries respected (no driver includes in services/screens)
- [ ] IV. Power contract defined for every new module
- [ ] V. Input latency budget met by design
- [ ] VI. ISRs capture-only; time comparisons wrap-safe
- [ ] VII. Native tests planned for every host-testable criterion
- [ ] VIII. Logging, self-test, CLI and failure behavior defined
- [ ] IX. New options behind feature flags
- [ ] X. Any added complexity recorded in Complexity Tracking
