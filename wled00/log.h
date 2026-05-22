#pragma once
/*
 * log.h — WLED structured logging framework.
 *
 * USAGE — global functions (tag + fmt, both accept const char* or F("...")):
 *   wled::logI(F("wifi"),    F("Connected to %s"), ssid);
 *   wled::logD(F("effects"), F("Frame took %u ms"), dt);
 *   wled::logE(F("bus"),     F("Allocation failed (size=%u)"), n);
 *
 * USAGE — per-file wrappers with tag and level baked in:
 *   // At file scope (before use):
 *   WLED_LOG_DECLARE("wifi", WLED_LOG_LEVEL_DEBUG)
 *   // Then:
 *   logD(F("Connected to %s"), ssid);
 *
 * ON ESP8266: const char* tag/fmt arguments must be PROGMEM (PSTR-wrapped).
 * Use F("...") at call sites to handle both platforms transparently.
 *
 * COMPILE-TIME LEVEL GATE
 *   Global minimum (build flag): -D WLED_LOG_LEVEL=1  (0=none … 5=verbose)
 *   When the gate is not met, the function body is empty and eliminated by -Os.
 *   Note: function arguments are always evaluated; only the dispatch is skipped.
 *
 * SINK REGISTRATION
 *   In one .cpp file, alongside a static-storage-duration LogSink subclass:
 *     static MySink g_my_sink;
 *     REGISTER_LOG_SINK(g_my_sink);
 *
 * NEWLINE CONVENTION
 *   Each log call is one logical line.  Do NOT put '\n' in format strings.
 */

#include <stdint.h>
#include <stdarg.h>
#include "dynarray.h"

class Print;
class __FlashStringHelper;

// ── Compile-time level constants (match esp_log_level_t values) ──────────────
#define WLED_LOG_LEVEL_NONE     0
#define WLED_LOG_LEVEL_ERROR    1
#define WLED_LOG_LEVEL_WARN     2
#define WLED_LOG_LEVEL_INFO     3
#define WLED_LOG_LEVEL_DEBUG    4
#define WLED_LOG_LEVEL_VERBOSE  5

// Global default: DEBUG if WLED_DEBUG is set, else NONE.
// Override at build time: -D WLED_LOG_LEVEL=3
#ifndef WLED_LOG_LEVEL
  #ifdef WLED_DEBUG
    #define WLED_LOG_LEVEL WLED_LOG_LEVEL_DEBUG
  #else
    #define WLED_LOG_LEVEL WLED_LOG_LEVEL_NONE
  #endif
#endif

// ── Public types ──────────────────────────────────────────────────────────────
namespace wled {

enum class LogLevel : uint8_t {
  None    = WLED_LOG_LEVEL_NONE,
  Error   = WLED_LOG_LEVEL_ERROR,
  Warn    = WLED_LOG_LEVEL_WARN,
  Info    = WLED_LOG_LEVEL_INFO,
  Debug   = WLED_LOG_LEVEL_DEBUG,
  Verbose = WLED_LOG_LEVEL_VERBOSE,
};

/*
 * LogSink — base class for all log output targets.
 *
 * Rules for write() implementations:
 *   • Must not block (no WiFi calls, no RTOS delays).
 *   • Must not call any log function (would deadlock the spinlock).
 *   • 'tag' and 'msg' are always in RAM.
 *   • Append '\n' to output if msg does not already end with one.
 */
class LogSink {
public:
  virtual ~LogSink() = default;
  virtual void setup() {}
  virtual void write(LogLevel level, const char* tag,
                     const char* msg, size_t len) = 0;
};

// ── Dispatch functions ────────────────────────────────────────────────────────
// tag_P and fmt_P are PROGMEM pointers on ESP8266, normal pointers on ESP32.

void log_write(LogLevel level, const char* tag_P, const char* fmt_P, ...)
  __attribute__((format(printf, 3, 4)));

void log_write_v(LogLevel level, const char* tag_P,
                 const char* fmt_P, va_list ap);

// FSH overload of log_write_v: keeps the type all the way to vsnprintf_P / vsnprintf.
void log_write_v(LogLevel level, const __FlashStringHelper* tag,
                 const __FlashStringHelper* fmt, va_list ap);

// FSH overload of log_write: accepts F("tag") and F("fmt") directly.
void log_write(LogLevel level, const __FlashStringHelper* tag,
               const __FlashStringHelper* fmt, ...);

// Pass a pre-formatted line to all sinks (for external log sources).
// tag must already be in RAM.
void log_write_raw(LogLevel level, const char* tag,
                   const char* line, size_t len);

// Call setup() on every registered sink.  Must be called once during boot.
void log_setup();

// ── Compile-time-gated global logging functions ───────────────────────────────
// Convenience wrappers for each log level.  Use logE/logW/logI/logD/logV directly.
// When WLED_LOG_LEVEL is below the function's level, the function will be eliminated
// by the optimizer.
template<typename str_type, LogLevel lvl>
inline void log_write_lvl_v(const str_type* tag, const str_type* fmt, va_list ap) {
  if (lvl <= WLED_LOG_LEVEL) { log_write_v(lvl, tag, fmt, ap); };
}

// Variant for WLED_LOG_DECLARE: gated on a per-file local_min rather than the
// global WLED_LOG_LEVEL.
template<typename str_type, LogLevel lvl, int local_min>
inline void log_write_local_v(const str_type* tag, const str_type* fmt, va_list ap) {
  if (static_cast<int>(lvl) <= local_min) { log_write_v(lvl, tag, fmt, ap); }
}

// Common function body
#define WLED_LOG_LVL_FUNC(fn, lvl_enum) \
  template<typename str_type> \
  inline void fn(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3))) \
  { va_list ap; va_start(ap, fmt); log_write_lvl_v<str_type, lvl_enum>(tag, fmt, ap); va_end(ap); }

// Specific implementations
WLED_LOG_LVL_FUNC(logE, LogLevel::Error)
WLED_LOG_LVL_FUNC(logW, LogLevel::Warn)
WLED_LOG_LVL_FUNC(logI, LogLevel::Info)
WLED_LOG_LVL_FUNC(logD, LogLevel::Debug)
WLED_LOG_LVL_FUNC(logV, LogLevel::Verbose)

} // namespace wled

// ── Sink registration ─────────────────────────────────────────────────────────
// Place in exactly one .cpp file per sink object.
// 'instance' must be an lvalue with static storage duration.
#define REGISTER_LOG_SINK(instance) \
  DYNARRAY_MEMBER(wled::LogSink*, log_sinks, log_sink_##instance, 5) = &(instance)

// ── Per-file wrapper declaration macro ───────────────────────────────────────
// WLED_LOG_DECLARE(tag_str, local_level)
//
// Declares file-local logE/logW/logI/logD/logV functions with the tag string
// and minimum level baked in.  Place at file scope before first use.
//
//   WLED_LOG_DECLARE("wifi", WLED_LOG_LEVEL_DEBUG)
//   logD(F("signal: %d dBm"), rssi);   // tag is "wifi", only if DEBUG >= level
//
// local_level must be a compile-time constant for the body to be dead-code-
// eliminated when the level is not met.
// On ESP8266, const char* fmt must be PROGMEM; use F("...") for portability.

#define WLED_LOG_FUNC_(fn, lvl_enum, tag_str, local_level) \
  static inline void fn(const char* fmt, ...) __attribute__((format(printf, 1, 2))) { \
    va_list _ap; va_start(_ap, fmt); \
    ::wled::log_write_local_v<char, lvl_enum, (local_level)>(PSTR(tag_str), fmt, _ap); \
    va_end(_ap); \
  } \
  static inline void fn(const __FlashStringHelper* fmt, ...) { \
    va_list _ap; va_start(_ap, fmt); \
    ::wled::log_write_local_v<__FlashStringHelper, lvl_enum, (local_level)>( \
      reinterpret_cast<const __FlashStringHelper*>(PSTR(tag_str)), fmt, _ap); \
    va_end(_ap); \
  }

#define WLED_LOG_DECLARE(tag_str, local_level) \
  WLED_LOG_FUNC_(logE, ::wled::LogLevel::Error,   tag_str, local_level) \
  WLED_LOG_FUNC_(logW, ::wled::LogLevel::Warn,    tag_str, local_level) \
  WLED_LOG_FUNC_(logI, ::wled::LogLevel::Info,    tag_str, local_level) \
  WLED_LOG_FUNC_(logD, ::wled::LogLevel::Debug,   tag_str, local_level) \
  WLED_LOG_FUNC_(logV, ::wled::LogLevel::Verbose, tag_str, local_level)
