// ZDX CORE-0 — input service: debounce integrator + per-key gesture FSM
// (input.h, spec/modules/input.md).
//
// Raw edges arrive via feed_key() (lock-free ring; one producer context). tick() applies an
// integrator debounce: a level change is accepted on the first sample taken >= debounce_ms
// after the last raw edge. Accepted edges drive the gesture FSM, which posts EventType::Input.
#include "config.h"
#if CORE0_FEAT_INPUT  // feature off = zero code (constitution IX)

#include "input.h"

#include <string.h>

#include "log.h"
#include "ring.h"
#if CORE0_FEAT_POWER
#  include "power.h"
#endif

namespace core0 {
namespace input {
namespace {

const ms_t kComboWindowMs = 100;
const ms_t kIdleDeadlineMs = 60000;

struct RawKey {
  uint8_t key;
  uint8_t pressed;
  ms_t    at;
};

enum class KeySt : uint8_t { Up, Down, LongFired };

struct KeyState {
  KeyConfig cfg;
  bool      raw;           // last raw level fed (true = pressed)
  ms_t      raw_at;        // time of last raw change
  bool      stable;        // debounced level
  KeySt     st;
  ms_t      down_at;
  ms_t      next_repeat;
  uint32_t  repeats;
  bool      combo;         // part of a Combo: suppress Click/LongPress/Repeat
  bool      click_pending; // double-click keys: Click deferred
  ms_t      click_deadline;
};

KeyState    keys[CORE0_MAX_KEYS];
uint8_t     nkeys = 0;
InputTiming timing = kDefaultInputTiming;
SpscRing<RawKey, 32> raw_ring;
bool        swallowing = false;
ms_t        swallow_busy_at = 0;  // last time a key was seen down or unsettled while swallowing

int index_of(Key k) {
  for (uint8_t i = 0; i < nkeys; ++i)
    if (keys[i].cfg.key == k) return i;
  return -1;
}

void emit(InputCode code, Key k, uint32_t data, ms_t now) {
  if (swallowing) return;
#if CORE0_FEAT_POWER
  power::activity(now);
#else
  (void)now;
#endif
  bus::post(make_event(EventType::Input, static_cast<uint8_t>(code), static_cast<uint16_t>(k), data));
}

bool any_down_or_unstable() {
  for (uint8_t i = 0; i < nkeys; ++i)
    if (keys[i].stable || keys[i].raw) return true;
  return false;
}

void on_press(KeyState& s, ms_t now) {
#if CORE0_FEAT_POWER
  // This press wakes the display from Dim/Idle: it must not also act (input.md).
  if (power::state() >= PowerState::Dim) {
    power::activity(now);
    swallow_until_release();
  }
#endif
  s.st = KeySt::Down;
  s.down_at = now;
  s.repeats = 0;
  s.next_repeat = now + timing.repeat_delay_ms;
  s.combo = false;
  if (s.click_pending && time_until(now, s.click_deadline) >= 0) {
    // Second press inside the double-click window.
    s.click_pending = false;
    s.combo = true;  // swallow this press's own Click/LongPress
    emit(InputCode::Press, s.cfg.key, 0, now);
    emit(InputCode::DoubleClick, s.cfg.key, 0, now);
    return;
  }
  emit(InputCode::Press, s.cfg.key, 0, now);
  // Combo: another key went down within the window.
  for (uint8_t i = 0; i < nkeys; ++i) {
    KeyState& o = keys[i];
    if (&o == &s || o.st == KeySt::Up || o.combo) continue;
    if (time_since(now, o.down_at) <= kComboWindowMs) {
      o.combo = s.combo = true;
      const uint16_t arg = static_cast<uint16_t>((static_cast<uint8_t>(o.cfg.key) << 8) | static_cast<uint8_t>(s.cfg.key));
      if (!swallowing) {
#if CORE0_FEAT_POWER
        power::activity(now);
#endif
        bus::post(make_event(EventType::Input, static_cast<uint8_t>(InputCode::Combo), arg, 0));
      }
      break;
    }
  }
}

void on_release(KeyState& s, ms_t now) {
  const ms_t held = time_since(now, s.down_at);
  const bool was_long = s.st == KeySt::LongFired;
  s.st = KeySt::Up;
  emit(InputCode::Release, s.cfg.key, held, now);
  // Click = released before long_ms (input.md). A long hold (LongPress or auto-repeat) never clicks.
  if (!was_long && !s.combo && held < timing.long_ms) {
    if (s.cfg.flags & kKeyDoubleClick) {
      s.click_pending = true;
      s.click_deadline = now + timing.double_ms;
    } else {
      emit(InputCode::Click, s.cfg.key, 0, now);
    }
  }
  s.combo = false;
}

Status init() {
  C0_LOGI("in", "init ok (%u keys, debounce %u ms)", static_cast<unsigned>(nkeys),
          static_cast<unsigned>(timing.debounce_ms));
  return Status::Ok;
}

void tick(ms_t now) {
  RawKey r;
  while (raw_ring.pop(r)) {
    const int i = index_of(static_cast<Key>(r.key));
    if (i < 0) continue;
    const bool p = r.pressed != 0;
    if (p != keys[i].raw) {
      keys[i].raw = p;
      keys[i].raw_at = r.at;
    }
  }
  for (uint8_t i = 0; i < nkeys; ++i) {
    KeyState& s = keys[i];
    if (s.raw != s.stable && time_since(now, s.raw_at) >= timing.debounce_ms) {
      s.stable = s.raw;
      if (s.stable) on_press(s, now);
      else on_release(s, now);
    }
    if (s.st != KeySt::Up && !s.combo) {
      if ((s.cfg.flags & kKeyLongPress) && s.st == KeySt::Down && time_since(now, s.down_at) >= timing.long_ms) {
        s.st = KeySt::LongFired;
        emit(InputCode::LongPress, s.cfg.key, 0, now);
      }
      if ((s.cfg.flags & kKeyRepeat) && time_reached(now, s.next_repeat)) {
        do { s.next_repeat += timing.repeat_rate_ms; } while (time_reached(now, s.next_repeat));
        ++s.repeats;
        emit(InputCode::Repeat, s.cfg.key, s.repeats, now);
      }
    }
    if (s.click_pending && time_until(now, s.click_deadline) < 0) {
      s.click_pending = false;
      emit(InputCode::Click, s.cfg.key, 0, now);
    }
  }
  // Ends only after every key has stayed up for a debounce time: right after a wake, the contact
  // that woke the device can still read high while it bounces.
  if (swallowing) {
    if (any_down_or_unstable()) swallow_busy_at = now;
    else if (time_since(now, swallow_busy_at) >= timing.debounce_ms) swallowing = false;
  }
}

ms_t next_deadline(ms_t now) {
  ms_t best = now + kIdleDeadlineMs;
  for (uint8_t i = 0; i < nkeys; ++i) {
    const KeyState& s = keys[i];
    ms_t d = best;
    if (s.raw != s.stable) d = s.raw_at + timing.debounce_ms;
    if (time_until(best, d) < 0) best = d;
    if (s.st != KeySt::Up && !s.combo) {
      if ((s.cfg.flags & kKeyLongPress) && s.st == KeySt::Down && time_until(best, s.down_at + timing.long_ms) < 0)
        best = s.down_at + timing.long_ms;
      if ((s.cfg.flags & kKeyRepeat) && time_until(best, s.next_repeat) < 0) best = s.next_repeat;
    }
    if (s.click_pending && time_until(best, s.click_deadline + 1) < 0) best = s.click_deadline + 1;
  }
  if (swallowing && time_until(best, swallow_busy_at + timing.debounce_ms) < 0)
    best = swallow_busy_at + timing.debounce_ms;
  return best;
}

void on_resume(PowerState from, uint32_t wake) {
  // The press that woke the device must not also act (input.md "Interaction with power").
  if (from >= PowerState::Dim && ((wake & hal::kWakeButton) || any_down_or_unstable())) swallow_until_release();
}

}  // namespace

Status configure(const KeyConfig* cfg, uint8_t count, const InputTiming& t) {
  if (count > CORE0_MAX_KEYS) return Status::NoSpace;
  memset(keys, 0, sizeof(keys));
  for (uint8_t i = 0; i < count; ++i) keys[i].cfg = cfg[i];
  nkeys = count;
  timing = t;
  return Status::Ok;
}

void feed_key(Key key, bool pressed, ms_t at) {
  RawKey r = {static_cast<uint8_t>(key), static_cast<uint8_t>(pressed ? 1 : 0), at};
  raw_ring.push(r);  // full ring: drop (the next poll re-feeds the level)
}

void feed_encoder(int8_t detents, ms_t at) {
  (void)detents;
  (void)at;  // CORE0_FEAT_ENCODER is off on this product
}

bool is_down(Key key) {
  const int i = index_of(key);
  return i >= 0 && keys[i].stable;
}

ms_t held_ms(Key key, ms_t now) {
  const int i = index_of(key);
  return (i >= 0 && keys[i].stable) ? time_since(now, keys[i].down_at) : 0;
}

void poll_gpio(ms_t now) {
  (void)now;  // CORE0_INPUT_POLL_GPIO=0: the board module feeds keys via feed_key()
}

void swallow_until_release() {
  swallowing = true;
  swallow_busy_at = hal::millis();
}

const Module kModule = {"input", init, tick, next_deadline, nullptr, nullptr, on_resume};

}  // namespace input

#if defined(CORE0_NATIVE)
namespace test {
void reset_input() {
  memset(input::keys, 0, sizeof(input::keys));
  input::nkeys = 0;
  input::timing = kDefaultInputTiming;
  input::RawKey r;
  while (input::raw_ring.pop(r)) {
  }
  input::swallowing = false;
  input::swallow_busy_at = 0;
}
}  // namespace test
#endif

}  // namespace core0

#endif  // CORE0_FEAT_INPUT
