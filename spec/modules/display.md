# Display

Header: `display.h` · Event type: `EventType::Display` · Flag: `CORE0_FEAT_DISPLAY`

## Responsibilities

The render service decides **when** to draw and **how** to refresh. Screens decide **what** to draw.

- **`Gfx`** is the drawing interface screens use: primitives, text, bitmaps, clipping, plus a `native()` escape hatch.
- **`Panel`** is the driver contract: `begin`, `caps`, `gfx`, `flush(dirty, full)`, `busy`, `set_power`, `set_brightness`. A project wraps its library (U8g2, Adafruit_GFX, GxEPD2) in one `Panel` + `Gfx` adapter. `busy()` is true while any queued command is pending, not only a flush, because power waits on it before sleeping.
- **Render service** (`display::kModule`), in `tick()`:
  1. If nothing is dirty, return.
  2. If `panel.busy()` (e-ink refreshing) or less than `caps.min_frame_ms` since the last frame, return. Dirty regions keep accumulating.
  3. Call `DrawFn` (default `ui::draw`) with the union of dirty rects. The screen draws.
  4. Call `panel.flush(dirty, full)`, where `full` is decided by the refresh policy below.
  5. Post `DisplayCode::FrameDone`.

Screens call `display::invalidate(rect)` or `invalidate_all()`. They never call flush.

Drawing rules:
- The `DrawFn` owns the frame: the service does not clear between frames. `ui::draw` clears the dirty rect, so screens must not call `Gfx::clear()`, which would wipe the other half of a transition.
- `display::set_draw_fn()` lets a module take over the frame, for example a test pattern. Restoring `ui::draw` hands it back.
- `set_font(nullptr)` selects the panel's default UI font, so foundation screens never name a backend font.
- Nothing is drawn while the panel is off, and a dirty rect must not keep the loop awake then.

## Refresh policy by panel kind

| Screen `RefreshClass` | OLED / LCD | E-ink |
|---|---|---|
| `Live` | redraw when dirty, up to the fps cap | partial refresh, capped (≥ 300 ms) |
| `Partial` | same as Live | partial; every `caps.full_every` partials → full (ghosting) |
| `Full` | same as Live | always full refresh |
| `Static` | draw on enter / `invalidate_all` only | same, full refresh |

E-ink rules:
- No animation or Live content in sleep states.
- Before Deep or Off, draw the "sleep face" (clock, status, or logo) and do a full refresh, so the image left on glass is intentional.
- The panel's power is cut after each refresh. E-ink holds its image without power.

## Power contract

| State | OLED | E-ink |
|---|---|---|
| Active | brightness = user setting | normal |
| Dim | brightness = the lower of the user setting and `dim_level` | no change |
| Idle | panel off (`set_power(false)`), or a static always-on face if the product wants one | last image stays, controller asleep |
| Sleep/Deep/Off | off | sleep face, controller off |

On resume to Active, the service restores power and brightness and invalidates everything. A background wake (wake bits only `kWakeRtc`, see [power](power.md)) leaves the panel off.

## Burn-in (OLED)

Products with long-lived static content (clock faces) shift the content by ±1–2 px every few minutes. The screen handles this; it is listed here so plans account for it.

## Acceptance

- [ ] With no invalidation, `flush` is never called (verify with the mock panel).
- [ ] With 10 invalidations inside one frame period, exactly one flush happens.
- [ ] E-ink: after `full_every` partial flushes, the next flush is full.
- [ ] Dim, Idle, and Active transitions call `set_brightness` / `set_power` as specified.
- [ ] The native build renders a screen into a mock framebuffer and can dump it to a PBM/PNG for golden-image tests.
