// mock_net.h — MockNet: a scripted NetDriver for the net service tests (spec/modules/storage-net.md).
// connect() moves the state to Connecting; the test moves it on (Up, Error) with `st`.
// disconnect() goes straight to Off unless off_on_disconnect is false (a slow radio shutdown).
#pragma once
#include <stdio.h>
#include <core0/core0.h>

class MockNet : public core0::NetDriver {
 public:
  int              begins, connects, disconnects;
  core0::NetState  st;
  int8_t           rssi_;
  bool             off_on_disconnect;
  const char*      desc;

  MockNet() { reset(); }
  void reset() {
    begins = connects = disconnects = 0;
    st = core0::NetState::Off;
    rssi_ = 0;
    off_on_disconnect = true;
    desc = "mock";
  }
  core0::Status begin() override {
    ++begins;
    return core0::Status::Ok;
  }
  core0::Status connect() override {
    ++connects;
    st = core0::NetState::Connecting;
    return core0::Status::Ok;
  }
  void disconnect() override {
    ++disconnects;
    if (off_on_disconnect) st = core0::NetState::Off;
  }
  core0::NetState state() override { return st; }
  int8_t          rssi() override { return rssi_; }
  void            describe(char* buf, uint8_t n) override {
    if (n) snprintf(buf, n, "%s", desc);
  }
};
