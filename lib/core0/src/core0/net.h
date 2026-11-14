// ZDX CORE-0 — optional connectivity slot (Wi-Fi / BLE).
//
// Radios are the largest power consumer on these devices, so the contract is
// reference-counted on-demand: a feature calls acquire(), waits for Up,
// does its work, calls release(). With zero users the radio is OFF.
// Typical uses: NTP sync (clock.h), weather fetch, file transfer, OTA.
#pragma once
#include "event.h"
#include "module.h"

namespace core0 {

enum class NetState : uint8_t { Off, Connecting, Up, Error };

// Event codes for EventType::Net.  arg = NetState
enum class NetCode : uint8_t { StateChanged, TimeSynced };

class NetDriver {
 public:
  virtual ~NetDriver() {}
  virtual Status   begin() = 0;
  virtual Status   connect() = 0;      // non-blocking start
  virtual void     disconnect() = 0;   // radio fully off
  virtual NetState state() = 0;
  virtual int8_t   rssi() = 0;
};

namespace net {

typedef int8_t NetUser;

Status   attach(NetDriver& d);
NetUser  acquire(const char* who, ms_t timeout_ms);  // posts StateChanged when Up / Error
void     release(NetUser& u);
NetState state();

extern const Module kModule;   // vetoes Sleep while a user holds the radio

}  // namespace net
}  // namespace core0
