/*
 * LogBuffer usermod — in-memory ring buffer log sink with HTTP streaming.
 *
 * Implements both wled::LogSink (receives formatted lines from the dispatch
 * layer) and wled::ILogBuffer (streams contents to the /log HTTP endpoint).
 *
 * CAPACITY
 *   Build default: 32 KB when PSRAM is available, 4 KB otherwise.
 *   Override at build time: -D WLED_LOG_BUFFER_SIZE=N (bytes).
 *   Runtime: adjustable via the WLED settings page ("size_kb" field).
 *   The new size takes effect after the next reboot.
 *
 * Enable by adding "LogBuffer" to custom_usermods in your PlatformIO env.
 */

#include "wled.h"

#ifndef WLED_LOG_BUFFER_SIZE
  #ifdef BOARD_HAS_PSRAM
    #define WLED_LOG_BUFFER_SIZE (32 * 1024)
  #else
    #define WLED_LOG_BUFFER_SIZE (4 * 1024)
  #endif
#endif

class LogBufferUsermod : public Usermod, public wled::LogSink, public wled::ILogBuffer {
public:
  LogBufferUsermod() { wled::ILogBuffer::_register(this); }
  ~LogBufferUsermod() { if (_buf) { free(_buf); _buf = nullptr; } }

  // ── wled::LogSink ──────────────────────────────────────────────────────────
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

  // ── wled::ILogBuffer ───────────────────────────────────────────────────────
  bool isAvailable() const override { return _buf != nullptr; }

  void streamTo(Print& out) const override {
    _spinLock();
    if (!_buf || _used == 0) { _spinUnlock(); return; }
    if (_used < _capacity) {
      out.write(reinterpret_cast<const uint8_t*>(_buf), _used);
    } else {
      out.write(reinterpret_cast<const uint8_t*>(_buf + _head), _capacity - _head);
      out.write(reinterpret_cast<const uint8_t*>(_buf),          _head);
    }
    _spinUnlock();
  }

  void clear() override {
    _spinLock();
    _head = 0;
    _used = 0;
    _spinUnlock();
  }

  // ── Usermod ────────────────────────────────────────────────────────────────
  void loop() override {}

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

private:
  // ── Ring-buffer state ──────────────────────────────────────────────────────
  char*  _buf             = nullptr;
  size_t _capacity        = 0;
  size_t _head            = 0;   // next write position; also oldest byte when full
  size_t _used            = 0;
  size_t _desiredCapacity = WLED_LOG_BUFFER_SIZE;

  // ── Allocation ─────────────────────────────────────────────────────────────
  void _allocate(size_t cap) {
    if (cap == 0 || _buf) return;
#ifdef BOARD_HAS_PSRAM
    if (psramFound()) _buf = static_cast<char*>(ps_malloc(cap));
#endif
    if (!_buf) _buf = static_cast<char*>(malloc(cap));
    if (_buf) _capacity = cap;
  }

  // ── Low-level ring write (no lock — caller holds the dispatch spinlock) ─────
  void _ringWrite(const char* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
      _buf[_head] = data[i];
      _head = (_head + 1) % _capacity;
      if (_used < _capacity) _used++;
    }
  }

  // ── Spinlock for streamTo / clear (called outside the dispatch lock) ────────
#ifdef ESP8266
  void _spinLock()   const { noInterrupts(); }
  void _spinUnlock() const { interrupts(); }
#else
  mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
  void _spinLock()   const { portENTER_CRITICAL(&_mux); }
  void _spinUnlock() const { portEXIT_CRITICAL(&_mux);  }
#endif
};

static LogBufferUsermod g_log_buffer;
REGISTER_USERMOD(g_log_buffer);
REGISTER_LOG_SINK(g_log_buffer);
