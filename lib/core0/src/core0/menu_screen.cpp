// ZDX CORE-0 — data-driven menus (ui.h MenuScreen, spec/modules/ui.md "Data-driven menus").
//
// Layout is derived from the panel's default UI font (set_font(nullptr)): title bar of one
// font height, then rows of font height + 1 — no board constants. Navigation:
//   Up/Down Press|Repeat  act on the press edge (key -> visible response budget), move the selection (wrap only when items exceed one screen), or step
//                         the value while editing (clamped, on_change per real change).
//   Select Click          Action/Submenu/Screen/Toggle/Back act; Range/Choice enter/leave edit.
//   Back Click            leaves edit (value kept); otherwise unhandled -> ui pops the screen.
// Events: UiCode::ValueChanged per value change; UiCode::MenuSelected on activation and on
// confirming an edit with Select (products use it for confirm feedback).
// Submenus are pushed using a static MenuScreen pool indexed by stack level (no allocation).
#include "config.h"
#if CORE0_FEAT_UI  // feature off = zero code (constitution IX)

#include "ui.h"

#include <stdio.h>

#include "anim.h"
#include "port.h"

namespace core0 {
namespace {

const uint16_t kEaseMs = 80;
const ms_t kIdleDeadlineMs = 60000;

const Menu kEmptyMenu = {"", nullptr, 0};
MenuScreen g_pool[CORE0_SCREEN_STACK_DEPTH] = {
#if CORE0_SCREEN_STACK_DEPTH >= 1
    MenuScreen(kEmptyMenu),
#endif
#if CORE0_SCREEN_STACK_DEPTH >= 2
    MenuScreen(kEmptyMenu),
#endif
#if CORE0_SCREEN_STACK_DEPTH >= 3
    MenuScreen(kEmptyMenu),
#endif
#if CORE0_SCREEN_STACK_DEPTH >= 4
    MenuScreen(kEmptyMenu),
#endif
#if CORE0_SCREEN_STACK_DEPTH >= 5
    MenuScreen(kEmptyMenu),
#endif
#if CORE0_SCREEN_STACK_DEPTH >= 6
    MenuScreen(kEmptyMenu),
#endif
#if CORE0_SCREEN_STACK_DEPTH >= 7
    MenuScreen(kEmptyMenu),
#endif
#if CORE0_SCREEN_STACK_DEPTH >= 8
    MenuScreen(kEmptyMenu),
#endif
};
static_assert(CORE0_SCREEN_STACK_DEPTH <= 8, "extend the MenuScreen pool initialiser");

// Per-instance animation state, kept outside the (header-fixed) class layout. Indexed by the
// stack level the menu currently occupies.
struct Anim {
  const MenuScreen* owner;
  Tween bar;
};
Anim g_anim[CORE0_SCREEN_STACK_DEPTH];

Anim& anim_for(const MenuScreen* m) {
  for (uint8_t i = 0; i < CORE0_SCREEN_STACK_DEPTH; ++i)
    if (g_anim[i].owner == m) return g_anim[i];
  const uint8_t d = ui::depth() ? static_cast<uint8_t>(ui::depth() - 1) : 0;
  g_anim[d].owner = m;
  return g_anim[d];
}

bool visible(const MenuItem& it) { return !it.visible || it.visible(); }
bool selectable(const MenuItem& it) { return visible(it) && it.kind != MenuItemKind::Info; }

uint8_t count_visible(const Menu& m) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < m.count; ++i) n += visible(m.items[i]);
  return n;
}
// Index of the k-th visible item.
uint8_t nth_visible(const Menu& m, uint8_t k) {
  for (uint8_t i = 0; i < m.count; ++i)
    if (visible(m.items[i]) && k-- == 0) return i;
  return 0;
}
uint8_t visible_pos(const Menu& m, uint8_t index) {
  uint8_t k = 0;
  for (uint8_t i = 0; i < index && i < m.count; ++i) k += visible(m.items[i]);
  return k;
}

void post(UiCode c, uint8_t id, uint32_t data = 0) {
  bus::post(make_event(EventType::Ui, static_cast<uint8_t>(c), id, data));
}

void value_text(const MenuItem& it, char* buf, uint8_t len) {
  buf[0] = 0;
  if (it.format_value) { it.format_value(buf, len); return; }
  switch (it.kind) {
    case MenuItemKind::Toggle: snprintf(buf, len, "%s", (it.value && *it.value) ? "On" : "Off"); break;
    case MenuItemKind::Range: if (it.value) snprintf(buf, len, "%d", *it.value); break;
    case MenuItemKind::Choice:
      if (it.value && it.choices && *it.value >= 0 && *it.value < it.n_choices) snprintf(buf, len, "%s", it.choices[*it.value]);
      break;
    case MenuItemKind::Submenu:
    case MenuItemKind::Screen: snprintf(buf, len, ">"); break;
    default: break;
  }
}

struct Layout {
  int16_t w, h, title_h, row_h;
  uint8_t rows;
};
Layout layout(Gfx* g) {
  Layout l = {128, 64, 12, 13, 4};
  if (!g) {
    if (Panel* p = display::panel()) g = &p->gfx();
  }
  if (g) {
    g->set_font(nullptr);
    l.w = g->width();
    l.h = g->height();
    l.title_h = g->font_height();
    l.row_h = static_cast<int16_t>(g->font_height() + 1);
  }
  l.rows = static_cast<uint8_t>(l.row_h > 0 ? (l.h - l.title_h) / l.row_h : 1);
  if (l.rows == 0) l.rows = 1;
  return l;
}

}  // namespace

void MenuScreen::on_enter(const void* arg) {
  if (arg) menu_ = static_cast<const Menu*>(arg);  // pool screens receive their menu as arg
  sel_ = 0;
  scroll_ = 0;
  editing_ = false;
  while (sel_ < menu_->count && !selectable(menu_->items[sel_])) ++sel_;
  if (sel_ >= menu_->count) sel_ = 0;
  Anim& a = anim_for(this);
  a.owner = this;
  a.bar.begin(0, 0, 0, Ease::OutCubic, hal::millis());
}

bool MenuScreen::on_event(const Event& e) {
  if (e.type != EventType::Input || menu_->count == 0) return false;
  const Key k = static_cast<Key>(e.arg);
  const InputCode c = static_cast<InputCode>(e.code);
  MenuItem const& it = menu_->items[sel_];
  const bool step = c == InputCode::Press || c == InputCode::Repeat;

  if (editing_) {
    if (step && (k == Key::Up || k == Key::Down) && it.value) {
      const int16_t dir = k == Key::Up ? 1 : -1;
      int32_t v = *it.value + dir * (it.step ? it.step : 1);
      if (v < it.min) v = it.min;
      if (v > it.max) v = it.max;
      if (v != *it.value) {
        *it.value = static_cast<int16_t>(v);
        if (it.on_change) it.on_change(*it.value);
        post(UiCode::ValueChanged, it.id, static_cast<uint32_t>(*it.value));
        display::invalidate_all();
      }
      return true;
    }
    if (c == InputCode::Click && (k == Key::Select || k == Key::Back)) {
      editing_ = false;
      if (k == Key::Select) post(UiCode::MenuSelected, it.id);  // confirm
      display::invalidate_all();
      return true;
    }
    return k == Key::Up || k == Key::Down || k == Key::Select;
  }

  if (step && (k == Key::Up || k == Key::Down)) {
    const Layout l = layout(nullptr);
    const uint8_t nvis = count_visible(*menu_);
    const bool wrap = nvis > l.rows;
    uint8_t i = sel_;
    for (uint8_t tries = 0; tries < menu_->count; ++tries) {
      if (k == Key::Down) {
        if (i + 1 < menu_->count) ++i;
        else if (wrap) i = 0;
        else break;
      } else {
        if (i > 0) --i;
        else if (wrap) i = static_cast<uint8_t>(menu_->count - 1);
        else break;
      }
      if (selectable(menu_->items[i])) {
        const int16_t from_row = static_cast<int16_t>(visible_pos(*menu_, sel_) - scroll_);
        sel_ = i;
        const uint8_t pos = visible_pos(*menu_, sel_);
        if (pos < scroll_) scroll_ = pos;
        if (pos >= scroll_ + l.rows) scroll_ = static_cast<uint8_t>(pos - l.rows + 1);
        const int16_t to_row = static_cast<int16_t>(pos - scroll_);
        anim_for(this).bar.begin(static_cast<int16_t>(from_row * l.row_h), static_cast<int16_t>(to_row * l.row_h), kEaseMs,
                                 Ease::OutCubic, hal::millis());
        display::invalidate_all();
        break;
      }
    }
    return true;
  }

  if (c == InputCode::Click && k == Key::Select) {
    switch (it.kind) {
      case MenuItemKind::Action:
        post(UiCode::MenuSelected, it.id);
        if (it.action) it.action();
        break;
      case MenuItemKind::Submenu:
        post(UiCode::MenuSelected, it.id);
        if (it.sub && ui::depth() < CORE0_SCREEN_STACK_DEPTH) ui::push(g_pool[ui::depth()], it.sub);
        break;
      case MenuItemKind::Screen:
        post(UiCode::MenuSelected, it.id);
        if (it.screen) ui::push(*it.screen);
        break;
      case MenuItemKind::Toggle:
        if (it.value) {
          *it.value = *it.value ? 0 : 1;
          if (it.on_change) it.on_change(*it.value);
          post(UiCode::ValueChanged, it.id, static_cast<uint32_t>(*it.value));
          post(UiCode::MenuSelected, it.id);
        }
        break;
      case MenuItemKind::Range:
      case MenuItemKind::Choice:
        editing_ = true;
        break;
      case MenuItemKind::Back:
        ui::pop();
        return true;
      case MenuItemKind::Info:
        break;
    }
    display::invalidate_all();
    return true;
  }
  return false;  // Back Click / LongPress fall through to the UI (pop / home)
}

void MenuScreen::tick(ms_t now) {
  const Anim& a = anim_for(this);
  if (!a.bar.done(now)) display::invalidate_all();
}

void MenuScreen::draw(Gfx& g, const Rect&) {
  const Layout l = layout(&g);
  const ms_t now = hal::millis();
  // Title bar.
  Rect title = {0, 0, l.w, l.title_h};
  g.rect(title, Color::White, true);
  g.text(static_cast<int16_t>(l.w / 2), static_cast<int16_t>(l.title_h - 2), menu_->title, Color::Black, Align::Center);
  // Selection bar (eased), then rows drawn XOR so text inverts over the bar.
  const Anim& a = anim_for(this);
  const int16_t bar_y = static_cast<int16_t>(l.title_h + a.bar.value(now));
  const uint8_t nvis = count_visible(*menu_);
  const bool scrollbar = nvis > l.rows;
  const int16_t text_w = static_cast<int16_t>(scrollbar ? l.w - 3 : l.w);
  Rect bar = {0, bar_y, text_w, l.row_h};
  g.rect(bar, Color::White, true);
  char val[24];
  for (uint8_t r = 0; r < l.rows && scroll_ + r < nvis; ++r) {
    const MenuItem& it = menu_->items[nth_visible(*menu_, static_cast<uint8_t>(scroll_ + r))];
    const int16_t base = static_cast<int16_t>(l.title_h + (r + 1) * l.row_h - 3);
    g.text(2, base, it.label, Color::Invert, Align::Left);
    value_text(it, val, sizeof(val));
    if (val[0]) {
      char shown[28];
      const bool ed = editing_ && &it == &menu_->items[sel_];
      snprintf(shown, sizeof(shown), ed ? "[%s]" : "%s", val);
      g.text(static_cast<int16_t>(text_w - 2), base, shown, Color::Invert, Align::Right);
    }
  }
  if (scrollbar) {
    const int16_t track = static_cast<int16_t>(l.h - l.title_h);
    const int16_t thumb_h = static_cast<int16_t>(track * l.rows / nvis < 3 ? 3 : track * l.rows / nvis);
    const int16_t thumb_y = static_cast<int16_t>(l.title_h + (track - thumb_h) * scroll_ / (nvis - l.rows));
    Rect thumb = {static_cast<int16_t>(l.w - 2), thumb_y, 2, thumb_h};
    g.rect(thumb, Color::White, true);
  }
}

}  // namespace core0

#endif  // CORE0_FEAT_UI
