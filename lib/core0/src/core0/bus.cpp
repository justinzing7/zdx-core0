// ZDX CORE-0 — event bus implementation (spec/modules/core.md "Event bus").
#include "event.h"

#include "fault.h"
#include "port.h"

namespace core0 {
namespace bus {
namespace {

static_assert((CORE0_EVENT_QUEUE_LEN & (CORE0_EVENT_QUEUE_LEN - 1)) == 0,
              "CORE0_EVENT_QUEUE_LEN must be a power of two");

struct Sub {
  EventType type;
  Handler   fn;
  void*     ctx;
};

Event             q[CORE0_EVENT_QUEUE_LEN];
volatile uint32_t head = 0;  // next write (producers, under critical section)
volatile uint32_t tail = 0;  // next read (loop only)
Sub               subs[CORE0_MAX_SUBSCRIBERS];
uint8_t           nsubs = 0;
volatile uint32_t dropped = 0;
volatile bool     drop_pending = false;

}  // namespace

Status subscribe(EventType type, Handler fn, void* ctx) {
  if (!fn) return Status::Invalid;
  if (nsubs >= CORE0_MAX_SUBSCRIBERS) return Status::NoSpace;
  subs[nsubs].type = type;
  subs[nsubs].fn = fn;
  subs[nsubs].ctx = ctx;
  ++nsubs;
  return Status::Ok;
}

Status post(const Event& e) {
  hal::CriticalSection cs;
  if (head - tail >= CORE0_EVENT_QUEUE_LEN) {
    ++dropped;
    drop_pending = true;
    return Status::NoSpace;
  }
  q[head & (CORE0_EVENT_QUEUE_LEN - 1)] = e;
  head = head + 1;
  return Status::Ok;
}

uint8_t dispatch(uint8_t budget) {
  // Snapshot: events posted by handlers during this pass wait for the next pass.
  uint32_t avail;
  {
    hal::CriticalSection cs;
    avail = head - tail;
  }
  if (avail > budget) avail = budget;
  for (uint32_t n = 0; n < avail; ++n) {
    Event e;
    {
      hal::CriticalSection cs;
      e = q[tail & (CORE0_EVENT_QUEUE_LEN - 1)];
      tail = tail + 1;
    }
    fault::note_event(e);
    for (uint8_t i = 0; i < nsubs; ++i) {
      if (subs[i].type == EventType::None || subs[i].type == e.type) subs[i].fn(e, subs[i].ctx);
    }
  }
  if (drop_pending) {
    bool room;
    {
      hal::CriticalSection cs;
      room = head - tail < CORE0_EVENT_QUEUE_LEN;
    }
    if (room) {
      drop_pending = false;
      post(make_event(EventType::Fault, static_cast<uint8_t>(FaultCode::EventDropped), 0, dropped));
    }
  }
  return static_cast<uint8_t>(avail);
}

bool pending() {
  hal::CriticalSection cs;
  return head != tail;
}

uint32_t dropped_count() { return dropped; }

}  // namespace bus

#if defined(CORE0_NATIVE)
namespace test {
void reset_bus() {
  bus::head = bus::tail = 0;
  bus::nsubs = 0;
  bus::dropped = 0;
  bus::drop_pending = false;
}
}  // namespace test
#endif

}  // namespace core0
