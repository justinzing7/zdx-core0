// ZDX CORE-0 — screen stack (ui.h, spec/modules/ui.md "Screen stack").
//
// Drawing contract: ui::draw() clears the dirty rect before calling Screen::draw(), so screens
// draw their content only and never call Gfx::clear() (that would wipe a transition's other
// half). Each stack level keeps its own ui::forward() mask, so it survives cover/uncover.
// Transitions are a ~150 ms horizontal wipe (two clipped draw passes); none on e-ink.
// Power contract: none (no hardware; the display module handles panel power).
#include "config.h"
#if CORE0_FEAT_UI  // feature off = zero code (constitution IX)

#include "ui.h"

#include <string.h>

#include "log.h"
#include "port.h"

namespace core0 {
namespace ui {
namespace {

const ms_t kTransitionMs = 150;
const ms_t kTransitionFrameMs = 16;

Screen*    stack[CORE0_SCREEN_STACK_DEPTH];
uint32_t   fwd_mask[CORE0_SCREEN_STACK_DEPTH];  // bit n = EventType n (n < 32)
bool       fwd_app[CORE0_SCREEN_STACK_DEPTH];   // EventType::App + n
uint8_t    depth_ = 0;

Screen*    trans_from = nullptr;
Transition trans = Transition::None;
ms_t       trans_start = 0;

bool transitions_enabled() {
  Panel* p = display::panel();
  return p && p->caps().kind != PanelKind::Eink;
}

void begin_transition(Screen* from, Transition t) {
  if (t == Transition::None || !from || !transitions_enabled()) {
    trans = Transition::None;
    return;
  }
  trans_from = from;
  trans = t;
  trans_start = hal::millis();
}

void changed() {
  display::invalidate_all();
  bus::post(make_event(EventType::Ui, static_cast<uint8_t>(UiCode::ScreenChanged), depth_));
}

bool forwarded(EventType t) {
  if (depth_ == 0) return false;
  const uint8_t v = static_cast<uint8_t>(t);
  if (v >= static_cast<uint8_t>(EventType::App)) return fwd_app[depth_ - 1];
  return v < 32 && (fwd_mask[depth_ - 1] & (1u << v));
}

void unwind_to_root() {
  while (depth_ > 1) {
    stack[depth_ - 1]->on_exit();
    --depth_;
  }
  trans = Transition::None;
  if (depth_) stack[0]->on_uncover();
  changed();
}

void on_bus(const Event& e, void*) {
  if (depth_ == 0) return;
  if (e.type != EventType::Input && !forwarded(e.type)) return;
  Screen* top_screen = stack[depth_ - 1];
  if (top_screen->on_event(e)) return;
  if (e.type != EventType::Input || e.arg != static_cast<uint16_t>(Key::Back)) return;
  if (e.code == static_cast<uint8_t>(InputCode::Click)) pop();
  else if (e.code == static_cast<uint8_t>(InputCode::LongPress)) unwind_to_root();
}

Status init() {
  const Status s = bus::subscribe(EventType::None, on_bus);
  if (!ok(s)) {
    C0_LOGE("ui", "init failed: subscribe");
    return s;
  }
  C0_LOGI("ui", "init ok (stack depth %u)", static_cast<unsigned>(CORE0_SCREEN_STACK_DEPTH));
  return Status::Ok;
}

void tick(ms_t now) {
  if (trans != Transition::None) {
    if (time_since(now, trans_start) >= kTransitionMs) trans = Transition::None;
    display::invalidate_all();
  }
  if (depth_) stack[depth_ - 1]->tick(now);
}

ms_t next_deadline(ms_t now) {
  if (trans != Transition::None) return now + kTransitionFrameMs;
  return depth_ ? stack[depth_ - 1]->next_deadline(now) : now + 60000;
}

}  // namespace

Status push(Screen& s, const void* arg, Transition t) {
  if (depth_ >= CORE0_SCREEN_STACK_DEPTH) {
    C0_LOGW("ui", "push %s: stack full", s.name());
    return Status::NoSpace;
  }
  Screen* from = depth_ ? stack[depth_ - 1] : nullptr;
  if (from) from->on_cover();
  stack[depth_] = &s;
  fwd_mask[depth_] = 0;
  fwd_app[depth_] = false;
  ++depth_;
  begin_transition(from, t);
  s.on_enter(arg);
  changed();
  return Status::Ok;
}

Status pop(Transition t) {
  if (depth_ <= 1) return Status::Invalid;  // the root screen is never popped
  Screen* from = stack[depth_ - 1];
  from->on_exit();
  --depth_;
  begin_transition(from, t);
  stack[depth_ - 1]->on_uncover();
  changed();
  return Status::Ok;
}

Status replace(Screen& s, const void* arg, Transition t) {
  if (depth_ == 0) return push(s, arg, t);
  Screen* from = stack[depth_ - 1];
  from->on_exit();
  stack[depth_ - 1] = &s;
  fwd_mask[depth_ - 1] = 0;
  fwd_app[depth_ - 1] = false;
  begin_transition(from, t);
  s.on_enter(arg);
  changed();
  return Status::Ok;
}

Status home(Screen& root, Transition t) {
  while (depth_ > 0) {
    stack[depth_ - 1]->on_exit();
    --depth_;
  }
  trans = Transition::None;
  return push(root, nullptr, t);
}

Screen* top() { return depth_ ? stack[depth_ - 1] : nullptr; }
uint8_t depth() { return depth_; }

Status forward(EventType type) {
  if (depth_ == 0) return Status::Invalid;
  const uint8_t v = static_cast<uint8_t>(type);
  if (v >= static_cast<uint8_t>(EventType::App)) fwd_app[depth_ - 1] = true;
  else if (v < 32) fwd_mask[depth_ - 1] |= 1u << v;
  else return Status::Unsupported;
  return Status::Ok;
}

void draw(Gfx& g, const Rect& dirty) {
  g.set_clip(nullptr);
  g.rect(dirty, Color::Black, true);
  if (depth_ == 0) return;
  Screen* to = stack[depth_ - 1];
  if (trans == Transition::None || !trans_from) {
    to->draw(g, dirty);
    return;
  }
  // Wipe: the new screen's share `reveal` grows 0 -> w over kTransitionMs.
  //   SlideLeft  (push): new enters from the right -> [old | new]
  //   SlideRight (pop) : new enters from the left  -> [new | old]
  const ms_t el = time_since(hal::millis(), trans_start);
  const int16_t w = g.width(), h = g.height();
  const int16_t reveal =
      static_cast<int16_t>((static_cast<int32_t>(w) * (el > kTransitionMs ? kTransitionMs : el)) / kTransitionMs);
  const bool from_left = trans == Transition::SlideRight;
  Rect new_r = {from_left ? static_cast<int16_t>(0) : static_cast<int16_t>(w - reveal), 0, reveal, h};
  Rect old_r = {from_left ? reveal : static_cast<int16_t>(0), 0, static_cast<int16_t>(w - reveal), h};
  if (old_r.w > 0) { g.set_clip(&old_r); trans_from->draw(g, old_r); }
  if (new_r.w > 0) { g.set_clip(&new_r); to->draw(g, new_r); }
  g.set_clip(nullptr);
}

const Module kModule = {"ui", init, tick, next_deadline, nullptr, nullptr, nullptr};

}  // namespace ui

#if defined(CORE0_NATIVE)
namespace test {
void reset_ui() {
  ui::depth_ = 0;
  ui::trans = Transition::None;
  ui::trans_from = nullptr;
}
}  // namespace test
#endif

}  // namespace core0

#endif  // CORE0_FEAT_UI
