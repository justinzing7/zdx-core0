# Power

Header: `power.h` · Event type: `EventType::Power` · Flag: `CORE0_FEAT_POWER`

## States

| State | Display | MCU between events | Typical wake | Entered by |
|---|---|---|---|---|
| **Active** | on, user brightness | `Idle` sleep until next deadline | any IRQ | activity |
| **Dim** | dimmed | same | any IRQ | `dim_after_ms` inactivity |
| **Idle** | off or static face | same | any IRQ / key | `idle_after_ms` |
| **Sleep** | off / e-ink face | `Light` sleep | key, RTC, USB | `sleep_after_ms`, menu, power key |
| **Deep** | off / e-ink face | `Deep` (may reset) | key, RTC | `deep_after_ms`, low battery |
| **Off** | off | Deep, minimal wake mask | power key, charger | long-press power, critical battery |

Note: in Active, Dim, and Idle the MCU still sleeps between events (`SleepDepth::Idle`). The state names describe the **device**, not the CPU.

## Transition algorithm (`power::service`)

1. Compute the target from the policy and inactivity time, or from a pending `request()`.
2. **Going down** (lower power):
   1. Check that no `PowerLock` floor is above the target.
   2. Check that every module's `can_enter(target)` returns true. If any check fails, stay put and retry on the next relevant event.
   3. Call `on_suspend(target)` on modules in **reverse** registration order.
   4. Post `StateChanged`.
   5. For Sleep/Deep: arm the wake mask plus the RTC wake (`set_wake_at` and the earliest timer if it's within the sleep window), then call `hal::sleep`.
      Do this on a **later pass** than `on_suspend`, once the events are dispatched and `can_enter` passes again. `on_suspend` may only queue work, such as a panel-off command, and sleeping at once could leave it undone. Off stops waiting for vetoes after 100 ms, because it ignores them.
3. **Waking:**
   1. `timers::compensate_sleep`.
   2. Call `on_resume(from, wake_bits)` in forward order.
   3. Post `Resumed`, then `StateChanged` to Active (or to Idle for RTC-only wakes, such as a background battery sample or an alarm check that doesn't need the screen).
   4. **Background wake** (the wake bits are only `kWakeRtc`): every module's `on_resume` runs, and modules facing the user (display, LED, amp) check the wake bits and stay off. The due timers run on the next pass, then the device returns to the state it woke from. Work that needs more passes holds `PowerLock(Idle)`. Work that needs the user, such as a ringing alarm, calls `request(Active)`, which resumes the modules again with the screen on. A timer wake counts only once a timer is due; an early one sleeps again.
4. **Every pass** (Active/Dim/Idle), when `bus::pending()` is false: sleep with `SleepDepth::Idle` until `sys::next_deadline(now)`, capped at `CORE0_SERVICE_TICK_MS` if any module is busy-polling.

`sys::run_once` always calls `power::service`, but power changes state only when `power::kModule` is registered. Without it, `service` does only the Idle wait, so a product or test without the power module never follows the default policy into Sleep.

**ESP32 Deep and STM32 Standby reset on wake.** Code must not assume `on_resume` runs after Deep. State that must survive goes in `CORE0_RETAINED` RAM or in settings. Boot then sees `ResetReason::DeepSleepWake` and resumes with a quick path: no splash, straight to the right screen.

## Locks vs vetoes

- **`PowerLock(floor, owner)`:** "I'm busy; don't go below `floor`." Use it for audio playback, recording, file writes, and active network work. Locks are held only while the activity runs, and the CLI command `pwr` lists holders by owner.
- **`can_enter(target)`:** a module-level check of its own state. Use it when the rule depends on state, not on an ongoing activity.

## Low battery

- On `BatteryState::Critical`, power posts `LowBatteryForce`.
- The UI shows a "battery empty" face for about 2 s, settings flush, and the device goes to Off.
- Locks are ignored in this path, but `on_suspend` still runs. Data loss is worse than 2 seconds of runtime.

## Power budget (every product's plan must fill this in)

| State | Target current | Measured | Notes |
|---|---|---|---|
| Active (display on) | | | |
| Idle | | | |
| Sleep | | | |
| Deep / Off | | | |
| Peak (audio/radio) | | | |

## Acceptance

- [ ] With the default policy and no input, the device steps Active→Dim→Idle→Sleep at the configured times (±1 tick).
- [ ] A held `PowerLock(Idle)` keeps the device out of Sleep. After unlock, it sleeps within one service pass.
- [ ] A timer wake from Sleep runs `on_resume`, reports Idle, leaves the display and LED off, and returns to Sleep after the timer has run. `request(Active)` from that timer brings the screen on.
- [ ] The wake press does not trigger an action (see input).
- [ ] Settings marked dirty before sleep are on flash after wake (or after a Deep reset).
- [ ] Measured Sleep current is recorded in the board's power budget.
