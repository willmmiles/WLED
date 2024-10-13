#include <Arduino.h>
#include "esp8266trace.h"

constexpr auto NUM_EVENT_SLOTS = 64U;   // Must also be changed in event_trace_isr_asm.S

struct event_info {
  uint32_t lvl; // exception level.  High bit is set for s/w events
  intptr_t pc;
  intptr_t sptr;
  uint32_t ccy;
  uint32_t interrupt; // top 16 bits: intenable, bottom 16 bits: interrupt
  uint32_t data;  // can be packed with interesting info, isr leaves 0
};
unsigned event_slot_index = 0;
event_info event_buf[NUM_EVENT_SLOTS] __attribute__((section(".noinit")));  // placed in noinit section to survive reboots

static inline __attribute__((always_inline)) uint32_t get_interrupt() {
  uint32_t interrupt;
  __asm__ __volatile__("rsr %0,interrupt":"=a"(interrupt));
  return interrupt;
}

static inline __attribute__((always_inline)) uint32_t get_intenable() {
  uint32_t interrupt;
  __asm__ __volatile__("rsr %0,intenable":"=a"(interrupt));
  return interrupt;
}

static inline __attribute__((always_inline)) uint32_t get_cycle_count() {
  uint32_t ccount;
  __asm__ __volatile__("rsr %0,ccount":"=a"(ccount));
  return ccount;
}

IRAM_ATTR __attribute__((__noinline__)) void ESP8266Trace::track_event(uint32_t lvl, uint32_t data, intptr_t pc, intptr_t sp) {
  register intptr_t pcx asm("a0"); // dunno if this works?
  register intptr_t spx asm("a1");
  
  uint32_t savedPS = xt_rsil(15);
  ++event_slot_index;  
  event_buf[event_slot_index & (NUM_EVENT_SLOTS-1)] = event_info { lvl, pc ? pc : pcx, sp ? sp : spx, get_cycle_count(), get_interrupt() + (get_intenable() << 16), data };
  xt_wsr_ps(savedPS);
}

void ESP8266Trace::print_events(Print& p) {  
  auto buf = reinterpret_cast<event_info*>(malloc(NUM_EVENT_SLOTS * sizeof(event_info)));
  if (buf) {
    noInterrupts();
    memcpy(buf, event_buf, sizeof(event_buf));
    interrupts();

    // Find minimum point
    auto min_idx = 0U;
    for(auto i = 1U; i < NUM_EVENT_SLOTS; ++i) {
      if (buf[i].ccy < buf[min_idx].ccy) min_idx = i;
    };

    p.printf_P(PSTR("ISR log [%u]:\n"), get_cycle_count());
    for(auto i = 0U; i < NUM_EVENT_SLOTS; ++i) {
      event_info& info = buf[(min_idx + i) % NUM_EVENT_SLOTS];
      auto id_char = 'U';
      if (info.lvl >= 0x80000000U) {
        id_char = 'I';
        info.lvl -= 0x80000000U;
      }
      p.printf_P(PSTR("[%u] - %c%04u  %04X:%04X - %08X %08X - %08X\n"), info.ccy, id_char, info.lvl, info.interrupt>>16, info.interrupt & 0xFFFF, info.pc, info.sptr, info.data);
    }
    p.print(F("\n"));
    free(buf);
  } else {
    p.println(F("Insufficient RAM to print ISR log!"));
  }
}

void ESP8266Trace::clear_events() {
  noInterrupts();
  memset(event_buf, 0, sizeof(event_buf));
  interrupts();
}


extern "C" void InstrumentedVectorTable();
void ESP8266Trace::setup_isr_tracking() {
  const intptr_t our_vecbase = (intptr_t) &InstrumentedVectorTable;
  int32_t old_vb;
  __asm__ __volatile__("rsr %0,vecbase":"=a"(old_vb));
  //Serial.printf_P(PSTR("Vector base: %08X -> %08X\n"), old_vb, our_vecbase);
  __asm__ __volatile__("wsr %0,vecbase"::"a"(our_vecbase));
}


// Hook the scheduler
static inline bool track_active_tasks(int id, intptr_t pc, intptr_t sp, bool force) {
  auto active_tasks = *(uint32_t*)0x3FFFDAB8;
  if (force || (active_tasks & 0xFFFFFFFE)) {  // ignore the 1st task (the user task); otherwise a delay() or yield() loop generates a *lot* of events
    ESP8266Trace::track_event(id, active_tasks, pc, sp);
    return true;
  }
  return false;
}

extern "C" void __esp_suspend();
extern "C" IRAM_ATTR void esp_suspend() {
  register intptr_t pcx asm("a0");
  register intptr_t spx asm("a1");
  auto r = track_active_tasks(1000, pcx, spx, false);
  __esp_suspend();
  track_active_tasks(1001, pcx, spx, r);
}

extern "C" void __esp_delay(unsigned long ms);
extern "C" void esp_delay(unsigned long ms) {
  register intptr_t pcx asm("a0");
  register intptr_t spx asm("a1");
  auto r = track_active_tasks(3000, pcx, spx, false);
  __esp_delay(ms);
  track_active_tasks(3001, pcx, spx, r);
}

extern "C" void __yield();
extern "C" void yield() {
  register intptr_t pcx asm("a0");
  register intptr_t spx asm("a1");
  auto r = track_active_tasks(2000, pcx, spx, false);
  __yield();
  track_active_tasks(2001, pcx, spx, r);
}
