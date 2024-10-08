# ESP8266Trace

A library for tracing ESP8266 platform events, useful for debugging system failures.  The event trace buffer is stored in noinit memory, so event traces will persist across WDT crashes.


## Usage

TODO
```
void clear_events();
void print_events();
void setup_isr_tracking();
void track_event(uint32_t lvl, uint32_t data, intptr_t pc = 0, intptr_t sp = 0);
```

## Event types

### User events

- 0-999 - levels for use with your user code
- 1000/1001 - exit and return to user code from esp_suspend()
- 2000/2001 - exit and return to user code from yield()
- 3000/3001 - exit and return to user code from esp_delay()

Note that events will only be logged if another task is scheduled, eg. if there's nothing waiting and yield() does nothing then no event is emitted.

### Interrupt events

- 01XX - debug exception
- 02XX - NMI
- 03XX - kernel exception
- 04XX - user exception.  Most interesting one is 0404, a hardware interrupt event
- 05XX - double exception

