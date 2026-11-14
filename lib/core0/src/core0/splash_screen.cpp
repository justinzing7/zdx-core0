// ZDX CORE-0 — boot splash (anim.h SplashScreen, spec/modules/ui.md "Splash").
// Holds >= min_ms, then replaces itself with `next` once SystemCode::InitDone was seen, or at
// max_ms regardless (logged W). Keys never cut the brand moment short; after min_ms with init
// done the splash leaves on its own, so input is simply consumed.
#include "config.h"
#if CORE0_FEAT_UI  // feature off = zero code (constitution IX)

#include "anim.h"

#include "log.h"
#include "port.h"

namespace core0 {

void SplashScreen::on_enter(const void*) {
  start_ = hal::millis();
  init_done_ = false;
  ui::forward(EventType::System);
  if (cfg_.animation && cfg_.animation->count) {
    // Centre in the panel area above the caption band (1/5 of the height); sizes come from
    // the attached panel, never from board constants.
    int16_t w = 0, h = 0;
    if (Panel* p = display::panel()) {
      const PanelCaps c = p->caps();
      w = c.width;
      h = static_cast<int16_t>(cfg_.caption ? c.height - c.height / 5 : c.height);
    }
    const Bitmap* b = cfg_.animation->frames[0].bitmap;
    anim_.start(*cfg_.animation, static_cast<int16_t>((w - b->w) / 2), static_cast<int16_t>((h - b->h) / 2), start_);
  }
}

bool SplashScreen::on_event(const Event& e) {
  if (e.type == EventType::System && e.code == static_cast<uint8_t>(SystemCode::InitDone)) {
    init_done_ = true;
    return true;
  }
  return e.type == EventType::Input;  // consume keys (including Back) while the splash shows
}

void SplashScreen::tick(ms_t now) {
  if (anim_.running() && anim_.update(now)) display::invalidate(anim_.bounds());
  const ms_t el = time_since(now, start_);
  const bool done = (init_done_ && el >= cfg_.min_ms) || el >= cfg_.max_ms;
  if (!done || !cfg_.next) return;
  if (!init_done_) C0_LOGW("ui", "splash: init not done after %u ms, continuing", static_cast<unsigned>(cfg_.max_ms));
  else C0_LOGI("ui", "splash -> %s after %lu ms", cfg_.next->name(), static_cast<unsigned long>(el));
  ui::replace(*cfg_.next, nullptr, Transition::None);
}

void SplashScreen::draw(Gfx& g, const Rect&) {
  const int16_t w = g.width(), h = g.height();
  bool art = false;
  if (anim_.running() || (cfg_.animation && cfg_.animation->count)) {
    anim_.draw(g);
    art = true;
  } else if (cfg_.logo) {
    const int16_t art_h = static_cast<int16_t>(cfg_.caption ? h - h / 5 : h);  // same band as the animation
    g.bitmap(static_cast<int16_t>((w - cfg_.logo->w) / 2), static_cast<int16_t>((art_h - cfg_.logo->h) / 2), *cfg_.logo,
             Color::White);
    art = true;
  }
  if (cfg_.caption) {
    g.set_font(nullptr);
    if (art) {
      g.text(static_cast<int16_t>(w / 2), static_cast<int16_t>(h - 2), cfg_.caption, Color::White, Align::Center);
    } else {
      // No art: a framed wordmark.
      const int16_t tw = g.text_width(cfg_.caption);
      const int16_t fh = g.font_height();
      Rect box = {static_cast<int16_t>((w - tw) / 2 - 6), static_cast<int16_t>(h / 2 - fh), static_cast<int16_t>(tw + 12),
                  static_cast<int16_t>(fh * 2)};
      g.round_rect(box, 3, Color::White, false);
      g.text(static_cast<int16_t>(w / 2), static_cast<int16_t>(h / 2 + fh / 2 - 1), cfg_.caption, Color::White, Align::Center);
    }
  }
}

// The loop may sleep until this deadline, so it must cover the min/max hold and the next frame.
ms_t SplashScreen::next_deadline(ms_t now) {
  (void)now;
  ms_t best = start_ + (init_done_ ? cfg_.min_ms : cfg_.max_ms);
  if (anim_.running()) {
    const ms_t f = anim_.next_deadline();
    if (time_until(best, f) < 0) best = f;
  }
  return best;
}

}  // namespace core0

#endif  // CORE0_FEAT_UI
