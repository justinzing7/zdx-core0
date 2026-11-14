// ZDX CORE-0 — render service (display.h, spec/modules/display.md).
// Screens invalidate; this module decides when to draw and flush.
#include "config.h"
#if CORE0_FEAT_DISPLAY  // feature off = zero code (constitution IX)

#include "display.h"

#include "log.h"
#include "port.h"
#if CORE0_FEAT_UI
#  include "ui.h"
#endif

namespace core0 {
namespace display {
namespace {

const ms_t kIdleDeadlineMs = 60000;
const ms_t kBusyPollMs = 5;

Panel*   pnl = nullptr;
Rect     dirty = {0, 0, 0, 0};
bool     full_pending = false;
bool     frame_seen = false;
ms_t     last_frame_at = 0;
uint16_t frame_ms = 0;
uint16_t min_frame = 16;
uint8_t  bright = 255;
uint8_t  dim_level = 8;
bool     dimmed = false;
bool     panel_off = false;  // Idle and below: nothing is drawn
#if CORE0_FEAT_UI
DrawFn   draw_fn = ui::draw;
#else
DrawFn   draw_fn = nullptr;
#endif

// Screen size comes only from the attached panel's caps (no board-specific defaults).
// Without a panel the rect is empty, so invalidation is a no-op.
Rect screen_rect() {
  Rect r = {0, 0, 0, 0};
  if (pnl) {
    const PanelCaps c = pnl->caps();
    r.w = c.width;
    r.h = c.height;
  }
  return r;
}

Status init() {
  if (!pnl) {
    C0_LOGE("disp", "init failed: no panel attached");
    return Status::NotFound;
  }
  const Status s = pnl->begin();
  if (!ok(s)) {
    C0_LOGE("disp", "init failed: panel begin (%u)", static_cast<unsigned>(s));
    return s;
  }
  const PanelCaps c = pnl->caps();
  min_frame = c.min_frame_ms;
  pnl->set_brightness(bright);
  invalidate_all();
  C0_LOGI("disp", "init ok (%dx%d, frame cap %u ms)", c.width, c.height, static_cast<unsigned>(min_frame));
  return Status::Ok;
}

void tick(ms_t now) {
  if (!pnl || panel_off || dirty.w <= 0 || dirty.h <= 0) return;  // resume invalidates everything
  if (pnl->busy()) return;
  if (frame_seen && time_since(now, last_frame_at) < min_frame) return;
  const Rect d = dirty;
  dirty.w = dirty.h = 0;
  const bool full = full_pending;
  full_pending = false;
  Gfx& g = pnl->gfx();
  if (draw_fn) draw_fn(g, d);
  const Status s = pnl->flush(&d, full);
  frame_ms = static_cast<uint16_t>(time_since(hal::millis(), now));
  last_frame_at = now;
  frame_seen = true;
  if (ok(s)) bus::post(make_event(EventType::Display, static_cast<uint8_t>(DisplayCode::FrameDone), 0, frame_ms));
  else bus::post(make_event(EventType::Display, static_cast<uint8_t>(DisplayCode::PanelError)));
}

ms_t next_deadline(ms_t now) {
  if (!pnl || panel_off || dirty.w <= 0 || dirty.h <= 0) return now + kIdleDeadlineMs;
  if (pnl->busy()) return now + kBusyPollMs;  // the panel task also wakes the loop when done
  return frame_seen ? last_frame_at + min_frame : now;
}

bool can_enter(PowerState target) {
  return target < PowerState::Sleep || !(pnl && pnl->busy());
}

void on_suspend(PowerState target) {
  if (!pnl) return;
  if (target == PowerState::Dim) {
    dimmed = true;
    pnl->set_brightness(bright < dim_level ? bright : dim_level);  // never brighter than the user level
  } else if (target >= PowerState::Idle) {
    dimmed = true;  // brightness edits wait for on_resume
    panel_off = true;
    pnl->set_power(false);
  }
}

void on_resume(PowerState from, uint32_t wake) {
  (void)from;
  if (!pnl || wake == hal::kWakeRtc) return;  // background wake (power.md): the panel stays off
  dimmed = false;
  if (panel_off) pnl->set_power(true);
  panel_off = false;
  pnl->set_brightness(bright);
  invalidate_all();
}

}  // namespace

Status attach(Panel& panel) {
  pnl = &panel;
  return Status::Ok;
}
Panel* panel() { return pnl; }

void invalidate(const Rect& r) {
  const Rect s = screen_rect();
  Rect c = r;
  if (c.x < 0) { c.w += c.x; c.x = 0; }
  if (c.y < 0) { c.h += c.y; c.y = 0; }
  if (c.x + c.w > s.w) c.w = static_cast<int16_t>(s.w - c.x);
  if (c.y + c.h > s.h) c.h = static_cast<int16_t>(s.h - c.y);
  if (c.w <= 0 || c.h <= 0) return;
  dirty = rect_union(dirty, c);
}

void invalidate_all() { dirty = screen_rect(); }
void force_full_refresh() { full_pending = true; }

void set_brightness(uint8_t level) {
  bright = level;
  if (pnl && !dimmed) pnl->set_brightness(level);
}
void set_dim_level(uint8_t level) { dim_level = level; }
void set_draw_fn(DrawFn fn) { draw_fn = fn; }
uint16_t last_frame_ms() { return frame_ms; }

const Module kModule = {"display", init, tick, next_deadline, can_enter, on_suspend, on_resume};

}  // namespace display

#if defined(CORE0_NATIVE)
namespace test {
void reset_display() {
  display::pnl = nullptr;
  display::dirty.w = display::dirty.h = 0;
  display::full_pending = false;
  display::frame_seen = false;
  display::last_frame_at = 0;
  display::frame_ms = 0;
  display::min_frame = 16;
  display::bright = 255;
  display::dim_level = 8;
  display::dimmed = false;
  display::panel_off = false;
#if CORE0_FEAT_UI
  display::draw_fn = ui::draw;
#endif
}
}  // namespace test
#endif

}  // namespace core0

#endif  // CORE0_FEAT_DISPLAY
