#include <Arduino.h>
#include <flash_hal.h>
#include <user_interface.h>
#include <esp8266trace.h>
#include "DynamicBuffer.h"

namespace {
  struct crash_metadata {
    uint32_t magic;
    rst_info info;
    uintptr_t stack, stack_end;
  } __attribute__((aligned(4)));

  constexpr uint32_t MAGIC_NUMBER = 0xDEAD9876U;
  constexpr uint32_t RAM_BASE = 0x3FFE8000;
  constexpr uint32_t RAM_SIZE = 0x18000;
}

extern rst_info resetInfo;

// Stolen from core_esp8266_postmortem
/*
static void ets_printf_P(const char *str, ...) {
    char destStr[160];
    char *c = destStr;
    va_list argPtr;
    va_start(argPtr, str);
    vsnprintf(destStr, sizeof(destStr), str, argPtr);
    va_end(argPtr);
    while (*c) {
        ets_uart_putc1(*(c++));
    }
}
*/

static uintptr_t getCrashFlashAddr() {
  // Immediately after the sketch
  return (ESP.getSketchSize() + FLASH_SECTOR_SIZE - 1) & (~(FLASH_SECTOR_SIZE - 1));
}

extern "C" void custom_crash_callback( struct rst_info * rst_info, uint32_t stack, uint32_t stack_end )
{
  // We're going to store the RAM contents in to the flash for later recovery
  // Use the OTA area immediately after the program
  // First block holds the crash_metadata; subsequent blocks, the RAM contents 
  
  auto addr = getCrashFlashAddr();  
  uint32_t existing_magic = 0;
  ESP.flashRead(addr, &existing_magic, sizeof(existing_magic));
  //ets_printf_P(PSTR("Found magic number: %08x\n"), existing_magic);
  if (existing_magic != 0xFFFFFFFFU) return;   // try to avoid hammering the flash
  
  if ((addr + RAM_SIZE) > (FS_start - 0x40200000)) return; // not enough space????

  // Erase enough flash blocks for size, + 1
  // (The first block is our metadata block)  
  for(auto block = 0U; block < ((RAM_SIZE/FLASH_SECTOR_SIZE)+1U); ++block) {
    ESP.flashEraseSector((addr/FLASH_SECTOR_SIZE) + block);
  }
  {
    crash_metadata meta = { MAGIC_NUMBER, *rst_info, stack, stack_end };
    ESP.flashWrite(addr, (uint32_t*) &meta, sizeof(meta));  // guaranteed alignment, so use uint32_t overload
  }  
  ESP.flashWrite(addr + FLASH_SECTOR_SIZE, (uint32_t*) RAM_BASE, RAM_SIZE);  // all of RAM
}

static const char crashFileName[] PROGMEM = "/dump.txt";

static void cut_here(Print& dest) {
    dest.print('\n');
    for (auto i = 0; i < 15; i++ ) {
        dest.print('-');
    }
    dest.print(F(" CUT HERE FOR EXCEPTION DECODER "));
    for (auto i = 0; i < 15; i++ ) {
        dest.print('-');
    }
    dest.print('\n');  
}

static void print_stack(Print& p, uint32_t start, size_t size, const uint32_t* buf) {
    for (uint32_t pos = 0; pos < size; pos += 0x10) {
        const uint32_t* values = buf + (pos/sizeof(uint32_t));
        // rough indicator: stack frames usually have SP saved as the second word
        bool looksLikeStackFrame = (values[2] == pos + 0x10);
        p.printf_P(PSTR("%08x:  %08x %08x %08x %08x %c\n"),
            start+pos, values[0], values[1], values[2], values[3], (looksLikeStackFrame)?'<':' ');
    }
}


bool ESP8266Trace::crash_data_available() {
  auto addr = getCrashFlashAddr();
  crash_metadata meta = { 0 };
  ESP.flashRead(addr, (uint32_t*) &meta, sizeof(meta));
  if ((meta.magic != MAGIC_NUMBER) && (meta.magic != 0xFFFFFFFF)) {
    // something else is there - you're checking, so obviously you want a crash dump
    // clear it and get ready to make one
    ESP8266Trace::clear_crash_data();
  }
  return (meta.magic == MAGIC_NUMBER);
}

void ESP8266Trace::clear_crash_data() {
  ESP.flashEraseSector(getCrashFlashAddr() / FLASH_SECTOR_SIZE);  
}

void ESP8266Trace::print_crash_data(Print& dest)
{
  // Double check that we've got something
  auto addr = getCrashFlashAddr();
  crash_metadata meta = { 0 };
  ESP.flashRead(addr, (uint32_t*) &meta, sizeof(meta));
  if (meta.magic != MAGIC_NUMBER) return;

  // Print reset info
  if (resetInfo.reason == REASON_WDT_RST) {
    // HWDT - use global resetinfo
    dest.println(ESP.getResetInfo());
  } else {  
    auto ri_backup = resetInfo;
    resetInfo = meta.info;
    dest.println(ESP.getResetInfo());
    resetInfo = ri_backup;
  };

  // Replicate some of the core_esp8266_postmortem print logic
  cut_here(dest); // do we need this?
  dest.print(F("\n>>>stack>>>\n"));
  
  const uint32_t cont_stack_start = (uint32_t) &(g_pcont->stack);
  const uint32_t cont_stack_end = (uint32_t) g_pcont->stack_end;

  if ((meta.stack > cont_stack_start) && (meta.stack < cont_stack_end)) {
    dest.print(F("\nctx: cont\n"));
  } else {
    dest.print(F("\nctx: sys\n"));
  }
  dest.printf_P(PSTR("sp: %08x end: %08x offset: %04x\n"), meta.stack, meta.stack_end, 0);  // TODO - offset??

  // Read and print flash blocks until we're out of space
  DynamicBuffer buf(1024);
  addr += FLASH_SECTOR_SIZE;
  
  size_t offset = meta.stack - RAM_BASE;
  while(offset < (meta.stack_end - RAM_BASE)) {
    size_t print_amount = std::min((meta.stack_end - RAM_BASE) - offset, buf.size());
    ESP.flashRead(addr + offset, reinterpret_cast<uint32_t*>(buf.data()), print_amount);      
    print_stack(dest, meta.stack+offset, print_amount, (uint32_t*) buf.data());
    offset += print_amount;
  }
  dest.print(F("\n<<<stack<<<\n\n"));

  // Also print the event trace
  print_events(dest);
}

void ESP8266Trace::dump_core(Print& dest) {
  if (!crash_data_available()) return;

  // Dump flash contents to dest
  DynamicBuffer buf(1024);
  auto addr = getCrashFlashAddr() + FLASH_SECTOR_SIZE;
  
  size_t offset = 0;
  while(offset < RAM_SIZE) {
    size_t amount = std::min(RAM_SIZE - offset, buf.size());
    ESP.flashRead(addr + offset, reinterpret_cast<uint32_t*>(buf.data()), amount);      
    dest.write(buf.data(), amount);
    offset += amount;
  }
};

