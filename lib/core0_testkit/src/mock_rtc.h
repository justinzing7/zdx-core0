// mock_rtc.h — MockRtc: a settable RTC whose time advances with the native clock.
#pragma once
#include <core0/core0.h>

class MockRtc : public core0::RtcSource {
 public:
  core0::epoch_t base;
  core0::ms_t base_ms;
  bool lost, fail;

  MockRtc() { reset(); }
  void reset() { base = 0; base_ms = 0; lost = false; fail = false; }
  core0::Status begin() override { return fail ? core0::Status::IoError : core0::Status::Ok; }
  core0::Status read(core0::epoch_t& out) override {
    if (fail) return core0::Status::IoError;
    out = base + (core0::hal::millis() - base_ms) / 1000;
    return core0::Status::Ok;
  }
  core0::Status write(core0::epoch_t t) override {
    base = t;
    base_ms = core0::hal::millis();
    lost = false;
    return core0::Status::Ok;
  }
  bool lost_power() override { return lost; }
};
