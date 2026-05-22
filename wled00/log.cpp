/*
 * log.cpp — WLED logging framework: dispatch core and built-in sink registrations.
 */

// wled.h must be first so Arduino, platform macros, and the global variable
// definitions are all available before the sink headers reference them.
#include "wled.h"
#include "log.h"
#include "log_sink_serial.h"
#include "log_sink_netdebug.h"

// ── Dynarray: compile-time list of registered log sinks ──────────────────────
// DECLARE_DYNARRAY creates the begin/end sentinel symbols for this TU.
// Sinks registered via REGISTER_LOG_SINK() in other TUs land in the same
// linker sections and are discovered at runtime via pointer arithmetic.
DECLARE_DYNARRAY(wled::LogSink*, log_sinks);

// ── Platform spinlock ─────────────────────────────────────────────────────────
// Held for the duration of sink dispatch.  Sinks must not block.
#ifdef ESP8266
  // ESP8266 is single-core; noInterrupts() is sufficient.
  struct LogGuard {
    LogGuard()  { noInterrupts(); }
    ~LogGuard() { interrupts();   }
  };
#else
  // ESP32 is multi-core; use a FreeRTOS spinlock.
  static portMUX_TYPE s_log_mux = portMUX_INITIALIZER_UNLOCKED;
  struct LogGuard {
    LogGuard()  { portENTER_CRITICAL(&s_log_mux); }
    ~LogGuard() { portEXIT_CRITICAL(&s_log_mux);  }
  };
#endif

// ── Message buffer ────────────────────────────────────────────────────────────
// Stack-allocated per call.  Override with -D WLED_LOG_MSG_SIZE=N.
#ifndef WLED_LOG_MSG_SIZE
  #define WLED_LOG_MSG_SIZE 256
#endif

// ── Dispatch ──────────────────────────────────────────────────────────────────

void wled::log_write_raw(LogLevel level, const char* tag,
                          const char* msg, size_t len)
{
  LogGuard guard;
  for (auto s = DYNARRAY_BEGIN(log_sinks); s < DYNARRAY_END(log_sinks); ++s)
    (*s)->write(level, tag, msg, len);
}

void wled::log_write_v(LogLevel level, const char* tag_P,
                        const char* fmt_P, va_list ap)
{
  // Format the message into a stack buffer.
#ifdef ESP8266
  // On ESP8266, PSTR() pointers live in SPI flash; use the _P variant.
  char msg[WLED_LOG_MSG_SIZE];
  vsnprintf_P(msg, sizeof(msg), fmt_P, ap);

  // tag_P is also PROGMEM; copy to RAM before passing to sinks.
  char tag[32];
  strncpy_P(tag, tag_P, sizeof(tag) - 1);
  tag[sizeof(tag) - 1] = '\0';
#else
  // On ESP32, PSTR() is a no-op; both pointers are already in RAM-accessible flash.
  char msg[WLED_LOG_MSG_SIZE];
  vsnprintf(msg, sizeof(msg), fmt_P, ap);
  const char* tag = tag_P;
#endif

  log_write_raw(level, tag, msg, strlen(msg));
}

void wled::log_write_v(LogLevel level,
                        const __FlashStringHelper* tag,
                        const __FlashStringHelper* fmt,
                        va_list ap)
{
  char msg[WLED_LOG_MSG_SIZE];
#ifdef ESP8266
  vsnprintf_P(msg, sizeof(msg), reinterpret_cast<const char*>(fmt), ap);
  char tag_buf[32];
  strncpy_P(tag_buf, reinterpret_cast<const char*>(tag), sizeof(tag_buf) - 1);
  tag_buf[sizeof(tag_buf) - 1] = '\0';
  log_write_raw(level, tag_buf, msg, strlen(msg));
#else
  vsnprintf(msg, sizeof(msg), reinterpret_cast<const char*>(fmt), ap);
  log_write_raw(level, reinterpret_cast<const char*>(tag), msg, strlen(msg));
#endif
}

void wled::log_write(LogLevel level, const char* tag_P,
                      const char* fmt_P, ...)
{
  va_list ap;
  va_start(ap, fmt_P);
  log_write_v(level, tag_P, fmt_P, ap);
  va_end(ap);
}

void wled::log_write(LogLevel level,
                      const __FlashStringHelper* tag,
                      const __FlashStringHelper* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  log_write_v(level, tag, fmt, ap);
  va_end(ap);
}

void wled::log_setup()
{
  for (auto s = DYNARRAY_BEGIN(log_sinks); s < DYNARRAY_END(log_sinks); ++s)
    (*s)->setup();
}

// ── Built-in sink instances and registrations ─────────────────────────────────
// These are registered at priority 1 (before user/ring-buffer sinks at priority 5).

#ifdef WLED_DEBUG
  static wled::SerialLogSink   g_serial_sink;
  DYNARRAY_MEMBER(wled::LogSink*, log_sinks, log_sink_serial,   1) = &g_serial_sink;
#endif

#ifdef WLED_DEBUG_HOST
  static wled::NetDebugLogSink g_netdebug_sink;
  DYNARRAY_MEMBER(wled::LogSink*, log_sinks, log_sink_netdebug, 2) = &g_netdebug_sink;
#endif
