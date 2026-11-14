// ZDX CORE-0 — shared primitive types and wrap-safe time helpers.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "config.h"

namespace core0 {

// Milliseconds from hal::millis(). Wraps every ~49.7 days: ALWAYS compare
// with the helpers below, never with `now > deadline`.
typedef uint32_t ms_t;

// True once `now` has reached or passed `deadline` (wrap-safe for spans < 24.8 days).
inline bool time_reached(ms_t now, ms_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

// Elapsed ms since `since` (wrap-safe).
inline ms_t time_since(ms_t now, ms_t since) { return now - since; }

// Signed ms until `deadline` (negative = overdue).
inline int32_t time_until(ms_t now, ms_t deadline) {
  return static_cast<int32_t>(deadline - now);
}

// Common return code. Functions that can fail return Status, never bool+errno.
enum class Status : uint8_t {
  Ok = 0,
  Busy,         // try again later (resource in use)
  Timeout,
  NotFound,
  NoSpace,      // static pool / queue full
  Invalid,      // bad argument
  Unsupported,  // feature compiled out or hardware absent
  IoError,      // bus/driver failure
  Corrupt,      // CRC / version mismatch
};

inline bool ok(Status s) { return s == Status::Ok; }

// Screen-space rectangle (display & UI).
struct Rect {
  int16_t x, y, w, h;
};

inline Rect rect_union(const Rect& a, const Rect& b) {
  if (a.w <= 0 || a.h <= 0) return b;
  if (b.w <= 0 || b.h <= 0) return a;
  const int16_t x0 = a.x < b.x ? a.x : b.x;
  const int16_t y0 = a.y < b.y ? a.y : b.y;
  const int16_t x1 = (a.x + a.w) > (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
  const int16_t y1 = (a.y + a.h) > (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
  Rect r = {x0, y0, static_cast<int16_t>(x1 - x0), static_cast<int16_t>(y1 - y0)};
  return r;
}

// Compile-time array length.
template <typename T, size_t N>
constexpr size_t count_of(const T (&)[N]) { return N; }

}  // namespace core0
