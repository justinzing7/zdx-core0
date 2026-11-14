// ZDX CORE-0 — animation basics: easing, tweens, frame sequences, splash.
//
// Everything is time-based (ms since start), never frame-count based, so an
// animation looks the same at 20 fps on e-ink-adjacent hardware or 60 fps OLED.
// Integer/fixed-point only: no float required (ATmega/SAMD21 friendly).
#pragma once
#include "ui.h"

namespace core0 {

// Progress in Q8 fixed point: 0..256 == 0.0..1.0
typedef uint16_t q8_t;
static const q8_t kQ8One = 256;

enum class Ease : uint8_t { Linear, InQuad, OutQuad, InOutQuad, OutCubic, OutBack };

inline q8_t ease(Ease e, q8_t t) {
  if (t >= kQ8One) return kQ8One;
  const uint32_t x = t;
  switch (e) {
    case Ease::Linear:    return t;
    case Ease::InQuad:    return static_cast<q8_t>((x * x) >> 8);
    case Ease::OutQuad:   return static_cast<q8_t>(kQ8One - (((kQ8One - x) * (kQ8One - x)) >> 8));
    case Ease::InOutQuad: return x < 128 ? static_cast<q8_t>((2 * x * x) >> 8)
                                         : static_cast<q8_t>(kQ8One - ((2 * (kQ8One - x) * (kQ8One - x)) >> 8));
    case Ease::OutCubic: {
      const uint32_t r = kQ8One - x;
      return static_cast<q8_t>(kQ8One - ((r * r * r) >> 16));
    }
    case Ease::OutBack: {  // slight overshoot (~10%), clamped to int range
      const int32_t s = static_cast<int32_t>(x) - static_cast<int32_t>(kQ8One);
      const int32_t v = static_cast<int32_t>(kQ8One) + ((s * s * s) >> 16) * 27 / 10 + ((s * s) >> 8) * 17 / 10;
      return static_cast<q8_t>(v < 0 ? 0 : v);
    }
  }
  return t;
}

// Interpolate a -> b with progress p (Q8).
inline int16_t lerp_q8(int16_t a, int16_t b, q8_t p) {
  return static_cast<int16_t>(a + ((static_cast<int32_t>(b - a) * p) >> 8));
}

// A single value moving from->to over duration with easing.
struct Tween {
  int16_t from, to;
  ms_t    start;
  uint16_t duration_ms;
  Ease    curve;

  void begin(int16_t f, int16_t t, uint16_t dur, Ease e, ms_t now) {
    from = f; to = t; duration_ms = dur; curve = e; start = now;
  }
  bool done(ms_t now) const { return time_since(now, start) >= duration_ms; }
  int16_t value(ms_t now) const {
    if (duration_ms == 0 || done(now)) return to;
    const q8_t p = static_cast<q8_t>((time_since(now, start) * kQ8One) / duration_ms);
    return lerp_q8(from, to, ease(curve, p));
  }
};

// Flip-book animation from bitmap frames (asset pipeline output).
struct AnimFrame {
  const Bitmap* bitmap;
  uint16_t      hold_ms;
};

struct Animation {
  const AnimFrame* frames;
  uint8_t          count;
  bool             loop;
};

class Animator {
 public:
  Animator() : anim_(nullptr), x_(0), y_(0), frame_(0), frame_start_(0), running_(false) {}
  void start(const Animation& a, int16_t x, int16_t y, ms_t now) {
    anim_ = &a; x_ = x; y_ = y; frame_ = 0; frame_start_ = now; running_ = true;
  }
  void stop() { running_ = false; }
  bool running() const { return running_; }

  // Advance; returns true if the visible frame changed (caller invalidates bounds()).
  bool update(ms_t now) {
    if (!running_ || !anim_ || anim_->count == 0) return false;
    bool changed = false;
    for (;;) {
      const uint16_t hold = anim_->frames[frame_].hold_ms ? anim_->frames[frame_].hold_ms : 1;
      if (time_since(now, frame_start_) < hold) break;
      frame_start_ += hold;
      if (frame_ + 1 >= anim_->count) {
        if (!anim_->loop) { running_ = false; return changed; }
        frame_ = 0;
      } else {
        ++frame_;
      }
      changed = true;
    }
    return changed;
  }
  void draw(Gfx& g, Color c = Color::White) const {
    if (anim_ && anim_->count) g.bitmap(x_, y_, *anim_->frames[frame_].bitmap, c);
  }
  Rect bounds() const {
    Rect r = {x_, y_, 0, 0};
    if (anim_ && anim_->count) {
      r.w = static_cast<int16_t>(anim_->frames[frame_].bitmap->w);
      r.h = static_cast<int16_t>(anim_->frames[frame_].bitmap->h);
    }
    return r;
  }
  ms_t next_deadline() const {
    return running_ ? frame_start_ + anim_->frames[frame_].hold_ms : frame_start_ + 60000;
  }

 private:
  const Animation* anim_;
  int16_t          x_, y_;
  uint8_t          frame_;
  ms_t             frame_start_;
  bool             running_;
};

// Boot splash: plays an animation (or static logo) for at least `min_ms`
// while modules finish bring-up, then replaces itself with `next`.
// Any key press skips once InitDone has been posted.
struct SplashConfig {
  const Animation* animation;   // may be nullptr (logo only)
  const Bitmap*    logo;        // may be nullptr
  const char*      caption;     // e.g. product name / version, may be nullptr
  uint16_t         min_ms;      // minimum display time (brand moment), e.g. 1200
  uint16_t         max_ms;      // give up waiting for InitDone, e.g. 5000
  Screen*          next;        // usually the home screen
};

class SplashScreen : public Screen {
 public:
  explicit SplashScreen(const SplashConfig& cfg) : cfg_(cfg), start_(0), init_done_(false) {}
  const char* name() const override { return "splash"; }
  void on_enter(const void* arg) override;
  bool on_event(const Event& e) override;
  void tick(ms_t now) override;
  void draw(Gfx& g, const Rect& dirty) override;
  ms_t next_deadline(ms_t now) override;  // min_ms / max_ms and the next animation frame
  RefreshClass refresh_class() const override { return RefreshClass::Live; }

 private:
  const SplashConfig& cfg_;
  Animator            anim_;
  ms_t                start_;
  bool                init_done_;
};

}  // namespace core0
