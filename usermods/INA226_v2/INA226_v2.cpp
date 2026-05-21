#include "wled.h"
#include <INA226_WE.h>

#ifndef INA226_ADDRESS
#define INA226_ADDRESS 0x40 // Default I2C address for INA226
#endif

#ifndef INA226_CHECK_INTERVAL_MS
#define INA226_CHECK_INTERVAL_MS 60000 // Default check interval in milliseconds
#endif

#define DEFAULT_CHECKINTERVAL INA226_CHECK_INTERVAL_MS
#define DEFAULT_INASAMPLES 128
#define DEFAULT_INASAMPLESENUM AVERAGE_128
#define DEFAULT_INACONVERSIONTIME 1100
#define DEFAULT_INACONVERSIONTIMEENUM CONV_TIME_1100

// Compile-time defaults for shunt resistor (micro-ohms), current range (mA), and current offset (mA)
// These can be overridden via -D flags in platformio.ini / platformio_override.ini
#ifndef INA226_SHUNT_MICRO_OHMS
#define INA226_SHUNT_MICRO_OHMS 1000000  // 1 Ohm = 1,000,000 μΩ
#endif

#ifndef INA226_DEFAULT_CURRENT_RANGE
#define INA226_DEFAULT_CURRENT_RANGE 1000  // 1000 mA = 1 A
#endif

#ifndef INA226_CURRENT_OFFSET_MA
#define INA226_CURRENT_OFFSET_MA 0  // No offset by default
#endif

#ifndef INA226_ENABLED_DEFAULT
#define INA226_ENABLED_DEFAULT false
#endif

// A packed version of all INA settings enums and their human friendly counterparts packed into a 32 bit structure
// Some values are shifted and need to be preprocessed before usage
struct InaSettingLookup
{
    uint16_t avgSamples : 11;          // Max 1024, which could be in 10 bits if we shifted by 1; if we somehow handle the edge case with "1"
    uint8_t avgEnum : 4;               // Shift by 8 to get the INA226_AVERAGES value, accepts 0x00 to 0x0F, we need 0x00 to 0x0E
    uint16_t convTimeUs : 14;          // We could save 2 bits by shifting this, but we won't save anything at present.
    INA226_CONV_TIME convTimeEnum : 3; // Only the lowest 3 bits are defined in the conversion time enumerations
};

const InaSettingLookup _inaSettingsLookup[] = {
    {1024, AVERAGE_1024 >> 8, 8244, CONV_TIME_8244},
    {512, AVERAGE_512 >> 8, 4156, CONV_TIME_4156},
    {256, AVERAGE_256 >> 8, 2116, CONV_TIME_2116},
    {128, AVERAGE_128 >> 8, 1100, CONV_TIME_1100},
    {64, AVERAGE_64 >> 8, 588, CONV_TIME_588},
    {16, AVERAGE_16 >> 8, 332, CONV_TIME_332},
    {4, AVERAGE_4 >> 8, 204, CONV_TIME_204},
    {1, AVERAGE_1 >> 8, 140, CONV_TIME_140}};

// Note: Will update the provided arg to be the correct value
INA226_AVERAGES getAverageEnum(uint16_t &samples)
{
    for (const auto &setting : _inaSettingsLookup)
    {
        // If a user supplies 2000 samples, we serve up the highest possible value
        if (samples >= setting.avgSamples)
        {
            samples = setting.avgSamples;
            return static_cast<INA226_AVERAGES>(setting.avgEnum << 8);
        }
    }
    // Default value if not found
    samples = DEFAULT_INASAMPLES;
    return DEFAULT_INASAMPLESENUM;
}

INA226_CONV_TIME getConversionTimeEnum(uint16_t &timeUs)
{
    for (const auto &setting : _inaSettingsLookup)
    {
        // If a user supplies 9000 μs, we serve up the highest possible value
        if (timeUs >= setting.convTimeUs)
        {
            timeUs = setting.convTimeUs;
            return setting.convTimeEnum;
        }
    }
    // Default value if not found
    timeUs = DEFAULT_INACONVERSIONTIME;
    return DEFAULT_INACONVERSIONTIMEENUM;
}

class UsermodINA226 : public Usermod
{
private:
    static const char _name[];

    unsigned long _lastLoopCheck = 0;
    unsigned long _lastTriggerTime = 0;

    bool _settingEnabled : 1;                  // Enable the usermod
    bool _mqttPublish : 1;                     // Publish MQTT values
    bool _mqttPublishAlways : 1;               // Publish always, regardless if there is a change
    bool _mqttHomeAssistant : 1;               // Enable Home Assistant docs
    bool _initDone : 1;                        // Initialization is done
    bool _isTriggeredOperationMode : 1;        // false = continuous, true = triggered
    bool _measurementTriggered : 1;            // if triggered mode, then true indicates we're waiting for measurements
    uint16_t _settingInaConversionTimeUs : 12; // Conversion time, shift by 2
    uint16_t _settingInaSamples : 11;          // Number of samples for averaging, max 1024

    uint8_t _i2cAddress;
    uint32_t _checkIntervalMs;  // milliseconds, user settings is in seconds
    float _decimalFactor;       // a power of 10 factor. 1 would be no change, 10 is one decimal, 100 is two etc. User sees a power of 10 (0, 1, 2, ..)
    uint32_t _shuntResistorUOhm; // Shunt resistor value in micro-ohms (μΩ)
    uint16_t _currentRangeMa;    // Expected maximum current in milliamps
    int16_t _currentOffsetMa;    // Current offset in milliamps, subtracted from readings

    uint8_t _lastStatus = 0;
    float _lastCurrent = 0;
    float _lastVoltage = 0;
    float _lastPower = 0;
    float _lastShuntVoltage = 0;
    bool _lastOverflow = false;

#ifndef WLED_DISABLE_MQTT
    float _lastCurrentSent = 0;
    float _lastVoltageSent = 0;
    float _lastPowerSent = 0;
    float _lastShuntVoltageSent = 0;
    bool _lastOverflowSent = false;
#endif

    INA226_WE *_ina226 = nullptr;

    float truncateDecimals(float val)
    {
        return roundf(val * _decimalFactor) / _decimalFactor;
    }

    void initializeINA226()
    {
        if (_ina226 != nullptr)
        {
            delete _ina226;
        }

        _ina226 = new INA226_WE(_i2cAddress);
        if (!_ina226->init())
        {
            DEBUG_PRINTLN("INA226: init failed!");
            return;
        }
        DEBUG_PRINTF("INA226: addr=0x%02X shunt=%luμΩ range=%umA offset=%dmA\n",
                       _i2cAddress, _shuntResistorUOhm, _currentRangeMa, _currentOffsetMa);
        _ina226->setCorrectionFactor(1.0);

        uint16_t tmpShort = _settingInaSamples;
        _ina226->setAverage(getAverageEnum(tmpShort));

        tmpShort = _settingInaConversionTimeUs << 2;
        _ina226->setConversionTime(getConversionTimeEnum(tmpShort));

        if (_checkIntervalMs >= 20000)
        {
            _isTriggeredOperationMode = true;
            _ina226->setMeasureMode(TRIGGERED);
        }
        else
        {
            _isTriggeredOperationMode = false;
            _ina226->setMeasureMode(CONTINUOUS);
        }

        _ina226->setResistorRange(static_cast<float>(_shuntResistorUOhm) / 1000000.0f, static_cast<float>(_currentRangeMa) / 1000.0f);

        DEBUG_PRINTF("INA226: mode=%s interval=%lums samples=%u convTime=%uμs\n",
                       _isTriggeredOperationMode ? "triggered" : "continuous",
                       _checkIntervalMs, _settingInaSamples, _settingInaConversionTimeUs << 2);
    }

    void fetchAndPushValues()
    {
        _lastStatus = _ina226->getI2cErrorCode();

        if (_lastStatus != 0)
            return;

        float current = truncateDecimals((_ina226->getCurrent_mA() - _currentOffsetMa) / 1000.0f);
        float voltage = truncateDecimals(_ina226->getBusVoltage_V());
        float power = truncateDecimals(_ina226->getBusPower() / 1000.0f);
        float shuntVoltage = truncateDecimals(_ina226->getShuntVoltage_mV());
        bool overflow = _ina226->overflow;

#ifndef WLED_DISABLE_MQTT
        mqttPublishIfChanged("current", _lastCurrentSent, current, 0.01f);
        mqttPublishIfChanged("voltage", _lastVoltageSent, voltage, 0.01f);
        mqttPublishIfChanged("power", _lastPowerSent, power, 0.1f);
        // Publish in V for backward compatibility
        float shuntVoltageV = shuntVoltage / 1000.0f;
        mqttPublishIfChanged("shunt_voltage", _lastShuntVoltageSent, shuntVoltageV, 0.01f);
        mqttPublishIfChanged("overflow", _lastOverflowSent, overflow);
#endif

        _lastCurrent = current;
        _lastVoltage = voltage;
        _lastPower = power;
        _lastShuntVoltage = shuntVoltage;
        _lastOverflow = overflow;

        DEBUG_PRINTF("INA226: %.3fA %.2fV %.2fW shunt=%.2fmV%s\n",
                       current, voltage, power, shuntVoltage, overflow ? " OVF" : "");
    }

    void handleTriggeredMode(unsigned long currentTime)
    {
        if (_measurementTriggered)
        {
            // Test if we have a measurement every 400ms
            if (currentTime - _lastTriggerTime >= 400)
            {
                _lastTriggerTime = currentTime;
                if (_ina226->isBusy())
                    return;

                fetchAndPushValues();
                _measurementTriggered = false;
            }
        }
        else
        {
            if (currentTime - _lastLoopCheck >= _checkIntervalMs)
            {
                // Start a measurement and use isBusy() later to determine when it is done
                _ina226->startSingleMeasurementNoWait();
                _lastLoopCheck = currentTime;
                _lastTriggerTime = currentTime;
                _measurementTriggered = true;
            }
        }
    }

    void handleContinuousMode(unsigned long currentTime)
    {
        if (currentTime - _lastLoopCheck >= _checkIntervalMs)
        {
            _lastLoopCheck = currentTime;
            fetchAndPushValues();
        }
    }

#ifndef WLED_DISABLE_MQTT
    void mqttInitialize()
    {
        if (!WLED_MQTT_CONNECTED || !_mqttPublish || !_mqttHomeAssistant)
            return;

        char topic[128];
        auto buildTopic = [&](const char *suffix) {
            snprintf(topic, sizeof(topic), "%s/%s", mqttDeviceTopic, suffix);
        };

        buildTopic("current");
        mqttCreateHassSensor("Current", topic, "current", "A");

        buildTopic("voltage");
        mqttCreateHassSensor("Voltage", topic, "voltage", "V");

        buildTopic("power");
        mqttCreateHassSensor("Power", topic, "power", "W");

        buildTopic("shunt_voltage");
        mqttCreateHassSensor("Shunt Voltage", topic, "voltage", "V");

        buildTopic("overflow");
        mqttCreateHassBinarySensor("Overflow", topic);
    }

    void mqttPublishIfChanged(const __FlashStringHelper *topic, float &lastState, float state, float minChange)
    {
        if (WLED_MQTT_CONNECTED && _mqttPublish && (_mqttPublishAlways || fabsf(lastState - state) > minChange))
        {
            char subuf[128];
            snprintf(subuf, 127, "%s/%s", mqttDeviceTopic, (const char *)topic);
            mqtt->publish(subuf, 0, false, String(state).c_str());

            lastState = state;
        }
    }

    void mqttPublishIfChanged(const __FlashStringHelper *topic, bool &lastState, bool state)
    {
        if (WLED_MQTT_CONNECTED && _mqttPublish && (_mqttPublishAlways || lastState != state))
        {
            char subuf[128];
            snprintf(subuf, 127, "%s/%s", mqttDeviceTopic, (const char *)topic);
            mqtt->publish(subuf, 0, false, state ? "true" : "false");

            lastState = state;
        }
    }

    void mqttCreateHassSensor(const String &name, const String &topic, const String &deviceClass, const String &unitOfMeasurement)
    {
        String t = String("homeassistant/sensor/") + mqttClientID + "/" + name + "/config";

        StaticJsonDocument<600> doc;

        doc["name"] = name;
        doc["state_topic"] = topic;
        doc["unique_id"] = String(mqttClientID) + name;
        if (unitOfMeasurement != "")
            doc["unit_of_measurement"] = unitOfMeasurement;
        if (deviceClass != "")
            doc["device_class"] = deviceClass;
        doc["expire_after"] = 1800;

        JsonObject device = doc.createNestedObject("device");
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

    void mqttCreateHassBinarySensor(const String &name, const String &topic)
    {
        String t = String("homeassistant/binary_sensor/") + mqttClientID + "/" + name + "/config";

        StaticJsonDocument<600> doc;

        doc["name"] = name;
        doc["state_topic"] = topic;
        doc["unique_id"] = String(mqttClientID) + name;

        JsonObject device = doc.createNestedObject("device");
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
#endif

public:
    UsermodINA226()
    {
        // Default values
        _settingEnabled = INA226_ENABLED_DEFAULT;
        _settingInaSamples = DEFAULT_INASAMPLES;
        _settingInaConversionTimeUs = DEFAULT_INACONVERSIONTIME >> 2; // stored shifted to fit 12-bit field

        _i2cAddress = INA226_ADDRESS;
        _checkIntervalMs = DEFAULT_CHECKINTERVAL;
        _decimalFactor = 100;
        _shuntResistorUOhm = INA226_SHUNT_MICRO_OHMS;
        _currentRangeMa = INA226_DEFAULT_CURRENT_RANGE;
        _currentOffsetMa = INA226_CURRENT_OFFSET_MA;

        _mqttPublish = false;
        _mqttPublishAlways = false;
        _mqttHomeAssistant = false;
        _initDone = false;
        _isTriggeredOperationMode = false;
        _measurementTriggered = false;
    }

    void setup()
    {
        initializeINA226();
    }

    void loop()
    {
        if (!_settingEnabled || strip.isUpdating())
            return;

        unsigned long currentTime = millis();

        if (_isTriggeredOperationMode)
        {
            handleTriggeredMode(currentTime);
        }
        else
        {
            handleContinuousMode(currentTime);
        }
    }

#ifndef WLED_DISABLE_MQTT
    void onMqttConnect(bool sessionPresent)
    {
        mqttInitialize();
    }
#endif

    uint16_t getId()
    {
        return USERMOD_ID_INA226;
    }

    void addToJsonInfo(JsonObject &root) override
    {
        JsonObject user = root["u"];
        if (user.isNull())
            user = root.createNestedObject("u");

#ifdef USERMOD_INA226_DEBUG
        JsonArray temp = user.createNestedArray("INA226 last loop");
        temp.add(_lastLoopCheck);

        temp = user.createNestedArray("INA226 last status");
        temp.add(_lastStatus);

        temp = user.createNestedArray("INA226 average samples");
        temp.add(_settingInaSamples);
        temp.add("samples");

        temp = user.createNestedArray("INA226 conversion time");
        temp.add(_settingInaConversionTimeUs << 2);
        temp.add("μs");

        // INA226 uses (2 * conversion time * samples) time to take a reading.
        temp = user.createNestedArray("INA226 expected sample time");
        uint32_t sampleTimeNeededUs = (static_cast<uint32_t>(_settingInaConversionTimeUs) << 2) * _settingInaSamples * 2;
        temp.add(truncateDecimals(sampleTimeNeededUs / 1000.0));
        temp.add("ms");

        temp = user.createNestedArray("INA226 mode");
        temp.add(_isTriggeredOperationMode ? "triggered" : "continuous");

        if (_isTriggeredOperationMode)
        {
            temp = user.createNestedArray("INA226 triggered");
            temp.add(_measurementTriggered ? "waiting for measurement" : "");
        }
#endif

        JsonArray jsonCurrent = user.createNestedArray("Current");
        JsonArray jsonVoltage = user.createNestedArray("Voltage");
        JsonArray jsonPower = user.createNestedArray("Power");
        JsonArray jsonShuntVoltage = user.createNestedArray("Shunt Voltage Drop");
        JsonArray jsonOverflow = user.createNestedArray("Overflow");

        if (_lastLoopCheck == 0)
        {
            jsonCurrent.add("Not read yet");
            jsonVoltage.add("Not read yet");
            jsonPower.add("Not read yet");
            jsonShuntVoltage.add("Not read yet");
            jsonOverflow.add("Not read yet");
            return;
        }

        if (_lastStatus != 0)
        {
            jsonCurrent.add("An error occurred");
            jsonVoltage.add("An error occurred");
            jsonPower.add("An error occurred");
            jsonShuntVoltage.add("An error occurred");
            jsonOverflow.add("An error occurred");
            return;
        }

        jsonCurrent.add(_lastCurrent);
        jsonCurrent.add("A");

        jsonVoltage.add(_lastVoltage);
        jsonVoltage.add("V");

        jsonPower.add(_lastPower);
        jsonPower.add("W");

        jsonShuntVoltage.add(_lastShuntVoltage);
        jsonShuntVoltage.add("mV");

        jsonOverflow.add(_lastOverflow ? "true" : "false");
    }

    void addToConfig(JsonObject &root)
    {
        JsonObject top = root.createNestedObject(_name);
        top["Enabled"] = _settingEnabled;
        top["I2CAddress"] = static_cast<uint8_t>(_i2cAddress);
        top["CheckInterval"] = _checkIntervalMs / 1000;
        top["INASamples"] = _settingInaSamples;
        top["INAConversionTime"] = _settingInaConversionTimeUs << 2;
        top["Decimals"] = log10f(_decimalFactor);
        top["ShuntResistor"] = static_cast<float>(_shuntResistorUOhm) / 1000.0f;
        top["CurrentRange"] = _currentRangeMa;
        top["CurrentOffset"] = _currentOffsetMa;
#ifndef WLED_DISABLE_MQTT
        top["MqttPublish"] = _mqttPublish;
        top["MqttPublishAlways"] = _mqttPublishAlways;
        top["MqttHomeAssistantDiscovery"] = _mqttHomeAssistant;
#endif

        DEBUG_PRINTLN("INA226 config saved.");
    }

    void appendConfigData() override
    {
        oappend("addInfo('INA226:CheckInterval',1,'seconds');");
        oappend("addInfo('INA226:INASamples',1,'samples (1-1024)');");
        oappend("addInfo('INA226:INAConversionTime',1,'&micro;s');");
        oappend("addInfo('INA226:Decimals',1,'(0-5)');");
        oappend("addInfo('INA226:ShuntResistor',1,'m&Omega;');");
        oappend("addInfo('INA226:CurrentRange',1,'mA');");
        oappend("addInfo('INA226:CurrentOffset',1,'mA');");
    }

    bool readFromConfig(JsonObject &root) override
    {
        JsonObject top = root[_name];

        bool configComplete = !top.isNull();
        if (!configComplete)
            return false;

        bool tmpBool;
        if (getJsonValue(top["Enabled"], tmpBool))
            _settingEnabled = tmpBool;
        else
            configComplete = false;

        configComplete &= getJsonValue(top["I2CAddress"], _i2cAddress);
        if (getJsonValue(top["CheckInterval"], _checkIntervalMs))
        {
            if (1 <= _checkIntervalMs && _checkIntervalMs <= 600)
                _checkIntervalMs *= 1000;
            else
                _checkIntervalMs = DEFAULT_CHECKINTERVAL;
        }
        else
            configComplete = false;

        uint16_t tmpShort;
        if (getJsonValue(top["INASamples"], tmpShort))
        {
            // The method below will fix the provided value to a valid one
            getAverageEnum(tmpShort);
            _settingInaSamples = tmpShort;
        }
        else
            configComplete = false;

        if (getJsonValue(top["INAConversionTime"], tmpShort))
        {
            // The method below will fix the provided value to a valid one
            getConversionTimeEnum(tmpShort);
            _settingInaConversionTimeUs = tmpShort >> 2;
        }
        else
            configComplete = false;

        if (getJsonValue(top["Decimals"], _decimalFactor))
        {
            if (0 <= _decimalFactor && _decimalFactor <= 5)
                _decimalFactor = pow10f(_decimalFactor);
            else
                _decimalFactor = 100;
        }
        else
            configComplete = false;

        float shuntMilliOhms;
        if (getJsonValue(top["ShuntResistor"], shuntMilliOhms))
        {
            if (shuntMilliOhms > 0)
                _shuntResistorUOhm = static_cast<uint32_t>(shuntMilliOhms * 1000.0f + 0.5f);
            else
                _shuntResistorUOhm = INA226_SHUNT_MICRO_OHMS;
        }
        else
            configComplete = false;

        if (getJsonValue(top["CurrentRange"], _currentRangeMa))
        {
            if (_currentRangeMa == 0 || _currentRangeMa > 20000)
                _currentRangeMa = INA226_DEFAULT_CURRENT_RANGE;
        }
        else
            configComplete = false;
        if (!getJsonValue(top["CurrentOffset"], _currentOffsetMa))
            _currentOffsetMa = INA226_CURRENT_OFFSET_MA;  // Use compile-time default if missing from config

#ifndef WLED_DISABLE_MQTT
        if (getJsonValue(top["MqttPublish"], tmpBool))
            _mqttPublish = tmpBool;
        else
            configComplete = false;

        if (getJsonValue(top["MqttPublishAlways"], tmpBool))
            _mqttPublishAlways = tmpBool;
        else
            configComplete = false;

        if (getJsonValue(top["MqttHomeAssistantDiscovery"], tmpBool))
            _mqttHomeAssistant = tmpBool;
        else
            configComplete = false;
#endif

        if (_initDone)
        {
            initializeINA226();

#ifndef WLED_DISABLE_MQTT
            mqttInitialize();
#endif
        }

        _initDone = true;
        return configComplete;
    }

    ~UsermodINA226()
    {
        delete _ina226;
        _ina226 = nullptr;
    }

};

const char UsermodINA226::_name[] PROGMEM = "INA226";


static UsermodINA226 ina226_v2;
REGISTER_USERMOD(ina226_v2);