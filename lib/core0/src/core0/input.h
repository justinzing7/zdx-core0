// ZDX CORE-0 — input service: debounce, gestures, encoder acceleration.
//
// Pipeline:  driver (GPIO poll / ISR / I2C expander / touch)
//              -> input::feed_key() / feed_encoder()        [raw, any context]
//              -> debounce + gesture state machine (tick)   [loop context]
//              -> EventType::Input events                    [bus]
//              -> ui routes to the top screen
//
// Physical buttons map to LOGICAL keys in board_config.h, so screens never
// know pin numbers and a 3-button board and a 5-way joystick share UI code.
#pragma once
#include "event.h"
#include "module.h"

namespace core0 {

enum class Key : uint8_t {
  Up, Down, Left, Right,
  Select,   // OK / encoder push
  Back,
  Menu,
  Power,
  Aux1, Aux2, Aux3,   // device-specific (record, play, snooze...)
  None = 0xFF,
};

// Event codes for EventType::Input.  arg = Key,  data as noted.
enum class InputCode : uint8_t {
  Press,        // edge, emitted immediately after debounce (lowest latency)
  Release,      // data = held ms
  Click,        // short press released (delayed by double_ms if double-click enabled for key)
  DoubleClick,
  LongPress,    // held >= long_ms, emitted once while still held
  Repeat,       // auto-repeat while held, data = repeat count
  Rotate,       // encoder: arg = Key::None, data = (int32_t) accelerated steps (+cw / -ccw)
  Combo,        // two keys held together, arg = (k1 << 8) | k2
};

enum KeyFlags : uint8_t {
  kKeyActiveLow   = 1u << 0,
  kKeyDoubleClick = 1u << 1,  // enable double-click (adds double_ms latency to Click)
  kKeyRepeat      = 1u << 2,  // enable auto-repeat
  kKeyLongPress   = 1u << 3,
  kKeyWake        = 1u << 4,  // configured as a wake source
};

struct KeyConfig {
  Key     key;
  uint8_t pin;     // 0xFF = not a GPIO (fed by an expander/touch driver)
  uint8_t flags;   // KeyFlags
};

struct InputTiming {
  uint8_t  debounce_ms;      // 5..30 typical (mechanical tact: 15-20)
  uint16_t long_ms;          // 500..800
  uint16_t repeat_delay_ms;  // first repeat
  uint16_t repeat_rate_ms;   // subsequent repeats
  uint16_t double_ms;        // double-click window
};

static const InputTiming kDefaultInputTiming = {15, 600, 400, 80, 250};

// Encoder acceleration ("inertial" scrolling): detent rate -> multiplier.
// Pick the first row whose max_interval_ms >= time since previous detent.
struct AccelStep {
  uint16_t max_interval_ms;
  uint8_t  multiplier;
};
static const AccelStep kDefaultAccel[] = {
  {12, 8}, {25, 4}, {50, 2}, {0xFFFF, 1},
};

namespace input {

Status configure(const KeyConfig* keys, uint8_t count, const InputTiming& timing = kDefaultInputTiming);

// Raw feeds — safe from ISRs (lock-free ring into the service).
void feed_key(Key key, bool pressed, ms_t at);
void feed_encoder(int8_t detents, ms_t at);

#if CORE0_FEAT_ENCODER
void set_acceleration(const AccelStep* table, uint8_t count);  // nullptr = off
#endif

bool is_down(Key key);
ms_t held_ms(Key key, ms_t now);

// Optional GPIO polling helper for simple boards (call from a driver tick or
// let the input module poll configured pins itself when CORE0_INPUT_POLL_GPIO=1).
void poll_gpio(ms_t now);

// Suppress all input events (e.g. while the display is waking, so the
// waking press does not also trigger an action). Cleared on next release.
void swallow_until_release();

extern const Module kModule;

}  // namespace input
}  // namespace core0
