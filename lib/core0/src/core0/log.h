// ZDX CORE-0 — logging.
//
//   C0_LOGI("batt", "%u mV (%u%%)", mv, pct);
//
// Levels above CORE0_LOG_LEVEL compile to nothing (arguments not evaluated).
// Output: "[  12.345] I batt: 3912 mV (71%)\n" via hal::console_write.
// Never log from an ISR — post an event and log in the handler.
#pragma once
#include <stdarg.h>
#include "types.h"

namespace core0 {
namespace logger {

void write(uint8_t level, const char* tag, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;
void vwrite(uint8_t level, const char* tag, const char* fmt, va_list ap);

// Runtime filter (CLI "log <n>"), cannot exceed the compile-time level.
void    set_level(uint8_t level);
uint8_t level();

// Last N lines kept in a RAM ring for the "dmesg" CLI command and fault dumps.
void dump_recent();

}  // namespace logger
}  // namespace core0

#define C0_LOG_(lvl, tag, ...) ::core0::logger::write((lvl), (tag), __VA_ARGS__)

#if CORE0_LOG_LEVEL >= CORE0_LOG_ERROR
#  define C0_LOGE(tag, ...) C0_LOG_(CORE0_LOG_ERROR, tag, __VA_ARGS__)
#else
#  define C0_LOGE(tag, ...) do {} while (0)
#endif
#if CORE0_LOG_LEVEL >= CORE0_LOG_WARN
#  define C0_LOGW(tag, ...) C0_LOG_(CORE0_LOG_WARN, tag, __VA_ARGS__)
#else
#  define C0_LOGW(tag, ...) do {} while (0)
#endif
#if CORE0_LOG_LEVEL >= CORE0_LOG_INFO
#  define C0_LOGI(tag, ...) C0_LOG_(CORE0_LOG_INFO, tag, __VA_ARGS__)
#else
#  define C0_LOGI(tag, ...) do {} while (0)
#endif
#if CORE0_LOG_LEVEL >= CORE0_LOG_DEBUG
#  define C0_LOGD(tag, ...) C0_LOG_(CORE0_LOG_DEBUG, tag, __VA_ARGS__)
#else
#  define C0_LOGD(tag, ...) do {} while (0)
#endif
#if CORE0_LOG_LEVEL >= CORE0_LOG_TRACE
#  define C0_LOGT(tag, ...) C0_LOG_(CORE0_LOG_TRACE, tag, __VA_ARGS__)
#else
#  define C0_LOGT(tag, ...) do {} while (0)
#endif
