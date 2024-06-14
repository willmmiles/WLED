
#include <Arduino.h>
#include "isr_debug.h"

extern "C" {
#include "user_interface.h"
}

static inline void __wsr_vecbase(uint32_t vector_base) {
	asm volatile("wsr.vecbase %0" :: "r" (vector_base));
}

constexpr auto NUM_EVENT_SLOTS = 16U;
constexpr auto NUM_ISR_VECS = 64U;

typedef void (*isr_func)(void);
isr_func int_vectors[NUM_ISR_VECS];  // storage for original interrupt vectors
extern "C" void track_isr_hook(); // assembly hook

struct event_info {
  uint32_t lvl; // exception level.  High bit is set for s/w events
  intptr_t pc;
  intptr_t sptr;
  uint32_t ccy;
  uint32_t interrupt; // top 16 bits: intenable, bottom 16 bits: interrupt
  uint32_t data;  // can be packed with interesting info, isr leaves 0
};
unsigned event_slot_index = 0;


static inline IRAM_ATTR intptr_t get_vecbase() {
  intptr_t val;
  __asm__ __volatile__("rsr %0,vecbase":"=a"(val));
  return val;    
}

static inline IRAM_ATTR uint32_t get_cause() {
  uint32_t val;
  __asm__ __volatile__("rsr %0,exccause":"=a"(val));
  return val;  
}

static inline IRAM_ATTR uint32_t get_interrupt() {
  uint32_t interrupt;
  __asm__ __volatile__("rsr %0,interrupt":"=a"(interrupt));
  return interrupt;
}

static inline IRAM_ATTR uint32_t get_intenable() {
  uint32_t interrupt;
  __asm__ __volatile__("rsr %0,intenable":"=a"(interrupt));
  return interrupt;
}

static inline IRAM_ATTR uint32_t get_cycle_count() {
  uint32_t ccount;
  __asm__ __volatile__("rsr %0,ccount":"=a"(ccount));
  return ccount;
}

IRAM_ATTR __attribute__((__noinline__)) void track_event(uint32_t lvl, uint32_t data) {
  register intptr_t pc asm("a0"); // dunno if this works?
  register intptr_t sp asm("a1");

  noInterrupts();
  auto info = event_info { 0x80000000U + lvl, pc, sp, get_cycle_count(), get_interrupt() + (get_intenable() << 16), data };
  system_rtc_mem_write(64 + event_slot_index * sizeof(event_info) / 4, (uint32_t*) &info, sizeof(info));
  event_slot_index = (event_slot_index+1)%NUM_EVENT_SLOTS;
  interrupts();
}

void print_events() {  
  auto buf = reinterpret_cast<event_info*>(malloc(NUM_EVENT_SLOTS * sizeof(event_info)));
  if (buf) {
    noInterrupts();
    system_rtc_mem_read(64, buf, NUM_EVENT_SLOTS * sizeof(event_info));
    interrupts();

    // Find minimum point
    auto min_idx = 0U;
    for(auto i = 1U; i < NUM_EVENT_SLOTS; ++i) {
      if (buf[i].ccy < buf[min_idx].ccy) min_idx = i;
    };

    Serial.printf_P(PSTR("ISR log [%u]:\r\n"), get_cycle_count());
    for(auto i = 0U; i < NUM_EVENT_SLOTS; ++i) {
      event_info& info = buf[(min_idx + i) % NUM_EVENT_SLOTS];
      auto id_char = 'I';
      if (info.lvl >= 0x80000000U) {
        id_char = 'U';
        info.lvl -= 0x80000000U;
      }
      Serial.printf_P(PSTR("[%u] - %c%02u  %04X:%04X - %08X %08X - %u\r\n"), info.ccy, id_char, info.lvl, info.interrupt>>16, info.interrupt & 0xFFFF, info.pc, info.sptr, info.data);
    }
    Serial.print(PSTR("\r\n"));
    free(buf);
  } else {
    Serial.println(PSTR("Insufficient RAM to print ISR log!"));
  }
}

void clear_events() {
  char null_entry[sizeof(event_info)];
  memset(&null_entry, 0, sizeof(null_entry));
  for(auto i = 0U; i < NUM_EVENT_SLOTS; ++i) {
    system_rtc_mem_write(64 + i * sizeof(event_info) / 4, (uint32_t*) &null_entry, sizeof(null_entry));
  }
}

void setup_isr_tracking() {
  auto vb = (isr_func*) 0x3fffc000; // ISR table address, from Ghidra  
  noInterrupts();
  memcpy(int_vectors, vb, sizeof(int_vectors));

  // rewrite table entries
  for(auto i = 0U; i < NUM_ISR_VECS; ++i) {
    //if (i == 4) continue;    
    vb[i] = track_isr_hook;
  }

  interrupts();
};  
