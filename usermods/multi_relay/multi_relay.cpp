#include "wled.h"

#define COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#ifndef MULTI_RELAY_MAX_RELAYS
  #define MULTI_RELAY_MAX_RELAYS 4
#else
  #if MULTI_RELAY_MAX_RELAYS>8
    #undef MULTI_RELAY_MAX_RELAYS
    #define MULTI_RELAY_MAX_RELAYS 8
    #warning Maximum relays set to 8
  #endif
#endif

#ifndef MULTI_RELAY_PINS
  #define MULTI_RELAY_PINS -1
  #define MULTI_RELAY_ENABLED false
#else
  #define MULTI_RELAY_ENABLED true
#endif

#ifndef MULTI_RELAY_HA_DISCOVERY
  #define MULTI_RELAY_HA_DISCOVERY false
#endif

#ifndef MULTI_RELAY_DELAYS
  #define MULTI_RELAY_DELAYS 0
#endif

#ifndef MULTI_RELAY_EXTERNALS
  #define MULTI_RELAY_EXTERNALS false
#endif

#ifndef MULTI_RELAY_INVERTS
  #define MULTI_RELAY_INVERTS false
#endif

#define WLED_DEBOUNCE_THRESHOLD 50 //only consider button input of at least 50ms as valid (debouncing)

#define ON  true
#define OFF false

#ifndef USERMOD_USE_PCF8574
  #undef USE_PCF8574
  #define USE_PCF8574 false
#else
  #undef USE_PCF8574
  #define USE_PCF8574 true
#endif

#ifndef PCF8574_ADDRESS
  #define PCF8574_ADDRESS 0x20  // some may start at 0x38
#endif

/*
 * This usermod handles multiple relay outputs.
 * These outputs complement built-in relay output in a way that the activation can be delayed.
 * They can also activate/deactivate in reverse logic independently.
 * 
 * Written and maintained by @blazoncek
 */


typedef struct relay_t {
  int8_t pin;
  struct { // reduces memory footprint
    bool active   : 1;  // is the relay waiting to be switched
    bool invert   : 1;  // does On mean 1 or 0
    bool state    : 1;  // 1 relay is On, 0 relay is Off
    bool external : 1;  // is the relay externally controlled
    int8_t button : 4;  // which button triggers relay
  };
  uint16_t delay;       // amount of ms to wait after it is activated
} Relay;


class MultiRelay : public Usermod {

  private:
    // array of relays
    Relay    _relay[MULTI_RELAY_MAX_RELAYS];

    uint32_t _switchTimerStart; // switch timer start time
    bool     _oldMode;          // old brightness
    bool     enabled;           // usermod enabled
    bool     initDone;          // status of initialisation
    bool     usePcf8574;
    uint8_t  addrPcf8574;
    bool     HAautodiscovery;
    uint16_t periodicBroadcastSec;
    unsigned long lastBroadcast;

    // strings to reduce flash memory usage (used more than twice)
    static const char _name[];
    static const char _enabled[];
    static const char _relay_str[];
    static const char _delay_str[];
    static const char _activeHigh[];
    static const char _external[];
    static const char _button[];
    static const char _broadcast[];
    static const char _HAautodiscovery[];
    static const char _pcf8574[];
    static const char _pcfAddress[];
    static const char _switch[];
    static const char _toggle[];
    static const char _Command[];

    void handleOffTimer();
    void InitHtmlAPIHandle();
    int getValue(String data, char separator, int index);
    uint8_t getActiveRelayCount();

    byte IOexpanderWrite(byte address, byte _data);
    byte IOexpanderRead(int address);

    void publishMqtt(int relay);
#ifndef WLED_DISABLE_MQTT
    void publishHomeAssistantAutodiscovery();
#endif

  public:
    /**
     * constructor
     */
    MultiRelay();

    /**
     * desctructor
     */
    //~MultiRelay() {}

    /**
     * Enable/Disable the usermod
     */
    inline void enable(bool enable) { enabled = enable; }

    /**
     * Get usermod enabled/disabled state
     */
    inline bool isEnabled() { return enabled; }

    /**
     * getId() allows you to optionally give your V2 usermod an unique ID (please define it in const.h!).
     * This could be used in the future for the system to determine whether your usermod is installed.
     */
    inline uint16_t getId() override { return USERMOD_ID_MULTI_RELAY; }

    /**
     * switch relay on/off
     */
    void switchRelay(uint8_t relay, bool mode);

    /**
     * toggle relay
     */
    inline void toggleRelay(uint8_t relay) {
      switchRelay(relay, !_relay[relay].state);
    }

    /**
     * setup() is called once at boot. WiFi is not yet connected at this point.
     * You can use it to initialize variables, sensors or similar.
     */
    void setup() override;

    /**
     * connected() is called every time the WiFi is (re)connected
     * Use it to initialize network interfaces
     */
    inline void connected() override { InitHtmlAPIHandle(); }

    /**
     * loop() is called continuously. Here you can check for events, read sensors, etc.
     */
    void loop() override;

#ifndef WLED_DISABLE_MQTT
    bool onMqttMessage(char* topic, char* payload) override;
    void onMqttConnect(bool sessionPresent) override;
#endif

    /**
     * handleButton() can be used to override default button behaviour. Returning true
     * will prevent button working in a default way.
     * Replicating button.cpp
     */
    bool handleButton(uint8_t b) override;

    /**
     * addToJsonInfo() can be used to add custom entries to the /json/info part of the JSON API.
     */
    void addToJsonInfo(JsonObject &root) override;

    /**
     * addToJsonState() can be used to add custom entries to the /json/state part of the JSON API (state object).
     * Values in the state object may be modified by connected clients
     */
    void addToJsonState(JsonObject &root) override;

    /**
     * readFromJsonState() can be used to receive data clients send to the /json/state part of the JSON API (state object).
     * Values in the state object may be modified by connected clients
     */
    void readFromJsonState(JsonObject &root) override;

    /**
     * provide the changeable values
     */
    void addToConfig(JsonObject &root) override;

    void appendConfigData() override;

    /**
     * restore the changeable values
     * readFromConfig() is called before setup() to populate properties from values stored in cfg.json
     * 
     * The function should return true if configuration was successfully loaded or false if there was no configuration.
     */
    bool readFromConfig(JsonObject &root) override;
};


// class implementation

void MultiRelay::publishMqtt(int relay) {
#ifndef WLED_DISABLE_MQTT
  //Check if MQTT Connected, otherwise it will crash the 8266
  if (WLED_MQTT_CONNECTED){
    char subuf[64];
    sprintf(subuf, "%s/relay/%d", mqttDeviceTopic, relay);
    mqtt->publish(subuf, 0, false, _relay[relay].state ? "on" : "off");
  }
#endif
}

/**
 * switch off the strip if the delay has elapsed 
 */
void MultiRelay::handleOffTimer() {
  unsigned long now = millis();
  bool activeRelays = false;
  for (int i=0; i<MULTI_RELAY_MAX_RELAYS; i++) {
    if (_relay[i].active && _switchTimerStart > 0 && now - _switchTimerStart > (_relay[i].delay*1000)) {
      if (!_relay[i].external) switchRelay(i, !offMode);
      _relay[i].active = false;
    } else if (periodicBroadcastSec && now - lastBroadcast > (periodicBroadcastSec*1000)) {
      if (_relay[i].pin>=0) publishMqtt(i);
    }
    activeRelays = activeRelays || _relay[i].active;
  }
  if (!activeRelays) _switchTimerStart = 0;
  if (periodicBroadcastSec && now - lastBroadcast > (periodicBroadcastSec*1000)) lastBroadcast = now;
}

/**
 * HTTP API handler
 * borrowed from:
 * https://github.com/gsieben/WLED/blob/master/usermods/GeoGab-Relays/usermod_GeoGab.h
 */
#define GEOGABVERSION "0.1.3"
void MultiRelay::InitHtmlAPIHandle() {  // https://github.com/me-no-dev/ESPAsyncWebServer
  DEBUG_PRINTLN("Relays: Initialize HTML API");

  server.on("/relays", HTTP_GET, [this](AsyncWebServerRequest *request) {
    DEBUG_PRINTLN("Relays: HTML API");
    String janswer;
    String error = "";
    //int params = request->params();
    janswer = "{\"NoOfRelays\":";
    janswer += String(MULTI_RELAY_MAX_RELAYS) + ",";

    if (getActiveRelayCount()) {
      // Commands
      if (request->hasParam(_switch)) {
        /**** Switch ****/
        AsyncWebParameter* p = request->getParam(_switch);
        // Get Values
        for (int i=0; i<MULTI_RELAY_MAX_RELAYS; i++) {
          int value = getValue(p->value(), ',', i);
          if (value==-1) {
            error = "There must be as many arguments as relays";
          } else {
            // Switch
            if (_relay[i].external) switchRelay(i, (bool)value);
          }
        }
      } else if (request->hasParam(_toggle)) {
        /**** Toggle ****/
        AsyncWebParameter* p = request->getParam(_toggle);
        // Get Values
        for (int i=0;i<MULTI_RELAY_MAX_RELAYS;i++) {
          int value = getValue(p->value(), ',', i);
          if (value==-1) {
            error = "There must be as many arguments as relays";
          } else {
            // Toggle
            if (value && _relay[i].external) toggleRelay(i);
          }
        }
      } else {
        error = "No valid command found";
      }
    } else {
      error = "No active relays";
    }

    // Status response
    char sbuf[16];
    for (int i=0; i<MULTI_RELAY_MAX_RELAYS; i++) {
      sprintf(sbuf, "\"%d\":%d,", i, (_relay[i].pin<0 ? -1 : (int)_relay[i].state));
      janswer += sbuf;
    }
    janswer += "\"error\":\"";
    janswer += error;
    janswer += "\",";
    janswer += "\"SW Version\":\"";
    janswer += String(GEOGABVERSION);
    janswer += "\"}";
    request->send(200, "application/json", janswer);
  });
}

int MultiRelay::getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i)==separator || i==maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }
  return found>index ? data.substring(strIndex[0], strIndex[1]).toInt() : -1;
}

//Write a byte to the IO expander
byte MultiRelay::IOexpanderWrite(byte address, byte _data ) {
  Wire.beginTransmission(address);
  Wire.write(_data);
  return Wire.endTransmission(); 
}

//Read a byte from the IO expander
byte MultiRelay::IOexpanderRead(int address) {
  byte _data = 0;
  Wire.requestFrom(address, 1);
  if (Wire.available()) {
    _data = Wire.read();
  }
  return _data;
}


// public methods

MultiRelay::MultiRelay()
  : _switchTimerStart(0)
  , enabled(MULTI_RELAY_ENABLED)
  , initDone(false)
  , usePcf8574(USE_PCF8574)
  , addrPcf8574(PCF8574_ADDRESS)
  , HAautodiscovery(MULTI_RELAY_HA_DISCOVERY)
  , periodicBroadcastSec(60)
  , lastBroadcast(0)
{
  const int8_t defPins[] = {MULTI_RELAY_PINS};
  const int8_t relayDelays[] = {MULTI_RELAY_DELAYS};
  const bool relayExternals[] = {MULTI_RELAY_EXTERNALS};
  const bool relayInverts[] = {MULTI_RELAY_INVERTS};

  for (size_t i=0; i<MULTI_RELAY_MAX_RELAYS; i++) {
    _relay[i].pin      = i < COUNT_OF(defPins) ? defPins[i] : -1;
    _relay[i].delay    = i < COUNT_OF(relayDelays) ? relayDelays[i] : 0;
    _relay[i].invert   = i < COUNT_OF(relayInverts) ? relayInverts[i] : false;
    _relay[i].active   = false;
    _relay[i].state    = false;
    _relay[i].external = i < COUNT_OF(relayExternals) ? relayExternals[i] : false;
    _relay[i].button   = -1;
  }
}

/**
 * switch relay on/off
 */
void MultiRelay::switchRelay(uint8_t relay, bool mode) {
  if (relay>=MULTI_RELAY_MAX_RELAYS || _relay[relay].pin<0) return;
  _relay[relay].state = mode;
  if (usePcf8574 && _relay[relay].pin >= 100) {
    // we need to send all outputs at the same time
    uint8_t state = 0;
    for (int i=0; i<MULTI_RELAY_MAX_RELAYS; i++) {
      if (_relay[i].pin < 100) continue;
      uint8_t pin = _relay[i].pin - 100;
      state |= (_relay[i].invert ? !_relay[i].state : _relay[i].state) << pin; // fill relay states for all pins
    }
    IOexpanderWrite(addrPcf8574, state);
    DEBUG_PRINT("Writing to PCF8574: "); DEBUG_PRINTLN(state);
  } else if (_relay[relay].pin < 100) {
    pinMode(_relay[relay].pin, OUTPUT);
    digitalWrite(_relay[relay].pin, _relay[relay].invert ? !_relay[relay].state : _relay[relay].state);
  } else return;
  publishMqtt(relay);
}

uint8_t MultiRelay::getActiveRelayCount() {
  uint8_t count = 0;
  for (int i=0; i<MULTI_RELAY_MAX_RELAYS; i++) if (_relay[i].pin>=0) count++;
  return count;
}


//Functions called by WLED

#ifndef WLED_DISABLE_MQTT
/**
 * handling of MQTT message
 * topic only contains stripped topic (part after /wled/MAC)
 * topic should look like: /relay/X/command; where X is relay number, 0 based
 */
bool MultiRelay::onMqttMessage(char* topic, char* payload) {
  if (strlen(topic) > 8 && strncmp(topic, "/relay/", 7) == 0 && strncmp(topic+8, _Command, 8) == 0) {
    uint8_t relay = strtoul(topic+7, NULL, 10);
    if (relay<MULTI_RELAY_MAX_RELAYS) {
      String action = payload;
      if (action == "on") {
        if (_relay[relay].external) switchRelay(relay, true);
        return true;
      } else if (action == "off") {
        if (_relay[relay].external) switchRelay(relay, false);
        return true;
      } else if (action == _toggle) {
        if (_relay[relay].external) toggleRelay(relay);
        return true;
      }
    }
  }
  return false;
}

/**
 * subscribe to MQTT topic for controlling relays
 */
void MultiRelay::onMqttConnect(bool sessionPresent) {
  //(re)subscribe to required topics
  char subuf[64];
  if (mqttDeviceTopic[0] != 0) {
    strcpy(subuf, mqttDeviceTopic);
    strcat(subuf, "/relay/#");
    mqtt->subscribe(subuf, 0);
    if (HAautodiscovery) publishHomeAssistantAutodiscovery();
    for (int i=0; i<MULTI_RELAY_MAX_RELAYS; i++) {
      if (_relay[i].pin<0) continue;
      publishMqtt(i); //publish current state
    }
  }
}

void MultiRelay::publishHomeAssistantAutodiscovery() {
  for (int i = 0; i < MULTI_RELAY_MAX_RELAYS; i++) {
    char uid[24], json_str[1024], buf[128];
    size_t payload_size;
    sprintf(uid, "%s_sw%d", escapedMac.c_str(), i);

    if (_relay[i].pin >= 0 && _relay[i].external) {
      StaticJsonDocument<1024> json;
      sprintf(buf, "%s Switch %d", serverDescription, i); //max length: 33 + 8 + 3 = 44
      json["name"] = buf;

      sprintf(buf, "%s/relay/%d", mqttDeviceTopic, i); //max length: 33 + 7 + 3 = 43
      json["~"] = buf;
      strcat(buf, _Command);
      mqtt->subscribe(buf, 0);

      json["stat_t"]  = "~";
      json["cmd_t"]   = "~/command";
      json["pl_off"]  = "off";
      json["pl_on"]   = "on";
      json["uniq_id"] = uid;

      strcpy(buf, mqttDeviceTopic); //max length: 33 + 7 = 40
      strcat(buf, "/status");
      json["avty_t"]       = buf;
      json["pl_avail"]     = "online";
      json["pl_not_avail"] = "offline";
      //TODO: dev
      payload_size = serializeJson(json, json_str);
    } else {
      //Unpublish disabled or internal relays
      json_str[0]  = 0;
      payload_size = 0;
    }
    sprintf(buf, "homeassistant/switch/%s/config", uid);
    mqtt->publish(buf, 0, true, json_str, payload_size);
  }
}
#endif

/**
 * setup() is called once at boot. WiFi is not yet connected at this point.
 * You can use it to initialize variables, sensors or similar.
 */
void MultiRelay::setup() {
  // pins retrieved from cfg.json (readFromConfig()) prior to running setup()
  // if we want PCF8574 expander I2C pins need to be valid
  if (i2c_sda<0 || i2c_scl<0) usePcf8574 = false;

  uint8_t state = 0;
  for (int i=0; i<MULTI_RELAY_MAX_RELAYS; i++) {
    if (usePcf8574 && _relay[i].pin >= 100) {
      uint8_t pin = _relay[i].pin - 100;
      if (!_relay[i].external) _relay[i].state = !offMode;
      state |= (uint8_t)(_relay[i].invert ? !_relay[i].state : _relay[i].state) << pin;
    } else if (_relay[i].pin<100 && _relay[i].pin>=0) {
      if (PinManager::allocatePin(_relay[i].pin,true, PinOwner::UM_MultiRelay)) {
        if (!_relay[i].external) _relay[i].state = !offMode;
        switchRelay(i, _relay[i].state);
        _relay[i].active = false;
      } else {
        _relay[i].pin = -1;  // allocation failed
      }
    }
  }
  if (usePcf8574) {
    IOexpanderWrite(addrPcf8574, state);  // init expander (set all outputs)
    DEBUG_PRINTLN("PCF8574(s) inited.");
  }
  _oldMode = offMode;
  initDone = true;
}

/**
 * loop() is called continuously. Here you can check for events, read sensors, etc.
 */
void MultiRelay::loop() {
  static unsigned long lastUpdate = 0;
  yield();
  if (!enabled || (strip.isUpdating() && millis() - lastUpdate < 100)) return;

  if (millis() - lastUpdate < 100) return;  // update only 10 times/s
  lastUpdate = millis();

  //set relay when LEDs turn on
  if (_oldMode != offMode) {
    _oldMode = offMode;
    _switchTimerStart = millis();
    for (int i=0; i<MULTI_RELAY_MAX_RELAYS; i++) {
      if ((_relay[i].pin>=0) && !_relay[i].external) _relay[i].active = true;
    }
  }

  handleOffTimer();
}

/**
 * handleButton() can be used to override default button behaviour. Returning true
 * will prevent button working in a default way.
 * Replicating button.cpp
 */
bool MultiRelay::handleButton(uint8_t b) {
  yield();
  if (!enabled
    || buttons[b].type == BTN_TYPE_NONE
    || buttons[b].type == BTN_TYPE_RESERVED
    || buttons[b].type == BTN_TYPE_PIR_SENSOR
    || buttons[b].type == BTN_TYPE_ANALOG
    || buttons[b].type == BTN_TYPE_ANALOG_INVERTED) {
    return false;
  }

  bool handled = false;
  for (int i=0; i<MULTI_RELAY_MAX_RELAYS; i++) {
    if (_relay[i].button == b && _relay[i].external) {
      handled = true;
    }
  }
  if (!handled) return false;

  unsigned long now = millis();

  //button is not momentary, but switch. This is only suitable on pins whose on-boot state does not matter (NOT gpio0)
  if (buttons[b].type == BTN_TYPE_SWITCH) {
    //handleSwitch(b);
    if (buttons[b].pressedBefore != isButtonPressed(b)) {
      buttons[b].pressedTime = now;
      buttons[b].pressedBefore = !buttons[b].pressedBefore;
    }

    if (buttons[b].longPressed == buttons[b].pressedBefore) return handled;
      
    if (now - buttons[b].pressedTime > WLED_DEBOUNCE_THRESHOLD) { //fire edge event only after 50ms without change (debounce)
      for (int i=0; i<MULTI_RELAY_MAX_RELAYS; i++) {
        if (_relay[i].button == b) {
          switchRelay(i, buttons[b].pressedBefore);
          buttons[b].longPressed = buttons[b].pressedBefore; //save the last "long term" switch state
        }
      }
    }
    return handled;
  }

  //momentary button logic
  if (isButtonPressed(b)) { //pressed

    if (!buttons[b].pressedBefore) buttons[b].pressedTime = now;
    buttons[b].pressedBefore = true;

    if (now - buttons[b].pressedTime > 600) { //long press
      //longPressAction(b); //not exposed
      //handled = false; //use if you want to pass to default behaviour
      buttons[b].longPressed = true;
    }

  } else if (!isButtonPressed(b) && buttons[b].pressedBefore) { //released

    long dur = now - buttons[b].pressedTime;
    if (dur < WLED_DEBOUNCE_THRESHOLD) {
      buttons[b].pressedBefore = false;
      return handled;
    } //too short "press", debounce
    bool doublePress = buttons[b].waitTime; //did we have short press before?
    buttons[b].waitTime = 0;

    if (!buttons[b].longPressed) { //short press
      // if this is second release within 350ms it is a double press (buttonWaitTime!=0)
      if (doublePress) {
        //doublePressAction(b); //not exposed
        //handled = false; //use if you want to pass to default behaviour
      } else  {
        buttons[b].waitTime = now;
      }
    }
    buttons[b].pressedBefore = false;
    buttons[b].longPressed = false;
  }
  // if 350ms elapsed since last press/release it is a short press
  if (buttons[b].waitTime && now - buttons[b].waitTime > 350 && !buttons[b].pressedBefore) {
    buttons[b].waitTime = 0;
    //shortPressAction(b); //not exposed
    for (int i=0; i<MULTI_RELAY_MAX_RELAYS; i++) {
      if (_relay[i].button == b) {
        toggleRelay(i);
      }
    }
  }
  return handled;
}

/**
 * addToJsonInfo() can be used to add custom entries to the /json/info part of the JSON API.
 */
void MultiRelay::addToJsonInfo(JsonObject &root) {
  if (enabled) {
    JsonObject user = root["u"];
    if (user.isNull())
      user = root.createNestedObject("u");

    JsonArray infoArr = user.createNestedArray(_name); //name
    infoArr.add(String(getActiveRelayCount()));
    infoArr.add(" relays");

    String uiDomString;
    for (int i=0; i<MULTI_RELAY_MAX_RELAYS; i++) {
      if (_relay[i].pin<0 || !_relay[i].external) continue;
      uiDomString = "Relay "; uiDomString += i;
      infoArr = user.createNestedArray(uiDomString); // timer value

      uiDomString = "<button class=\"btn btn-xs\" onclick=\"requestJson({";
      uiDomString += _name;
      uiDomString += ":{";
      uiDomString += _relay_str;
      uiDomString += ":";
      uiDomString += i;
      uiDomString += ",on:";
      uiDomString += _relay[i].state ? "false" : "true";
      uiDomString += "}});\">";
      uiDomString += "<i class=\"icons ";
      uiDomString += _relay[i].state ? "on" : "off";
      uiDomString += "\">&#xe08f;</i></button>";
      infoArr.add(uiDomString);
    }
  }
}

/**
 * addToJsonState() can be used to add custom entries to the /json/state part of the JSON API (state object).
 * Values in the state object may be modified by connected clients
 */
void MultiRelay::addToJsonState(JsonObject &root) {
  if (!initDone || !enabled) return;  // prevent crash on boot applyPreset()
  JsonObject multiRelay = root[_name];
  if (multiRelay.isNull()) {
    multiRelay = root.createNestedObject(_name);
  }
  #if MULTI_RELAY_MAX_RELAYS > 1
  JsonArray rel_arr = multiRelay.createNestedArray("relays");
  for (int i=0; i<MULTI_RELAY_MAX_RELAYS; i++) {
    if (_relay[i].pin < 0) continue;
    JsonObject relay = rel_arr.createNestedObject();
    relay[_relay_str] = i;
    relay["state"] = _relay[i].state;
  }
  #else
  multiRelay[_relay_str] = 0;
  multiRelay["state"] = _relay[0].state;
  #endif
}

/**
 * readFromJsonState() can be used to receive data clients send to the /json/state part of the JSON API (state object).
 * Values in the state object may be modified by connected clients
 */
void MultiRelay::readFromJsonState(JsonObject &root) {
  if (!initDone || !enabled) return;  // prevent crash on boot applyPreset()
  JsonObject usermod = root[_name];
  if (!usermod.isNull()) {
    if (usermod[_relay_str].is<int>() && usermod[_relay_str].as<int>()>=0) {
      int rly = usermod[_relay_str].as<int>();
      if (usermod["on"].is<bool>()) {
        switchRelay(rly, usermod["on"].as<bool>());
      } else if (usermod["on"].is<const char*>() && usermod["on"].as<const char*>()[0] == 't') {
        toggleRelay(rly);
      }
    }
  } else if (root[_name].is<JsonArray>()) {
    JsonArray relays = root[_name].as<JsonArray>();
    for (JsonVariant r : relays) {
      if (r[_relay_str].is<int>() && r[_relay_str].as<int>()>=0) {
        int rly = r[_relay_str].as<int>();
        if (r["on"].is<bool>()) {
          switchRelay(rly, r["on"].as<bool>());
        } else if (r["on"].is<const char*>() && r["on"].as<const char*>()[0] == 't') {
          toggleRelay(rly);
        }
      }
    }
  }
}

/**
 * provide the changeable values
 */
void MultiRelay::addToConfig(JsonObject &root) {
  JsonObject top = root.createNestedObject(_name);

  top[_enabled] = enabled;
  top[_pcf8574] = usePcf8574;
  top[_pcfAddress] = addrPcf8574;
  top[_broadcast] = periodicBroadcastSec;
  top[_HAautodiscovery] = HAautodiscovery;
  for (int i=0; i<MULTI_RELAY_MAX_RELAYS; i++) {
    String parName = _relay_str; parName += '-'; parName += i;
    JsonObject relay = top.createNestedObject(parName);
    relay["pin"]              = _relay[i].pin;
    relay[_activeHigh] = _relay[i].invert;
    relay[_delay_str]  = _relay[i].delay;
    relay[_external]   = _relay[i].external;
    relay[_button]     = _relay[i].button;
  }
  DEBUG_PRINTLN("MultiRelay config saved.");
}

void MultiRelay::appendConfigData() {
  oappend("addInfo('MultiRelay:PCF8574-address',1,'<i>(not hex!)</i>');");
  oappend("addInfo('MultiRelay:broadcast-sec',1,'(MQTT message)');");
  //oappend("addInfo('MultiRelay:relay-0:pin',1,'(use -1 for PCF8574)');");
  oappend("d.extra.push({'MultiRelay':{pin:[['P0',100],['P1',101],['P2',102],['P3',103],['P4',104],['P5',105],['P6',106],['P7',107]]}});");
}

/**
 * restore the changeable values
 * readFromConfig() is called before setup() to populate properties from values stored in cfg.json
 * 
 * The function should return true if configuration was successfully loaded or false if there was no configuration.
 */
bool MultiRelay::readFromConfig(JsonObject &root) {
  int8_t oldPin[MULTI_RELAY_MAX_RELAYS];

  JsonObject top = root[_name];
  if (top.isNull()) {
    DEBUG_PRINT(_name);
    DEBUG_PRINTLN(": No config found. (Using defaults.)");
    return false;
  }

  //bool configComplete = !top.isNull();
  //configComplete &= getJsonValue(top[_enabled], enabled);
  enabled = top[_enabled] | enabled;
  usePcf8574 = top[_pcf8574] | usePcf8574;
  addrPcf8574 = top[_pcfAddress] | addrPcf8574;
  // if I2C is not globally initialised just ignore
  if (i2c_sda<0 || i2c_scl<0) usePcf8574 = false;
  periodicBroadcastSec = top[_broadcast] | periodicBroadcastSec;
  periodicBroadcastSec = min(900,max(0,(int)periodicBroadcastSec));
  HAautodiscovery = top[_HAautodiscovery] | HAautodiscovery;

  for (int i=0; i<MULTI_RELAY_MAX_RELAYS; i++) {
    String parName = _relay_str; parName += '-'; parName += i;
    oldPin[i]          = _relay[i].pin;
    _relay[i].pin      = top[parName]["pin"] | _relay[i].pin;
    _relay[i].invert   = top[parName][_activeHigh] | _relay[i].invert;
    _relay[i].external = top[parName][_external]   | _relay[i].external;
    _relay[i].delay    = top[parName][_delay_str]  | _relay[i].delay;
    _relay[i].button   = top[parName][_button]     | _relay[i].button;
    _relay[i].delay    = min(600,max(0,abs((int)_relay[i].delay))); // bounds checking max 10min
  }

  DEBUG_PRINT(_name);
  if (!initDone) {
    // reading config prior to setup()
    DEBUG_PRINTLN(" config loaded.");
  } else {
    // deallocate all pins 1st
    for (int i=0; i<MULTI_RELAY_MAX_RELAYS; i++)
      if (oldPin[i]>=0 && oldPin[i]<100) {
        PinManager::deallocatePin(oldPin[i], PinOwner::UM_MultiRelay);
      }
    // allocate new pins
    setup();
    DEBUG_PRINTLN(" config (re)loaded.");
  }
  // use "return !top["newestParameter"].isNull();" when updating Usermod with new features
  return !top[_pcf8574].isNull();
}

// strings to reduce flash memory usage (used more than twice)
const char MultiRelay::_name[]            PROGMEM = "MultiRelay";
const char MultiRelay::_enabled[]         PROGMEM = "enabled";
const char MultiRelay::_relay_str[]       PROGMEM = "relay";
const char MultiRelay::_delay_str[]       PROGMEM = "delay-s";
const char MultiRelay::_activeHigh[]      PROGMEM = "active-high";
const char MultiRelay::_external[]        PROGMEM = "external";
const char MultiRelay::_button[]          PROGMEM = "button";
const char MultiRelay::_broadcast[]       PROGMEM = "broadcast-sec";
const char MultiRelay::_HAautodiscovery[] PROGMEM = "HA-autodiscovery";
const char MultiRelay::_pcf8574[]         PROGMEM = "use-PCF8574";
const char MultiRelay::_pcfAddress[]      PROGMEM = "PCF8574-address";
const char MultiRelay::_switch[]          PROGMEM = "switch";
const char MultiRelay::_toggle[]          PROGMEM = "toggle";
const char MultiRelay::_Command[]         PROGMEM = "/command";


static MultiRelay multi_relay;
REGISTER_USERMOD(multi_relay);