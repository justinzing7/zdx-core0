// mock_led.h — MockLed records every colour written.
#pragma once
#include <core0/core0.h>

class MockLed : public core0::LedDriver {
 public:
  enum { kMaxHist = 256 };
  core0::Rgb last;
  core0::Rgb hist[kMaxHist];
  core0::ms_t hist_at[kMaxHist];
  int n;
  bool powered;

  MockLed() { reset(); }
  void reset() { last.r = last.g = last.b = 0; n = 0; powered = true; }
  core0::Status begin() override { return core0::Status::Ok; }
  void set(const core0::Rgb& c) override {
    last = c;
    if (n < kMaxHist) { hist[n] = c; hist_at[n] = core0::hal::millis(); ++n; }
  }
  void set_power(bool on) override { powered = on; }
};
