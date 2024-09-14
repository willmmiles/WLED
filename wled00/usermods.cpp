#include "wled.h"
/*
 * Registration and management utility for v2 usermods
 */

// Global usermod instance list
static Usermod* ums[WLED_MAX_USERMODS];
static byte numMods = 0;  // static initialization safe

Usermod::Usermod() : um_data(nullptr) {
  // Register the usermod
  if (numMods < WLED_MAX_USERMODS) {
    ums[numMods++] = this;
  }
} 

//Usermod Manager internals
size_t usermods_getCount() { return numMods; };
void usermods_setup()             { for (unsigned i = 0; i < numMods; i++) ums[i]->setup(); }
void usermods_connected()         { for (unsigned i = 0; i < numMods; i++) ums[i]->connected(); }
void usermods_loop()              { for (unsigned i = 0; i < numMods; i++) ums[i]->loop();  }
void usermods_handleOverlayDraw() { for (unsigned i = 0; i < numMods; i++) ums[i]->handleOverlayDraw(); }
void usermods_appendConfigData()  { for (unsigned i = 0; i < numMods; i++) ums[i]->appendConfigData(); }
bool usermods_handleButton(uint8_t b) {
  bool overrideIO = false;
  for (unsigned i = 0; i < numMods; i++) {
    if (ums[i]->handleButton(b)) overrideIO = true;
  }
  return overrideIO;
}
bool getUMData(um_data_t **data, uint8_t mod_id) {
  for (unsigned i = 0; i < numMods; i++) {
    if (mod_id > 0 && ums[i]->getId() != mod_id) continue;  // only get data form requested usermod if provided
    if (ums[i]->getUMData(data)) return true;               // if usermod does provide data return immediately (only one usermod can provide data at one time)
  }
  return false;
}
void usermods_addToJsonState(JsonObject& obj)    { for (unsigned i = 0; i < numMods; i++) ums[i]->addToJsonState(obj); }
void usermods_addToJsonInfo(JsonObject& obj)     { for (unsigned i = 0; i < numMods; i++) ums[i]->addToJsonInfo(obj); }
void usermods_readFromJsonState(JsonObject& obj) { for (unsigned i = 0; i < numMods; i++) ums[i]->readFromJsonState(obj); }
void usermods_addToConfig(JsonObject& obj)       { for (unsigned i = 0; i < numMods; i++) ums[i]->addToConfig(obj); }
bool usermods_readFromConfig(JsonObject& obj)    {
  bool allComplete = true;
  for (unsigned i = 0; i < numMods; i++) {
    if (!ums[i]->readFromConfig(obj)) allComplete = false;
  }
  return allComplete;
}
#ifndef WLED_DISABLE_MQTT
void usermods_onMqttConnect(bool sessionPresent) { for (unsigned i = 0; i < numMods; i++) ums[i]->onMqttConnect(sessionPresent); }
bool usermods_onMqttMessage(char* topic, char* payload) {
  for (unsigned i = 0; i < numMods; i++) if (ums[i]->onMqttMessage(topic, payload)) return true;
  return false;
}
#endif
#ifndef WLED_DISABLE_ESPNOW
bool usermods_onEspNowMessage(uint8_t* sender, uint8_t* payload, uint8_t len) {
  for (unsigned i = 0; i < numMods; i++) if (ums[i]->onEspNowMessage(sender, payload, len)) return true;
  return false;
}
#endif
void usermods_onUpdateBegin(bool init) { for (unsigned i = 0; i < numMods; i++) ums[i]->onUpdateBegin(init); } // notify usermods that update is to begin
void usermods_onStateChange(uint8_t mode) { for (unsigned i = 0; i < numMods; i++) ums[i]->onStateChange(mode); } // notify usermods that WLED state changed

/*
 * Enables usermods to lookup another Usermod.
 */
Usermod* usermod_lookup(uint16_t mod_id) {
  for (unsigned i = 0; i < numMods; i++) {
    if (ums[i]->getId() == mod_id) {
      return ums[i];
    }
  }
  return nullptr;
}

