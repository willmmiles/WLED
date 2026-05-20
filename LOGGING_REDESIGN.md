# WLED Logging Framework — Design Plan

*Author: Claude Sonnet 4.6 / session 2026-05-20*
*Status: Draft for review — not yet implemented*

---

## 1. Background and Motivation

The existing WLED debug system is a collection of preprocessor macros
(`DEBUG_PRINT`, `DEBUG_PRINTF`, `DEBUG_PRINTF_P`, …) that route to one of
three hard-coded output sinks depending on compile flags:

| Condition | Sink |
|-----------|------|
| `WLED_DEBUG_HOST` set | `NetDebug` (UDP) |
| else | `Serial` |
| `BOARD_HAS_PSRAM` | also `wledLog` (PSRAM ring buffer) |

Problems with this approach:
- **Sink list is fixed at compile time** — adding a new sink (syslog, MQTT,
  Bluetooth) requires touching the macro pile in `wled.h`.
- **No log levels** — the choice is binary (debug on / debug off per
  component).  Fine-grained filtering requires separate `WLED_DEBUG_FS`,
  `WLED_DEBUG_WIFI`, etc. defines that each grow independently.
- **PSRAM ring buffer is gated on `BOARD_HAS_PSRAM`** — non-PSRAM devices
  cannot retrieve logs after the fact.
- **Call sites mix `F()` / `PSTR()` decoration** — inconsistent, error-prone.
- **No component tagging** — all debug output streams together; impossible to
  filter on the receiver side.

---

## 2. Requirements

| # | Requirement |
|---|-------------|
| R1 | Log calls include **component tag**, **log level**, and **printf-style format + args**. |
| R2 | **Compile-time filtering** by level and component (filtered messages leave zero code in release builds). |
| R3 | Prefer **type-safe C++** over raw macros in the implementation; call-site macros are acceptable wrappers. |
| R4 | Component and format strings stored in **PROGMEM automatically** (no explicit `F()` / `PSTR()` at call sites). |
| R5 | **Extensible sink system** — a usermod or library can add a sink without patching core files. |
| R6 | Existing sinks (Serial, UDP/NetDebug) reimplemented as sink components. |
| R7 | Ring buffer sink available on **all platforms** (not only PSRAM boards), though PSRAM devices get a larger default. |
| R8 | Must compile and run on **ESP8266** (Arduino Core 3.x, GCC 10, C++17 available) and **ESP32** (Arduino Core 2.x, C++11 as baseline). |
| R9 | **Minimal flash overhead** — template bloat is unacceptable; sinks dispatch through virtual calls. |
| R10 | **ESP-IDF log API compatible** — `ESP_LOGx` style macros and a hook to capture IDF-internal logs on ESP32. |

---

## 3. Architecture Overview

```
Call site (any .cpp)
  │
  │  WLOG_I("wifi", "Connected to %s", ssid)
  ▼
wled/log.h  ─── compile-time level gate (WLED_LOCAL_LOG_LEVEL) ─── dropped if filtered
  │
  │  ::wled::log_write(LogLevel::Info, PSTR("wifi"), PSTR("Connected to %s"), ssid)
  ▼
wled/log.cpp :: log_write()
  ├── vsnprintf_P into stack buffer [256 B]  ← format once, fan out
  ├── acquire platform lock
  └── iterate dynarray<LogSink*>
        ├── SerialLogSink         (replaces DEBUGOUT + DEBUG_*)
        ├── NetDebugLogSink       (replaces NetworkDebugPrinter direct use)
        └── RingBufferLogSink     (replaces LogBuffer/LogPrint, all platforms)
              … (user sinks via REGISTER_LOG_SINK)
```

The sink list is a **dynarray** — the same linker-section trick used for
usermods.  Each sink is registered at compile time; there is no runtime
`registerSink()` call.  This means zero dynamic allocation for the list itself
and a tight dispatch loop.

---

## 4. API Design

### 4.1 Log Levels

```cpp
// wled/log.h
#define WLED_LOG_LEVEL_NONE     0
#define WLED_LOG_LEVEL_ERROR    1   // matches esp_log_level_t ESP_LOG_ERROR
#define WLED_LOG_LEVEL_WARN     2
#define WLED_LOG_LEVEL_INFO     3
#define WLED_LOG_LEVEL_DEBUG    4
#define WLED_LOG_LEVEL_VERBOSE  5

namespace wled {
  enum class LogLevel : uint8_t {
    None    = WLED_LOG_LEVEL_NONE,
    Error   = WLED_LOG_LEVEL_ERROR,
    Warn    = WLED_LOG_LEVEL_WARN,
    Info    = WLED_LOG_LEVEL_INFO,
    Debug   = WLED_LOG_LEVEL_DEBUG,
    Verbose = WLED_LOG_LEVEL_VERBOSE,
  };
}
```

### 4.2 Compile-Time Level Gate

```cpp
// Global level — set via build flag -D WLED_LOG_LEVEL=3 or auto-detected:
#ifndef WLED_LOG_LEVEL
  #ifdef WLED_DEBUG
    #define WLED_LOG_LEVEL WLED_LOG_LEVEL_DEBUG
  #else
    #define WLED_LOG_LEVEL WLED_LOG_LEVEL_INFO
  #endif
#endif

// Per-file override — define BEFORE including log.h:
// #define WLED_LOCAL_LOG_LEVEL WLED_LOG_LEVEL_VERBOSE
#ifndef WLED_LOCAL_LOG_LEVEL
  #define WLED_LOCAL_LOG_LEVEL WLED_LOG_LEVEL
#endif
```

### 4.3 Call-Site Macros

```cpp
// PROGMEM wrapping is automatic — no PSTR() or F() at call sites.
// On ESP32, PSTR() is a no-op; on ESP8266 it places the string in flash.

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

// ESP-IDF-compatible aliases (tag can be a PROGMEM const char[] defined per-file)
#define LOGE(tag, fmt, ...) WLOG_E(tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) WLOG_W(tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) WLOG_I(tag, fmt, ##__VA_ARGS__)
#define LOGD(tag, fmt, ...) WLOG_D(tag, fmt, ##__VA_ARGS__)
#define LOGV(tag, fmt, ...) WLOG_V(tag, fmt, ##__VA_ARGS__)
```

> **Macros vs. pure C++:** The macros are thin wrappers whose sole purpose is
> (a) compile-time elimination when below the level threshold, and (b)
> automatic PSTR() wrapping.  Everything else — buffer management, sink
> dispatch, locking — is in regular C++ functions and classes.  Removing the
> macros entirely would require `if constexpr` (C++17, unavailable on ESP32
> baseline) *and* would lose the PSTR/PROGMEM auto-placement.  This hybrid is
> the minimum-macro approach that meets all requirements.

### 4.4 LogSink Interface

```cpp
// wled/log.h (or wled/log_sink.h)
namespace wled {

class LogSink {
public:
  virtual ~LogSink() = default;

  // One-time initialization (called from log_setup() during WLED boot).
  // Network sinks should check connectivity inside write() rather than here.
  virtual void setup() {}

  // Deliver one formatted log line to this sink.
  //
  // component_P  — component/tag string; lives in PROGMEM on 8266 (use
  //                strcpy_P / strcmp_P).  On ESP32 it is a normal const char*.
  // level        — log level of this message.
  // msg          — pre-formatted UTF-8 string in RAM (NOT PROGMEM).
  // len          — strlen(msg); provided for sinks that send over the network.
  //
  // This method MUST NOT block.  Network sinks should buffer and send async,
  // or drop messages when the network is unavailable.
  virtual void write(LogLevel level, const char* component_P,
                     const char* msg, size_t len) = 0;

  // Runtime gate.  Checked before calling write(); saves overhead when a sink
  // is temporarily disabled (e.g., NetDebug before WiFi connects).
  virtual bool isEnabled() const { return true; }

  // Per-sink minimum level (independent of the compile-time global).
  // Return LogLevel::None to accept everything that passes the global gate.
  virtual LogLevel minLevel() const { return LogLevel::None; }
};

} // namespace wled
```

### 4.5 Core Dispatch Function

```cpp
// Declaration in wled/log.h
namespace wled {
  // Primary entry point — called by WLOG_* macros.
  // component_P and fmt_P are PROGMEM pointers on ESP8266, normal pointers on ESP32.
  void log_write(LogLevel level, const char* component_P, const char* fmt_P, ...)
    __attribute__((format(printf, 3, 4)));

  void log_write_v(LogLevel level, const char* component_P,
                   const char* fmt_P, va_list args);

  // Write a pre-formatted line (for routing external logs, e.g. ESP-IDF vprintf hook).
  // The line is passed to all sinks as-is; level and component are hints only.
  void log_write_raw(LogLevel level, const char* component_P,
                     const char* line, size_t len);

  // Called once during WLED boot to call setup() on all registered sinks.
  void log_setup();

  // On ESP32: register this as the IDF vprintf handler to capture WiFi/BT logs.
  // int idf_log_vprintf(const char* fmt, va_list args);
  // Usage: esp_log_set_vprintf(wled::idf_log_vprintf);
} // namespace wled
```

```cpp
// Implementation sketch — wled/log.cpp
#include "log.h"
#include "dynarray.h"

DECLARE_DYNARRAY(wled::LogSink*, log_sinks);

// Platform-appropriate spinlock
#ifdef ESP8266
  // Single-core; interrupt disable is sufficient and simpler than portMUX
  struct LogGuard {
    uint32_t _saved;
    LogGuard()  { _saved = xt_rsil(15); }  // save & disable interrupts
    ~LogGuard() { xt_wsr_ps(_saved); }     // restore
  };
#else
  static portMUX_TYPE s_log_mux = portMUX_INITIALIZER_UNLOCKED;
  struct LogGuard {
    LogGuard()  { portENTER_CRITICAL(&s_log_mux); }
    ~LogGuard() { portEXIT_CRITICAL(&s_log_mux); }
  };
#endif

// Format string buffer — one per invocation; stack-allocated to be re-entrant
#ifndef WLED_LOG_MSG_SIZE
  #define WLED_LOG_MSG_SIZE 256
#endif

void wled::log_write_v(LogLevel level, const char* component_P,
                        const char* fmt_P, va_list args)
{
  char buf[WLED_LOG_MSG_SIZE];
#ifdef ESP8266
  vsnprintf_P(buf, sizeof(buf), fmt_P, args);
#else
  vsnprintf(buf, sizeof(buf), fmt_P, args);
#endif
  log_write_raw(level, component_P, buf, strlen(buf));
}

void wled::log_write(LogLevel level, const char* component_P,
                      const char* fmt_P, ...)
{
  va_list ap;
  va_start(ap, fmt_P);
  log_write_v(level, component_P, fmt_P, ap);
  va_end(ap);
}

void wled::log_write_raw(LogLevel level, const char* component_P,
                          const char* msg, size_t len)
{
  LogGuard guard;  // hold lock while iterating — sinks must not block
  for (auto s = DYNARRAY_BEGIN(log_sinks); s < DYNARRAY_END(log_sinks); ++s) {
    wled::LogSink* sink = *s;
    if (!sink->isEnabled()) continue;
    if (static_cast<uint8_t>(level) < static_cast<uint8_t>(sink->minLevel())) continue;
    sink->write(level, component_P, msg, len);
  }
}

void wled::log_setup() {
  for (auto s = DYNARRAY_BEGIN(log_sinks); s < DYNARRAY_END(log_sinks); ++s)
    (*s)->setup();
}
```

### 4.6 Sink Registration

```cpp
// In wled/log.h — reuses the existing dynarray.h mechanism:
#include "dynarray.h"

// REGISTER_LOG_SINK(instance)
// Place in a .cpp file alongside a sink object.  'instance' must be a
// non-const lvalue with static storage duration.
//
// Example:
//   static wled::SerialLogSink g_serial_sink;
//   REGISTER_LOG_SINK(g_serial_sink);
//
// Priority suffix _PRIO(instance, n) can be added later for ordering.
#define REGISTER_LOG_SINK(instance) \
  DYNARRAY_MEMBER(wled::LogSink*, log_sinks, log_sink_##instance, 50) = &(instance)
```

The dynarray linker-section trick sorts sections by name, so lower-numbered
priorities run first.  Recommended conventions:

| Priority | Purpose |
|----------|---------|
| 10 | Serial (always first for early-boot visibility) |
| 20 | NetDebug |
| 50 | Ring buffer and user sinks (default) |
| 90 | Low-priority / lossy sinks (future syslog) |

---

## 5. Platform Compatibility

### 5.1 PROGMEM and `PSTR()`

On **ESP8266**, `PSTR(literal)` is a GCC extension that places the string in
SPI flash (PROGMEM).  Functions that accept PROGMEM strings must use the `_P`
variants (`printf_P`, `strcmp_P`, `strcpy_P`, etc.).

On **ESP32**, `PSTR(literal)` is a no-op (string lands in regular flash as
with any `const char*` literal).  The `_P` variants are defined as aliases
for their non-P counterparts.

Our dispatch code uses a compile-time branch:

```cpp
#ifdef ESP8266
  vsnprintf_P(buf, sizeof(buf), fmt_P, args);
  // component_P is a PROGMEM pointer; sinks that need it as RAM copy:
  char tag[16];
  strncpy_P(tag, component_P, sizeof(tag)-1);
  tag[sizeof(tag)-1] = '\0';
#else
  vsnprintf(buf, sizeof(buf), fmt_P, args);
  const char* tag = component_P;  // already in RAM-addressable flash
#endif
```

Sinks receive the **tag as a RAM string** after the copy above, so they do not
need to handle PROGMEM themselves.

### 5.2 Thread Safety

| Platform | Mechanism |
|----------|-----------|
| ESP8266 (single-core, FreeRTOS-light) | `xt_rsil(15)` / `xt_wsr_ps()` — saves and restores interrupt level |
| ESP32 (multi-core FreeRTOS) | `portMUX_TYPE` spinlock via `portENTER_CRITICAL` / `portEXIT_CRITICAL` |

The lock is held for the duration of sink dispatch.  **Sinks must not block**
(no WiFi, no RTOS `delay()`, no mutex waits).  Blocking sinks must buffer
internally and drain from a task or timer.

### 5.3 C++ Standard Compatibility

The implementation targets **C++11** as the minimum common denominator so
the same source compiles on both platforms without `#ifdef ESP8266` in the
logic layer.  Features used:

- `enum class` ✓ (C++11)
- `virtual` functions ✓ (C++11)
- Range-for over pointer arrays ✓ (C++11)
- `static_assert` ✓ (C++11)
- `override` / `final` ✓ (C++11)

Specifically avoided:
- `if constexpr` (C++17) — not available on ESP32 Arduino Core 2.x by default
- Fold expressions, CTAD — C++17
- `std::string_view` — C++17

---

## 6. Built-In Sink Implementations

### 6.1 SerialLogSink

Replaces the `DEBUG_PRINT` / `DEBUG_PRINTF` path.  Only active when
`WLED_DEBUG` is defined (to preserve the existing behavior of keeping serial
logging opt-in for release builds).

```cpp
// wled/log_sink_serial.h
#ifdef WLED_DEBUG
class SerialLogSink : public wled::LogSink {
public:
  void write(wled::LogLevel level, const char* /*tag*/, const char* msg, size_t) override {
    // Optionally prefix with level initial:
    // static const char lvl_char[] = "NEWIDV";
    // Serial.write(lvl_char[static_cast<uint8_t>(level)]);
    // Serial.write(' ');
    Serial.print(msg);
  }
};

static SerialLogSink g_serial_sink;
REGISTER_LOG_SINK(g_serial_sink);
#endif
```

> **Question for review:** Should the serial sink include a level prefix and/or
> timestamp by default?  Lean yes for timestamp (millis()), but it adds 12+ bytes
> per line to the ring buffer too if we're not careful to only prefix in the sink.

### 6.2 NetDebugLogSink

Replaces `NetworkDebugPrinter` / `NetDebug` as a direct output target.
Enabled by `WLED_DEBUG_HOST`.

```cpp
// wled/log_sink_netdebug.h
#ifdef WLED_DEBUG_HOST
class NetDebugLogSink : public wled::LogSink {
  NetworkDebugPrinter _printer;
public:
  bool isEnabled() const override { return netDebugEnabled && WLED_CONNECTED; }
  void write(wled::LogLevel, const char*, const char* msg, size_t len) override {
    _printer.write(reinterpret_cast<const uint8_t*>(msg), len);
  }
};

static NetDebugLogSink g_netdebug_sink;
REGISTER_LOG_SINK(g_netdebug_sink);
#endif
```

### 6.3 RingBufferLogSink

Replaces the current `LogBuffer` / `LogPrint` pair.  Key changes from the
PR's implementation:

1. **Available on all platforms** — allocates from PSRAM if available, falls
   back to normal heap if explicitly requested.
2. **Format includes timestamp and level** so the viewer page is useful without
   a serial connection.
3. **Configurable capacity** via build flag or runtime API.

```cpp
// wled/log_sink_ringbuf.h

// Default capacity — tunable per build target:
//   -D WLED_LOG_BUFFER_SIZE=8192   (explicit; required without PSRAM)
// Without an explicit size, the ring buffer is DISABLED on non-PSRAM builds.
#if defined(BOARD_HAS_PSRAM) && !defined(WLED_LOG_BUFFER_SIZE)
  #define WLED_LOG_BUFFER_SIZE (32 * 1024)
#endif

#ifdef WLED_LOG_BUFFER_SIZE

class RingBufferLogSink : public wled::LogSink {
public:
  static constexpr size_t CAPACITY = WLED_LOG_BUFFER_SIZE;

  void setup() override {
    #ifdef BOARD_HAS_PSRAM
      if (psramFound()) _buf = static_cast<char*>(ps_malloc(CAPACITY));
    #endif
    if (!_buf) _buf = static_cast<char*>(malloc(CAPACITY));
    // If malloc fails, the sink silently does nothing (isEnabled returns false)
  }

  bool isEnabled() const override { return _buf != nullptr; }

  void write(wled::LogLevel level, const char* tag, const char* msg, size_t len) override {
    // Write a formatted line: "I(123456) wifi: message\n"
    static const char lvl_char[] = "NEWIDV";
    char hdr[32];
    int hdr_len = snprintf(hdr, sizeof(hdr), "%c(%7lu) ",
                           lvl_char[static_cast<uint8_t>(level)], millis());
    _write(hdr, hdr_len);
    _write(tag, strlen(tag));
    _write(": ", 2);
    _write(msg, len);
    if (len == 0 || msg[len-1] != '\n') _write("\n", 1);
  }

  // Stream ring buffer contents to a Print sink (for HTTP /log endpoint)
  size_t printTo(Print& out) const { /* same as existing LogBuffer::printTo */ }

  void clear() { /* same as existing */ }

private:
  char*   _buf     = nullptr;
  size_t  _cap     = CAPACITY;
  size_t  _head    = 0;
  size_t  _used    = 0;
  // Note: no internal spinlock — locking is handled by the dispatch layer

  void _write(const char* data, size_t len) { /* ring buffer byte write */ }
};

static RingBufferLogSink g_ringbuf_sink;
REGISTER_LOG_SINK(g_ringbuf_sink);

// Global accessor for HTTP endpoint — replaces extern wledLogBuffer
inline RingBufferLogSink& wledLogBuffer() { return g_ringbuf_sink; }

#endif // WLED_LOG_BUFFER_SIZE
```

> **Note on locking:** The existing `LogBuffer` holds its own `portMUX`.  In the
> new design, the **dispatch layer** holds the lock before iterating sinks.  The
> `RingBufferLogSink::write()` therefore does NOT need its own lock, which
> eliminates the double-lock concern.  If a sink needs to be callable outside
> the dispatch path (e.g. the HTTP handler calls `printTo` concurrently with a
> log dispatch), it must take its own lock for that external call path only.

### 6.4 Future: SyslogSink (not implemented here)

The open PR for syslog output can be added as a `SyslogLogSink` registered
with `REGISTER_LOG_SINK`.  It would buffer a UDP payload using RFC 5424
format constructed from the `level`, `tag`, and `msg` parameters — no changes
to core logging code required.

---

## 7. Compile-Time Filtering Details

### Global level

```ini
; platformio.ini / platformio_override.ini
build_flags =
  -D WLED_LOG_LEVEL=4   ; WLED_LOG_LEVEL_DEBUG
```

### Per-file override

```cpp
// At the top of a particular translation unit, before any #include of log.h:
#define WLED_LOCAL_LOG_LEVEL WLED_LOG_LEVEL_VERBOSE
#include "log.h"

// All WLOG_V() calls in this file will now compile in, even if the global
// level is INFO.  WLOG_* calls in other files are unaffected.
```

When `WLED_LOCAL_LOG_LEVEL` is below the threshold, the macro expands to
`((void)0)`, which the compiler discards completely.  No function call, no
string literal, no flash usage.

### Per-component runtime filtering (optional, phase 2)

For runtime filtering by component (useful for live debugging), the
`LogSink::write()` can inspect the `tag` argument and drop messages early.
A helper utility sink could expose a runtime filter table:

```cpp
// Sketch only — not part of initial implementation:
class FilterSink : public wled::LogSink { /* ... */ };
```

---

## 8. Backward Compatibility & Migration

### Phase 1 — Parallel implementation

Add `wled/log.h`, `wled/log.cpp`, and the three built-in sink files.  Do NOT
yet change any existing `DEBUG_*` call sites.  The existing macros remain and
continue to work; the new system runs alongside.

New code written after phase 1 should use `WLOG_*` macros.

Add compatibility shims in a transitional header so both old and new macros
work:

```cpp
// Placed at the bottom of wled/log.h during migration only:
// (Remove these after all call sites are migrated)
#ifdef WLED_DEBUG
  #define DEBUG_PRINT(x)       WLOG_D("WLED", "%s", String(x).c_str())
  #define DEBUG_PRINTLN(x)     WLOG_D("WLED", "%s\n", String(x).c_str())
  #define DEBUG_PRINTF(fmt, ...) WLOG_D("WLED", fmt, ##__VA_ARGS__)
  #define DEBUG_PRINTF_P(fmt, ...) WLOG_D("WLED", fmt, ##__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)       ((void)0)
  #define DEBUG_PRINTLN(x)     ((void)0)
  #define DEBUG_PRINTF(...)    ((void)0)
  #define DEBUG_PRINTF_P(...)  ((void)0)
#endif
```

> **Caveat:** `DEBUG_PRINT(x)` for non-string types (integers, float) uses
> `String(x)` which heap-allocates.  Existing call sites using `DEBUG_PRINT`
> with integers should be migrated to `WLOG_D("tag", "%d", x)` in phase 2.

### Phase 2 — Migrate existing call sites

Mechanically replace each `DEBUG_PRINTF_P(PSTR("…"), …)` with
`WLOG_D("component", "…", …)`, choosing a meaningful component name.
This can be done per-file.

Suggested component names for existing call sites:
| File | Tag |
|------|-----|
| `wled.cpp` | `"wled"` |
| `wled_server.cpp` | `"http"` |
| `json.cpp` | `"json"` |
| `wifi.cpp` | `"wifi"` |
| `bus_manager.cpp` | `"bus"` |

### Phase 3 — Remove compatibility shims

Once all call sites use `WLOG_*`, remove the `DEBUG_*` compat macros and the
old `log_buffer.h` / `net_debug.h` includes from `wled.h`.

---

## 9. File Layout

```
wled00/
├── log.h                   ← Public API: macros, LogSink interface, registration
├── log.cpp                 ← log_write(), log_setup(), DECLARE_DYNARRAY
├── log_sink_serial.h       ← SerialLogSink + REGISTER_LOG_SINK
├── log_sink_netdebug.h     ← NetDebugLogSink + REGISTER_LOG_SINK
├── log_sink_ringbuf.h      ← RingBufferLogSink + REGISTER_LOG_SINK + HTTP helper
│
│   (existing files — to be modified)
├── wled.h                  ← Include log.h; remove old DEBUG_* macros
├── wled.cpp                ← Call log_setup() early in setup()
├── wled_server.cpp         ← /log endpoint: remove #ifdef BOARD_HAS_PSRAM gate
└── json.cpp                ← serveLog(): use wledLogBuffer() accessor
```

Syslog and other future sinks live in `usermods/` or `wled00/` as appropriate.

---

## 10. HTTP Log Endpoint Changes

The `/log` page and `/json/log` endpoint currently require `BOARD_HAS_PSRAM`.
After the redesign:

- The endpoint is always compiled in.
- If no ring buffer sink is registered or available, it returns `503` with
  `"Log buffer unavailable."` (same as today but without the `#ifdef`).
- The endpoint calls `g_ringbuf_sink.printTo(*response)` directly (the sink
  is addressable via the global accessor).

---

## 11. ESP-IDF Log Routing (ESP32 only)

To capture ESP-IDF internal logs (WiFi, TCP/IP, BLE, etc.) and route them
through the WLED ring buffer:

```cpp
// Call once during setup(), after log_setup():
#ifndef ESP8266
  esp_log_set_vprintf([](const char* fmt, va_list args) -> int {
    // IDF pre-formats with level/tag prefix; extract or pass raw.
    // Simplest: pass as raw to all sinks at Info level.
    char buf[WLED_LOG_MSG_SIZE];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    wled::log_write_raw(wled::LogLevel::Info, PSTR("idf"), buf, n > 0 ? n : 0);
    return n;
  });
#endif
```

This means WiFi association logs, TLS errors, mDNS events, etc. all appear in
the ring buffer and serial output alongside WLED-native logs.

---

## 12. Third-Party Library Evaluation

The following embedded logging libraries were considered:

| Library | Verdict |
|---------|---------|
| **spdlog** | Too heavy; runtime fmt library adds ~50 KB. Rejected. |
| **rxi/log.c** | Simple, ~200 LoC C library. No PROGMEM support, no dynarray sink model. Would need significant wrapping; easier to write from scratch. |
| **ESP-IDF `esp_log`** | ESP32-only. Good API design that we borrow (level names, TAG pattern, vprintf hook). Can route its output into our system. |
| **NanoLog** | Requires 64-bit host post-processing. Not applicable. |
| **ArduinoLog** | Arduino-specific, Print-based, no PROGMEM, limited to Serial+Stream sinks. Rejected. |

**Decision:** Custom implementation.  The requirements (PROGMEM auto-placement,
dynarray sinks, ESP8266 compatibility, minimal flash overhead) are specific
enough that no off-the-shelf library fits without significant modification.
The implementation is straightforward and estimated at ~400 LoC.

---

## 13. Design Decisions (Resolved)

| # | Question | Decision |
|---|----------|----------|
| 1 | Message format in ring buffer | Structured: `D wifi: message\n`. Level initial + tag + message. No timestamp for now. |
| 2 | Lock scope | Single lock for the dispatch loop. Sinks must not block. |
| 3 | Serial sink level prefix | Level initial and tag prefix (matching ring buffer format). No timestamps. |
| 4 | Serial outside `WLED_DEBUG` | **Completely disabled.** The serial port carries light-control protocols; arbitrary output in release builds is unsafe. |
| 5 | Ring buffer availability | Available on all platforms. Build-time selectable (`WLED_LOG_BUFFER_SIZE` controls inclusion and default size). Runtime configurable via the usermod settings page (takes effect on next boot). |
| 6 | Ring buffer as usermod | The ring buffer is implemented as a `Usermod`+`LogSink` subclass in `wled00/` (not in `usermods/` yet for build-system simplicity). It uses the Usermod config JSON interface for runtime size configuration. |
| 7 | `DEBUG_*` compat shim lifespan | Shims remain **permanently** in `wled.h` (external usermods depend on them). In-repo call sites are migrated in a later commit. |
| 8 | Newline convention | Each `log_write()` call is one logical line. Sinks append `\n` if the formatted message doesn't already end with one. Call sites should **not** include trailing `\n` in format strings. |

---

## 14. Commit Sequence

Each commit should build cleanly. Commits 1 and 2 add new files only; nothing is wired in yet, so they are safe to review and land incrementally.

### Commit 1 — Core logging framework
*New files only; no existing code changed.*

- `wled00/log.h` — `LogLevel`, `LogSink`, `log_write` / `log_setup` declarations, `REGISTER_LOG_SINK`, `WLOG_*` macros.
- `wled00/log_sink_serial.h` — `SerialLogSink` class (guarded by `WLED_DEBUG`).
- `wled00/log_sink_netdebug.h` — `NetDebugLogSink` class (guarded by `WLED_DEBUG_HOST`).
- `wled00/log.cpp` — `DECLARE_DYNARRAY`, `log_write`/`log_write_raw`/`log_setup`, platform spinlock, sink instance creation and `REGISTER_LOG_SINK` calls.

### Commit 2 — Ring buffer usermod
*New files only.*

- `wled00/const.h` — add `USERMOD_ID_LOG_BUFFER = 59`.
- `wled00/log_buffer_usermod.h` — `LogBufferUsermod` class (inherits `Usermod` + `wled::LogSink`).  Runtime-configurable size; lazy PSRAM-first allocation; `streamTo(Print&)` for HTTP; static `instance()` accessor.
- `wled00/log_buffer_usermod.cpp` — `REGISTER_USERMOD` + `REGISTER_LOG_SINK` for the single static instance.

### Commit 3 — Wire framework into WLED
*Existing files modified; old ring buffer and DEBUG macros replaced.*

- `wled00/wled.h`:
  - Remove `#include "log_buffer.h"` + old PSRAM globals block.
  - Remove `#ifdef WLED_DEBUG_HOST` DEBUGOUT/NetDebug block.
  - Remove the large `DEBUG_*` / `DEBUGFS_*` macro block.
  - Add `#include "log.h"` and `#include "log_buffer_usermod.h"`.
  - Add permanent backward-compatible `DEBUG_*` / `DEBUGFS_*` shims delegating to `WLOG_D`.
  - Keep `netDebugEnabled` / `netDebugPrintHost` / `netDebugPrintPort` globals.
- `wled00/wled.cpp`:
  - Call `wled::log_setup()` as the first statement in `WLED::setup()`.
  - Remove the `#ifdef BOARD_HAS_PSRAM` `wledLogBuffer.init()` block (now handled in usermod).
  - Optionally wire ESP-IDF vprintf hook on ESP32.
- `wled00/wled_server.cpp`: remove `#ifdef BOARD_HAS_PSRAM` around `/log` and `/log.htm` routes.
- `wled00/json.cpp` `serveLog()`: use `LogBufferUsermod::instance()` instead of `wledLogBuffer`; remove `#ifdef BOARD_HAS_PSRAM`.
- Remove `wled00/log_buffer.h` (replaced by `log_buffer_usermod.h`).

### Commit 4 — Migrate in-repo call sites
*Mechanical search-and-replace of `DEBUG_*` → `WLOG_*` throughout `wled00/*.cpp`.*

- Assign a meaningful tag per subsystem (see §8 table).
- Strip trailing `\n` from format strings (sinks now append it).
- Replace `DEBUG_PRINT(F("…"))` / `DEBUG_PRINTLN(F("…"))` with `WLOG_I`/`WLOG_D` as appropriate.
- Leave `DEBUG_*` shim definitions in place (external usermods still use them).
