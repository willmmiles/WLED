#include "wled.h"
#include "wled_ethernet.h"

/*
 * Sending XML status files to client
 */

// forward declarations
static void appendGPIOinfo(Print& settingsScript);

//build XML response to HTTP /win API request
void XML_response(Print& dest)
{
  dest.printf("<?xml version=\"1.0\" ?><vs><ac>%d</ac>", (nightlightActive && nightlightMode > NL_MODE_SET) ? briT : bri);
  for (int i = 0; i < 3; i++)
  {
   dest.printf("<cl>%d</cl>", colPri[i]);
  }
  for (int i = 0; i < 3; i++)
  {
    dest.printf("<cs>%d</cs>", colSec[i]);
  }
  dest.printf("<ns>%d</ns><nr>%d</nr><nl>%d</nl><nf>%d</nf><nd>%d</nd><nt>%d</nt><fx>%d</fx><sx>%d</sx><ix>%d</ix><fp>%d</fp><wv>%d</wv><ws>%d</ws><ps>%d</ps><cy>%d</cy><ds>%s%s</ds><ss>%d</ss></vs>",
    notifyDirect, receiveGroups!=0, nightlightActive, nightlightMode > NL_MODE_SET, nightlightDelayMins,
    nightlightTargetBri, effectCurrent, effectSpeed, effectIntensity, effectPalette,
    strip.hasWhiteChannel() ? colPri[3] : -1, colSec[3], currentPreset, currentPlaylist >= 0,
    serverDescription, realtimeMode ? " (live)" : "",
    strip.getFirstSelectedSegId()
  );
}

static void extractPin(Print& settingsScript, const JsonObject &obj, const char *key)
{
  if (obj[key].is<JsonArray>()) {
    JsonArray pins = obj[key].as<JsonArray>();
    for (JsonVariant pv : pins) {
      if (pv.as<int>() > -1) { settingsScript.print(","); settingsScript.print(pv.as<int>()); }
    }
  } else {
    if (obj[key].as<int>() > -1) { settingsScript.print(","); settingsScript.print(obj[key].as<int>()); }
  }
}

static void fillWLEDVersion(char *buf, size_t len)
{
  if (!buf || len == 0) return;

  snprintf(buf,len,"WLED %s (%d)<br>\\\"%s\\\"<br>(Processor: %s)",
    versionString,
    VERSION,
    releaseString,
  #if defined(ARDUINO_ARCH_ESP32)
    ESP.getChipModel()
  #else
    "ESP8266"
  #endif
  );
}

// print used pins by scanning JsonObject (1 level deep)
static void fillUMPins(Print& settingsScript, const JsonObject &mods)
{
  for (JsonPair kv : mods) {
    // kv.key() is usermod name or subobject key
    // kv.value() is object itself
    JsonObject obj = kv.value();
    if (!obj.isNull()) {
      // element is an JsonObject
      if (!obj["pin"].isNull()) {
        extractPin(settingsScript, obj, "pin");
      } else {
        // scan keys (just one level deep as is possible with usermods)
        for (JsonPair so : obj) {
          const char *key = so.key().c_str();
          if (strstr(key, "pin")) {
            // we found a key containing "pin" substring
            if (strlen(strstr(key, "pin")) == 3) {
              // and it is at the end, we found another pin
              extractPin(settingsScript, obj, key);
              continue;
            }
          }
          if (!obj[so.key()].is<JsonObject>()) continue;
          JsonObject subObj = obj[so.key()];
          if (!subObj["pin"].isNull()) {
            // get pins from subobject
            extractPin(settingsScript, subObj, "pin");
          }
        }
      }
    }
  }
}

static void appendGPIOinfo(Print& settingsScript)
{
  settingsScript.print("d.um_p=[-1"); // has to have 1 element
  if (i2c_sda > -1 && i2c_scl > -1) {
    settingsScript.printf(",%d,%d", i2c_sda, i2c_scl);
  }
  if (spi_mosi > -1 && spi_sclk > -1) {
    settingsScript.printf(",%d,%d", spi_mosi, spi_sclk);
  }
  // usermod pin reservations will become unnecessary when settings pages will read cfg.json directly
  if (requestJSONBufferLock(JSON_LOCK_XML)) {
    // if we can't allocate JSON buffer ignore usermod pins
    JsonObject mods = pDoc->createNestedObject("um");
    UsermodManager::addToConfig(mods);
    if (!mods.isNull()) fillUMPins(settingsScript, mods);
    releaseJSONBufferLock();
  }
  settingsScript.print("];");

  // add reserved (unusable) pins
  bool firstPin = true;
  settingsScript.print("d.rsvd=[");
  for (unsigned i = 0; i < WLED_NUM_PINS; i++) {
    if (!PinManager::isPinOk(i, false)) {  // include readonly pins
      if (!firstPin) settingsScript.print(',');
      settingsScript.print(i);
      firstPin = false;
    }
  }
  #ifdef WLED_ENABLE_DMX
  if (!firstPin) settingsScript.print(',');
  settingsScript.print(2); // DMX hardcoded pin
  firstPin = false;
  #endif
  #if defined(WLED_DEBUG) && !defined(WLED_DEBUG_HOST)
  if (!firstPin) settingsScript.print(',');
  settingsScript.print(hardwareTX); // debug output (TX) pin
  firstPin = false;
  #endif
  #if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)
  if (ethernetType != WLED_ETH_NONE && ethernetType < WLED_NUM_ETH_TYPES) {
    if (!firstPin) settingsScript.print(',');
    for (unsigned p=0; p<WLED_ETH_RSVD_PINS_COUNT; p++) { settingsScript.printf("%d,",esp32_nonconfigurable_ethernet_pins[p].pin); }
    if (ethernetBoards[ethernetType].eth_power >= 0)    { settingsScript.printf("%d,",ethernetBoards[ethernetType].eth_power); }
    if (ethernetBoards[ethernetType].eth_mdc >= 0)      { settingsScript.printf("%d,",ethernetBoards[ethernetType].eth_mdc); }
    if (ethernetBoards[ethernetType].eth_mdio >= 0)     { settingsScript.printf("%d,",ethernetBoards[ethernetType].eth_mdio); }
    switch (ethernetBoards[ethernetType].eth_clk_mode)  {
      case ETH_CLOCK_GPIO0_IN:
      case ETH_CLOCK_GPIO0_OUT:
        settingsScript.print(0);
        break;
      case ETH_CLOCK_GPIO16_OUT:
        settingsScript.print(16);
        break;
      case ETH_CLOCK_GPIO17_OUT:
        settingsScript.print(17);
        break;
    }
  }
  #endif
  settingsScript.print("];"); // rsvd

  // add info for read-only GPIO
  settingsScript.print("d.ro_gpio=[");
  firstPin = true;
  for (unsigned i = 0; i < WLED_NUM_PINS; i++) {
    if (PinManager::isReadOnlyPin(i)) {
      // No comma before the first pin
      if (!firstPin) settingsScript.print(',');
      settingsScript.print(i);
      firstPin = false;
    }
  }
  settingsScript.print("];");

  // add info about max. # of pins
  settingsScript.printf("d.max_gpio=%d;",WLED_NUM_PINS);

  // add info about touch-capable GPIO (ESP32 only, not on C3)
  #if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
  settingsScript.print("d.touch=[");
  firstPin = true;
  for (unsigned i = 0; i < WLED_NUM_PINS; i++) {
    if (digitalPinToTouchChannel(i) >= 0) {
      if (!firstPin) settingsScript.print(',');
      settingsScript.print(i);
      firstPin = false;
    }
  }
  settingsScript.print("];");
  #else
  settingsScript.print("d.touch=[];");
  #endif

  // add info about ADC-capable GPIO (for analog button pin filtering)
  settingsScript.print("d.adc=[");
  firstPin = true;
  for (unsigned i = 0; i < WLED_NUM_PINS; i++) {
    if (PinManager::isAnalogPin(i)) {
      if (!firstPin) settingsScript.print(',');
      settingsScript.print(i);
      firstPin = false;
    }
  }
  settingsScript.print("];");
}

//get values for settings form in javascript
void getSettingsJS(byte subPage, Print& settingsScript)
{
  //0: menu 1: wifi 2: leds 3: ui 4: sync 5: time 6: sec
  DEBUG_PRINTF("settings resp %u\n", (unsigned)subPage);

  if (subPage <0 || subPage >SUBPAGE_LAST) return;
  char nS[32];

  if (subPage == SUBPAGE_MENU)
  {
  #ifdef WLED_DISABLE_2D // include only if 2D is not compiled in
    settingsScript.print("gId('2dbtn').style.display='none';");
  #endif
  #ifdef WLED_ENABLE_DMX // include only if DMX is enabled
    settingsScript.print("gId('dmxbtn').style.display='';");
  #endif
  }

  if (subPage == SUBPAGE_WIFI)
  {
    size_t l;
    settingsScript.printf("resetWiFi(%d);", WLED_MAX_WIFI_COUNT);
    for (size_t n = 0; n < multiWiFi.size(); n++) {
      l = strlen(multiWiFi[n].clientPass);
      char fpass[l+1]; //fill password field with ***
      fpass[l] = 0;
      memset(fpass,'*',l);
      char bssid[13];
      fillMAC2Str(bssid, multiWiFi[n].bssid);
#ifdef WLED_ENABLE_WPA_ENTERPRISE
      settingsScript.printf("addWiFi(\"%s\",\"%s\",\"%s\",0x%X,0x%X,0x%X,\"%u\",\"%s\",\"%s\");",
        multiWiFi[n].clientSSID,
        fpass,
        bssid,
        (uint32_t) multiWiFi[n].staticIP, // explicit cast required as this is a struct
        (uint32_t) multiWiFi[n].staticGW,
        (uint32_t) multiWiFi[n].staticSN,
        multiWiFi[n].encryptionType,
        multiWiFi[n].enterpriseAnonIdentity,
        multiWiFi[n].enterpriseIdentity);
#else
      settingsScript.printf("addWiFi(\"%s\",\"%s\",\"%s\",0x%X,0x%X,0x%X);",
        multiWiFi[n].clientSSID,
        fpass,
        bssid,
        (uint32_t) multiWiFi[n].staticIP, // explicit cast required as this is a struct
        (uint32_t) multiWiFi[n].staticGW,
        (uint32_t) multiWiFi[n].staticSN);
#endif
    }

    printSetFormValue(settingsScript,"D0",dnsAddress[0]);
    printSetFormValue(settingsScript,"D1",dnsAddress[1]);
    printSetFormValue(settingsScript,"D2",dnsAddress[2]);
    printSetFormValue(settingsScript,"D3",dnsAddress[3]);

    printSetFormValue(settingsScript,"CM",cmDNS);
    printSetFormIndex(settingsScript,"AB",apBehavior);
    printSetFormValue(settingsScript,"AS",apSSID);
    printSetFormCheckbox(settingsScript,"AH",apHide);

    l = strlen(apPass);
    char fapass[l+1]; //fill password field with ***
    fapass[l] = 0;
    memset(fapass,'*',l);
    printSetFormValue(settingsScript,"AP",fapass);

    printSetFormValue(settingsScript,"AC",apChannel);
    #ifdef ARDUINO_ARCH_ESP32
    printSetFormValue(settingsScript,"TX",txPower);
    #else
    settingsScript.print("gId('tx').style.display='none';");
    #endif
    printSetFormCheckbox(settingsScript,"FG",force802_3g);
    printSetFormCheckbox(settingsScript,"WS",noWifiSleep);

    #ifndef WLED_DISABLE_ESPNOW
    printSetFormCheckbox(settingsScript,"RE",enableESPNow);
    settingsScript.printf("rstR();"); // reset remote list
    for (size_t i = 0; i < linked_remotes.size(); i++) {
      settingsScript.printf("aR(\"RM%u\",\"%s\");", i, linked_remotes[i].data()); // add remote to list
    }
    settingsScript.print("tE();"); // fill fields
    #else
    //hide remote settings if not compiled
    settingsScript.print("toggle('ESPNOW');");  // hide ESP-NOW setting
    #endif

    #if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)
    printSetFormValue(settingsScript,"ETH",ethernetType);
    #else
    //hide ethernet setting if not compiled in
    settingsScript.print("gId('ethd').style.display='none';");
    #endif

    if (Network.isConnected()) //is connected
    {
      char s[32];
      IPAddress localIP = Network.localIP();
      sprintf(s, "%d.%d.%d.%d", localIP[0], localIP[1], localIP[2], localIP[3]);

      #if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)
      if (Network.isEthernet()) strcat(s ," (Ethernet)");
      #endif
      printSetClassElementHTML(settingsScript,"sip",0,s);
    } else
    {
      printSetClassElementHTML(settingsScript,"sip",0,(char*)"Not connected");
    }

    if (WiFi.softAPIP()[0] != 0) //is active
    {
      char s[16];
      IPAddress apIP = WiFi.softAPIP();
      sprintf(s, "%d.%d.%d.%d", apIP[0], apIP[1], apIP[2], apIP[3]);
      printSetClassElementHTML(settingsScript,"sip",1,s);
    } else
    {
      printSetClassElementHTML(settingsScript,"sip",1,(char*)"Not active");
    }

    #ifndef WLED_DISABLE_ESPNOW
    if (strlen(last_signal_src) > 0) { //Have seen an ESP-NOW Remote
      printSetClassElementHTML(settingsScript,"rlid",0,last_signal_src);
    }
    #endif
  }

  if (subPage == SUBPAGE_LEDS)
  {
    appendGPIOinfo(settingsScript);

    settingsScript.printf("d.ledTypes=%s;", BusManager::getLEDTypesJSONString().c_str());

    // set limits
    settingsScript.printf("bLimits(%d,%d,%d,%d,%d,%d,%d,%d,%d,%d);",
      WLED_PLATFORM_ID, // TODO: replace with a info json lookup
      MAX_LEDS_PER_BUS,
      MAX_LED_MEMORY,
      MAX_LEDS,
      WLED_MAX_COLOR_ORDER_MAPPINGS,
      WLED_MAX_DIGITAL_CHANNELS,
      WLED_MAX_RMT_CHANNELS,
      WLED_MAX_I2S_CHANNELS,
      WLED_MAX_ANALOG_CHANNELS,
      WLED_MAX_BUTTONS
    );

    printSetFormCheckbox(settingsScript,"MS",strip.autoSegments);
    printSetFormCheckbox(settingsScript,"CCT",strip.correctWB);
    printSetFormCheckbox(settingsScript,"IC",cctICused);
    printSetFormCheckbox(settingsScript,"CR",strip.cctFromRgb);
    printSetFormValue(settingsScript,"CB",Bus::getCCTBlend());
    printSetFormValue(settingsScript,"FR",strip.getTargetFps());
    printSetFormValue(settingsScript,"AW",Bus::getGlobalAWMode());

    unsigned sumMa = 0;
    for (size_t s = 0; s < BusManager::getNumBusses(); s++) {
      const Bus *bus = BusManager::getBus(s);
      if (!bus) break; // should not happen but for safety
      int offset = s < 10 ? '0' : 'A' - 10;
      char lp[4] = "L0"; lp[2] = offset+s; lp[3] = 0; //ascii 0-9 //strip data pin
      char lc[4] = "LC"; lc[2] = offset+s; lc[3] = 0; //strip length
      char co[4] = "CO"; co[2] = offset+s; co[3] = 0; //strip color order
      char lt[4] = "LT"; lt[2] = offset+s; lt[3] = 0; //strip type
      char ld[4] = "LD"; ld[2] = offset+s; ld[3] = 0; //driver type (RMT=0, I2S=1)
      char ls[4] = "LS"; ls[2] = offset+s; ls[3] = 0; //strip start LED
      char cv[4] = "CV"; cv[2] = offset+s; cv[3] = 0; //strip reverse
      char sl[4] = "SL"; sl[2] = offset+s; sl[3] = 0; //skip 1st LED
      char rf[4] = "RF"; rf[2] = offset+s; rf[3] = 0; //off refresh
      char aw[4] = "AW"; aw[2] = offset+s; aw[3] = 0; //auto white mode
      char wo[4] = "WO"; wo[2] = offset+s; wo[3] = 0; //swap channels
      char sp[4] = "SP"; sp[2] = offset+s; sp[3] = 0; //bus clock speed
      char la[4] = "LA"; la[2] = offset+s; la[3] = 0; //LED current
      char ma[4] = "MA"; ma[2] = offset+s; ma[3] = 0; //max per-port PSU current
      char hs[4] = "HS"; hs[2] = offset+s; hs[3] = 0; //hostname (for network types, custom text for others)
      settingsScript.print("addLEDs(1);");
      uint8_t pins[OUTPUT_MAX_PINS];
      int nPins = bus->getPins(pins);
      for (int i = 0; i < nPins; i++) {
        lp[1] = '0'+i;
        if (PinManager::isPinOk(pins[i]) || bus->isVirtual() || Bus::isHub75(bus->getType())) printSetFormValue(settingsScript,lp,pins[i]);
      }
      printSetFormValue(settingsScript,lc,bus->getLength());
      printSetFormValue(settingsScript,lt,bus->getType());
      printSetFormValue(settingsScript,ld,bus->getDriverType());
      printSetFormValue(settingsScript,co,bus->getColorOrder() & 0x0F);
      printSetFormValue(settingsScript,ls,bus->getStart());
      printSetFormCheckbox(settingsScript,cv,bus->isReversed());
      printSetFormValue(settingsScript,sl,bus->skippedLeds());
      printSetFormCheckbox(settingsScript,rf,bus->isOffRefreshRequired());
      printSetFormValue(settingsScript,aw,bus->getAutoWhiteMode());
      printSetFormValue(settingsScript,wo,bus->getColorOrder() >> 4);
      unsigned speed = bus->getFrequency();
      if (bus->isPWM()) {
        switch (speed) {
          case WLED_PWM_FREQ/2    : speed = 0; break;
          case WLED_PWM_FREQ*2/3  : speed = 1; break;
          default:
          case WLED_PWM_FREQ      : speed = 2; break;
          case WLED_PWM_FREQ*2    : speed = 3; break;
          case WLED_PWM_FREQ*10/3 : speed = 4; break; // uint16_t max (19531 * 3.333)
        }
      } else if (bus->is2Pin()) {
        switch (speed) {
          case  1000 : speed = 0; break;
          case  2000 : speed = 1; break;
          default:
          case  5000 : speed = 2; break;
          case 10000 : speed = 3; break;
          case 20000 : speed = 4; break;
        }
      }
      printSetFormValue(settingsScript,sp,speed);
      printSetFormValue(settingsScript,la,bus->getLEDCurrent());
      printSetFormValue(settingsScript,ma,bus->getMaxCurrent());
      printSetFormValue(settingsScript,hs,bus->getCustomText().c_str());
      sumMa += bus->getMaxCurrent();
    }
    printSetFormValue(settingsScript,"MA",BusManager::ablMilliampsMax() ? BusManager::ablMilliampsMax() : sumMa);
    printSetFormCheckbox(settingsScript,"ABL",BusManager::ablMilliampsMax() || sumMa > 0);
    printSetFormCheckbox(settingsScript,"PPL",!BusManager::ablMilliampsMax() && sumMa > 0);

    settingsScript.printf("resetCOM(%d);", WLED_MAX_COLOR_ORDER_MAPPINGS);
    const ColorOrderMap& com = BusManager::getColorOrderMap();
    for (int s = 0; s < com.count(); s++) {
      const ColorOrderMapEntry* entry = com.get(s);
      if (!entry || !entry->len) break;
      settingsScript.printf("addCOM(%d,%d,%d);", entry->start, entry->len, entry->colorOrder);
    }

    printSetFormValue(settingsScript,"CA",briS);

    printSetFormCheckbox(settingsScript,"BO",turnOnAtBoot);
    printSetFormValue(settingsScript,"BP",bootPreset);

    printSetFormCheckbox(settingsScript,"GB",gammaCorrectBri);
    printSetFormCheckbox(settingsScript,"GC",gammaCorrectCol);
    dtostrf(gammaCorrectVal,3,1,nS); printSetFormValue(settingsScript,"GV",nS);
    printSetFormValue(settingsScript,"TD",transitionDelayDefault);
    printSetFormValue(settingsScript,"TP",randomPaletteChangeTime);
    printSetFormCheckbox(settingsScript,"TH",useHarmonicRandomPalette);
    printSetFormValue(settingsScript,"BF",briMultiplier);
    printSetFormValue(settingsScript,"TB",nightlightTargetBri);
    printSetFormValue(settingsScript,"TL",nightlightDelayMinsDefault);
    printSetFormValue(settingsScript,"TW",nightlightMode);
    printSetFormIndex(settingsScript,"PB",paletteBlend);
    printSetFormValue(settingsScript,"RL",rlyPin);
    printSetFormCheckbox(settingsScript,"RM",rlyMde);
    printSetFormCheckbox(settingsScript,"RO",rlyOpenDrain);
    int i = 0;
    for (const auto &button : buttons) {
      settingsScript.printf("addBtn(%d,%d,%d);", i++, button.pin, button.type);
    }
    printSetFormCheckbox(settingsScript,"IP",disablePullUp);
    printSetFormValue(settingsScript,"TT",touchThreshold);
#ifndef WLED_DISABLE_INFRARED
    printSetFormValue(settingsScript,"IR",irPin);
    printSetFormValue(settingsScript,"IT",irEnabled);
#endif    
    printSetFormCheckbox(settingsScript,"MSO",!irApplyToAllSelected);
  }

  if (subPage == SUBPAGE_UI)
  {
    printSetFormValue(settingsScript,"DS",serverDescription);
    printSetFormCheckbox(settingsScript,"SU",simplifiedUI);
  }

  if (subPage == SUBPAGE_SYNC)
  {
    printSetFormValue(settingsScript,"UP",udpPort);
    printSetFormValue(settingsScript,"U2",udpPort2);
  #ifndef WLED_DISABLE_ESPNOW
    if (enableESPNow) printSetFormCheckbox(settingsScript,"EN",useESPNowSync);
    else              settingsScript.print("toggle('ESPNOW');");  // hide ESP-NOW setting
  #else
    settingsScript.print("toggle('ESPNOW');");  // hide ESP-NOW setting
  #endif
    printSetFormValue(settingsScript,"GS",syncGroups);
    printSetFormValue(settingsScript,"GR",receiveGroups);

    printSetFormCheckbox(settingsScript,"RB",receiveNotificationBrightness);
    printSetFormCheckbox(settingsScript,"RC",receiveNotificationColor);
    printSetFormCheckbox(settingsScript,"RX",receiveNotificationEffects);
    printSetFormCheckbox(settingsScript,"RP",receiveNotificationPalette);
    printSetFormCheckbox(settingsScript,"SO",receiveSegmentOptions);
    printSetFormCheckbox(settingsScript,"SG",receiveSegmentBounds);
    printSetFormCheckbox(settingsScript,"SS",sendNotifications);
    printSetFormCheckbox(settingsScript,"SD",notifyDirect);
    printSetFormCheckbox(settingsScript,"SB",notifyButton);
    printSetFormCheckbox(settingsScript,"SH",notifyHue);
    printSetFormValue(settingsScript,"UR",udpNumRetries);

    printSetFormCheckbox(settingsScript,"NL",nodeListEnabled);
    printSetFormCheckbox(settingsScript,"NB",nodeBroadcastEnabled);

    printSetFormCheckbox(settingsScript,"RD",receiveDirect);
    printSetFormCheckbox(settingsScript,"MO",useMainSegmentOnly);
    printSetFormCheckbox(settingsScript,"RLM",realtimeRespectLedMaps);
    printSetFormValue(settingsScript,"EP",e131Port);
    printSetFormCheckbox(settingsScript,"ES",e131SkipOutOfSequence);
    printSetFormCheckbox(settingsScript,"EM",e131Multicast);
    printSetFormValue(settingsScript,"EU",e131Universe);
#ifdef WLED_ENABLE_DMX
    settingsScript.print(SET_F("hideNoDMXOutput();"));  // hide "not compiled in" message
#endif
#ifndef WLED_ENABLE_DMX_INPUT
    settingsScript.print(SET_F("hideDMXInput();"));  // hide "dmx input" settings
#else
    settingsScript.print(SET_F("hideNoDMXInput();"));  //hide "not compiled in" message
    printSetFormValue(settingsScript,SET_F("IDMT"),dmxInputTransmitPin);
    printSetFormValue(settingsScript,SET_F("IDMR"),dmxInputReceivePin);
    printSetFormValue(settingsScript,SET_F("IDME"),dmxInputEnablePin);
    printSetFormValue(settingsScript,SET_F("IDMP"),dmxInputPort);
#endif
    printSetFormValue(settingsScript,"DA",DMXAddress);
    printSetFormValue(settingsScript,"XX",DMXSegmentSpacing);
    printSetFormValue(settingsScript,"PY",e131Priority);
    printSetFormValue(settingsScript,"DM",DMXMode);
    printSetFormValue(settingsScript,"ET",realtimeTimeoutMs);
    printSetFormCheckbox(settingsScript,"FB",arlsForceMaxBri);
    printSetFormCheckbox(settingsScript,"RG",arlsDisableGammaCorrection);
    printSetFormValue(settingsScript,"WO",arlsOffset);
    #ifndef WLED_DISABLE_ALEXA
    printSetFormCheckbox(settingsScript,"AL",alexaEnabled);
    printSetFormValue(settingsScript,"AI",alexaInvocationName);
    printSetFormCheckbox(settingsScript,"SA",notifyAlexa);
    printSetFormValue(settingsScript,"AP",alexaNumPresets);
    #else
    settingsScript.print("toggle('Alexa');");  // hide Alexa settings
    #endif

    #ifndef WLED_DISABLE_MQTT
    printSetFormCheckbox(settingsScript,"MQ",mqttEnabled);
    printSetFormValue(settingsScript,"MS",mqttServer);
    printSetFormValue(settingsScript,"MQPORT",mqttPort);
    printSetFormValue(settingsScript,"MQUSER",mqttUser);
    byte l = strlen(mqttPass);
    char fpass[l+1]; //fill password field with ***
    fpass[l] = 0;
    memset(fpass,'*',l);
    printSetFormValue(settingsScript,"MQPASS",fpass);
    printSetFormValue(settingsScript,"MQCID",mqttClientID);
    printSetFormValue(settingsScript,"MD",mqttDeviceTopic);
    printSetFormValue(settingsScript,"MG",mqttGroupTopic);
    printSetFormCheckbox(settingsScript,"BM",buttonPublishMqtt);
    printSetFormCheckbox(settingsScript,"RT",retainMqttMsg);
    settingsScript.printf("d.Sf.MD.maxLength=%d;d.Sf.MG.maxLength=%d;d.Sf.MS.maxLength=%d;",
                  MQTT_MAX_TOPIC_LEN, MQTT_MAX_TOPIC_LEN, MQTT_MAX_SERVER_LEN);
    #else
    settingsScript.print("toggle('MQTT');");    // hide MQTT settings
    #endif

    #ifndef WLED_DISABLE_HUESYNC
    printSetFormValue(settingsScript,"H0",hueIP[0]);
    printSetFormValue(settingsScript,"H1",hueIP[1]);
    printSetFormValue(settingsScript,"H2",hueIP[2]);
    printSetFormValue(settingsScript,"H3",hueIP[3]);
    printSetFormValue(settingsScript,"HL",huePollLightId);
    printSetFormValue(settingsScript,"HI",huePollIntervalMs);
    printSetFormCheckbox(settingsScript,"HP",huePollingEnabled);
    printSetFormCheckbox(settingsScript,"HO",hueApplyOnOff);
    printSetFormCheckbox(settingsScript,"HB",hueApplyBri);
    printSetFormCheckbox(settingsScript,"HC",hueApplyColor);
    char hueErrorString[25];
    switch (hueError)
    {
      case HUE_ERROR_INACTIVE     : strcpy(hueErrorString,"Inactive");                break;
      case HUE_ERROR_ACTIVE       : strcpy(hueErrorString,"Active");                  break;
      case HUE_ERROR_UNAUTHORIZED : strcpy(hueErrorString,"Unauthorized");            break;
      case HUE_ERROR_LIGHTID      : strcpy(hueErrorString,"Invalid light ID");        break;
      case HUE_ERROR_PUSHLINK     : strcpy(hueErrorString,"Link button not pressed"); break;
      case HUE_ERROR_JSON_PARSING : strcpy(hueErrorString,"JSON parsing error");      break;
      case HUE_ERROR_TIMEOUT      : strcpy(hueErrorString,"Timeout");                 break;
      default: sprintf(hueErrorString,"Bridge Error %i",hueError);
    }

    printSetClassElementHTML(settingsScript,"sip",0,hueErrorString);
    #else
    settingsScript.print("toggle('Hue');");    // hide Hue Sync settings
    #endif
    printSetFormValue(settingsScript,"BD",serialBaud);
    #ifndef WLED_ENABLE_ADALIGHT
    settingsScript.print("toggle('Serial');");
    #endif
  }

  if (subPage == SUBPAGE_TIME)
  {
    printSetFormCheckbox(settingsScript,"NT",ntpEnabled);
    printSetFormValue(settingsScript,"NS",ntpServerName);
    printSetFormCheckbox(settingsScript,"CF",!useAMPM);
    printSetFormIndex(settingsScript,"TZ",currentTimezone);
    printSetFormValue(settingsScript,"UO",utcOffsetSecs);
    char tm[32];
    dtostrf(longitude,4,2,tm);
    printSetFormValue(settingsScript,"LN",tm);
    dtostrf(latitude,4,2,tm);
    printSetFormValue(settingsScript,"LT",tm);
    getTimeString(tm);
    printSetClassElementHTML(settingsScript,"times",0,tm);
    if ((int)(longitude*10.0f) || (int)(latitude*10.0f)) {
      sprintf(tm, "Sunrise: %02d:%02d Sunset: %02d:%02d", hour(sunrise), minute(sunrise), hour(sunset), minute(sunset));
      printSetClassElementHTML(settingsScript,"times",1,tm);
    }
    printSetFormCheckbox(settingsScript,"OL",overlayCurrent);
    printSetFormValue(settingsScript,"O1",overlayMin);
    printSetFormValue(settingsScript,"O2",overlayMax);
    printSetFormValue(settingsScript,"OM",analogClock12pixel);
    printSetFormCheckbox(settingsScript,"OS",analogClockSecondsTrail);
    printSetFormCheckbox(settingsScript,"O5",analogClock5MinuteMarks);
    printSetFormCheckbox(settingsScript,"OB",analogClockSolidBlack);

    printSetFormCheckbox(settingsScript,"CE",countdownMode);
    printSetFormValue(settingsScript,"CY",countdownYear);
    printSetFormValue(settingsScript,"CI",countdownMonth);
    printSetFormValue(settingsScript,"CD",countdownDay);
    printSetFormValue(settingsScript,"CH",countdownHour);
    printSetFormValue(settingsScript,"CM",countdownMin);
    printSetFormValue(settingsScript,"CS",countdownSec);

    printSetFormValue(settingsScript,"A0",macroAlexaOn);
    printSetFormValue(settingsScript,"A1",macroAlexaOff);
    printSetFormValue(settingsScript,"MC",macroCountdown);
    printSetFormValue(settingsScript,"MN",macroNl);
    int ii = 0;
    for (const auto &button : buttons) {
      settingsScript.printf("addRow(%d,%d,%d,%d);", ii++, button.macroButton, button.macroLongPress, button.macroDoublePress);
    }

    settingsScript.printf("maxTimers=%d;", WLED_MAX_TIMERS);
    if (timers.empty()) {
      settingsScript.print("addTimerRow();");
    } else {
      for (size_t ti = 0; ti < timers.size(); ti++) {
        const Timer& timer = timers[ti];
        settingsScript.printf("addTimerRow(%d,%d,%d,%d,%d,%d,%d,%d);",
                               timer.hour, timer.minute, timer.preset, timer.weekdays,
                               timer.monthStart, timer.dayStart, timer.monthEnd, timer.dayEnd);
      }
    }
  }

  if (subPage == SUBPAGE_SEC)
  {
    byte l = strlen(settingsPIN);
    char fpass[l+1]; //fill PIN field with 0000
    fpass[l] = 0;
    memset(fpass,'0',l);
    printSetFormValue(settingsScript,"PIN",fpass);
    printSetFormCheckbox(settingsScript,"NO",otaLock);
    printSetFormCheckbox(settingsScript,"OW",wifiLock);
    printSetFormCheckbox(settingsScript,"AO",aOtaEnabled);
    printSetFormCheckbox(settingsScript,"SU",otaSameSubnet);
    char tmp_buf[128];
    fillWLEDVersion(tmp_buf,sizeof(tmp_buf));
    printSetClassElementHTML(settingsScript,"sip",0,tmp_buf);
    settingsScript.printf("sd=\"%s\";", serverDescription);
    //hide settings if not compiled
    #ifdef WLED_DISABLE_OTA
    settingsScript.print("toggle('OTA');");  // hide update section
    #endif
    #ifndef WLED_ENABLE_AOTA
    settingsScript.print("toggle('aOTA');");  // hide ArduinoOTA checkbox
    #endif
  }

  #ifdef WLED_ENABLE_DMX // include only if DMX is enabled
  if (subPage == SUBPAGE_DMX)
  {
    printSetFormValue(settingsScript,"PU",e131ProxyUniverse);

    printSetFormValue(settingsScript,"CN",DMXChannels);
    printSetFormValue(settingsScript,"CG",DMXGap);
    printSetFormValue(settingsScript,"CS",DMXStart);
    printSetFormValue(settingsScript,"SL",DMXStartLED);

    printSetFormIndex(settingsScript,"CH1",DMXFixtureMap[0]);
    printSetFormIndex(settingsScript,"CH2",DMXFixtureMap[1]);
    printSetFormIndex(settingsScript,"CH3",DMXFixtureMap[2]);
    printSetFormIndex(settingsScript,"CH4",DMXFixtureMap[3]);
    printSetFormIndex(settingsScript,"CH5",DMXFixtureMap[4]);
    printSetFormIndex(settingsScript,"CH6",DMXFixtureMap[5]);
    printSetFormIndex(settingsScript,"CH7",DMXFixtureMap[6]);
    printSetFormIndex(settingsScript,"CH8",DMXFixtureMap[7]);
    printSetFormIndex(settingsScript,"CH9",DMXFixtureMap[8]);
    printSetFormIndex(settingsScript,"CH10",DMXFixtureMap[9]);
    printSetFormIndex(settingsScript,"CH11",DMXFixtureMap[10]);
    printSetFormIndex(settingsScript,"CH12",DMXFixtureMap[11]);
    printSetFormIndex(settingsScript,"CH13",DMXFixtureMap[12]);
    printSetFormIndex(settingsScript,"CH14",DMXFixtureMap[13]);
    printSetFormIndex(settingsScript,"CH15",DMXFixtureMap[14]);
  }
  #endif

  if (subPage == SUBPAGE_UM) //usermods
  {
    appendGPIOinfo(settingsScript);
    settingsScript.printf("numM=%d;", UsermodManager::getModCount());
    printSetFormValue(settingsScript,"SDA",i2c_sda);
    printSetFormValue(settingsScript,"SCL",i2c_scl);
    printSetFormValue(settingsScript,"MOSI",spi_mosi);
    printSetFormValue(settingsScript,"MISO",spi_miso);
    printSetFormValue(settingsScript,"SCLK",spi_sclk);
    settingsScript.printf("addInfo('SDA','%d');"
                 "addInfo('SCL','%d');"
                 "addInfo('MOSI','%d');"
                 "addInfo('MISO','%d');"
                 "addInfo('SCLK','%d');",
      HW_PIN_SDA, HW_PIN_SCL, HW_PIN_DATASPI, HW_PIN_MISOSPI, HW_PIN_CLOCKSPI
    );
    UsermodManager::appendConfigData(settingsScript);
  }

  if (subPage == SUBPAGE_2D) // 2D matrices
  {
    printSetFormValue(settingsScript,"SOMP",strip.isMatrix);
    #ifndef WLED_DISABLE_2D
    settingsScript.printf("maxPanels=%d;resetPanels();",WLED_MAX_PANELS);
    if (strip.isMatrix) {
      printSetFormValue(settingsScript,"PW",strip.panel.size()>0?strip.panel[0].width:8); //Set generator Width and Height to first panel size for convenience
      printSetFormValue(settingsScript,"PH",strip.panel.size()>0?strip.panel[0].height:8);
      printSetFormValue(settingsScript,"MPC",strip.panel.size());
      // panels
      for (unsigned i=0; i<strip.panel.size(); i++) {
        settingsScript.printf("addPanel(%d);", i);
        char pO[8] = { '\0' };
        snprintf(pO, 7, "P%d", i);       // WLED_WLED_MAX_PANELS is less than 100 so pO will always only be 4 characters or less
        pO[7] = '\0';
        unsigned l = strlen(pO);
        // create P0B, P1B, ..., P63B, etc for other PxxX
        pO[l] = 'B'; printSetFormValue(settingsScript,pO,strip.panel[i].bottomStart);
        pO[l] = 'R'; printSetFormValue(settingsScript,pO,strip.panel[i].rightStart);
        pO[l] = 'V'; printSetFormValue(settingsScript,pO,strip.panel[i].vertical);
        pO[l] = 'S'; printSetFormCheckbox(settingsScript,pO,strip.panel[i].serpentine);
        pO[l] = 'X'; printSetFormValue(settingsScript,pO,strip.panel[i].xOffset);
        pO[l] = 'Y'; printSetFormValue(settingsScript,pO,strip.panel[i].yOffset);
        pO[l] = 'W'; printSetFormValue(settingsScript,pO,strip.panel[i].width);
        pO[l] = 'H'; printSetFormValue(settingsScript,pO,strip.panel[i].height);
      }
    }
    #else
    settingsScript.print("gId(\"somp\").remove(1);"); // remove 2D option from dropdown
    #endif
  }

  if (subPage == SUBPAGE_PINS) // pins info
  {
    appendGPIOinfo(settingsScript);
  }
}
