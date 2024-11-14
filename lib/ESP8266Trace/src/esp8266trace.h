// ESP8266 tracing and crash handling library
#include <cstdint>
#include <Print.h>


namespace ESP8266Trace {

  // ------- Event tracing -------
  // These functions store event information in a ring buffer, which persists across reboots.
  // The ring buffer automatically tracks entries and exits to the System context.
  
  // Inject a custom event.
  void track_event(uint32_t lvl, uint32_t data, intptr_t pc = 0, intptr_t sp = 0);

  // Clear all events in the ring buffer.  Useful at startup after printing contents.
  void clear_events();

  // Write the contents of the ring buffer to an output source.
  void print_events(Print&);

  // Enable  event tracking
  void start_event_tracking();
  //void stop_event_tracking();



  // ------- Crash handling -------
  // This library also sets up a crash handler which saves the RAM contents
  // to the OTA area when a "regular" crash occurs.
  bool crash_data_available();
  void clear_crash_data();
  void print_crash_data(Print&);
  void dump_core(Print&);

}