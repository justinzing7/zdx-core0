// ZDX CORE-0 — display: drawing interface, panel driver contract, render service.
//
// Screens draw through `Gfx` (a thin virtual interface). A project provides an
// adapter over its library of choice (U8g2, Adafruit_GFX, GxEPD2...) and a mock
// framebuffer backs the native build. Need something the interface lacks?
// Use gfx.native() for the backend object — but keep that inside the screen.
//
// The render service owns WHEN to draw: screens only invalidate.
#pragma once
#include "event.h"
#include "module.h"

namespace core0 {

enum class PanelKind : uint8_t { Oled, Eink, Lcd, Memory /* sharp memory LCD */ };

// How a screen wants to be refreshed (matters most for e-ink).
enum class RefreshClass : uint8_t {
  Live,     // redraw whenever dirty, capped at caps.min_frame_ms (menus, animation)
  Partial,  // e-ink partial refresh OK; full refresh forced every caps.full_every partials
  Full,     // always full refresh (e-ink static pages, clean ghosting)
  Static,   // draw once on enter, then only on explicit invalidate_all()
};

enum class Color : uint8_t { Black = 0, White = 1, Invert = 2, Gray1 = 3, Gray2 = 4 };

enum class Align : uint8_t { Left, Center, Right };

typedef const void* FontRef;  // backend font handle (e.g. const uint8_t* for U8g2)

// 1-bpp packed bitmap, row-major, MSB first (asset pipeline output, spec/modules/assets.md).
struct Bitmap {
  uint16_t       w, h;
  const uint8_t* data;
};

struct PanelCaps {
  int16_t   width, height;
  PanelKind kind;
  uint8_t   bpp;              // 1 = mono, 2/4 = gray
  uint16_t  min_frame_ms;     // frame cap (OLED 16-33, e-ink partial ~300, full ~1500+)
  bool      partial_refresh;  // supports partial window updates
  uint8_t   full_every;       // e-ink: force full refresh after N partials (0 = never)
  bool      has_brightness;   // OLED contrast / backlight
};

// Drawing surface. Coordinates in pixels, origin top-left.
class Gfx {
 public:
  virtual ~Gfx() {}
  virtual int16_t width() const = 0;
  virtual int16_t height() const = 0;
  virtual void    clear(Color c = Color::Black) = 0;
  virtual void    pixel(int16_t x, int16_t y, Color c) = 0;
  virtual void    line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, Color c) = 0;
  virtual void    rect(const Rect& r, Color c, bool filled) = 0;
  virtual void    round_rect(const Rect& r, uint8_t radius, Color c, bool filled) = 0;
  virtual void    circle(int16_t cx, int16_t cy, int16_t r, Color c, bool filled) = 0;
  virtual void    bitmap(int16_t x, int16_t y, const Bitmap& bm, Color c) = 0;
  virtual void    set_font(FontRef font) = 0;
  // Draws with baseline at y. Returns advance width.
  virtual int16_t text(int16_t x, int16_t y, const char* s, Color c, Align a = Align::Left) = 0;
  virtual int16_t text_width(const char* s) = 0;
  virtual int16_t font_height() = 0;
  virtual void    set_clip(const Rect* r) = 0;  // nullptr = no clip
  virtual void*   native() = 0;                 // escape hatch to the backend object
};

// Panel driver: lifecycle + pushing the framebuffer to glass.
class Panel {
 public:
  virtual ~Panel() {}
  virtual Status    begin() = 0;
  virtual PanelCaps caps() const = 0;
  virtual Gfx&      gfx() = 0;
  // Push `dirty` (or whole screen if nullptr). `full` requests a full e-ink refresh.
  // Must not block for e-ink refresh time: start it and report busy().
  virtual Status    flush(const Rect* dirty, bool full) = 0;
  virtual bool      busy() = 0;
  virtual void      set_power(bool on) = 0;           // panel sleep / charge pump off
  virtual void      set_brightness(uint8_t level) = 0;  // 0..255, ignored if unsupported
};

// Event codes for EventType::Display.
enum class DisplayCode : uint8_t {
  FrameDone,     // flush completed, data = frame time ms
  PanelError,
};

namespace display {

Status attach(Panel& panel);
Panel* panel();

void invalidate(const Rect& r);
void invalidate_all();
void force_full_refresh();   // next flush is a full refresh (e-ink)

void set_brightness(uint8_t level);  // active brightness; Dim state uses dim_level
void set_dim_level(uint8_t level);

// The render service calls this to get pixels: wired to ui::draw by default.
typedef void (*DrawFn)(Gfx& g, const Rect& dirty);
void set_draw_fn(DrawFn fn);

uint16_t last_frame_ms();
extern const Module kModule;   // reacts to PowerState: Dim -> dim, Idle -> off/static

}  // namespace display
}  // namespace core0
