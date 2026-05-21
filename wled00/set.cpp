#include "wled.h"

/*
 * Receives client input
 */

//called upon POST settings form submit
void handleSettingsSet(AsyncWebServerRequest *request, byte subPage)
{
  if (subPage == SUBPAGE_PINREQ)
  {
    checkSettingsPIN(request->arg("PIN").c_str());
    return;
  }

  //0: menu 1: wifi 2: leds 3: ui 4: sync 5: time 6: sec 7: DMX 8: usermods 9: N/A 10: 2D
  if (subPage < 1 || subPage > 10 || !correctPIN) return;

  //WIFI SETTINGS
  if (subPage == SUBPAGE_WIFI)
  {
    unsigned cnt = 0;
    for (size_t n = 0; n < WLED_MAX_WIFI_COUNT; n++) {
      char cs[4] = "CS"; cs[2] = 48+n; cs[3] = 0; //client SSID
      char pw[4] = "PW"; pw[2] = 48+n; pw[3] = 0; //client password
      char bs[4] = "BS"; bs[2] = 48+n; bs[3] = 0; //BSSID
      char ip[5] = "IP"; ip[2] = 48+n; ip[4] = 0; //IP address
      char gw[5] = "GW"; gw[2] = 48+n; gw[4] = 0; //GW address
      char sn[5] = "SN"; sn[2] = 48+n; sn[4] = 0; //subnet mask
#ifdef WLED_ENABLE_WPA_ENTERPRISE
      char et[4] = "ET"; et[2] = 48+n; et[3] = 0; //WiFi encryption type
      char ea[4] = "EA"; ea[2] = 48+n; ea[3] = 0; //enterprise anonymous identity
      char ei[4] = "EI"; ei[2] = 48+n; ei[3] = 0; //enterprise identity
#endif
      if (request->hasArg(cs)) {
        if (n >= multiWiFi.size()) multiWiFi.emplace_back(); // expand vector by one
        char oldSSID[33]; strcpy(oldSSID, multiWiFi[n].clientSSID);
        char oldPass[65]; strcpy(oldPass, multiWiFi[n].clientPass);
        uint8_t oldBSSID[6]; memcpy(oldBSSID, multiWiFi[n].bssid, 6);  // save old BSSID

        strlcpy(multiWiFi[n].clientSSID, request->arg(cs).c_str(), 33);
        if (strlen(oldSSID) == 0 || strncmp(multiWiFi[n].clientSSID, oldSSID, 32) != 0) {
          forceReconnect = true;
        }
        if (!isAsterisksOnly(request->arg(pw).c_str(), 65)) {
          strlcpy(multiWiFi[n].clientPass, request->arg(pw).c_str(), 65);
          forceReconnect = true;
        }
        fillStr2MAC(multiWiFi[n].bssid, request->arg(bs).c_str());
        if (memcmp(oldBSSID, multiWiFi[n].bssid, 6) != 0) {  // check if BSSID changed
          forceReconnect = true;
        }
        for (size_t i = 0; i < 4; i++) {
          ip[3] = 48+i;
          gw[3] = 48+i;
          sn[3] = 48+i;
          multiWiFi[n].staticIP[i] = request->arg(ip).toInt();
          multiWiFi[n].staticGW[i] = request->arg(gw).toInt();
          multiWiFi[n].staticSN[i] = request->arg(sn).toInt();
        }

#ifdef WLED_ENABLE_WPA_ENTERPRISE
        byte oldType = multiWiFi[n].encryptionType;
        char oldAnon[65]; strcpy(oldAnon, multiWiFi[n].enterpriseAnonIdentity);
        char oldIden[65]; strcpy(oldIden, multiWiFi[n].enterpriseIdentity);
        if (request->hasArg(et) && request->hasArg(ea) && request->hasArg(ei)) {
          multiWiFi[n].encryptionType = request->arg(et).toInt();
          strlcpy(multiWiFi[n].enterpriseAnonIdentity, request->arg(ea).c_str(), 65);
          strlcpy(multiWiFi[n].enterpriseIdentity, request->arg(ei).c_str(), 65);
        } else {
          // No enterprise settings provided, default to PSK
          multiWiFi[n].encryptionType = WIFI_ENCRYPTION_TYPE_PSK;
        }

        if (multiWiFi[n].encryptionType == WIFI_ENCRYPTION_TYPE_PSK) {
          // PSK - Clear the anonymous identity and identity fields
          multiWiFi[n].enterpriseAnonIdentity[0] = '\0';
          multiWiFi[n].enterpriseIdentity[0] = '\0';
        }
        forceReconnect |= oldType != multiWiFi[n].encryptionType;
        if (strncmp(multiWiFi[n].enterpriseAnonIdentity, oldAnon, 64) != 0) {
          forceReconnect = true;
        }
        if (strncmp(multiWiFi[n].enterpriseIdentity, oldIden, 64) != 0) {
          forceReconnect = true;
        }
#endif

        cnt++;
      }
    }
    // remove unused
    if (cnt < multiWiFi.size()) {
      cnt = multiWiFi.size() - cnt;
      while (cnt--) multiWiFi.pop_back();
      multiWiFi.shrink_to_fit(); // release memory
    }

    if (request->hasArg("D0")) {
      dnsAddress = IPAddress(request->arg("D0").toInt(),request->arg("D1").toInt(),request->arg("D2").toInt(),request->arg("D3").toInt());
    }

    strlcpy(cmDNS, request->arg("CM").c_str(), 33);

    apBehavior = request->arg("AB").toInt();
    char oldSSID[33]; strcpy(oldSSID, apSSID);
    strlcpy(apSSID, request->arg("AS").c_str(), 33);
    if (!strcmp(oldSSID, apSSID) && apActive) forceReconnect = true;
    apHide = request->hasArg("AH");
    int passlen = request->arg("AP").length();
    if (passlen == 0 || (passlen > 7 && !isAsterisksOnly(request->arg("AP").c_str(), 65))) {
      strlcpy(apPass, request->arg("AP").c_str(), 65);
      forceReconnect = true;
    }
    int t = request->arg("AC").toInt();
    if (t != apChannel) forceReconnect = true;
    if (t > 0 && t < 14) apChannel = t;

    #ifdef ARDUINO_ARCH_ESP32
    int tx = request->arg("TX").toInt();
    txPower = min(max(tx, (int)WIFI_POWER_2dBm), (int)WIFI_POWER_19_5dBm);
    #endif

    force802_3g = request->hasArg("FG");
    noWifiSleep = request->hasArg("WS");

    #ifndef WLED_DISABLE_ESPNOW
    bool oldESPNow = enableESPNow;
    enableESPNow = request->hasArg("RE");
    if (oldESPNow != enableESPNow) forceReconnect = true;
    linked_remotes.clear();  // clear old remotes
    for (size_t n = 0; n < 10; n++) {
      char rm[4];
      snprintf(rm, sizeof(rm), "RM%d", n); // "RM0" to "RM9"
      if (request->hasArg(rm)) {
        const String& arg = request->arg(rm);
        if (arg.isEmpty()) continue;
        std::array<char, 13> mac{};
        strlcpy(mac.data(), request->arg(rm).c_str(), 13);
        strlwr(mac.data());
        if (mac[0] != '\0') {
          linked_remotes.emplace_back(mac);
        }
      }
    }
    #endif

    #if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)
    ethernetType = request->arg("ETH").toInt();
    initEthernet();
    #endif
  }

  //LED SETTINGS
  if (subPage == SUBPAGE_LEDS)
  {
    int t = 0;

    if (rlyPin>=0 && PinManager::isPinAllocated(rlyPin, PinOwner::Relay)) {
       PinManager::deallocatePin(rlyPin, PinOwner::Relay);
    }
    #ifndef WLED_DISABLE_INFRARED
    if (irPin>=0 && PinManager::isPinAllocated(irPin, PinOwner::IR)) {
      deInitIR();
      PinManager::deallocatePin(irPin, PinOwner::IR);
    }
    #endif
    for (const auto &button : buttons) {
      if (button.pin >= 0 && PinManager::isPinAllocated(button.pin, PinOwner::Button)) {
        PinManager::deallocatePin(button.pin, PinOwner::Button);
        #ifdef SOC_TOUCH_VERSION_2 // ESP32 S2 and S3 have a function to check touch state, detach interrupt
        if (digitalPinToTouchChannel(button.pin) >= 0) // if touch capable pin
          touchDetachInterrupt(button.pin);            // if not assigned previously, this will do nothing
        #endif
      }
    }

    unsigned colorOrder, type, skip, awmode, channelSwap, maPerLed, driverType;
    unsigned length, start, maMax;
    uint8_t pins[OUTPUT_MAX_PINS] = {255, 255, 255, 255, 255};
    String text;

    // this will set global ABL max current used when per-port ABL is not used
    unsigned ablMilliampsMax = request->arg("MA").toInt();
    BusManager::setMilliampsMax(ablMilliampsMax);

    strip.autoSegments = request->hasArg("MS");
    strip.correctWB = request->hasArg("CCT");
    strip.cctFromRgb = request->hasArg("CR");
    cctICused = request->hasArg("IC");
    uint8_t cctBlending = request->arg("CB").toInt();
    Bus::setCCTBlend(cctBlending);
    Bus::setGlobalAWMode(request->arg("AW").toInt());
    strip.setTargetFps(request->arg("FR").toInt());

    bool busesChanged = false;
    for (int s = 0; s < 36; s++) { // theoretical limit is 36 : "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      int offset = s < 10 ? '0' : 'A' - 10;
      char lp[4] = "L0"; lp[2] = offset+s; lp[3] = 0; //ascii 0-9 //strip data pin
      char lc[4] = "LC"; lc[2] = offset+s; lc[3] = 0; //strip length
      char co[4] = "CO"; co[2] = offset+s; co[3] = 0; //strip color order
      char lt[4] = "LT"; lt[2] = offset+s; lt[3] = 0; //strip type
      char ls[4] = "LS"; ls[2] = offset+s; ls[3] = 0; //strip start LED
      char cv[4] = "CV"; cv[2] = offset+s; cv[3] = 0; //strip reverse
      char sl[4] = "SL"; sl[2] = offset+s; sl[3] = 0; //skip first N LEDs
      char rf[4] = "RF"; rf[2] = offset+s; rf[3] = 0; //refresh required
      char aw[4] = "AW"; aw[2] = offset+s; aw[3] = 0; //auto white mode
      char wo[4] = "WO"; wo[2] = offset+s; wo[3] = 0; //channel swap
      char sp[4] = "SP"; sp[2] = offset+s; sp[3] = 0; //bus clock speed (DotStar & PWM)
      char la[4] = "LA"; la[2] = offset+s; la[3] = 0; //LED mA
      char ma[4] = "MA"; ma[2] = offset+s; ma[3] = 0; //max mA
      char ld[4] = "LD"; ld[2] = offset+s; ld[3] = 0; //driver type (RMT=0, I2S=1)
      char hs[4] = "HS"; hs[2] = offset+s; hs[3] = 0; //hostname (for network types, custom text for others)
      if (!request->hasArg(lp)) {
        DEBUG_PRINTF_P("# of buses: %d\n", s);
        break;
      }
      for (int i = 0; i < 5; i++) {
        lp[1] = '0'+i;
        if (!request->hasArg(lp)) break;
        pins[i] = (request->arg(lp).length() > 0) ? request->arg(lp).toInt() : 255;
      }
      type = request->arg(lt).toInt();
      skip = request->arg(sl).toInt();
      colorOrder = request->arg(co).toInt();
      start = (request->hasArg(ls)) ? request->arg(ls).toInt() : t;
      if (request->hasArg(lc) && request->arg(lc).toInt() > 0) {
        t += length = request->arg(lc).toInt();
      } else {
        break;  // no parameter
      }
      awmode = request->arg(aw).toInt();
      uint16_t freq = request->arg(sp).toInt();
      if (Bus::isPWM(type)) {
        switch (freq) {
          case 0 : freq = WLED_PWM_FREQ/2;    break;
          case 1 : freq = WLED_PWM_FREQ*2/3;  break;
          default:
          case 2 : freq = WLED_PWM_FREQ;      break;
          case 3 : freq = WLED_PWM_FREQ*2;    break;
          case 4 : freq = WLED_PWM_FREQ*10/3; break; // uint16_t max (19531 * 3.333)
        }
      } else if (Bus::is2Pin(type)) {
        switch (freq) {
          default:
          case 0 : freq =  1000; break;
          case 1 : freq =  2000; break;
          case 2 : freq =  5000; break;
          case 3 : freq = 10000; break;
          case 4 : freq = 20000; break;
        }
      } else {
        freq = 0;
      }
      channelSwap = Bus::hasWhite(type) ? request->arg(wo).toInt() : 0;
      if (Bus::isOnOff(type) || Bus::isPWM(type) || Bus::isVirtual(type)) { // analog and virtual
        maPerLed = 0;
        maMax = 0;
      } else {
        maPerLed = request->arg(la).toInt();
        maMax = request->arg(ma).toInt() * request->hasArg("PPL"); // if PP-ABL is disabled maMax (per bus) must be 0
      }
      type |= request->hasArg(rf) << 7; // off refresh override
      driverType = request->arg(ld).toInt(); // 0=RMT (default), 1=I2S
      text = request->arg(hs).substring(0,31);
      // actual finalization is done in WLED::loop() (removing old busses and adding new)
      // this may happen even before this loop is finished so we do "doInitBusses" after the loop
      busConfigs.emplace_back(type, pins, start, length, colorOrder | (channelSwap<<4), request->hasArg(cv), skip, awmode, freq, maPerLed, maMax, driverType, text);
      busesChanged = true;
    }
    //doInitBusses = busesChanged; // we will do that below to ensure all input data is processed

    // we will not bother with pre-allocating ColorOrderMappings vector
    BusManager::getColorOrderMap().reset();
    for (int s = 0; s < WLED_MAX_COLOR_ORDER_MAPPINGS; s++) {
      int offset = s < 10 ? '0' : 'A' - 10;
      char xs[4] = "XS"; xs[2] = offset+s; xs[3] = 0; //start LED
      char xc[4] = "XC"; xc[2] = offset+s; xc[3] = 0; //strip length
      char xo[4] = "XO"; xo[2] = offset+s; xo[3] = 0; //color order
      char xw[4] = "XW"; xw[2] = offset+s; xw[3] = 0; //W swap
      if (request->hasArg(xs)) {
        start = request->arg(xs).toInt();
        length = request->arg(xc).toInt();
        colorOrder = request->arg(xo).toInt() & 0x0F;
        colorOrder |= (request->arg(xw).toInt() & 0x0F) << 4; // add W swap information
        if (!BusManager::getColorOrderMap().add(start, length, colorOrder)) break;
      }
    }

    // update other pins
    #ifndef WLED_DISABLE_INFRARED
    int hw_ir_pin = request->arg("IR").toInt();
    if (PinManager::allocatePin(hw_ir_pin,false, PinOwner::IR)) {
      irPin = hw_ir_pin;
    } else {
      irPin = -1;
    }
    irEnabled = request->arg("IT").toInt();
    initIR();
    #endif
    irApplyToAllSelected = !request->hasArg("MSO");

    int hw_rly_pin = request->arg("RL").toInt();
    if (PinManager::allocatePin(hw_rly_pin,true, PinOwner::Relay)) {
      rlyPin = hw_rly_pin;
    } else {
      rlyPin = -1;
    }
    rlyMde = (bool)request->hasArg("RM");
    rlyOpenDrain = (bool)request->hasArg("RO");

    disablePullUp = (bool)request->hasArg("IP");
    touchThreshold = request->arg("TT").toInt();
    for (int i = 0; i < WLED_MAX_BUTTONS; i++) {
      int offset = i < 10 ? '0' : 'A' - 10;
      char bt[4] = "BT"; bt[2] = offset+i; bt[3] = 0; // button pin (use A,B,C,... if WLED_MAX_BUTTONS>10)
      char be[4] = "BE"; be[2] = offset+i; be[3] = 0; // button type (use A,B,C,... if WLED_MAX_BUTTONS>10)
      int hw_btn_pin = request->hasArg(bt) ? request->arg(bt).toInt() : -1;
      if (i >= buttons.size()) buttons.emplace_back(hw_btn_pin, request->arg(be).toInt()); // add button to vector
      else {
        buttons[i].pin  = hw_btn_pin;
        buttons[i].type = request->arg(be).toInt();
      }
      if (buttons[i].pin >= 0 && PinManager::allocatePin(buttons[i].pin, false, PinOwner::Button)) {
        #ifdef ARDUINO_ARCH_ESP32
        // ESP32 only: check that button pin is a valid gpio
        if ((buttons[i].type == BTN_TYPE_ANALOG) || (buttons[i].type == BTN_TYPE_ANALOG_INVERTED)) {
          if (digitalPinToAnalogChannel(buttons[i].pin) < 0) {
            // not an ADC analog pin
            DEBUG_PRINTF_P("PIN ALLOC error: GPIO%d for analog button #%d is not an analog pin!\n", buttons[i].pin, i);
            PinManager::deallocatePin(buttons[i].pin, PinOwner::Button);
            buttons[i].type = BTN_TYPE_NONE;
          } else {
            analogReadResolution(12); // see #4040
          }
        } else if ((buttons[i].type == BTN_TYPE_TOUCH || buttons[i].type == BTN_TYPE_TOUCH_SWITCH)) {
          if (digitalPinToTouchChannel(buttons[i].pin) < 0) {
            // not a touch pin
            DEBUG_PRINTF_P("PIN ALLOC error: GPIO%d for touch button #%d is not an touch pin!\n", buttons[i].pin, i);
            PinManager::deallocatePin(buttons[i].pin, PinOwner::Button);
            buttons[i].type = BTN_TYPE_NONE;
          }
          #ifdef SOC_TOUCH_VERSION_2 // ESP32 S2 and S3 have a fucntion to check touch state but need to attach an interrupt to do so
          else touchAttachInterrupt(buttons[i].pin, touchButtonISR, touchThreshold << 4); // threshold on Touch V2 is much higher (1500 is a value given by Espressif example, I measured changes of over 5000)
          #endif
        } else
        #endif
        {
          // regular buttons and switches
          if (disablePullUp) {
            pinMode(buttons[i].pin, INPUT);
          } else {
            #ifdef ESP32
            pinMode(buttons[i].pin, buttons[i].type==BTN_TYPE_PUSH_ACT_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
            #else
            pinMode(buttons[i].pin, INPUT_PULLUP);
            #endif
          }
        }
      } else {
        buttons[i].pin  = -1;
        buttons[i].type = BTN_TYPE_NONE;
      }
    }
    // we should remove all unused buttons from the vector
    for (int i = buttons.size()-1; i > 0; i--) {
      if (buttons[i].pin < 0 && buttons[i].type == BTN_TYPE_NONE) {
        buttons.erase(buttons.begin() + i); // remove button from vector
      }
    }

    briS = request->arg("CA").toInt();

    turnOnAtBoot = request->hasArg("BO");
    t = request->arg("BP").toInt();
    if (t <= 250) bootPreset = t;
    gammaCorrectBri = request->hasArg("GB");
    gammaCorrectCol = request->hasArg("GC");
    gammaCorrectVal = request->arg("GV").toFloat();
    if (gammaCorrectVal < 0.1f || gammaCorrectVal > 3) {
      gammaCorrectVal = 1.0f; // no gamma correction
      gammaCorrectBri = false;
      gammaCorrectCol = false;
    }
    NeoGammaWLEDMethod::calcGammaTable(gammaCorrectVal); // fill look-up tables

    t = request->arg("TD").toInt();
    if (t >= 0) transitionDelayDefault = t;
    t = request->arg("TP").toInt();
    randomPaletteChangeTime = MIN(255,MAX(1,t));
    useHarmonicRandomPalette = request->hasArg("TH");

    nightlightTargetBri = request->arg("TB").toInt();
    t = request->arg("TL").toInt();
    if (t > 0) nightlightDelayMinsDefault = t;
    nightlightDelayMins = nightlightDelayMinsDefault;
    nightlightMode = request->arg("TW").toInt();

    t = request->arg("PB").toInt();
    if (t >= 0 && t < 4) paletteBlend = t;
    t = request->arg("BF").toInt();
    if (t > 0) briMultiplier = t;

    doInitBusses = busesChanged;
  }

  //UI
  if (subPage == SUBPAGE_UI)
  {
    strlcpy(serverDescription, request->arg("DS").c_str(), 33);
    simplifiedUI = request->hasArg("SU");
    DEBUG_PRINTLN("Enumerating ledmaps");
    enumerateLedmaps();
    DEBUG_PRINTLN("Loading custom palettes");
    loadCustomPalettes(); // (re)load all custom palettes
  }

  //SYNC
  if (subPage == SUBPAGE_SYNC)
  {
    int t = request->arg("UP").toInt();
    if (t > 0) udpPort = t;
    t = request->arg("U2").toInt();
    if (t > 0) udpPort2 = t;

    #ifndef WLED_DISABLE_ESPNOW
    useESPNowSync = request->hasArg("EN");
    #endif

    syncGroups = request->arg("GS").toInt();
    receiveGroups = request->arg("GR").toInt();

    receiveNotificationBrightness = request->hasArg("RB");
    receiveNotificationColor = request->hasArg("RC");
    receiveNotificationEffects = request->hasArg("RX");
    receiveNotificationPalette = request->hasArg("RP");
    receiveSegmentOptions = request->hasArg("SO");
    receiveSegmentBounds = request->hasArg("SG");
    sendNotifications = request->hasArg("SS");
    notifyDirect = request->hasArg("SD");
    notifyButton = request->hasArg("SB");
    notifyAlexa = request->hasArg("SA");
    notifyHue = request->hasArg("SH");

    t = request->arg("UR").toInt();
    if ((t>=0) && (t<30)) udpNumRetries = t;


    nodeListEnabled = request->hasArg("NL");
    if (!nodeListEnabled) Nodes.clear();
    nodeBroadcastEnabled = request->hasArg("NB");

    receiveDirect = request->hasArg("RD"); // UDP realtime
    useMainSegmentOnly = request->hasArg("MO");
    realtimeRespectLedMaps = request->hasArg("RLM");
    e131SkipOutOfSequence = request->hasArg("ES");
    e131Multicast = request->hasArg("EM");
    t = request->arg("EP").toInt();
    if (t > 0) e131Port = t;
    t = request->arg("EU").toInt();
    if (t >= 0  && t <= 63999) e131Universe = t;
    t = request->arg("DA").toInt();
    if (t >= 0  && t <= 510) DMXAddress = t;
    t = request->arg("XX").toInt();
    if (t >= 0  && t <= 150) DMXSegmentSpacing = t;
    t = request->arg("PY").toInt();
    if (t >= 0  && t <= 200) e131Priority = t;
    t = request->arg("DM").toInt();
    if (t >= DMX_MODE_DISABLED && t <= DMX_MODE_PRESET) DMXMode = t;
    t = request->arg("ET").toInt();
    if (t > 99  && t <= 65000) realtimeTimeoutMs = t;
    arlsForceMaxBri = request->hasArg("FB");
    arlsDisableGammaCorrection = request->hasArg("RG");
    t = request->arg("WO").toInt();
    if (t >= -255  && t <= 255) arlsOffset = t;

#ifdef WLED_ENABLE_DMX_INPUT
    dmxInputTransmitPin = request->arg("IDMT").toInt();
    dmxInputReceivePin = request->arg("IDMR").toInt();
    dmxInputEnablePin = request->arg("IDME").toInt();
    dmxInputPort = request->arg("IDMP").toInt();
    if(dmxInputPort <= 0 || dmxInputPort > 2) dmxInputPort = 2;
#endif

    #ifndef WLED_DISABLE_ALEXA
    alexaEnabled = request->hasArg("AL");
    strlcpy(alexaInvocationName, request->arg("AI").c_str(), 33);
    t = request->arg("AP").toInt();
    if (t >= 0 && t <= 9) alexaNumPresets = t;
    #endif

    #ifndef WLED_DISABLE_MQTT
    mqttEnabled = request->hasArg("MQ");
    strlcpy(mqttServer, request->arg("MS").c_str(), MQTT_MAX_SERVER_LEN+1);
    t = request->arg("MQPORT").toInt();
    if (t > 0) mqttPort = t;
    strlcpy(mqttUser, request->arg("MQUSER").c_str(), 41);
    if (!isAsterisksOnly(request->arg("MQPASS").c_str(), 41)) strlcpy(mqttPass, request->arg("MQPASS").c_str(), 65);
    strlcpy(mqttClientID, request->arg("MQCID").c_str(), 41);
    strlcpy(mqttDeviceTopic, request->arg("MD").c_str(), MQTT_MAX_TOPIC_LEN+1);
    strlcpy(mqttGroupTopic, request->arg("MG").c_str(), MQTT_MAX_TOPIC_LEN+1);
    buttonPublishMqtt = request->hasArg("BM");
    retainMqttMsg = request->hasArg("RT");
    #endif

    #ifndef WLED_DISABLE_HUESYNC
    for (int i=0;i<4;i++){
      String a = "H"+String(i);
      hueIP[i] = request->arg(a).toInt();
    }

    t = request->arg("HL").toInt();
    if (t > 0) huePollLightId = t;

    t = request->arg("HI").toInt();
    if (t > 50) huePollIntervalMs = t;

    hueApplyOnOff = request->hasArg("HO");
    hueApplyBri = request->hasArg("HB");
    hueApplyColor = request->hasArg("HC");
    huePollingEnabled = request->hasArg("HP");
    hueStoreAllowed = true;
    reconnectHue();
    #endif

    t = request->arg("BD").toInt();
    if (t >= 96 && t <= 15000) serialBaud = t;
    updateBaudRate(serialBaud *100);
  }

  //TIME
  if (subPage == SUBPAGE_TIME)
  {
    ntpEnabled = request->hasArg("NT");
    strlcpy(ntpServerName, request->arg("NS").c_str(), 33);
    useAMPM = !request->hasArg("CF");
    currentTimezone = request->arg("TZ").toInt();
    utcOffsetSecs = request->arg("UO").toInt();

    //start ntp if not already connected
    if (ntpEnabled && WLED_CONNECTED && !ntpConnected) ntpConnected = ntpUdp.begin(ntpLocalPort);
    ntpLastSyncTime = NTP_NEVER; // force new NTP query

    longitude = request->arg("LN").toFloat();
    latitude = request->arg("LT").toFloat();
    // force a sunrise/sunset re-calculation
    calculateSunriseAndSunset();

    overlayCurrent = request->hasArg("OL") ? 1 : 0;

    overlayMin = request->arg("O1").toInt();
    overlayMax = request->arg("O2").toInt();
    analogClock12pixel = request->arg("OM").toInt();
    analogClock5MinuteMarks = request->hasArg("O5");
    analogClockSecondsTrail = request->hasArg("OS");
    analogClockSolidBlack = request->hasArg("OB");

    countdownMode = request->hasArg("CE");
    countdownYear = request->arg("CY").toInt();
    countdownMonth = request->arg("CI").toInt();
    countdownDay = request->arg("CD").toInt();
    countdownHour = request->arg("CH").toInt();
    countdownMin = request->arg("CM").toInt();
    countdownSec = request->arg("CS").toInt();
    setCountdown();

    macroAlexaOn = request->arg("A0").toInt();
    macroAlexaOff = request->arg("A1").toInt();
    macroCountdown = request->arg("MC").toInt();
    macroNl = request->arg("MN").toInt();
    int ii = 0;
    for (auto &button : buttons) {
      char mp[4] = "MP"; mp[2] = (ii<10?'0':'A'-10)+ii; mp[3] = 0; // short
      char ml[4] = "ML"; ml[2] = (ii<10?'0':'A'-10)+ii; ml[3] = 0; // long
      char md[4] = "MD"; md[2] = (ii<10?'0':'A'-10)+ii; md[3] = 0; // double
      //if (!request->hasArg(mp)) break;
      button.macroButton = request->arg(mp).toInt();      // these will default to 0 if not present
      button.macroLongPress = request->arg(ml).toInt();
      button.macroDoublePress = request->arg(md).toInt();
      ii++;
    }

    clearTimers();
    char k[5]; k[4] = 0;
    for (int ti = 0; ti < (int)WLED_MAX_TIMERS; ti++) {
      if (ti < 10) {
        k[1] = ti + 48;
        k[2] = 0;
      } else {
        k[1] = '0' + (ti / 10);
        k[2] = '0' + (ti % 10);
        k[3] = 0;
      }
      k[0] = 'T';
      if (!request->hasArg(k)) continue;
      uint8_t p = request->arg(k).toInt();
      k[0] = 'H';
      uint8_t h = request->arg(k).toInt();
      k[0] = 'N';
      int minuteVal = request->arg(k).toInt();
      if (minuteVal < -120) minuteVal = -120;
      if (minuteVal > 120) minuteVal = 120;
      int8_t m = (int8_t)minuteVal;
      k[0] = 'W';
      uint8_t wd = request->arg(k).toInt();
      uint8_t ms = 1, me = 12, ds = 1, de = 31;
      k[0] = 'M';
      ms = request->arg(k).toInt();
      if (ms == 0) ms = 1;
      k[0] = 'P';
      me = request->arg(k).toInt();
      if (me == 0) me = 12;
      k[0] = 'D';
      ds = request->arg(k).toInt();
      if (ds == 0) ds = 1;
      k[0] = 'E';
      de = request->arg(k).toInt();
      if (de == 0) de = 31;
      addTimer(p, h, m, wd, ms, me, ds, de);
    }
    compactTimers();

  }

  //SECURITY
  if (subPage == SUBPAGE_SEC)
  {
    if (request->hasArg("RS")) //complete factory reset
    {
      WLED_FS.format();
      serveMessage(request, 200, "All Settings erased.", "Connect to WLED-AP to setup again",255);
      doReboot = true; // may reboot immediately on dual-core system (race condition) which is desireable in this case
    }

    if (request->hasArg("PIN")) {
      const char *pin = request->arg("PIN").c_str();
      unsigned pinLen = strlen(pin);
      if (pinLen == 4 || pinLen == 0) {
        unsigned numZeros = 0;
        for (unsigned i = 0; i < pinLen; i++) numZeros += (pin[i] == '0');
        if (numZeros < pinLen || pinLen == 0) { // ignore 0000 input (placeholder)
          strlcpy(settingsPIN, pin, 5);
        }
        settingsPIN[4] = 0;
      }
    }

    bool pwdCorrect = !otaLock; //always allow access if ota not locked
    if (request->hasArg("OP"))
    {
      if (otaLock && strcmp(otaPass,request->arg("OP").c_str()) == 0)
      {
        // brute force protection: do not unlock even if correct if last save was less than 3 seconds ago
        if (millis() - lastEditTime > PIN_RETRY_COOLDOWN) pwdCorrect = true;
      }
      if (!otaLock && request->arg("OP").length() > 0)
      {
        strlcpy(otaPass,request->arg("OP").c_str(), 33); // set new OTA password
      }
    }

    if (pwdCorrect) //allow changes if correct pwd or no ota active
    {
      otaLock = request->hasArg("NO");
      wifiLock = request->hasArg("OW");
      #ifndef WLED_DISABLE_OTA
      aOtaEnabled = request->hasArg("AO");
      #endif
      otaSameSubnet = request->hasArg("SU");
    }
  }

  #ifdef WLED_ENABLE_DMX // include only if DMX is enabled
  if (subPage == SUBPAGE_DMX)
  {
    int t = request->arg("PU").toInt();
    if (t >= 0  && t <= 63999) e131ProxyUniverse = t;

    t = request->arg("CN").toInt();
    if (t>0 && t<16) {
      DMXChannels = t;
    }
    t = request->arg("CS").toInt();
    if (t>0 && t<513) {
      DMXStart = t;
    }
    t = request->arg("CG").toInt();
    if (t>0 && t<513) {
      DMXGap = t;
    }
    t = request->arg("SL").toInt();
    if (t>=0 && t < MAX_LEDS) {
      DMXStartLED = t;
    }
    for (int i=0; i<15; i++) {
      String argname = "CH" + String((i+1));
      t = request->arg(argname).toInt();
      DMXFixtureMap[i] = t;
    }
  }
  #endif

  //USERMODS
  if (subPage == SUBPAGE_UM)
  {
    if (!requestJSONBufferLock(JSON_LOCK_SETTINGS)) {
      request->deferResponse();
      return;
    }

    // global I2C & SPI pins
    int8_t hw_sda_pin  = !request->arg("SDA").length() ? -1 : (int)request->arg("SDA").toInt();
    int8_t hw_scl_pin  = !request->arg("SCL").length() ? -1 : (int)request->arg("SCL").toInt();
    if (i2c_sda != hw_sda_pin || i2c_scl != hw_scl_pin) {
      // only if pins changed
      uint8_t old_i2c[2] = { static_cast<uint8_t>(i2c_scl), static_cast<uint8_t>(i2c_sda) };
      PinManager::deallocateMultiplePins(old_i2c, 2, PinOwner::HW_I2C); // just in case deallocation of old pins

      PinManagerPinType i2c[2] = { { hw_sda_pin, true }, { hw_scl_pin, true } };
      if (hw_sda_pin >= 0 && hw_scl_pin >= 0 && PinManager::allocateMultiplePins(i2c, 2, PinOwner::HW_I2C)) {
        i2c_sda = hw_sda_pin;
        i2c_scl = hw_scl_pin;
        // no bus re-initialisation as usermods do not get any notification
        //Wire.begin(i2c_sda, i2c_scl);
      } else {
        // there is no Wire.end()
        DEBUG_PRINTLN("Could not allocate I2C pins.");
        i2c_sda = -1;
        i2c_scl = -1;
      }
    }
    int8_t hw_mosi_pin = !request->arg("MOSI").length() ? -1 : (int)request->arg("MOSI").toInt();
    int8_t hw_miso_pin = !request->arg("MISO").length() ? -1 : (int)request->arg("MISO").toInt();
    int8_t hw_sclk_pin = !request->arg("SCLK").length() ? -1 : (int)request->arg("SCLK").toInt();
    #ifdef ESP8266
    // cannot change pins on ESP8266
    if (hw_mosi_pin >= 0 && hw_mosi_pin != HW_PIN_DATASPI)  hw_mosi_pin = HW_PIN_DATASPI;
    if (hw_miso_pin >= 0 && hw_miso_pin != HW_PIN_MISOSPI)  hw_mosi_pin = HW_PIN_MISOSPI;
    if (hw_sclk_pin >= 0 && hw_sclk_pin != HW_PIN_CLOCKSPI) hw_sclk_pin = HW_PIN_CLOCKSPI;
    #endif
    if (spi_mosi != hw_mosi_pin || spi_miso != hw_miso_pin || spi_sclk != hw_sclk_pin) {
      // only if pins changed
      uint8_t old_spi[3] = { static_cast<uint8_t>(spi_mosi), static_cast<uint8_t>(spi_miso), static_cast<uint8_t>(spi_sclk) };
      PinManager::deallocateMultiplePins(old_spi, 3, PinOwner::HW_SPI); // just in case deallocation of old pins
      PinManagerPinType spi[3] = { { hw_mosi_pin, true }, { hw_miso_pin, true }, { hw_sclk_pin, true } };
      if (hw_mosi_pin >= 0 && hw_sclk_pin >= 0 && PinManager::allocateMultiplePins(spi, 3, PinOwner::HW_SPI)) {
        spi_mosi = hw_mosi_pin;
        spi_miso = hw_miso_pin;
        spi_sclk = hw_sclk_pin;
        // no bus re-initialisation as usermods do not get any notification
        //SPI.end();
        #ifdef ESP32
        //SPI.begin(spi_sclk, spi_miso, spi_mosi);
        #else
        //SPI.begin();
        #endif
      } else {
        //SPI.end();
        DEBUG_PRINTLN("Could not allocate SPI pins.");
        spi_mosi = -1;
        spi_miso = -1;
        spi_sclk = -1;
      }
    }

    JsonObject um = pDoc->createNestedObject("um");

    size_t args = request->args();
    unsigned j=0;
    for (size_t i=0; i<args; i++) {
      String name = request->argName(i);
      String value = request->arg(i);

      // POST request parameters are combined as <usermodname>_<usermodparameter>
      int umNameEnd = name.indexOf(":");
      if (umNameEnd<1) continue;  // parameter does not contain ":" or on 1st place -> wrong

      JsonObject mod = um[name.substring(0,umNameEnd)]; // get a usermod JSON object
      if (mod.isNull()) {
        mod = um.createNestedObject(name.substring(0,umNameEnd)); // if it does not exist create it
      }
      DEBUG_PRINT(name.substring(0,umNameEnd));
      DEBUG_PRINT(":");
      name = name.substring(umNameEnd+1); // remove mod name from string

      // if the resulting name still contains ":" this means nested object
      JsonObject subObj;
      int umSubObj = name.indexOf(":");
      DEBUG_PRINTF_P("(%d):",umSubObj);
      if (umSubObj>0) {
        subObj = mod[name.substring(0,umSubObj)];
        if (subObj.isNull())
          subObj = mod.createNestedObject(name.substring(0,umSubObj));
        name = name.substring(umSubObj+1); // remove nested object name from string
      } else {
        subObj = mod;
      }
      DEBUG_PRINT(name);

      // check if parameters represent array
      if (name.endsWith("[]")) {
        name.replace("[]","");
        value.replace(",",".");      // just in case conversion
        if (!subObj[name].is<JsonArray>()) {
          JsonArray ar = subObj.createNestedArray(name);
          if (value.indexOf(".") >= 0) ar.add(value.toFloat());  // we do have a float
          else                         ar.add(value.toInt());    // we may have an int
          j=0;
        } else {
          if (value.indexOf(".") >= 0) subObj[name].add(value.toFloat());  // we do have a float
          else                         subObj[name].add(value.toInt());    // we may have an int
          j++;
        }
        DEBUG_PRINTF_P("[%d] = %s\n", j, value.c_str());
      } else {
        // we are using a hidden field with the same name as our parameter (!before the actual parameter!)
        // to describe the type of parameter (text,float,int), for boolean parameters the first field contains "off"
        // so checkboxes have one or two fields (first is always "false", existence of second depends on checkmark and may be "true")
        if (subObj[name].isNull()) {
          // the first occurrence of the field describes the parameter type (used in next loop)
          if (value == "false") subObj[name] = false; // checkboxes may have only one field
          else                  subObj[name] = value;
        } else {
          String type = subObj[name].as<String>();  // get previously stored value as a type
          if (subObj[name].is<bool>())   subObj[name] = true;   // checkbox/boolean
          else if (type == "number") {
            value.replace(",",".");      // just in case conversion
            if (value.indexOf(".") >= 0) subObj[name] = value.toFloat();  // we do have a float
            else                         subObj[name] = value.toInt();    // we may have an int
          } else if (type == "int")      subObj[name] = value.toInt();
          else                           subObj[name] = value;  // text fields
        }
        DEBUG_PRINTF_P(" = %s\n", value.c_str());
      }
    }
    UsermodManager::readFromConfig(um);  // force change of usermod parameters
    DEBUG_PRINTLN("Done re-init UsermodManager::");
    releaseJSONBufferLock();
  }

  #ifndef WLED_DISABLE_2D
  //2D panels
  if (subPage == SUBPAGE_2D)
  {
    strip.isMatrix = request->arg("SOMP").toInt();
    strip.panel.clear();
    if (strip.isMatrix) {
      unsigned panels = constrain(request->arg("MPC").toInt(), 1, WLED_MAX_PANELS);
      strip.panel.reserve(panels); // pre-allocate memory
      for (unsigned i=0; i<panels; i++) {
        WS2812FX::Panel p;
        char pO[8] = { '\0' };
        snprintf(pO, 7, "P%d", i);       // WLED_MAX_PANELS is less than 100 so pO will always only be 4 characters or less
        pO[7] = '\0';
        unsigned l = strlen(pO);
        // create P0B, P1B, ..., P63B, etc for other PxxX
        pO[l] = 'B'; if (!request->hasArg(pO)) break;
        pO[l] = 'B'; p.bottomStart = request->arg(pO).toInt();
        pO[l] = 'R'; p.rightStart  = request->arg(pO).toInt();
        pO[l] = 'V'; p.vertical    = request->arg(pO).toInt();
        pO[l] = 'S'; p.serpentine  = request->hasArg(pO);
        pO[l] = 'X'; p.xOffset     = request->arg(pO).toInt();
        pO[l] = 'Y'; p.yOffset     = request->arg(pO).toInt();
        pO[l] = 'W'; p.width       = request->arg(pO).toInt();
        pO[l] = 'H'; p.height      = request->arg(pO).toInt();
        strip.panel.push_back(p);
      }
    }
    strip.panel.shrink_to_fit();  // release unused memory
    // we are changing matrix/ledmap geometry which *will* affect existing segments
    // since we are not in loop() context we must make sure that effects are not running. credit @blazonchek for properly fixing #4911
    strip.suspend();
    strip.waitForIt();
    strip.deserializeMap(); // (re)load default ledmap (will also setUpMatrix() if ledmap does not exist)
    strip.makeAutoSegments(true); // force re-creation of segments
    strip.resume();
  }
  #endif

  lastEditTime = millis();
  // do not save if factory reset or LED settings (which are saved after LED re-init)
  configNeedsWrite = subPage != SUBPAGE_LEDS && !(subPage == SUBPAGE_SEC && doReboot);
  if (subPage == SUBPAGE_UM) doReboot = request->hasArg("RBT"); // prevent race condition on dual core system (set reboot here, after configNeedsWrite has been set)
  #ifndef WLED_DISABLE_ALEXA
  if (subPage == SUBPAGE_SYNC) alexaInit();
  #endif
}


//HTTP API request parser
bool handleSet(AsyncWebServerRequest *request, const String& req, bool apply)
{
  if (!(req.indexOf("win") >= 0)) return false;

  int pos = 0;
  DEBUG_PRINTF_P("API req: %s\n", req.c_str());

  //segment select (sets main segment)
  pos = req.indexOf("SM=");
  if (pos > 0 && !realtimeMode) {
    strip.setMainSegmentId(getNumVal(req, pos));
  }

  byte selectedSeg = strip.getFirstSelectedSegId();

  bool singleSegment = false;

  pos = req.indexOf("SS=");
  if (pos > 0) {
    unsigned t = getNumVal(req, pos);
    if (t < strip.getSegmentsNum()) {
      selectedSeg = t;
      singleSegment = true;
    }
  }

  Segment& selseg = strip.getSegment(selectedSeg);
  pos = req.indexOf("SV="); //segment selected
  if (pos > 0) {
    unsigned t = getNumVal(req, pos);
    if (t == 2) for (unsigned i = 0; i < strip.getSegmentsNum(); i++) strip.getSegment(i).selected = false; // unselect other segments
    selseg.selected = t;
  }

  // temporary values, write directly to segments, globals are updated by setValuesFromFirstSelectedSeg()
  uint32_t col0    = selseg.colors[0];
  uint32_t col1    = selseg.colors[1];
  uint32_t col2    = selseg.colors[2];
  byte colIn[4]    = {R(col0), G(col0), B(col0), W(col0)};
  byte colInSec[4] = {R(col1), G(col1), B(col1), W(col1)};
  byte effectIn    = selseg.mode;
  byte speedIn     = selseg.speed;
  byte intensityIn = selseg.intensity;
  byte paletteIn   = selseg.palette;
  byte custom1In   = selseg.custom1;
  byte custom2In   = selseg.custom2;
  byte custom3In   = selseg.custom3;
  byte check1In    = selseg.check1;
  byte check2In    = selseg.check2;
  byte check3In    = selseg.check3;
  uint16_t startI  = selseg.start;
  uint16_t stopI   = selseg.stop;
  uint16_t startY  = selseg.startY;
  uint16_t stopY   = selseg.stopY;
  uint8_t  grpI    = selseg.grouping;
  uint16_t spcI    = selseg.spacing;
  pos = req.indexOf("&S="); //segment start
  if (pos > 0) {
    startI = std::abs(getNumVal(req, pos));
  }
  pos = req.indexOf("S2="); //segment stop
  if (pos > 0) {
    stopI = std::abs(getNumVal(req, pos));
  }
  pos = req.indexOf("GP="); //segment grouping
  if (pos > 0) {
    grpI = std::max(1,getNumVal(req, pos));
  }
  pos = req.indexOf("SP="); //segment spacing
  if (pos > 0) {
    spcI = std::max(0,getNumVal(req, pos));
  }
  strip.suspend(); // must suspend strip operations before changing geometry
  selseg.setGeometry(startI, stopI, grpI, spcI, UINT16_MAX, startY, stopY, selseg.map1D2D);
  strip.resume();

  pos = req.indexOf("RV="); //Segment reverse
  if (pos > 0) selseg.reverse = req.charAt(pos+3) != '0';

  pos = req.indexOf("MI="); //Segment mirror
  if (pos > 0) selseg.mirror = req.charAt(pos+3) != '0';

  pos = req.indexOf("SB="); //Segment brightness/opacity
  if (pos > 0) {
    byte segbri = getNumVal(req, pos);
    selseg.setOption(SEG_OPTION_ON, segbri); // use transition
    if (segbri) {
      selseg.setOpacity(segbri);
    }
  }

  pos = req.indexOf("SW="); //segment power
  if (pos > 0) {
    switch (getNumVal(req, pos)) {
      case 0:  selseg.setOption(SEG_OPTION_ON, false);      break; // use transition
      case 1:  selseg.setOption(SEG_OPTION_ON, true);       break; // use transition
      default: selseg.setOption(SEG_OPTION_ON, !selseg.on); break; // use transition
    }
  }

  pos = req.indexOf("PS="); //saves current in preset
  if (pos > 0) savePreset(getNumVal(req, pos));

  pos = req.indexOf("P1="); //sets first preset for cycle
  if (pos > 0) presetCycMin = getNumVal(req, pos);

  pos = req.indexOf("P2="); //sets last preset for cycle
  if (pos > 0) presetCycMax = getNumVal(req, pos);

  //apply preset
  if (updateVal(req.c_str(), "PL=", presetCycCurr, presetCycMin, presetCycMax)) {
    applyPreset(presetCycCurr);
  }

  pos = req.indexOf("NP"); //advances to next preset in a playlist
  if (pos > 0) doAdvancePlaylist = true;

  //set brightness
  updateVal(req.c_str(), "&A=", bri);

  bool col0Changed = false, col1Changed = false, col2Changed = false;
  //set colors
  col0Changed |= updateVal(req.c_str(), "&R=", colIn[0]);
  col0Changed |= updateVal(req.c_str(), "&G=", colIn[1]);
  col0Changed |= updateVal(req.c_str(), "&B=", colIn[2]);
  col0Changed |= updateVal(req.c_str(), "&W=", colIn[3]);

  col1Changed |= updateVal(req.c_str(), "R2=", colInSec[0]);
  col1Changed |= updateVal(req.c_str(), "G2=", colInSec[1]);
  col1Changed |= updateVal(req.c_str(), "B2=", colInSec[2]);
  col1Changed |= updateVal(req.c_str(), "W2=", colInSec[3]);

  #ifdef WLED_ENABLE_LOXONE
  //lox parser
  pos = req.indexOf("LX="); // Lox primary color
  if (pos > 0) {
    int lxValue = getNumVal(req, pos);
    if (parseLx(lxValue, colIn)) {
      bri = 255;
      nightlightActive = false; //always disable nightlight when toggling
      col0Changed = true;
    }
  }
  pos = req.indexOf("LY="); // Lox secondary color
  if (pos > 0) {
    int lxValue = getNumVal(req, pos);
    if(parseLx(lxValue, colInSec)) {
      bri = 255;
      nightlightActive = false; //always disable nightlight when toggling
      col1Changed = true;
    }
  }
  #endif

  //set hue
  pos = req.indexOf("HU=");
  if (pos > 0) {
    uint16_t temphue = getNumVal(req, pos);
    byte tempsat = 255;
    pos = req.indexOf("SA=");
    if (pos > 0) {
      tempsat = getNumVal(req, pos);
    }
    byte sec = req.indexOf("H2");
    colorHStoRGB(temphue, tempsat, (sec>0) ? colInSec : colIn);
    col0Changed |= (!sec); col1Changed |= sec;
  }

  //set white spectrum (kelvin)
  pos = req.indexOf("&K=");
  if (pos > 0) {
    byte sec = req.indexOf("K2");
    colorKtoRGB(getNumVal(req, pos), (sec>0) ? colInSec : colIn);
    col0Changed |= (!sec); col1Changed |= sec;
  }

  //set color from HEX or 32bit DEC
  pos = req.indexOf("CL=");
  if (pos > 0) {
    colorFromDecOrHexString(colIn, (char*)req.substring(pos + 3).c_str());
    col0Changed = true;
  }
  pos = req.indexOf("C2=");
  if (pos > 0) {
    colorFromDecOrHexString(colInSec, (char*)req.substring(pos + 3).c_str());
    col1Changed = true;
  }
  pos = req.indexOf("C3=");
  if (pos > 0) {
    byte tmpCol[4];
    colorFromDecOrHexString(tmpCol, (char*)req.substring(pos + 3).c_str());
    col2 = RGBW32(tmpCol[0], tmpCol[1], tmpCol[2], tmpCol[3]);
    selseg.setColor(2, col2); // defined above (SS= or main)
    col2Changed = true;
  }

  //set to random hue SR=0->1st SR=1->2nd
  pos = req.indexOf("SR");
  if (pos > 0) {
    byte sec = getNumVal(req, pos);
    setRandomColor(sec? colInSec : colIn);
    col0Changed |= (!sec); col1Changed |= sec;
  }

  // apply colors to selected segment, and all selected segments if applicable
  if (col0Changed) {
    col0 = RGBW32(colIn[0], colIn[1], colIn[2], colIn[3]);
    selseg.setColor(0, col0);
  }

  if (col1Changed) {
    col1 = RGBW32(colInSec[0], colInSec[1], colInSec[2], colInSec[3]);
    selseg.setColor(1, col1);
  }

  //swap 2nd & 1st
  pos = req.indexOf("SC");
  if (pos > 0) {
    std::swap(col0,col1);
    col0Changed = col1Changed = true;
  }

  bool fxModeChanged = false, speedChanged = false, intensityChanged = false, paletteChanged = false;
  bool custom1Changed = false, custom2Changed = false, custom3Changed = false, check1Changed = false, check2Changed = false, check3Changed = false;
  // set effect parameters
  if (updateVal(req.c_str(), "FX=", effectIn, 0, strip.getModeCount()-1)) {
    if (request != nullptr) unloadPlaylist(); // unload playlist if changing FX using web request
    fxModeChanged = true;
  }
  speedChanged     = updateVal(req.c_str(), "SX=", speedIn);
  intensityChanged = updateVal(req.c_str(), "IX=", intensityIn);
  paletteChanged   = updateVal(req.c_str(), "FP=", paletteIn, 0, getPaletteCount()-1);
  custom1Changed   = updateVal(req.c_str(), "X1=", custom1In);
  custom2Changed   = updateVal(req.c_str(), "X2=", custom2In);
  custom3Changed   = updateVal(req.c_str(), "X3=", custom3In);
  check1Changed    = updateVal(req.c_str(), "M1=", check1In);
  check2Changed    = updateVal(req.c_str(), "M2=", check2In);
  check3Changed    = updateVal(req.c_str(), "M3=", check3In);

  stateChanged |= (fxModeChanged || speedChanged || intensityChanged || paletteChanged || custom1Changed || custom2Changed || custom3Changed || check1Changed || check2Changed || check3Changed);

  // apply to main and all selected segments to prevent #1618.
  for (unsigned i = 0; i < strip.getSegmentsNum(); i++) {
    Segment& seg = strip.getSegment(i);
    if (i != selectedSeg && (singleSegment || !seg.isActive() || !seg.isSelected())) continue; // skip non main segments if not applying to all
    if (fxModeChanged)    seg.setMode(effectIn, req.indexOf("FXD=")>0);  // apply defaults if FXD= is specified
    if (speedChanged)     seg.speed     = speedIn;
    if (intensityChanged) seg.intensity = intensityIn;
    if (paletteChanged)   seg.setPalette(paletteIn);
    if (col0Changed)      seg.setColor(0, col0);
    if (col1Changed)      seg.setColor(1, col1);
    if (col2Changed)      seg.setColor(2, col2);
    if (custom1Changed)   seg.custom1   = custom1In;
    if (custom2Changed)   seg.custom2   = custom2In;
    if (custom3Changed)   seg.custom3   = custom3In;
    if (check1Changed)    seg.check1    = (bool)check1In;
    if (check2Changed)    seg.check2    = (bool)check2In;
    if (check3Changed)    seg.check3    = (bool)check3In;
  }

  //set advanced overlay
  pos = req.indexOf("OL=");
  if (pos > 0) {
    overlayCurrent = getNumVal(req, pos);
  }

  //apply macro (deprecated, added for compatibility with pre-0.11 automations)
  pos = req.indexOf("&M=");
  if (pos > 0) {
    applyPreset(getNumVal(req, pos) + 16);
  }

  //toggle send UDP direct notifications
  pos = req.indexOf("SN=");
  if (pos > 0) notifyDirect = (req.charAt(pos+3) != '0');

  //toggle receive UDP direct notifications
  pos = req.indexOf("RN=");
  if (pos > 0) receiveGroups = (req.charAt(pos+3) != '0') ? receiveGroups | 1 : receiveGroups & 0xFE;

  //receive live data via UDP/Hyperion
  pos = req.indexOf("RD=");
  if (pos > 0) receiveDirect = (req.charAt(pos+3) != '0');

  //main toggle on/off (parse before nightlight, #1214)
  pos = req.indexOf("&T=");
  if (pos > 0) {
    nightlightActive = false; //always disable nightlight when toggling
    switch (getNumVal(req, pos))
    {
      case 0: if (bri != 0){briLast = bri; bri = 0;} break; //off, only if it was previously on
      case 1: if (bri == 0) bri = briLast; break; //on, only if it was previously off
      default: toggleOnOff(); //toggle
    }
  }

  //toggle nightlight mode
  bool aNlDef = false;
  if (req.indexOf("&ND") > 0) aNlDef = true;
  pos = req.indexOf("NL=");
  if (pos > 0)
  {
    if (req.charAt(pos+3) == '0')
    {
      nightlightActive = false;
    } else {
      nightlightActive = true;
      if (!aNlDef) nightlightDelayMins = getNumVal(req, pos);
      else         nightlightDelayMins = nightlightDelayMinsDefault;
      nightlightStartTime = millis();
    }
  } else if (aNlDef)
  {
    nightlightActive = true;
    nightlightDelayMins = nightlightDelayMinsDefault;
    nightlightStartTime = millis();
  }

  //set nightlight target brightness
  pos = req.indexOf("NT=");
  if (pos > 0) {
    nightlightTargetBri = getNumVal(req, pos);
    nightlightActiveOld = false; //re-init
  }

  //toggle nightlight fade
  pos = req.indexOf("NF=");
  if (pos > 0)
  {
    nightlightMode = getNumVal(req, pos);

    nightlightActiveOld = false; //re-init
  }
  if (nightlightMode > NL_MODE_SUN) nightlightMode = NL_MODE_SUN;

  pos = req.indexOf("TT=");
  if (pos > 0) transitionDelay = getNumVal(req, pos);
  strip.setTransition(transitionDelay);

  //set time (unix timestamp)
  pos = req.indexOf("ST=");
  if (pos > 0) {
    setTimeFromAPI(getNumVal(req, pos));
  }

  //set countdown goal (unix timestamp)
  pos = req.indexOf("CT=");
  if (pos > 0) {
    countdownTime = getNumVal(req, pos);
    if (countdownTime - toki.second() > 0) countdownOverTriggered = false;
  }

  pos = req.indexOf("LO=");
  if (pos > 0) {
    realtimeOverride = getNumVal(req, pos);
    if (realtimeOverride > 2) realtimeOverride = REALTIME_OVERRIDE_ALWAYS;
    if (realtimeMode && useMainSegmentOnly) {
      strip.getMainSegment().freeze = !realtimeOverride;
      realtimeOverride = REALTIME_OVERRIDE_NONE;  // ignore request for override if using main segment only
    }
  }

  pos = req.indexOf("RB");
  if (pos > 0) doReboot = true;

  // clock mode, 0: normal, 1: countdown
  pos = req.indexOf("NM=");
  if (pos > 0) countdownMode = (req.charAt(pos+3) != '0');

  pos = req.indexOf("U0="); //user var 0
  if (pos > 0) {
    userVar0 = getNumVal(req, pos);
  }

  pos = req.indexOf("U1="); //user var 1
  if (pos > 0) {
    userVar1 = getNumVal(req, pos);
  }
  // you can add more if you need

  // global colPri[], effectCurrent, ... are updated in stateChanged()
  if (!apply) return true; // when called by JSON API, do not call colorUpdated() here

  pos = req.indexOf("&NN"); //do not send UDP notifications this time
  stateUpdated((pos > 0) ? CALL_MODE_NO_NOTIFY : CALL_MODE_DIRECT_CHANGE);

  // internal call, does not send XML response
  pos = req.indexOf("IN");
  if ((request != nullptr) && (pos < 1)) {
    auto response = request->beginResponseStream("text/xml");
    XML_response(*response);
    request->send(response);
  }

  return true;
}
