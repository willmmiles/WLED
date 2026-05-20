#pragma once
/*
 * log.h — WLED structured logging framework.
 *
 * USAGE
 *   WLOG_I("wifi",    "Connected to %s", ssid);
 *   WLOG_D("effects", "Frame took %u ms", dt);
 *   WLOG_E("bus",     "Allocation failed (size=%u)", n);
 *
 * Tags and format strings are placed in PROGMEM automatically on ESP8266.
 * Both must be string literals (not runtime variables).
 *
 * COMPILE-TIME LEVEL GATE
 *   Global minimum (build flag):  -D WLED_LOG_LEVEL=3   (0=none … 5=verbose)
 *   Per-file override (before #include "log.h"):
 *     #define WLED_LOCAL_LOG_LEVEL WLED_LOG_LEVEL_VERBOSE
 *
 * SINK REGISTRATION
 *   In one .cpp file, alongside a static-storage-duration LogSink subclass:
 *     static MySink g_my_sink;
 *     REGISTER_LOG_SINK(g_my_sink);
 *
 * NEWLINE CONVENTION
 *   Each log_write() call is one logical line.  Sinks append '\n' if the
 *   message does not already end with one.  Do NOT put '\n' in format strings.
 */

#include <stdint.h>
#include <stdarg.h>
#include "dynarray.h"

// ── Compile-time level constants (match esp_log_level_t values) ──────────────
#define WLED_LOG_LEVEL_NONE     0
#define WLED_LOG_LEVEL_ERROR    1
#define WLED_LOG_LEVEL_WARN     2
#define WLED_LOG_LEVEL_INFO     3
#define WLED_LOG_LEVEL_DEBUG    4
#define WLED_LOG_LEVEL_VERBOSE  5

// Global default: DEBUG if WLED_DEBUG is set, else INFO.
#ifndef WLED_LOG_LEVEL
  #ifdef WLED_DEBUG
    #define WLED_LOG_LEVEL WLED_LOG_LEVEL_DEBUG
  #else
    #define WLED_LOG_LEVEL WLED_LOG_LEVEL_INFO
  #endif
#endif

// Per-translation-unit override.  Define BEFORE including this header.
#ifndef WLED_LOCAL_LOG_LEVEL
  #define WLED_LOCAL_LOG_LEVEL WLED_LOG_LEVEL
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
 * Subclass this and register with REGISTER_LOG_SINK().
 * Rules for write() implementations:
 *   • Must not block (no WiFi calls, no RTOS delays).
 *   • Must not call any WLOG_* macro (would deadlock the spinlock).
 *   • 'tag' and 'msg' are in RAM (the dispatch layer copied from PROGMEM).
 *   • Append '\n' to output if msg does not already end with one.
 *   • Filtering (by level, connection state, etc.) is the sink's responsibility.
 */
class LogSink {
public:
  virtual ~LogSink() = default;

  // Called once from wled::log_setup() during WLED::setup().
  virtual void setup() {}

  // Deliver one formatted log line.
  //   level  — severity of this message.
  //   tag    — component/subsystem name; always in RAM.
  //   msg    — pre-formatted message; always in RAM; may lack trailing '\n'.
  //   len    — strlen(msg).
  virtual void write(LogLevel level, const char* tag,
                     const char* msg, size_t len) = 0;
};

// ── Dispatch functions ────────────────────────────────────────────────────────
// Normally called via the WLOG_* macros below; rarely called directly.
// tag_P and fmt_P are PROGMEM pointers on ESP8266, normal pointers on ESP32.

void log_write(LogLevel level, const char* tag_P, const char* fmt_P, ...)
  __attribute__((format(printf, 3, 4)));

void log_write_v(LogLevel level, const char* tag_P,
                 const char* fmt_P, va_list ap);

// Pass a pre-formatted line to all sinks (for external log sources, e.g. the
// ESP-IDF vprintf hook).  tag is already in RAM.
void log_write_raw(LogLevel level, const char* tag,
                   const char* line, size_t len);

// Call setup() on every registered sink.  Must be called once, early in boot,
// before any WLOG_* calls.
void log_setup();

} // namespace wled

// ── Sink registration ─────────────────────────────────────────────────────────
// Place in exactly one .cpp file per sink object.
// 'instance' must be an lvalue with static storage duration.
//
// Priority 1  — reserved for core sinks (Serial, NetDebug) — use
//               REGISTER_LOG_SINK_CORE(instance) defined in log.cpp.
// Priority 5  — default for ring buffer and user sinks.
//
#define REGISTER_LOG_SINK(instance) \
  DYNARRAY_MEMBER(wled::LogSink*, log_sinks, log_sink_##instance, 5) = &(instance)

// ── Call-site macros ──────────────────────────────────────────────────────────
// PSTR() places string literals in SPI flash on ESP8266; it is a no-op on
// ESP32.  Both 'tag' and 'fmt' must be string literals, not runtime variables.

#if WLED_LOCAL_LOG_LEVEL >= WLED_LOG_LEVEL_ERROR
  #define WLOG_E(tag, fmt, ...) \
    ::wled::log_write(::wled::LogLevel::Error,   PSTR(tag), PSTR(fmt), ##__VA_ARGS__)
#else
  #define WLOG_E(tag, fmt, ...) ((void)0)
#endif

#if WLED_LOCAL_LOG_LEVEL >= WLED_LOG_LEVEL_WARN
  #define WLOG_W(tag, fmt, ...) \
    ::wled::log_write(::wled::LogLevel::Warn,    PSTR(tag), PSTR(fmt), ##__VA_ARGS__)
#else
  #define WLOG_W(tag, fmt, ...) ((void)0)
#endif

#if WLED_LOCAL_LOG_LEVEL >= WLED_LOG_LEVEL_INFO
  #define WLOG_I(tag, fmt, ...) \
    ::wled::log_write(::wled::LogLevel::Info,    PSTR(tag), PSTR(fmt), ##__VA_ARGS__)
#else
  #define WLOG_I(tag, fmt, ...) ((void)0)
#endif

#if WLED_LOCAL_LOG_LEVEL >= WLED_LOG_LEVEL_DEBUG
  #define WLOG_D(tag, fmt, ...) \
    ::wled::log_write(::wled::LogLevel::Debug,   PSTR(tag), PSTR(fmt), ##__VA_ARGS__)
#else
  #define WLOG_D(tag, fmt, ...) ((void)0)
#endif

#if WLED_LOCAL_LOG_LEVEL >= WLED_LOG_LEVEL_VERBOSE
  #define WLOG_V(tag, fmt, ...) \
    ::wled::log_write(::wled::LogLevel::Verbose, PSTR(tag), PSTR(fmt), ##__VA_ARGS__)
#else
  #define WLOG_V(tag, fmt, ...) ((void)0)
#endif

// ESP-IDF-compatible aliases so code written for ESP-IDF adapts easily.
#define LOGE(tag, fmt, ...) WLOG_E(tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) WLOG_W(tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) WLOG_I(tag, fmt, ##__VA_ARGS__)
#define LOGD(tag, fmt, ...) WLOG_D(tag, fmt, ##__VA_ARGS__)
#define LOGV(tag, fmt, ...) WLOG_V(tag, fmt, ##__VA_ARGS__)
