// ZDX CORE-0 — logging (spec/modules/diagnostics.md "Log").
// "[sssss.mmm] L tag: message\n" via hal::console_write, plus a RAM ring for dmesg.
// Power contract: none (no hardware; console output stays available in every awake state).
#include "log.h"

#include <stdio.h>

#include "port.h"

namespace core0 {
namespace logger {
namespace {

const size_t kLineMax = 120;
const size_t kRingSize = 512;

char    ring[kRingSize];
size_t  ring_head = 0;   // next write position
bool    ring_wrapped = false;
uint8_t runtime_level = CORE0_LOG_LEVEL;

char level_char(uint8_t level) {
  static const char kChars[] = {'-', 'E', 'W', 'I', 'D', 'T'};
  return level <= CORE0_LOG_TRACE ? kChars[level] : '?';
}

void ring_put(const char* s, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    ring[ring_head++] = s[i];
    if (ring_head == kRingSize) {
      ring_head = 0;
      ring_wrapped = true;
    }
  }
}

}  // namespace

void vwrite(uint8_t level, const char* tag, const char* fmt, va_list ap) {
  if (level > runtime_level) return;
  char line[kLineMax];
  const ms_t now = hal::millis();
  int n = snprintf(line, sizeof(line), "[%5lu.%03lu] %c %s: ", static_cast<unsigned long>(now / 1000),
                   static_cast<unsigned long>(now % 1000), level_char(level), tag);
  if (n < 0) return;
  if (static_cast<size_t>(n) < sizeof(line) - 1) {
    const int m = vsnprintf(line + n, sizeof(line) - 1 - n, fmt, ap);
    if (m > 0) n += m;
  }
  if (static_cast<size_t>(n) > sizeof(line) - 2) n = sizeof(line) - 2;  // truncated
  line[n++] = '\n';
  line[n] = 0;
  hal::console_write(line, static_cast<size_t>(n));
  ring_put(line, static_cast<size_t>(n));
}

void write(uint8_t level, const char* tag, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vwrite(level, tag, fmt, ap);
  va_end(ap);
}

void set_level(uint8_t level) { runtime_level = level > CORE0_LOG_LEVEL ? CORE0_LOG_LEVEL : level; }
uint8_t level() { return runtime_level; }

void dump_recent() {
  if (!ring_wrapped) {
    hal::console_write(ring, ring_head);
    return;
  }
  // Oldest data starts at ring_head; skip the partial first line.
  size_t start = ring_head;
  size_t skipped = 0;
  while (skipped < kRingSize && ring[start] != '\n') {
    start = (start + 1) % kRingSize;
    ++skipped;
  }
  start = (start + 1) % kRingSize;
  if (start >= ring_head) {
    hal::console_write(ring + start, kRingSize - start);
    hal::console_write(ring, ring_head);
  } else {
    hal::console_write(ring + start, ring_head - start);
  }
}

}  // namespace logger

#if defined(CORE0_NATIVE)
namespace test {
void reset_log() {
  logger::ring_head = 0;
  logger::ring_wrapped = false;
  logger::runtime_level = CORE0_LOG_LEVEL;
}
}  // namespace test
#endif

}  // namespace core0
