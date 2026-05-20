#pragma once
/*
 * log_sink_serial.h — SerialLogSink: routes log output to the hardware serial port.
 *
 * Only compiled when WLED_DEBUG is defined.  The serial port doubles as a
 * light-control protocol interface (Adalight, Improv, etc.) and must not
 * receive arbitrary output in release builds.
 *
 * A static instance is created and registered in log.cpp.
 */

#ifdef WLED_DEBUG

#include "log.h"
#include <Arduino.h>

namespace wled {

class SerialLogSink : public LogSink {
public:
  // Level-initial lookup: N E W I D V
  void write(LogLevel level, const char* tag, const char* msg, size_t len) override {
    static const char lvl[] = "NEWIDV";
    const uint8_t idx = static_cast<uint8_t>(level);
    Serial.write(idx < sizeof(lvl) - 1 ? lvl[idx] : '?');
    Serial.write(' ');
    Serial.print(tag);
    Serial.write(':');
    Serial.write(' ');
    Serial.print(msg);
    if (len == 0 || msg[len - 1] != '\n') Serial.write('\n');
  }
};

} // namespace wled

#endif // WLED_DEBUG
