# UI: screens, menus, animation, splash

Headers: `ui.h`, `anim.h` · Event type: `EventType::Ui` · Flag: `CORE0_FEAT_UI`

## Screen stack

- A `Screen` is a statically allocated object with the lifecycle `on_enter(arg)`, `on_exit`, `on_cover`, `on_uncover`, `on_event`, `tick`, `draw`, `refresh_class`, `next_deadline`.
- The stack has a fixed depth (`CORE0_SCREEN_STACK_DEPTH`, default 6).
  - `push` / `pop` / `replace` / `home` never allocate.
  - Pushing past the maximum depth returns `NoSpace` and logs W.
- Only the top screen ticks, draws, and receives Input events. A screen opts into other event types with `ui::forward(type)`; for example, a clock face forwards `Clock` events.
- **Back behavior.** If the top screen doesn't handle `Key::Back` Click, the UI pops it. A long press on Back, if configured, goes home.
- **Key conventions.** Three-key products use Up/Down/Select. Four-key products add Back: a Back click pops, and a Back long press goes home.
- **Transitions** (`SlideLeft/Right/Up`, `Fade`, `None`) last about 150 ms on OLED and are rendered by the UI using two draw passes. On e-ink, transitions are always `None`. A transition moves monotonically from the old screen to the new one: the new screen's share grows from nothing to the full width, in both directions.
- Screens never block. Anything slow becomes state shown by the screen, such as "Saving…" or a spinner driven by `tick`.

Typical stack for a product:

```
home (root) ──push──► main menu ──push──► settings menu ──push──► value editor
             └─push──► function screen (player / recorder / timer)
```

## Data-driven menus

A `Menu` is a `const` table of `MenuItem` built with the `menu_*()` constexpr helpers, so it lives in flash. Item kinds:

| Kind | Behavior |
|---|---|
| Action | Select → `action()` |
| Submenu | Select → push a `MenuScreen` for `sub` |
| Screen | Select → push `screen` |
| Toggle | Select flips `*value`, calls `on_change` |
| Range | Select enters inline edit: Up/Down/encoder change by `step` within [min,max]; Select/Back exits; `on_change` per step |
| Choice | Like Range, showing `choices[*value]` |
| Info | read-only, right side from `format_value()` |
| Back | pops |

Up/Down act on `Press` and `Repeat`, never on `Click`, so moving the selection or stepping a value meets the key to visible response budget however long the key is held. Select and Back act on `Click` (release).

`visible()` hides items dynamically, for example hiding Wi-Fi items when no radio is fitted.

Settings menus bind directly to fields of the settings struct and call `settings.changed()` from `on_change`. Persistence is deferred, so scrolling a value writes flash once.

`MenuScreen` rendering contract:
- Title bar.
- A visible window of N rows, where N comes from font height and panel height.
- The selection bar **eases** to the new row (`Tween`, `OutCubic`, ~80 ms).
- The scroll window follows with the same easing.
- Wraps from last to first item only when the menu has more than one screen of items.
- A scrollbar is shown when items exceed the window.

## Animation

All animation is time-based (`anim.h`), so it looks the same at any frame rate.

- `ease(curve, t)` with Q8 progress: `Linear`, `InQuad`, `OutQuad`, `InOutQuad`, `OutCubic`, `OutBack`.
- `Tween`: one value from→to over a duration. Use it for selection bars, sliding panels, and number roll-ups.
- `Animator` + `Animation`: flip-book playback of bitmap frames with per-frame hold times, from the asset pipeline. `update(now)` reports when to invalidate `bounds()`.
- A screen with a running animation returns its next frame time from `next_deadline()`, so the loop can idle between frames instead of polling.

## Splash

`SplashScreen` is configured with an `Animation` and/or logo, a caption, `min_ms`, `max_ms`, and the `next` screen.

- It holds for at least `min_ms`, giving a consistent brand moment.
- It then waits for `SystemCode::InitDone`, up to `max_ms`, and replaces itself with `next`.
- Keys are consumed while the splash shows and never cut it short. Once `min_ms` has passed and init is done, it leaves on its own.
- It reports its next deadline (`min_ms`, `max_ms` or the next frame), so the loop can sleep until then.
- If `max_ms` expires before InitDone, it proceeds anyway, logs W, and the faulted modules show their own errors.

## Acceptance

- [ ] Navigating the template settings menu works with three keys (Up/Down/Select) and with an encoder plus push switch, with no code change.
- [ ] Editing a Range value for 10 steps then exiting results in exactly one settings save.
- [ ] Pushing past the stack depth returns `NoSpace` without corrupting the stack.
- [ ] The splash never shows for less than `min_ms` and never longer than `max_ms` + 1 frame.
- [ ] Menu navigation latency (key → frame) meets the conventions budget on the target.
