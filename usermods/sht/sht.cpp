#include "ShtUsermod.h"
#include "SHT85.h"

// Strings to reduce flash memory usage (used more than twice)
const char ShtUsermod::_name[]            PROGMEM = "SHT-Sensor";
const char ShtUsermod::_enabled[]         PROGMEM = "Enabled";
const char ShtUsermod::_shtType[]         PROGMEM = "SHT-Type";
const char ShtUsermod::_unitOfTemp[]      PROGMEM = "Unit";
const char ShtUsermod::_haMqttDiscovery[] PROGMEM = "Add-To-HA-MQTT-Discovery";

/**
 * Initialise SHT sensor.
 *
 * Using the correct constructor according to config and initialises it using the
 * global i2c pins.
 *
 * @return void
 */
void ShtUsermod::initShtTempHumiditySensor()
{
  switch (shtType) {
    case USERMOD_SHT_TYPE_SHT30: shtTempHumidSensor = (SHT *) new SHT30(); break;
    case USERMOD_SHT_TYPE_SHT31: shtTempHumidSensor = (SHT *) new SHT31(); break;
    case USERMOD_SHT_TYPE_SHT35: shtTempHumidSensor = (SHT *) new SHT35(); break;
    case USERMOD_SHT_TYPE_SHT85: shtTempHumidSensor = (SHT *) new SHT85(); break;
  }

  shtTempHumidSensor->begin(shtI2cAddress); // uses &Wire
  if (shtTempHumidSensor->readStatus() == 0xFFFF) {
    DEBUG_PRINTF("[%s] SHT init failed!\n", _name);
    cleanup();
    return;
  }

  shtInitDone = true;
}

/**
 * Cleanup the SHT sensor.
 *
 * Properly calls "reset" for the sensor then releases it from memory.
 *
 * @return void
 */
void ShtUsermod::cleanupShtTempHumiditySensor()
{
  if (isShtReady()) {
    shtTempHumidSensor->reset();
    delete shtTempHumidSensor;
    shtTempHumidSensor = nullptr;
  }
  shtInitDone = false;
}

/**
 * Cleanup the mod completely.
 *
 * Calls ::cleanupShtTempHumiditySensor() to cleanup the SHT sensor and
 * deallocates pins.
 *
 * @return void
 */
void ShtUsermod::cleanup()
{
  cleanupShtTempHumiditySensor();
  enabled = false;
}

/**
 * Publish temperature and humidity to WLED device topic.
 *
 * Will add a "/temperature" and "/humidity" topic to the WLED device topic.
 * Temperature will be written in configured unit.
 *
 * @return void
 */
void ShtUsermod::publishTemperatureAndHumidityViaMqtt() {
  if (!WLED_MQTT_CONNECTED) return;
  char buf[128];

  snprintf(buf, 127, "%s/temperature", mqttDeviceTopic);
  mqtt->publish(buf, 0, false, String(getTemperature()).c_str());
  snprintf(buf, 127, "%s/humidity", mqttDeviceTopic);
  mqtt->publish(buf, 0, false, String(getHumidity()).c_str());
}

/**
 * If enabled, publishes HA MQTT device discovery topics.
 *
 * Will make Home Assistant add temperature and humidity as entities automatically.
 *
 * Note: Whenever usermods are part of the WLED integration in HA, this can be dropped.
 *
 * @return void
 */
void ShtUsermod::publishHomeAssistantAutodiscovery() {
  if (!WLED_MQTT_CONNECTED) return;

  char json_str[1024], buf[128];
  size_t payload_size;
  StaticJsonDocument<1024> json;

  snprintf(buf, 127, "%s Temperature", serverDescription);
  json["name"] = buf;
  snprintf(buf, 127, "%s/temperature", mqttDeviceTopic);
  json["stat_t"] = buf;
  json["dev_cla"] = "temperature";
  json["stat_cla"] = "measurement";
  snprintf(buf, 127, "%s-temperature", escapedMac.c_str());
  json["uniq_id"] = buf;
  json["unit_of_meas"] = unitOfTemp ? "°F" : "°C";
  appendDeviceToMqttDiscoveryMessage(json);
  payload_size = serializeJson(json, json_str);
  snprintf(buf, 127, "homeassistant/sensor/%s/%s-temperature/config", escapedMac.c_str(), escapedMac.c_str());
  mqtt->publish(buf, 0, true, json_str, payload_size);

  json.clear();

  snprintf(buf, 127, "%s Humidity", serverDescription);
  json["name"] = buf;
  snprintf(buf, 127, "%s/humidity", mqttDeviceTopic);
  json["stat_t"] = buf;
  json["dev_cla"] = "humidity";
  json["stat_cla"] = "measurement";
  snprintf(buf, 127, "%s-humidity", escapedMac.c_str());
  json["uniq_id"] = buf;
  json["unit_of_meas"] = "%";
  appendDeviceToMqttDiscoveryMessage(json);
  payload_size = serializeJson(json, json_str);
  snprintf(buf, 127, "homeassistant/sensor/%s/%s-humidity/config", escapedMac.c_str(), escapedMac.c_str());
  mqtt->publish(buf, 0, true, json_str, payload_size);

  haMqttDiscoveryDone = true;
}

/**
 * Helper to add device information to MQTT discovery topic.
 *
 * @return void
 */
void ShtUsermod::appendDeviceToMqttDiscoveryMessage(JsonDocument& root) {
  JsonObject device = root.createNestedObject("dev");
  device["ids"] = escapedMac.c_str();
  device["name"] = serverDescription;
  device["sw"] = versionString;
  device["mdl"] = ESP.getChipModel();
  device["mf"] = "espressif";
}

/**
 * Setup the mod.
 *
 * Allocates i2c pins as PinOwner::HW_I2C, so they can be allocated multiple times.
 * And calls ::initShtTempHumiditySensor() to initialise the sensor.
 *
 * @see Usermod::setup()
 * @see UsermodManager::setup()
 *
 * @return void
 */
void ShtUsermod::setup()
{
  if (enabled) {
    // GPIOs can be set to -1 , so check they're gt zero
    if (i2c_sda < 0 || i2c_scl < 0) {
      DEBUG_PRINTF("[%s] I2C bus not initialised!\n", _name);
      cleanup();
      return;
    }

    initShtTempHumiditySensor();

    initDone = true;
  }

  firstRunDone = true;
}

/**
 * Actually reading data (async) from the sensor every 30 seconds.
 *
 * If last reading is at least 30 seconds, it will trigger a reading using
 * SHT::requestData(). We will then continiously check SHT::dataReady() if
 * data is ready to be read. If so, it's read, stored locally and published
 * via MQTT.
 *
 * @see Usermod::loop()
 * @see UsermodManager::loop()
 *
 * @return void
 */
void ShtUsermod::loop()
{
  if (!enabled || !initDone || strip.isUpdating()) return;

  if (isShtReady()) {
    if (millis() - shtLastTimeUpdated > 30000 && !shtDataRequested) {
      shtTempHumidSensor->requestData();
      shtDataRequested = true;

      shtLastTimeUpdated = millis();
    }

    if (shtDataRequested) {
      if (shtTempHumidSensor->dataReady()) {
        if (shtTempHumidSensor->readData(false)) {
          shtCurrentTempC = shtTempHumidSensor->getTemperature();
          shtCurrentHumidity = shtTempHumidSensor->getHumidity();

          publishTemperatureAndHumidityViaMqtt();
          shtReadDataSuccess = true;
        } else {
          shtReadDataSuccess = false;
        }

        shtDataRequested = false;
      }
    }
  }
}

/**
 * Whenever MQTT is connected, publish HA autodiscovery topics.
 *
 * Is only done once.
 *
 * @see Usermod::onMqttConnect()
 * @see UsermodManager::onMqttConnect()
 *
 * @return void
 */
void ShtUsermod::onMqttConnect(bool sessionPresent) {
  if (haMqttDiscovery && !haMqttDiscoveryDone) publishHomeAssistantAutodiscovery();
}

/**
 * Add dropdown for sensor type and unit to UM config page.
 *
 * @see Usermod::appendConfigData()
 * @see UsermodManager::appendConfigData()
 *
 * @return void
 */
void ShtUsermod::appendConfigData() {
  oappend("dd=addDropdown('");
  oappend(_name);
  oappend("','");
  oappend(_shtType);
  oappend("');");
  oappend("addOption(dd,'SHT30',0);");
  oappend("addOption(dd,'SHT31',1);");
  oappend("addOption(dd,'SHT35',2);");
  oappend("addOption(dd,'SHT85',3);");
  oappend("dd=addDropdown('");
  oappend(_name);
  oappend("','");
  oappend(_unitOfTemp);
  oappend("');");
  oappend("addOption(dd,'Celsius',0);");
  oappend("addOption(dd,'Fahrenheit',1);");
}

/**
 * Add config data to be stored in cfg.json.
 *
 * @see Usermod::addToConfig()
 * @see UsermodManager::addToConfig()
 *
 * @return void
 */
void ShtUsermod::addToConfig(JsonObject &root)
{
  JsonObject top = root.createNestedObject(_name); // usermodname

  top[_enabled] = enabled;
  top[_shtType] = shtType;
  top[_unitOfTemp] = unitOfTemp;
  top[_haMqttDiscovery] = haMqttDiscovery;
}

/**
 * Apply config on boot or save of UM config page.
 *
 * This is called whenever WLED boots and loads cfg.json, or when the UM config
 * page is saved. Will properly re-instantiate the SHT class upon type change and
 * publish HA discovery after enabling.
 *
 * @see Usermod::readFromConfig()
 * @see UsermodManager::readFromConfig()
 *
 * @return bool
 */
bool ShtUsermod::readFromConfig(JsonObject &root)
{
  JsonObject top = root[_name];
  if (top.isNull()) {
    DEBUG_PRINTF("[%s] No config found. (Using defaults.)\n", _name);
    return false;
  }

  bool oldEnabled = enabled;
  byte oldShtType = shtType;
  byte oldUnitOfTemp = unitOfTemp;
  bool oldHaMqttDiscovery = haMqttDiscovery;

  getJsonValue(top[_enabled], enabled);
  getJsonValue(top[_shtType], shtType);
  getJsonValue(top[_unitOfTemp], unitOfTemp);
  getJsonValue(top[_haMqttDiscovery], haMqttDiscovery);

  // First run: reading from cfg.json, nothing to do here, will be all done in setup()
  if (!firstRunDone) {
    DEBUG_PRINTF("[%s] First run, nothing to do\n", _name);
  }
  // Check if mod has been en-/disabled
  else if (enabled != oldEnabled) {
    enabled ? setup() : cleanup();
    DEBUG_PRINTF("[%s] Usermod has been en-/disabled\n", _name);
  }
  // Config has been changed, so adopt to changes
  else if (enabled) {
    if (oldShtType != shtType) {
      cleanupShtTempHumiditySensor();
      initShtTempHumiditySensor();
    }

    if (oldUnitOfTemp != unitOfTemp) {
      publishTemperatureAndHumidityViaMqtt();
      publishHomeAssistantAutodiscovery();
    }

    if (oldHaMqttDiscovery != haMqttDiscovery && haMqttDiscovery) {
      publishHomeAssistantAutodiscovery();
    }

    DEBUG_PRINTF("[%s] Config (re)loaded\n", _name);
  }

  return true;
}

/**
 * Adds the temperature and humidity actually to the info section and /json info.
 *
 * This is called every time the info section is opened ot /json is called.
 *
 * @see Usermod::addToJsonInfo()
 * @see UsermodManager::addToJsonInfo()
 *
 * @return void
 */
void ShtUsermod::addToJsonInfo(JsonObject& root)
{
  if (!enabled && !isShtReady()) {
    return;
  }

  JsonObject user = root["u"];
  if (user.isNull()) user = root.createNestedObject("u");

  JsonArray jsonTemp = user.createNestedArray("Temperature");
  JsonArray jsonHumidity = user.createNestedArray("Humidity");

  if (shtLastTimeUpdated == 0 || !shtReadDataSuccess) {
    jsonTemp.add(0);
    jsonHumidity.add(0);
    if (shtLastTimeUpdated == 0) {
      jsonTemp.add(" Not read yet");
      jsonHumidity.add(" Not read yet");
    } else {
      jsonTemp.add(" Error");
      jsonHumidity.add(" Error");
    }
    return;
  }

  jsonHumidity.add(getHumidity());
  jsonHumidity.add(" RH");

  jsonTemp.add(getTemperature());
  jsonTemp.add(getUnitString());

  // sensor object
  JsonObject sensor = root["sensor"];
  if (sensor.isNull()) sensor = root.createNestedObject("sensor");

  jsonTemp = sensor.createNestedArray("temp");
  jsonTemp.add(getTemperature());
  jsonTemp.add(getUnitString());

  jsonHumidity = sensor.createNestedArray("humidity");
  jsonHumidity.add(getHumidity());
  jsonHumidity.add(" RH");
}

/**
 * Getter for last read temperature for configured unit.
 *
 * @return float
 */
float ShtUsermod::getTemperature() {
  return unitOfTemp ? getTemperatureF() : getTemperatureC();
}

/**
 * Returns the current configured unit as human readable string.
 *
 * @return const char*
 */
const char* ShtUsermod::getUnitString() {
  return unitOfTemp ? "°F" : "°C";
}

static ShtUsermod sht;
REGISTER_USERMOD(sht);
