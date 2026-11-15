// ZDX CORE-0 — connectivity slot (net.h, spec/modules/storage-net.md "Connectivity").
//
// The radio is off unless a user holds it. The first acquire() starts the connection and the last
// release() turns the radio off. tick() polls the driver (state() is a cached value, not a bus
// transaction) and posts StateChanged to everyone (data = kAllUsers) on each change. A user whose
// connect deadline passes before Up gets its own StateChanged(Error) (data = its user id). It keeps
// its slot until it calls release(), so the caller always decides when the radio goes off.
// Power contract: can_enter vetoes Sleep and below while any user holds the radio, or while the
// driver has not reached Off yet (a radio takes a moment to stop). No suspend or resume work:
// Sleep is never entered with the radio on.
// CLI view: `net`.
#include "config.h"
#if CORE0_FEAT_NET  // feature off = zero code (constitution IX)

#include "net.h"

#include <stdio.h>
#include <string.h>

#include "log.h"
#include "port.h"
#if CORE0_FEAT_CLI
#  include "cli.h"
#endif

namespace core0 {
namespace net {
namespace {

struct User {
  const char* who;
  ms_t        deadline;
  bool        active;
  bool        expired;  // its Error has been posted
};

const ms_t kPollMs = 100;  // driver poll while the radio is in use (the driver also wakes the loop)

NetDriver* drv = nullptr;
User       slots[CORE0_NET_MAX_USERS];
uint8_t    count = 0;
NetState   last = NetState::Off;

void post(NetState s, uint32_t data) {
  bus::post(make_event(EventType::Net, static_cast<uint8_t>(NetCode::StateChanged), static_cast<uint16_t>(s), data));
}

NetState driver_state() { return drv ? drv->state() : NetState::Off; }

#if CORE0_FEAT_CLI
bool cli_added = false;

void cmd_net(uint8_t, char**) {
  char who[48];
  size_t n = 0;
  who[0] = 0;
  for (uint8_t i = 0; i < CORE0_NET_MAX_USERS; ++i) {
    if (!slots[i].active || n >= sizeof(who) - 1) continue;
    const int w = snprintf(who + n, sizeof(who) - n, "%s%s", n ? "," : "", slots[i].who);
    if (w > 0) n += static_cast<size_t>(w);
  }
  char detail[48];
  detail[0] = 0;
  if (drv) drv->describe(detail, sizeof(detail));
  cli::print("net %s  users %u (%s)  rssi %d  %s", state_name(last), static_cast<unsigned>(count), who,
             (drv && last == NetState::Up) ? static_cast<int>(drv->rssi()) : 0, detail);
}
const CliCommand kCmdNet = {"net", "net: radio state, users, driver detail", cmd_net};
#endif

Status init() {
  if (!drv) {
    C0_LOGE("net", "init failed: no driver attached");
    return Status::Invalid;
  }
#if CORE0_FEAT_CLI
  if (!cli_added) cli_added = ok(cli::add(kCmdNet));
#endif
  const Status s = drv->begin();
  if (!ok(s)) {
    C0_LOGE("net", "init failed: driver begin (%u)", static_cast<unsigned>(s));
    return s;
  }
  last = driver_state();
  char d[48];
  d[0] = 0;
  drv->describe(d, sizeof(d));
  C0_LOGI("net", "init ok (driver %s)", d[0] ? d : "?");
  return Status::Ok;
}

void tick(ms_t now) {
  const NetState s = driver_state();
  if (s != last) {
    last = s;
    C0_LOGI("net", "%s", state_name(s));
    post(s, kAllUsers);
  }
  if (s == NetState::Up) return;
  for (uint8_t i = 0; i < CORE0_NET_MAX_USERS; ++i) {
    User& u = slots[i];
    if (!u.active || u.expired || !time_reached(now, u.deadline)) continue;
    u.expired = true;
    C0_LOGW("net", "%s: not connected in time", u.who);
    post(NetState::Error, i);
  }
}

ms_t next_deadline(ms_t now) { return now + (count ? kPollMs : 60000); }

bool can_enter(PowerState target) {
  if (target < PowerState::Sleep) return true;
  return count == 0 && driver_state() == NetState::Off;
}

}  // namespace

Status attach(NetDriver& d) {
  drv = &d;
  return Status::Ok;
}

NetUser acquire(const char* who, ms_t timeout_ms) {
  if (!drv) return -1;
  for (uint8_t i = 0; i < CORE0_NET_MAX_USERS; ++i) {
    User& u = slots[i];
    if (u.active) continue;
    u.who = who ? who : "?";
    u.deadline = hal::millis() + timeout_ms;
    u.active = true;
    u.expired = false;
    ++count;
    if (count == 1) {
      const Status s = drv->connect();
      if (!ok(s)) C0_LOGW("net", "connect failed (%u)", static_cast<unsigned>(s));
    } else if (last == NetState::Up) {
      post(NetState::Up, i);  // a newcomer still hears Up
    }
    return static_cast<NetUser>(i);
  }
  C0_LOGW("net", "%s: no free user slot (max %u)", who ? who : "?", static_cast<unsigned>(CORE0_NET_MAX_USERS));
  return -1;
}

void release(NetUser& u) {
  if (u < 0 || u >= static_cast<NetUser>(CORE0_NET_MAX_USERS) || !slots[u].active) {
    u = -1;
    return;
  }
  slots[u].active = false;
  u = -1;
  if (--count == 0 && drv) drv->disconnect();
}

NetState state() { return last; }

uint8_t users() { return count; }

const char* state_name(NetState s) {
  switch (s) {
    case NetState::Off: return "Off";
    case NetState::Connecting: return "Connecting";
    case NetState::Up: return "Up";
    case NetState::Error: return "Error";
  }
  return "?";
}

const Module kModule = {"net", init, tick, next_deadline, can_enter, nullptr, nullptr};

}  // namespace net

#if defined(CORE0_NATIVE)
namespace test {
void reset_net() {
  net::drv = nullptr;
  memset(net::slots, 0, sizeof(net::slots));
  net::count = 0;
  net::last = NetState::Off;
#if CORE0_FEAT_CLI
  net::cli_added = false;
#endif
}
}  // namespace test
#endif

}  // namespace core0

#endif  // CORE0_FEAT_NET
