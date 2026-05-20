#pragma once
/*
 * log_buffer_usermod.h — In-memory ring buffer log sink, implemented as a Usermod.
 *
 * LogBufferUsermod is both a wled::LogSink (receives formatted log lines from
 * the dispatch layer) and a Usermod (exposes runtime configuration via the
 * WLED settings page and is discoverable via UsermodManager::lookup()).
 *
 * BUILD-TIME SELECTION
 *   Compiled in unconditionally; buffer allocation occurs at runtime.
 *   Disable entirely with:  -D WLED_DISABLE_LOG_BUFFER
 *
 * DEFAULT CAPACITY
 *   PSRAM available:   WLED_LOG_BUFFER_SIZE  (default 32 KB)
 *   No PSRAM:          WLED_LOG_BUFFER_SIZE  (default  4 KB, heap only)
 *   Override both:    -D WLED_LOG_BUFFER_SIZE=8192
 *
 *   The user may change the size on the settings page; the new value takes
 *   effect after the next reboot.
 *
 * HTTP ENDPOINT
 *   json.cpp calls LogBufferUsermod::instance()->streamTo(*response) for GET /log.
 *   Posting ?clear wipes the buffer.
 *
 * THREAD SAFETY
 *   All writes arrive through the logging dispatch layer which holds the
 *   platform spinlock.  The streamTo() / clear() methods called from the HTTP
 *   handler task acquire their own spinlock independently.
 */

#ifndef WLED_DISABLE_LOG_BUFFER

#include "log.h"
#include "fcn_declare.h"   // Usermod base class
#include "const.h"         // USERMOD_ID_LOG_BUFFER

#include <Arduino.h>
#include <Print.h>

// Default capacity.  PSRAM devices get 32 KB; non-PSRAM devices get 4 KB.
// Override with -D WLED_LOG_BUFFER_SIZE=N (bytes).
#ifndef WLED_LOG_BUFFER_SIZE
  #ifdef BOARD_HAS_PSRAM
    #define WLED_LOG_BUFFER_SIZE (32 * 1024)
  #else
    #define WLED_LOG_BUFFER_SIZE (4 * 1024)
  #endif
#endif

class LogBufferUsermod : public Usermod, public wled::LogSink {
public:
  LogBufferUsermod()  { s_instance = this; }
  ~LogBufferUsermod() { if (_buf) { free(_buf); _buf = nullptr; } s_instance = nullptr; }

  // ── Static accessor ───────────────────────────────────────────────────────
  static LogBufferUsermod* instance() { return s_instance; }

  // ── wled::LogSink ─────────────────────────────────────────────────────────
  void setup() override { _allocate(_desiredCapacity); }

  void write(wled::LogLevel level, const char* tag,
             const char* msg, size_t len) override
  {
    if (!_buf) return;
    // Format: "D tag: message\n"
    static const char lvl[] = "NEWIDV";
    const uint8_t idx = static_cast<uint8_t>(level);
    const char    lc  = (idx < sizeof(lvl) - 1) ? lvl[idx] : '?';

    _ringWrite(&lc, 1);
    _ringWrite(" ", 1);
    _ringWrite(tag, strlen(tag));
    _ringWrite(": ", 2);
    _ringWrite(msg, len);
    if (len == 0 || msg[len - 1] != '\n') _ringWrite("\n", 1);
  }

  // ── Usermod ───────────────────────────────────────────────────────────────
  void loop() override {}

  uint16_t getId() override { return USERMOD_ID_LOG_BUFFER; }

  void addToConfig(JsonObject& obj) override {
    JsonObject top = obj.createNestedObject(F("LogBuffer"));
    top[F("size_kb")] = (unsigned)(_desiredCapacity / 1024);
  }

  bool readFromConfig(JsonObject& obj) override {
    JsonObject top = obj[F("LogBuffer")];
    if (top.isNull()) return false;
    unsigned kb = top[F("size_kb")] | (unsigned)(_desiredCapacity / 1024);
    _desiredCapacity = (size_t)kb * 1024;
    return true;
  }

  void addToJsonInfo(JsonObject& obj) override {
    JsonObject user = obj["u"];
    if (user.isNull()) user = obj.createNestedObject("u");
    JsonArray arr = user.createNestedArray(F("Log buffer"));
    if (_buf) {
      arr.add(_used);
      arr.add(F(" / "));
      arr.add(_capacity);
      arr.add(F(" B"));
    } else {
      arr.add(F("disabled"));
    }
  }

  // ── Public helpers (called from HTTP handlers, NOT under the dispatch lock) ─
  bool isAvailable() const { return _buf != nullptr; }
  size_t capacity()  const { return _capacity; }
  size_t used()      const { return _used; }

  // Stream ring-buffer contents (oldest byte first) to a Print sink.
  size_t streamTo(Print& out) const {
    _spinLock();
    if (!_buf || _used == 0) { _spinUnlock(); return 0; }
    if (_used < _capacity) {
      out.write(reinterpret_cast<const uint8_t*>(_buf), _used);
    } else {
      out.write(reinterpret_cast<const uint8_t*>(_buf + _head), _capacity - _head);
      out.write(reinterpret_cast<const uint8_t*>(_buf),          _head);
    }
    size_t n = (_used < _capacity) ? _used : _capacity;
    _spinUnlock();
    return n;
  }

  void clear() {
    _spinLock();
    _head = 0;
    _used = 0;
    _spinUnlock();
  }

private:
  // ── Ring-buffer state ─────────────────────────────────────────────────────
  char*   _buf            = nullptr;
  size_t  _capacity       = 0;
  size_t  _head           = 0;   // next write position; also oldest byte when full
  size_t  _used           = 0;
  size_t  _desiredCapacity = WLED_LOG_BUFFER_SIZE;

  // ── Singleton (defined in log_buffer_usermod.cpp) ────────────────────────
  static LogBufferUsermod* s_instance;

  // ── Allocation ────────────────────────────────────────────────────────────
  void _allocate(size_t cap) {
    if (cap == 0 || _buf) return;
#ifdef BOARD_HAS_PSRAM
    if (psramFound()) _buf = static_cast<char*>(ps_malloc(cap));
#endif
    if (!_buf) _buf = static_cast<char*>(malloc(cap));
    if (_buf) _capacity = cap;
  }

  // ── Low-level ring-buffer write (no lock — caller holds dispatch lock) ────
  void _ringWrite(const char* data, size_t len) {
    if (!_buf || !len) return;
    for (size_t i = 0; i < len; i++) {
      _buf[_head] = data[i];
      _head = (_head + 1) % _capacity;
      if (_used < _capacity) _used++;
    }
  }

  // ── Spinlock for streamTo / clear (called outside the dispatch lock) ──────
#ifdef ESP8266
  void _spinLock()   const { noInterrupts(); }
  void _spinUnlock() const { interrupts(); }
#else
  mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
  void _spinLock()   const { portENTER_CRITICAL(&_mux); }
  void _spinUnlock() const { portEXIT_CRITICAL(&_mux);  }
#endif
};

#endif // WLED_DISABLE_LOG_BUFFER
