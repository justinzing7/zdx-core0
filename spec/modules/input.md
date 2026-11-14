# Input

Header: `input.h` · Event type: `EventType::Input` · Flag: `CORE0_FEAT_INPUT`, `CORE0_FEAT_ENCODER`

## Model

Physical inputs map to **logical keys** (`Key::Up`, `Key::Select`, `Key::Aux1`, ...) in `board_config.h`. Screens react to logical keys and gestures only. This lets one UI run on a 3-button board, a 5-way joystick, or an encoder with a push switch.

```
driver/ISR ──feed_key(key, pressed, t)──► raw ring ──► debounce ──► gesture FSM ──► events
encoder ISR ──feed_encoder(detents, t)──► raw ring ──► acceleration ──► Rotate events
```

- **Sources.** GPIO buttons are polled every tick when `CORE0_INPUT_POLL_GPIO=1`; this is the simplest option, and wake is handled by power. Pin-change ISRs, I2C expanders, capacitive touch, or IMU gestures instead call `feed_key()` / `feed_encoder()`.
- **Debounce.** Uses a per-key integrator, not a dead-time. A state change is accepted after the input is stable for `debounce_ms` (default 15 ms). It is accepted on the **first stable sample** after that, never at a fixed delay after the edge.

## Gestures (per key, enabled by `KeyFlags`)

| Event | When | Notes |
|---|---|---|
| `Press` | debounced down edge | **Emitted immediately.** Use for anything latency-sensitive (game keys, record button) |
| `Release` | debounced up edge | `data` = held ms |
| `Click` | release before `long_ms` | If `kKeyDoubleClick` is set, delayed until `double_ms` passes with no second press; otherwise emitted on release. A key held past `long_ms` emits no Click, Repeat keys included |
| `DoubleClick` | second press within `double_ms` | Only keys with `kKeyDoubleClick` |
| `LongPress` | held ≥ `long_ms` | Once per hold. A subsequent Release emits no `Click` |
| `Repeat` | held ≥ `repeat_delay_ms`, then every `repeat_rate_ms` | `kKeyRepeat` keys; `data` = count. Use for scrolling lists |
| `Combo` | two keys both down within 100 ms | `arg = (k1 << 8) \| k2`; suppresses the individual Clicks |
| `Rotate` | encoder detents | `data` = (int32) accelerated steps, + = clockwise |

**Latency rule.** Double-click is off by default because it delays every Click by `double_ms`. Enable it only on keys that need it.

## Encoder acceleration (inertial menus)

The time between detents selects a multiplier from the `AccelStep` table. The default is <12 ms → ×8, <25 → ×4, <50 → ×2, else ×1. Slow turns move one item at a time; a fast spin jumps through long lists.

The UI adds visual inertia separately, with an eased selection bar and scroll (see [ui](ui.md)). Input stays deterministic: the accumulated step count is never lost.

## Interaction with power

- Every Input event calls `power::activity(now)`.
- If the press woke the display from Dim, Idle, or Sleep, input calls `swallow_until_release()`. That key's gesture events are discarded until it is released, so a wake press never also triggers an action (it can't select a menu item in the dark).
- The swallow ends once every key has stayed up for `debounce_ms`, including when no press was seen. A key pressed inside that window is swallowed too, so a bouncing wake contact cannot act.
- A per-screen opt-out, for an alarm screen whose first press should snooze, is not in 0.1.0. It is planned with the first product that needs it.
- `kKeyWake` keys are configured as wake sources by the port during `on_suspend`.

## Acceptance

- [ ] A bouncy contact (simulated 5 ms chatter) produces exactly one Press and one Release.
- [ ] Press→`Press` event latency is ≤ debounce + 1 tick.
- [ ] Holding a key for 2 s with Repeat enabled yields the expected Repeat count ±1.
- [ ] A press that wakes the display produces no Click or LongPress.
- [ ] Encoder: 20 detents at 10 ms intervals produce more than 20 steps; 20 detents at 200 ms produce exactly 20.
- [ ] All gesture tests run in `env:native` by feeding raw events with a mock clock.
