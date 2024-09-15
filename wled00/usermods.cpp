#include "wled.h"
/*
 * Registration and management utility for v2 usermods
 */

// Table begin and end references
// Zero-length arrays -- so they'll get assigned addresses, but consume no flash
// The numeric suffix ensures they're put in the right place; the linker script will sort them
// We stick them in the '.dtors' segment because it's always included by the linker scripts
// even though it never gets called.  Who calls exit() in an embedded program anyways?
// If someone ever does, though, it'll explode as these aren't function pointers.
static Usermod * const _usermod_table_begin[0] __attribute__((__section__(".dtors.tbl.usermods.0"), unused)) = {};
static Usermod * const _usermod_table_end[0] __attribute__((__section__(".dtors.tbl.usermods.99"), unused)) = {};

static size_t getCount() {  
  return &_usermod_table_end[0] - &_usermod_table_begin[0];
}

//Usermod Manager internals
size_t usermods_getCount() { return getCount(); };
void usermods_setup()             { for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) (*mod)->setup(); }
void usermods_connected()         { for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) (*mod)->connected(); }
void usermods_loop()              { for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) (*mod)->loop();  }
void usermods_handleOverlayDraw() { for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) (*mod)->handleOverlayDraw(); }
void usermods_appendConfigData()  { for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) (*mod)->appendConfigData(); }
bool usermods_handleButton(uint8_t b) {
  bool overrideIO = false;
  for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) {
    if ((*mod)->handleButton(b)) overrideIO = true;
  }
  return overrideIO;
}
bool getUMData(um_data_t **data, uint8_t mod_id) {
  for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) {
    if (mod_id > 0 && (*mod)->getId() != mod_id) continue;  // only get data form requested usermod if provided
    if ((*mod)->getUMData(data)) return true;               // if usermod does provide data return immediately (only one usermod can provide data at one time)
  }
  return false;
}
void usermods_addToJsonState(JsonObject& obj)    { for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) (*mod)->addToJsonState(obj); }
void usermods_addToJsonInfo(JsonObject& obj)     { 
  DEBUG_PRINTF_P(PSTR("Found %d usermods\n"), getCount());
  for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) (*mod)->addToJsonInfo(obj);
}
void usermods_readFromJsonState(JsonObject& obj) { for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) (*mod)->readFromJsonState(obj); }
void usermods_addToConfig(JsonObject& obj)       { for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) (*mod)->addToConfig(obj); }
bool usermods_readFromConfig(JsonObject& obj)    {
  bool allComplete = true;
  for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) {
    if (!(*mod)->readFromConfig(obj)) allComplete = false;
  }
  return allComplete;
}
#ifndef WLED_DISABLE_MQTT
void usermods_onMqttConnect(bool sessionPresent) { for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) (*mod)->onMqttConnect(sessionPresent); }
bool usermods_onMqttMessage(char* topic, char* payload) {
  for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) if ((*mod)->onMqttMessage(topic, payload)) return true;
  return false;
}
#endif
#ifndef WLED_DISABLE_ESPNOW
bool usermods_onEspNowMessage(uint8_t* sender, uint8_t* payload, uint8_t len) {
  for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) if ((*mod)->onEspNowMessage(sender, payload, len)) return true;
  return false;
}
#endif
void usermods_onUpdateBegin(bool init) { for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) (*mod)->onUpdateBegin(init); } // notify usermods that update is to begin
void usermods_onStateChange(uint8_t mode) { for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) (*mod)->onStateChange(mode); } // notify usermods that WLED state changed

/*
 * Enables usermods to lookup another Usermod.
 */
Usermod* usermod_lookup(uint16_t mod_id) {
  for (auto mod = _usermod_table_begin; mod < _usermod_table_end; ++mod) {
    if ((*mod)->getId() == mod_id) {
      return *mod;
    }
  }
  return nullptr;
}

