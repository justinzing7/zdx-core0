// mock_panel.h — MockGfx (128x64 1-bpp framebuffer, fixed font metrics) + MockPanel.
// Header-only; include from exactly one test_main.cpp per test program.
#pragma once
#include <stdio.h>
#include <string.h>
#include <core0/core0.h>

class MockGfx : public core0::Gfx {
 public:
  enum { W = 128, H = 64 };
  uint8_t fb[W * H];
  core0::FontRef font;
  int text_calls;
  char last_text[64];
  bool clipped;
  core0::Rect clip;
  // Fixed metrics: every font is 6x12, except `big_font` (if set by a product test) at 10x16.
  core0::FontRef big_font = nullptr;

  MockGfx() { reset(); }
  void reset() {
    memset(fb, 0, sizeof(fb));
    font = nullptr;
    text_calls = 0;
    last_text[0] = 0;
    clipped = false;
  }

  int16_t width() const override { return W; }
  int16_t height() const override { return H; }
  void clear(core0::Color c) override { memset(fb, c == core0::Color::White ? 1 : 0, sizeof(fb)); }
  void pixel(int16_t x, int16_t y, core0::Color c) override {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    if (clipped && (x < clip.x || y < clip.y || x >= clip.x + clip.w || y >= clip.y + clip.h)) return;
    uint8_t& p = fb[y * W + x];
    if (c == core0::Color::Invert) p ^= 1; else p = (c == core0::Color::Black) ? 0 : 1;
  }
  void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, core0::Color c) override {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
      pixel(x0, y0, c);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }
  void rect(const core0::Rect& r, core0::Color c, bool filled) override {
    for (int y = r.y; y < r.y + r.h; ++y)
      for (int x = r.x; x < r.x + r.w; ++x)
        if (filled || y == r.y || y == r.y + r.h - 1 || x == r.x || x == r.x + r.w - 1) pixel(x, y, c);
  }
  void round_rect(const core0::Rect& r, uint8_t, core0::Color c, bool filled) override { rect(r, c, filled); }
  void circle(int16_t cx, int16_t cy, int16_t rad, core0::Color c, bool filled) override {
    for (int y = -rad; y <= rad; ++y)
      for (int x = -rad; x <= rad; ++x) {
        int d = x * x + y * y;
        if (d <= rad * rad && (filled || d >= (rad - 1) * (rad - 1))) pixel(cx + x, cy + y, c);
      }
  }
  void bitmap(int16_t x, int16_t y, const core0::Bitmap& bm, core0::Color c) override {
    const int stride = (bm.w + 7) / 8;
    for (int j = 0; j < bm.h; ++j)
      for (int i = 0; i < bm.w; ++i)
        if (bm.data[j * stride + i / 8] & (0x80 >> (i & 7))) pixel(x + i, y + j, c);
  }
  void set_font(core0::FontRef f) override { font = f; }
  bool    is_big() const { return big_font != nullptr && font == big_font; }
  int16_t char_w() const { return is_big() ? 10 : 6; }
  int16_t text(int16_t x, int16_t y, const char* s, core0::Color c, core0::Align a) override {
    ++text_calls;
    strncpy(last_text, s, sizeof(last_text) - 1);
    last_text[sizeof(last_text) - 1] = 0;
    const int16_t w = text_width(s);
    if (a == core0::Align::Center) x -= w / 2;
    else if (a == core0::Align::Right) x -= w;
    // Represent glyphs as a 1-px underline so draws are visible in framebuffer tests.
    for (int16_t i = 0; i < w; ++i) pixel(x + i, y, c);
    return w;
  }
  int16_t text_width(const char* s) override { return static_cast<int16_t>(strlen(s) * char_w()); }
  int16_t font_height() override { return is_big() ? 16 : 12; }
  void set_clip(const core0::Rect* r) override {
    clipped = r != nullptr;
    if (r) clip = *r;
  }
  void* native() override { return this; }

  int count_set() const {
    int n = 0;
    for (int i = 0; i < W * H; ++i) n += fb[i];
    return n;
  }
  bool dump_pbm(const char* path) const {
    FILE* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "P1\n%d %d\n", W, H);
    for (int y = 0; y < H; ++y) {
      for (int x = 0; x < W; ++x) fputc(fb[y * W + x] ? '1' : '0', f);
      fputc('\n', f);
    }
    fclose(f);
    return true;
  }
};

class MockPanel : public core0::Panel {
 public:
  MockGfx g;
  core0::Status begin_result;
  bool is_busy;
  int begin_calls, flush_calls, full_flushes, power_calls, brightness_calls;
  bool power_on;
  uint8_t brightness;
  core0::Rect last_dirty;
  bool last_dirty_null;

  MockPanel() { reset(); }
  void reset() {
    g.reset();
    begin_result = core0::Status::Ok;
    is_busy = false;
    begin_calls = flush_calls = full_flushes = power_calls = brightness_calls = 0;
    power_on = true;
    brightness = 255;
    last_dirty_null = true;
  }

  core0::Status begin() override { ++begin_calls; return begin_result; }
  core0::PanelCaps caps() const override {
    core0::PanelCaps c = {128, 64, core0::PanelKind::Oled, 1, 16, true, 0, true};
    return c;
  }
  core0::Gfx& gfx() override { return g; }
  core0::Status flush(const core0::Rect* dirty, bool full) override {
    ++flush_calls;
    if (full) ++full_flushes;
    last_dirty_null = dirty == nullptr;
    if (dirty) last_dirty = *dirty;
    return core0::Status::Ok;
  }
  bool busy() override { return is_busy; }
  void set_power(bool on) override { ++power_calls; power_on = on; }
  void set_brightness(uint8_t level) override { ++brightness_calls; brightness = level; }
};
