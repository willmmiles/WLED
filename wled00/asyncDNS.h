#pragma once
/*
  asyncDNS.h - wrapper class for asynchronous DNS lookups using lwIP
  by @dedehai
*/

#include <Arduino.h>
#include <atomic>
#include <lwip/dns.h>
#include <lwip/err.h>


class AsyncDNS {
public:
  // note: passing the IP as a pointer to query() is not implemented because it is not thread-safe without mutexes
  //       with the IDF V4 bug external error handling is required anyway or dns can just stay stuck
  enum class result { Idle, Busy, Success, Error };

private:
  struct state_t {
    ip_addr_t raw_addr;
    std::atomic<result> status { result::Idle };
    uint16_t errorcount = 0;
  };
  std::shared_ptr<state_t> _state;

  // callback for dns_gethostbyname(), called when lookup is complete or timed out
  static void _dns_callback(const char *name, const ip_addr_t *ipaddr, void *arg) {
    std::shared_ptr<state_t>* state_ptr = reinterpret_cast<std::shared_ptr<state_t>*>(arg);
    state_t& state = **state_ptr;

    if (ipaddr) {
      state.raw_addr = *ipaddr;
      state.status = result::Success;
    } else {
      state.status = result::Error; // note: if query timed out (~5s), DNS lookup is broken until WiFi connection is reset (IDF V4 bug)
      state.errorcount++;
    }

    delete state_ptr;
  }

public:
  AsyncDNS() : _state(std::make_shared<state_t>()) {};
  AsyncDNS(const AsyncDNS&) = delete; // noncopyable
  AsyncDNS& operator=(const AsyncDNS&) = delete; // noncopyable

  // non-blocking query function to start DNS lookup
  result query(const char* hostname) {
    if (_state->status == result::Busy) return result::Busy; // in progress, waiting for callback

    std::shared_ptr<state_t>* callback_context = new std::shared_ptr<state_t>(_state);
    if (!callback_context) {
      _state->status = result::Error;
      _state->errorcount++;
      return result::Error;
    }

    _state->status = result::Busy;
    err_t err = dns_gethostbyname(hostname, &_state->raw_addr, _dns_callback, callback_context);
    if (err == ERR_OK) {
      _state->status = result::Success; // result already in cache
    } else if (err != ERR_INPROGRESS) {
      _state->status = result::Error;
      _state->errorcount++;
    }
    return _state->status;
  }

  // get the IP once Success is returned
  IPAddress getIP() const {
    if (_state->status != result::Success) return IPAddress(0,0,0,0);
    #ifdef ARDUINO_ARCH_ESP32
      return IPAddress(_state->raw_addr.u_addr.ip4.addr);
    #else
      return IPAddress(_state->raw_addr.addr);
    #endif
  }

  void renew() {  // reset status to allow re-query
    if (_state->status == result::Busy) {
      // Abandon old state, but keep error count
      uint16_t ec = _state->errorcount;
      _state = std::make_shared<state_t>();
      _state->errorcount = ec;
    } else {
      _state->status = result::Idle;
    }
  }

  void reset() { // reset status and error count
    if (_state->status == result::Busy) {
      // Abandon old state
      _state = std::make_shared<state_t>();
    } else {
      _state->status = result::Idle;
      _state->errorcount = 0;
    }
  }
    
  result status() const { return _state->status; }
  uint16_t getErrorCount() const { return _state->errorcount; }
};
