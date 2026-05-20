#pragma once
/*
 * log_sink_netdebug.h — NetDebugLogSink: routes log output to a UDP host.
 *
 * Only compiled when WLED_DEBUG_HOST is defined.  Mirrors the behaviour of the
 * original NetworkDebugPrinter but receives pre-formatted lines from the new
 * logging dispatch layer instead of being called directly.
 *
 * Globals accessed (defined in wled.h when WLED_DEBUG_HOST is set):
 *   extern bool netDebugEnabled;
 *   extern char netDebugPrintHost[33];
 *   extern int  netDebugPrintPort;
 *
 * This header must be included AFTER wled.h so those globals are visible.
 * In practice it is only ever included from log.cpp which includes wled.h first.
 *
 * A static instance is created and registered in log.cpp.
 */

#ifdef WLED_DEBUG_HOST

#include "log.h"
#include <WiFiUdp.h>

namespace wled {

class NetDebugLogSink : public LogSink {
public:
  bool isEnabled() const override {
    return netDebugEnabled && WLED_CONNECTED;
  }

  void write(LogLevel level, const char* tag, const char* msg, size_t len) override {
    if (!WLED_CONNECTED) return;

    // Resolve host on first write or after reset
    if (!_hostResolved) {
      _hostResolved = _ip.fromString(netDebugPrintHost);
      if (!_hostResolved) {
#ifdef ESP8266
        _hostResolved = (WiFi.hostByName(netDebugPrintHost, _ip, 750) == 1);
#else
  #ifdef WLED_USE_ETHERNET
        _hostResolved = ETH.hostByName(netDebugPrintHost, _ip);
  #else
        _hostResolved = (WiFi.hostByName(netDebugPrintHost, _ip) == 1);
  #endif
#endif
      }
      if (!_hostResolved) return;
    }

    _udp.beginPacket(_ip, netDebugPrintPort);
    _udp.write(reinterpret_cast<const uint8_t*>(msg), len);
    if (len == 0 || msg[len - 1] != '\n') _udp.write('\n');
    _udp.endPacket();
  }

  // Allow re-resolving the host (e.g. after a reconnect)
  void resetHost() { _hostResolved = false; }

private:
  WiFiUDP    _udp;
  IPAddress  _ip;
  bool       _hostResolved = false;
};

} // namespace wled

#endif // WLED_DEBUG_HOST
