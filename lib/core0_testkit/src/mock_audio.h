// mock_audio.h — MockAudioOut / MockAudioIn for native tests.
#pragma once
#include <string.h>
#include <core0/core0.h>

class MockAudioOut : public core0::AudioOut {
 public:
  enum { kMaxLog = 64 };
  struct Call { char what; uint32_t value; core0::ms_t at; };  // what: 't' tone, 'p' power, 'v' volume
  Call log[kMaxLog];
  int n;
  uint16_t freq;
  bool powered, streaming;
  uint8_t vol;
  uint32_t stream_rate;

  MockAudioOut() { reset(); }
  void reset() { n = 0; freq = 0; powered = false; streaming = false; vol = 0; stream_rate = 0; }
  void record(char w, uint32_t v) {
    if (n < kMaxLog) { log[n].what = w; log[n].value = v; log[n].at = core0::hal::millis(); ++n; }
  }
  core0::Status begin() override { return core0::Status::Ok; }
  void tone(uint16_t f) override { freq = f; record('t', f); }
  bool can_stream() const override { return true; }
  core0::Status stream_start(uint32_t rate) override { streaming = true; stream_rate = rate; return core0::Status::Ok; }
  void stream_stop() override { streaming = false; }
  void set_volume(uint8_t v) override { vol = v; record('v', v); }
  void set_power(bool on) override { powered = on; record('p', on ? 1 : 0); }
  int count(char w) const { int c = 0; for (int i = 0; i < n; ++i) c += log[i].what == w; return c; }
};

class MockAudioIn : public core0::AudioIn {
 public:
  enum { kMax = 4096 };
  int16_t script[kMax];
  size_t script_len, pos;
  bool started, powered;
  uint32_t rate;

  MockAudioIn() { reset(); }
  void reset() { script_len = pos = 0; started = powered = false; rate = 0; }
  void load(const int16_t* s, size_t n) {
    if (n > kMax) n = kMax;
    memcpy(script, s, n * sizeof(int16_t));
    script_len = n;
    pos = 0;
  }
  core0::Status begin(uint32_t r) override { rate = r; return core0::Status::Ok; }
  core0::Status start() override { started = true; return core0::Status::Ok; }
  void stop() override { started = false; }
  size_t read(int16_t* dst, size_t n) override {
    if (!started || script_len == 0) return 0;
    for (size_t i = 0; i < n; ++i) { dst[i] = script[pos]; pos = (pos + 1) % script_len; }
    return n;
  }
  void set_power(bool on) override { powered = on; }
};
