// force the compiler to show a warning to confirm that this file is included
#warning **** Included USERMOD_BH1750 ****

#include "wled.h"
#include "BH1750_v2.h"

#ifdef WLED_DISABLE_MQTT
#error "This user mod requires MQTT to be enabled."
#endif

static bool checkBoundSensor(float newValue, float prevValue, float maxDiff)
{
  return isnan(prevValue) || newValue <= prevValue - maxDiff || newValue >= prevValue + maxDiff || (newValue == 0.0 && prevValue > 0.0);
}

void Usermod_BH1750::_mqttInitialize()
{
  mqttLuminanceTopic = String(mqttDeviceTopic) + "/brightness";

  if (HomeAssistantDiscovery) _createMqttSensor("Brightness", mqttLuminanceTopic, "Illuminance", " lx");
}

// Create an MQTT Sensor for Home Assistant Discovery purposes, this includes a pointer to the topic that is published to in the Loop.
void Usermod_BH1750::_createMqttSensor(const String &name, const String &topic, const String &deviceClass, const String &unitOfMeasurement)
{
  String t = String("homeassistant/sensor/") + mqttClientID + "/" + name + "/config";
  
  StaticJsonDocument<600> doc;
  
  doc["name"] = String(serverDescription) + " " + name;
  doc["state_topic"] = topic;
  doc["unique_id"] = String(mqttClientID) + name;
  if (unitOfMeasurement != "")
    doc["unit_of_measurement"] = unitOfMeasurement;
  if (deviceClass != "")
    doc["device_class"] = deviceClass;
  doc["expire_after"] = 1800;

  JsonObject device = doc.createNestedObject("device"); // attach the sensor to the same device
  device["name"] = serverDescription;
  device["identifiers"] = "wled-sensor-" + String(mqttClientID);
  device["manufacturer"] = WLED_BRAND;
  device["model"] = WLED_PRODUCT_NAME;
  device["sw_version"] = versionString;

  String temp;
  serializeJson(doc, temp);
  DEBUG_PRINTLN(t);
  DEBUG_PRINTLN(temp);

  mqtt->publish(t.c_str(), 0, true, temp.c_str());
}

void Usermod_BH1750::setup()
{
  if (i2c_scl<0 || i2c_sda<0) { enabled = false; return; }
  sensorFound = lightMeter.begin();
  initDone = true;
}

void Usermod_BH1750::loop()
{
  if ((!enabled) || strip.isUpdating())
    return;

  unsigned long now = millis();

  // check to see if we are due for taking a measurement
  // lastMeasurement will not be updated until the conversion
  // is complete the the reading is finished
  if (now - lastMeasurement < minReadingInterval)
  {
    return;
  }

  bool shouldUpdate = now - lastSend > maxReadingInterval;

  float lux = lightMeter.readLightLevel();
  lastMeasurement = millis();
  getLuminanceComplete = true;

  if (shouldUpdate || checkBoundSensor(lux, lastLux, offset))
  {
    lastLux = lux;
    lastSend = millis();

    if (WLED_MQTT_CONNECTED)
    {
      if (!mqttInitialized)
        {
          _mqttInitialize();
          mqttInitialized = true;
        }
      mqtt->publish(mqttLuminanceTopic.c_str(), 0, true, String(lux).c_str());
      DEBUG_PRINTLN("Brightness: " + String(lux) + "lx");
    }
    else
    {
      DEBUG_PRINTLN("Missing MQTT connection. Not publishing data");
    }
  }
}


void Usermod_BH1750::addToJsonInfo(JsonObject &root)
{
  JsonObject user = root["u"];
  if (user.isNull())
    user = root.createNestedObject("u");

  JsonArray lux_json = user.createNestedArray("Luminance");
  if (!enabled) {
    lux_json.add("disabled");
  } else if (!sensorFound) {
      // if no sensor 
      lux_json.add("BH1750 ");
      lux_json.add("Not Found");
  } else if (!getLuminanceComplete) {
    // if we haven't read the sensor yet, let the user know
      // that we are still waiting for the first measurement
      lux_json.add((USERMOD_BH1750_FIRST_MEASUREMENT_AT - millis()) / 1000);
      lux_json.add(" sec until read");
      return;
  } else {
    lux_json.add(lastLux);
    lux_json.add(" lx");
  }
}

// (called from set.cpp) stores persistent properties to cfg.json
void Usermod_BH1750::addToConfig(JsonObject &root)
{
  // we add JSON object.
  JsonObject top = root.createNestedObject(_name); // usermodname
  top[_enabled] = enabled;
  top[_maxReadInterval] = maxReadingInterval;
  top[_minReadInterval] = minReadingInterval;
  top[_HomeAssistantDiscovery] = HomeAssistantDiscovery;
  top[_offset] = offset;

  DEBUG_PRINTLN("BH1750 config saved.");
}

// called before setup() to populate properties from values stored in cfg.json
bool Usermod_BH1750::readFromConfig(JsonObject &root)
{
  // we look for JSON object.
  JsonObject top = root[_name];
  if (top.isNull())
  {
    DEBUG_PRINT(_name);
    DEBUG_PRINT("BH1750");
    DEBUG_PRINTLN(": No config found. (Using defaults.)");
    return false;
  }
  bool configComplete = !top.isNull();

  configComplete &= getJsonValue(top[_enabled], enabled, false);
  configComplete &= getJsonValue(top[_maxReadInterval], maxReadingInterval, 10000); //ms
  configComplete &= getJsonValue(top[_minReadInterval], minReadingInterval, 500); //ms
  configComplete &= getJsonValue(top[_HomeAssistantDiscovery], HomeAssistantDiscovery, false);
  configComplete &= getJsonValue(top[_offset], offset, 1);

  DEBUG_PRINT(_name);
  if (!initDone) {
    DEBUG_PRINTLN(" config loaded.");
  } else {
    DEBUG_PRINTLN(" config (re)loaded.");
  }

  return configComplete;
  
}


// strings to reduce flash memory usage (used more than twice)
const char Usermod_BH1750::_name[] PROGMEM = "BH1750";
const char Usermod_BH1750::_enabled[] PROGMEM = "enabled";
const char Usermod_BH1750::_maxReadInterval[] PROGMEM = "max-read-interval-ms";
const char Usermod_BH1750::_minReadInterval[] PROGMEM = "min-read-interval-ms";
const char Usermod_BH1750::_HomeAssistantDiscovery[] PROGMEM = "HomeAssistantDiscoveryLux";
const char Usermod_BH1750::_offset[] PROGMEM = "offset-lx";


static Usermod_BH1750 bh1750_v2;
REGISTER_USERMOD(bh1750_v2);