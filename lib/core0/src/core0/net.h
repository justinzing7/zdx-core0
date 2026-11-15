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

// Event codes for EventType::Net.
//   StateChanged: arg = NetState, data = the user id it is meant for, or kAllUsers for a change of
//                 the radio itself. A user whose timeout passed before Up gets Error with its id.
//   TimeSynced:   posted by whoever syncs the clock over the network (arg = product result code).
enum class NetCode : uint8_t { StateChanged, TimeSynced };

class NetDriver {
 public:
  virtual ~NetDriver() {}
  virtual Status   begin() = 0;
  virtual Status   connect() = 0;      // non-blocking start
  virtual void     disconnect() = 0;   // radio fully off
  virtual NetState state() = 0;
  virtual int8_t   rssi() = 0;
  virtual void     describe(char* buf, uint8_t n) { if (n) buf[0] = 0; }  // optional detail for CLI `net`
};

namespace net {

typedef int8_t NetUser;              // -1 = none
const uint32_t kAllUsers = 0xFF;     // StateChanged data for a change of the radio itself

Status   attach(NetDriver& d);
// First user starts the radio. Returns -1 when all CORE0_NET_MAX_USERS slots are taken.
// `who` must outlive the hold (a string literal). A newcomer while Up hears StateChanged(Up).
NetUser  acquire(const char* who, ms_t timeout_ms);
void     release(NetUser& u);        // sets u = -1; the last release turns the radio off
NetState state();
uint8_t  users();
const char* state_name(NetState s);

extern const Module kModule;   // vetoes Sleep while a user holds the radio

}  // namespace net
}  // namespace core0
