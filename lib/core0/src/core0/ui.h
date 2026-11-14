// ZDX CORE-0 — UI: screen stack and data-driven menus.
//
// A Screen is one full-display "page" (splash, home, menu, settings, player).
// The UI keeps a fixed-depth stack; only the top screen receives input and
// draws. Screens are statically allocated objects — push() never allocates.
#pragma once
#include "display.h"
#include "input.h"

namespace core0 {

class Screen {
 public:
  virtual ~Screen() {}
  virtual const char* name() const = 0;

  // Lifecycle. `arg` is whatever the pusher passed (may be nullptr).
  virtual void on_enter(const void* arg) { (void)arg; }
  virtual void on_exit() {}
  virtual void on_cover() {}    // another screen pushed on top
  virtual void on_uncover() {}  // returned to after a pop

  // Return true if handled. Unhandled Key::Back Click pops the screen.
  // Receives Input events plus any event the screen asked for via ui::forward().
  virtual bool on_event(const Event& e) { (void)e; return false; }

  // Periodic work while on top (animations, clocks). Call display::invalidate*
  // when something changed; never draw here.
  virtual void tick(ms_t now) { (void)now; }

  // Draw the screen. Only called by the render service when dirty.
  virtual void draw(Gfx& g, const Rect& dirty) = 0;

  virtual RefreshClass refresh_class() const { return RefreshClass::Live; }

  // Earliest time tick() needs to run again (for idle sleep sizing).
  virtual ms_t next_deadline(ms_t now) { return now + 1000; }
};

enum class Transition : uint8_t { None, SlideLeft, SlideRight, SlideUp, Fade };

// Event codes for EventType::Ui.
enum class UiCode : uint8_t {
  ScreenChanged,  // arg = stack depth
  MenuSelected,   // arg = menu item id
  ValueChanged,   // arg = menu item id, data = new value
};

namespace ui {

Status push(Screen& s, const void* arg = nullptr, Transition t = Transition::SlideLeft);
Status pop(Transition t = Transition::SlideRight);
Status replace(Screen& s, const void* arg = nullptr, Transition t = Transition::None);
Status home(Screen& root, Transition t = Transition::None);  // clear stack, set root

Screen* top();
uint8_t depth();

// Let the top screen also receive events of `type` (e.g. Clock for a clock face).
Status forward(EventType type);

// Default DrawFn for display::set_draw_fn (draws top screen + transition).
void draw(Gfx& g, const Rect& dirty);

extern const Module kModule;

}  // namespace ui

// ===========================================================================
// Data-driven menus
// ===========================================================================

enum class MenuItemKind : uint8_t {
  Action,    // calls action()
  Submenu,   // pushes a MenuScreen for `sub`
  Screen,    // pushes `screen`
  Toggle,    // bool stored in *value (0/1)
  Range,     // *value in [min,max] step; Left/Right or encoder edits inline
  Choice,    // *value indexes choices[n_choices]
  Info,      // read-only line; label + format_value()
  Back,
};

struct Menu;

struct MenuItem {
  uint8_t      id;          // reported in UiCode events
  const char*  label;
  MenuItemKind kind;
  // ---- payload (by kind) ----
  void (*action)();                 // Action
  const Menu*  sub;                 // Submenu
  Screen*      screen;              // Screen
  int16_t*     value;               // Toggle / Range / Choice
  int16_t      min, max, step;      // Range
  const char* const* choices;       // Choice
  uint8_t      n_choices;
  // ---- optional hooks ----
  bool (*visible)();                // nullptr = always visible
  void (*on_change)(int16_t v);     // after Toggle/Range/Choice edit (e.g. mark settings dirty)
  void (*format_value)(char* buf, uint8_t len);  // custom right-hand text
};

struct Menu {
  const char*     title;
  const MenuItem* items;
  uint8_t         count;
};

// constexpr builders so `static const MenuItem kItems[] = {...}` stays in flash.
constexpr MenuItem menu_action(uint8_t id, const char* label, void (*fn)()) {
  return MenuItem{id, label, MenuItemKind::Action, fn, nullptr, nullptr, nullptr,
                  0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr};
}
constexpr MenuItem menu_submenu(uint8_t id, const char* label, const Menu& sub) {
  return MenuItem{id, label, MenuItemKind::Submenu, nullptr, &sub, nullptr, nullptr,
                  0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr};
}
constexpr MenuItem menu_screen(uint8_t id, const char* label, Screen& s) {
  return MenuItem{id, label, MenuItemKind::Screen, nullptr, nullptr, &s, nullptr,
                  0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr};
}
constexpr MenuItem menu_toggle(uint8_t id, const char* label, int16_t* v,
                               void (*on_change)(int16_t) = nullptr) {
  return MenuItem{id, label, MenuItemKind::Toggle, nullptr, nullptr, nullptr, v,
                  0, 1, 1, nullptr, 0, nullptr, on_change, nullptr};
}
constexpr MenuItem menu_range(uint8_t id, const char* label, int16_t* v, int16_t lo,
                              int16_t hi, int16_t step, void (*on_change)(int16_t) = nullptr) {
  return MenuItem{id, label, MenuItemKind::Range, nullptr, nullptr, nullptr, v,
                  lo, hi, step, nullptr, 0, nullptr, on_change, nullptr};
}
constexpr MenuItem menu_choice(uint8_t id, const char* label, int16_t* v,
                               const char* const* choices, uint8_t n,
                               void (*on_change)(int16_t) = nullptr) {
  return MenuItem{id, label, MenuItemKind::Choice, nullptr, nullptr, nullptr, v,
                  0, static_cast<int16_t>(n - 1), 1, choices, n, nullptr, on_change, nullptr};
}
constexpr MenuItem menu_info(uint8_t id, const char* label, void (*fmt)(char*, uint8_t)) {
  return MenuItem{id, label, MenuItemKind::Info, nullptr, nullptr, nullptr, nullptr,
                  0, 0, 0, nullptr, 0, nullptr, nullptr, fmt};
}
constexpr MenuItem menu_back(const char* label = "< Back") {
  return MenuItem{0xFF, label, MenuItemKind::Back, nullptr, nullptr, nullptr, nullptr,
                  0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr};
}

// Renders a Menu: title bar, scrolling list with selection, inline editors.
// Scrolling uses encoder acceleration (input.h) and an eased selection bar
// (anim.h) so long lists feel inertial while staying deterministic.
class MenuScreen : public Screen {
 public:
  explicit MenuScreen(const Menu& menu) : menu_(&menu), sel_(0), scroll_(0), editing_(false) {}
  const char* name() const override { return menu_->title; }
  void on_enter(const void* arg) override;
  bool on_event(const Event& e) override;
  void tick(ms_t now) override;
  void draw(Gfx& g, const Rect& dirty) override;

  void set_menu(const Menu& m) { menu_ = &m; sel_ = 0; scroll_ = 0; }

 protected:
  const Menu* menu_;
  uint8_t     sel_;
  uint8_t     scroll_;
  bool        editing_;
};

}  // namespace core0
