#include "wled.h"
#include "json_chunked.h"

using namespace json_chunked;

#define JSON_PATH_STATE      1
#define JSON_PATH_INFO       2
#define JSON_PATH_STATE_INFO 3
#define JSON_PATH_NODES      4
#define JSON_PATH_PALETTES   5
#define JSON_PATH_FXDATA     6
#define JSON_PATH_NETWORKS   7
#define JSON_PATH_EFFECTS    8

/*
 * JSON API (De)serialization
 */
namespace {
  typedef struct {
    uint32_t colors[NUM_COLORS];
    uint16_t start;
    uint16_t stop;
    uint16_t offset;
    uint16_t grouping;
    uint16_t spacing;
    uint16_t startY;
    uint16_t stopY;
    uint16_t options;
    uint8_t  mode;
    uint8_t  palette;
    uint8_t  opacity;
    uint8_t  speed;
    uint8_t  intensity;
    uint8_t  custom1;
    uint8_t  custom2;
    uint8_t  custom3;
    bool     check1;
    bool     check2;
    bool     check3;
  } SegmentCopy;

  uint8_t differs(const Segment& b, const SegmentCopy& a) {
    uint8_t d = 0;
    if (a.start != b.start)         d |= SEG_DIFFERS_BOUNDS;
    if (a.stop != b.stop)           d |= SEG_DIFFERS_BOUNDS;
    if (a.offset != b.offset)       d |= SEG_DIFFERS_GSO;
    if (a.grouping != b.grouping)   d |= SEG_DIFFERS_GSO;
    if (a.spacing != b.spacing)     d |= SEG_DIFFERS_GSO;
    if (a.opacity != b.opacity)     d |= SEG_DIFFERS_BRI;
    if (a.mode != b.mode)           d |= SEG_DIFFERS_FX;
    if (a.speed != b.speed)         d |= SEG_DIFFERS_FX;
    if (a.intensity != b.intensity) d |= SEG_DIFFERS_FX;
    if (a.palette != b.palette)     d |= SEG_DIFFERS_FX;
    if (a.custom1 != b.custom1)     d |= SEG_DIFFERS_FX;
    if (a.custom2 != b.custom2)     d |= SEG_DIFFERS_FX;
    if (a.custom3 != b.custom3)     d |= SEG_DIFFERS_FX;
    if (a.check1 != b.check1)       d |= SEG_DIFFERS_FX;
    if (a.check2 != b.check2)       d |= SEG_DIFFERS_FX;
    if (a.check3 != b.check3)       d |= SEG_DIFFERS_FX;
    if (a.startY != b.startY)       d |= SEG_DIFFERS_BOUNDS;
    if (a.stopY != b.stopY)         d |= SEG_DIFFERS_BOUNDS;

    //bit pattern: (msb first)
    // set:2, sound:2, mapping:3, transposed, mirrorY, reverseY, [reset,] paused, mirrored, on, reverse, [selected]
    if ((a.options & 0b1111111111011110U) != (b.options & 0b1111111111011110U)) d |= SEG_DIFFERS_OPT;
    if ((a.options & 0x0001U) != (b.options & 0x0001U))                         d |= SEG_DIFFERS_SEL;
    for (unsigned i = 0; i < NUM_COLORS; i++) if (a.colors[i] != b.colors[i])   d |= SEG_DIFFERS_COL;

    return d;
  }
}

static bool deserializeSegment(JsonObject elem, byte it, byte presetId = 0)
{
  byte id = elem["id"] | it;
  if (id >= WS2812FX::getMaxSegments()) return false;

  bool newSeg = false;
  int stop = elem["stop"] | -1;

  // append segment
  if (id >= strip.getSegmentsNum()) {
    if (stop <= 0) return false; // ignore empty/inactive segments
    strip.appendSegment(0, strip.getLengthTotal());
    id = strip.getSegmentsNum()-1; // segments are added at the end of list
    newSeg = true;
  }

  //DEBUG_PRINTLN(F("-- JSON deserialize segment."));
  Segment& seg = strip.getSegment(id);
  if (newSeg && presetId == 0) {
    seg.colors[0] = DEFAULT_COLOR; // set color of newly created segment to warm orange as an indicator to the user
  }
  // we do not want to make segment copy as it may use a lot of RAM (effect data and pixel buffer)
  // so we will create a copy of segment options and compare it with original segment when done processing
  SegmentCopy prev = {
    {seg.colors[0], seg.colors[1], seg.colors[2]},
    seg.start,
    seg.stop,
    seg.offset,
    seg.grouping,
    seg.spacing,
    seg.startY,
    seg.stopY,
    seg.options,
    seg.mode,
    seg.palette,
    seg.opacity,
    seg.speed,
    seg.intensity,
    seg.custom1,
    seg.custom2,
    seg.custom3,
    seg.check1,
    seg.check2,
    seg.check3
  };

  int start = elem["start"] | seg.start;
  if (stop < 0) {
    int len = elem["len"];
    stop = (len > 0) ? start + len : seg.stop;
  }
  // 2D segments
  int startY = elem["startY"] | seg.startY;
  int stopY = elem["stopY"] | seg.stopY;

  //repeat, multiplies segment until all LEDs are used, or max segments reached
  bool repeat = elem["rpt"] | false;
  if (repeat && stop>0) {
    elem.remove("id");  // remove for recursive call
    elem.remove("rpt"); // remove for recursive call
    elem.remove("n");   // remove for recursive call
    unsigned len = stop - start;
    for (size_t i=id+1; i<WS2812FX::getMaxSegments(); i++) {
      start = start + len;
      if (start >= strip.getLengthTotal()) break;
      //TODO: add support for 2D
      elem["start"] = start;
      elem["stop"]  = start + len;
      elem["rev"]   = !elem["rev"]; // alternate reverse on even/odd segments
      deserializeSegment(elem, i, presetId); // recursive call with new id
    }
    return true;
  }

  if (elem["n"]) {
    // name field exists
    const char * name = elem["n"].as<const char*>();
    seg.setName(name); // will resolve empty and null correctly
  } else if (start != seg.start || stop != seg.stop) {
    // clearing or setting segment without name field
    seg.clearName();
  }

  uint16_t grp       = elem["grp"] | seg.grouping;
  uint16_t spc       = elem[F("spc")] | seg.spacing;
  uint16_t of        = seg.offset;
  uint8_t  soundSim  = elem["si"] | seg.soundSim;
  uint8_t  map1D2D   = elem["m12"] | seg.map1D2D;
  uint8_t  set       = elem[F("set")] | seg.set;
  bool     selected  = getBoolVal(elem["sel"], seg.selected);
  bool     reverse   = getBoolVal(elem["rev"], seg.reverse);
  bool     mirror    = getBoolVal(elem["mi"] , seg.mirror);
  #ifndef WLED_DISABLE_2D
  bool     reverse_y = getBoolVal(elem["rY"]   , seg.reverse_y);
  bool     mirror_y  = getBoolVal(elem["mY"]   , seg.mirror_y);
  bool     transpose = getBoolVal(elem[F("tp")], seg.transpose);
  #endif

  // if segment's virtual dimensions change we need to restart effect (segment blending and PS rely on dimensions)
  if (seg.mirror != mirror) seg.markForReset();
  #ifndef WLED_DISABLE_2D
  if (seg.mirror_y != mirror_y || seg.transpose != transpose) seg.markForReset();
  #endif

  int len = (stop > start) ? stop - start : 1;
  int offset = elem[F("of")] | INT32_MAX;
  if (offset != INT32_MAX) {
    int offsetAbs = abs(offset);
    if (offsetAbs > len - 1) offsetAbs %= len;
    if (offset < 0) offsetAbs = len - offsetAbs;
    of = offsetAbs;
  }
  if (stop > start && of > len -1) of = len -1;

  // update segment (delete if necessary)
  seg.setGeometry(start, stop, grp, spc, of, startY, stopY, map1D2D); // strip needs to be suspended for this to work without issues

  if (newSeg) seg.refreshLightCapabilities(); // fix for #3403

  if (seg.reset && seg.stop == 0) {
    if (id == strip.getMainSegmentId()) strip.setMainSegmentId(0); // fix for #3403
    return true; // segment was deleted & is marked for reset, no need to change anything else
  }

  byte segbri = seg.opacity;
  if (getVal(elem["bri"], segbri)) {
    if (segbri > 0) seg.setOpacity(segbri); // use transition
    seg.setOption(SEG_OPTION_ON, segbri); // use transition
  }

  seg.setOption(SEG_OPTION_ON, getBoolVal(elem["on"], seg.on)); // use transition
  seg.freeze = getBoolVal(elem["frz"], seg.freeze);

  seg.setCCT(elem["cct"] | seg.cct);

  JsonArray colarr = elem["col"];
  if (!colarr.isNull())
  {
    if (seg.getLightCapabilities() & 3) {
      // segment has RGB or White
      for (size_t i = 0; i < NUM_COLORS; i++) {
        // JSON "col" array can contain the following values for each of segment's colors (primary, background, custom):
        // "col":[int|string|object|array, int|string|object|array, int|string|object|array]
        //   int = Kelvin temperature or 0 for black
        //   string = hex representation of [WW]RRGGBB or "r" for random color
        //   object = individual channel control {"r":0,"g":127,"b":255,"w":255}, each being optional (valid to send {})
        //   array = direct channel values [r,g,b,w] (w element being optional)
        int rgbw[] = {0,0,0,0};
        bool colValid = false;
        JsonArray colX = colarr[i];
        if (colX.isNull()) {
          JsonObject oCol = colarr[i];
          if (!oCol.isNull()) {
            // we have a JSON object for color {"w":123,"r":123,...}; allows individual channel control
            rgbw[0] = oCol["r"] | R(seg.colors[i]);
            rgbw[1] = oCol["g"] | G(seg.colors[i]);
            rgbw[2] = oCol["b"] | B(seg.colors[i]);
            rgbw[3] = oCol["w"] | W(seg.colors[i]);
            colValid = true;
          } else {
            byte brgbw[] = {0,0,0,0};
            const char* hexCol = colarr[i];
            if (hexCol == nullptr) { //Kelvin color temperature (or invalid), e.g 2400
              int kelvin = colarr[i] | -1;
              if (kelvin <  0) continue;
              if (kelvin == 0) seg.setColor(i, 0);
              if (kelvin >  0) colorKtoRGB(kelvin, brgbw);
              colValid = true;
            } else if (hexCol[0] == 'r' && hexCol[1] == '\0') { // Random colors via JSON API in Segment object like col=["r","r","r"] · Issue #4996
              setRandomColor(brgbw);
              colValid = true;
            } else { //HEX string, e.g. "FFAA00"
              colValid = colorFromHexString(brgbw, hexCol);
            }
            for (size_t c = 0; c < 4; c++) rgbw[c] = brgbw[c];
          }
        } else { //Array of ints (RGB or RGBW color), e.g. [255,160,0]
          byte sz = colX.size();
          if (sz == 0) continue; //do nothing on empty array
          copyArray(colX, rgbw, 4);
          colValid = true;
        }

        if (!colValid) continue;

        seg.setColor(i, RGBW32(rgbw[0],rgbw[1],rgbw[2],rgbw[3])); // use transition
        if (seg.mode == FX_MODE_STATIC) strip.trigger(); //instant refresh
      }
    } else {
      // non RGB & non White segment (usually On/Off bus)
      seg.setColor(0, ULTRAWHITE); // use transition
      seg.setColor(1, BLACK); // use transition
    }
  }

  // lx parser
  #ifdef WLED_ENABLE_LOXONE
  int lx = elem[F("lx")] | -1;
  if (lx >= 0) {
    parseLxJson(lx, id, false);
  }
  int ly = elem[F("ly")] | -1;
  if (ly >= 0) {
    parseLxJson(ly, id, true);
  }
  #endif

  seg.set       = constrain(set, 0, 3);
  seg.soundSim  = constrain(soundSim, 0, 3);
  seg.selected  = selected;
  seg.reverse   = reverse;
  seg.mirror    = mirror;
  #ifndef WLED_DISABLE_2D
  seg.reverse_y = reverse_y;
  seg.mirror_y  = mirror_y;
  seg.transpose = transpose;
  #endif

  byte fx = seg.mode;
  if (getVal(elem["fx"], fx, 0, strip.getModeCount())) {
    if (!presetId && currentPlaylist>=0) unloadPlaylist();
    if (fx != seg.mode) seg.setMode(fx, elem[F("fxdef")]); // use transition (WARNING: may change map1D2D causing geometry change)
  }

  getVal(elem["sx"], seg.speed);
  getVal(elem["ix"], seg.intensity);

  uint8_t pal = seg.palette;
  if (seg.getLightCapabilities() & 1) {  // ignore palette for White and On/Off segments
    if (getVal(elem["pal"], pal, 0, getPaletteCount())) seg.setPalette(pal);
  }

  getVal(elem["c1"], seg.custom1);
  getVal(elem["c2"], seg.custom2);
  uint8_t cust3 = seg.custom3;
  getVal(elem["c3"], cust3, 0, 31); // we can't pass reference to bitfield
  seg.custom3 = constrain(cust3, 0, 31);

  seg.check1 = getBoolVal(elem["o1"], seg.check1);
  seg.check2 = getBoolVal(elem["o2"], seg.check2);
  seg.check3 = getBoolVal(elem["o3"], seg.check3);

  getVal(elem["bm"], seg.blendMode);

  JsonArray iarr = elem[F("i")]; //set individual LEDs
  if (!iarr.isNull()) {
    // set brightness immediately and disable transition
    jsonTransitionOnce = true;
    if (seg.isInTransition()) seg.startTransition(0); // setting transition time to 0 will stop transition in next frame
    strip.setTransition(0);
    strip.setBrightness(bri, true);

    // freeze and init to black
    if (!seg.freeze) {
      seg.freeze = true;
      seg.clear();
    }

    unsigned iStart = 0, iStop = 0;
    unsigned iSet = 0; //0 nothing set, 1 start set, 2 range set

    for (size_t i = 0; i < iarr.size(); i++) {
      if (iarr[i].is<JsonInteger>()) {
        if (!iSet) {
          iStart = abs(iarr[i].as<int>());
          iSet++;
        } else {
          iStop = abs(iarr[i].as<int>());
          iSet++;
        }
      } else { //color
        uint8_t rgbw[] = {0,0,0,0};
        JsonArray icol = iarr[i];
        if (!icol.isNull()) { //array, e.g. [255,0,0]
          byte sz = icol.size();
          if (sz > 0 && sz < 5) copyArray(icol, rgbw);
        } else { //hex string, e.g. "FF0000"
          byte brgbw[] = {0,0,0,0};
          const char* hexCol = iarr[i];
          if (colorFromHexString(brgbw, hexCol)) {
            for (size_t c = 0; c < 4; c++) rgbw[c] = brgbw[c];
          }
        }

        if (iSet < 2 || iStop <= iStart) iStop = iStart + 1;
        uint32_t c = RGBW32(rgbw[0], rgbw[1], rgbw[2], rgbw[3]);
        while (iStart < iStop) seg.setRawPixelColor(iStart++, c); // sets pixel color without 1D->2D expansion, grouping or spacing
        iSet = 0;
      }
    }
    strip.trigger(); // force segment update
  }
  // send UDP/WS if segment options changed (except selection; will also deselect current preset)
  if (differs(seg, prev) & ~SEG_DIFFERS_SEL) stateChanged = true;

  return true;
}

// deserializes WLED state
// presetId is non-0 if called from handlePreset()
bool deserializeState(JsonObject root, byte callMode, byte presetId)
{
  bool stateResponse = root[F("v")] | false;

  #if defined(WLED_DEBUG) && defined(WLED_DEBUG_HOST)
  netDebugEnabled = root[F("debug")] | netDebugEnabled;
  #endif

  bool onBefore = bri;
  getVal(root["bri"], bri);
  if (bri != briOld) stateChanged = true;

  bool on = root["on"] | (bri > 0);
  if (!on != !bri) toggleOnOff();

  if (root["on"].is<const char*>() && root["on"].as<const char*>()[0] == 't') {
    if (onBefore || !bri) toggleOnOff(); // do not toggle off again if just turned on by bri (makes e.g. "{"on":"t","bri":32}" work)
  }

  if (bri && !onBefore) { // unfreeze all segments when turning on
    for (size_t s=0; s < strip.getSegmentsNum(); s++) {
      strip.getSegment(s).freeze = false;
    }
    if (realtimeMode && !realtimeOverride && useMainSegmentOnly) { // keep live segment frozen if live
      strip.getMainSegment().freeze = true;
    }
  }

  long tr = -1;
  if (!presetId || currentPlaylist < 0) { //do not apply transition time from preset if playlist active, as it would override playlist transition times
    tr = root[F("transition")] | -1;
    if (tr >= 0) {
      transitionDelay = tr * 100;
      strip.setTransition(transitionDelay);
    }
  }

  blendingStyle = root[F("bs")] | blendingStyle;
  blendingStyle &= 0x1F;

  // temporary transition (applies only once)
  tr = root[F("tt")] | -1;
  if (tr >= 0) {
    jsonTransitionOnce = true;
    strip.setTransition(tr * 100);
  }

  tr = root[F("tb")] | -1;
  if (tr >= 0) strip.timebase = (unsigned long)tr - millis();

  JsonObject nl       = root["nl"];
  if (!nl.isNull()) stateChanged = true;
  nightlightActive    = getBoolVal(nl["on"], nightlightActive);
  nightlightDelayMins = nl["dur"]     | nightlightDelayMins;
  nightlightMode      = nl["mode"]    | nightlightMode;
  nightlightTargetBri = nl[F("tbri")] | nightlightTargetBri;

  JsonObject udpn      = root["udpn"];
  sendNotificationsRT  = getBoolVal(udpn[F("send")], sendNotificationsRT);
  syncGroups           = udpn[F("sgrp")] | syncGroups;
  receiveGroups        = udpn[F("rgrp")] | receiveGroups;
  if ((bool)udpn[F("nn")]) callMode = CALL_MODE_NO_NOTIFY; //send no notification just for this request

  unsigned long timein = root["time"] | UINT32_MAX; //backup time source if NTP not synced
  if (timein != UINT32_MAX) {
    setTimeFromAPI(timein);
    if (presetsModifiedTime == 0) presetsModifiedTime = timein;
  }

  if (root[F("psave")].isNull()) doReboot = root[F("rb")] | doReboot;

  // do not allow changing main segment while in realtime mode (may get odd results else)
  if (!realtimeMode) strip.setMainSegmentId(root[F("mainseg")] | strip.getMainSegmentId()); // must be before realtimeLock() if "live"

  realtimeOverride = root[F("lor")] | realtimeOverride;
  if (realtimeOverride > 2) realtimeOverride = REALTIME_OVERRIDE_ALWAYS;
  if (realtimeMode && useMainSegmentOnly) {
    strip.getMainSegment().freeze = !realtimeOverride;
    realtimeOverride = REALTIME_OVERRIDE_NONE;  // ignore request for override if using main segment only
  }

  if (root.containsKey("live")) {
    if (root["live"].as<bool>()) {
      jsonTransitionOnce = true;
      strip.setTransition(0);
      realtimeLock(65000);
    } else {
      exitRealtime();
    }
  }

  int it = 0;
  JsonVariant segVar = root["seg"];
  if (!segVar.isNull()) {
    // we may be called during strip.service() so we must not modify segments while effects are executing
    strip.suspend();
    strip.waitForIt();
    if (segVar.is<JsonObject>()) {
      int id = segVar["id"] | -1;
      //if "seg" is not an array and ID not specified, apply to all selected/checked segments
      if (id < 0) {
        //apply all selected segments
        for (size_t s = 0; s < strip.getSegmentsNum(); s++) {
          const Segment &sg = strip.getSegment(s);
          if (sg.isActive() && sg.isSelected()) {
            deserializeSegment(segVar, s, presetId);
          }
        }
      } else {
        deserializeSegment(segVar, id, presetId); //apply only the segment with the specified ID
      }
    } else {
      size_t deleted = 0;
      JsonArray segs = segVar.as<JsonArray>();
      for (JsonObject elem : segs) {
        if (deserializeSegment(elem, it++, presetId) && !elem["stop"].isNull() && elem["stop"]==0) deleted++;
      }
      if (strip.getSegmentsNum() > 3 && deleted >= strip.getSegmentsNum()/2U) strip.purgeSegments(); // batch deleting more than half segments
    }
    strip.resume();
  }
  // reset segment request
  if (root[F("rSeg")] | false) {
    strip.suspend();
    strip.waitForIt();
    strip.makeAutoSegments(true);  // respects autoSegments flag
    strip.resume();
    stateChanged = true;
  }

  UsermodManager::readFromJsonState(root);

  loadLedmap = root[F("ledmap")] | loadLedmap;

  byte ps = root[F("psave")];
  if (ps > 0 && ps < 251) savePreset(ps, nullptr, root);

  ps = root[F("pdel")]; //deletion
  if (ps > 0 && ps < 251) deletePreset(ps);

  // HTTP API commands (must be handled before "ps")
  const char* httpwin = root["win"];
  if (httpwin) {
    String apireq = "win"; apireq += '&'; // reduce flash string usage
    apireq += httpwin;
    handleSet(nullptr, apireq, false);    // may set stateChanged
  }

  // Applying preset from JSON API has 2 cases: a) "pd" AKA "preset direct" and b) "ps" AKA "preset select"
  // a) "preset direct" can only be an integer value representing preset ID. "preset direct" assumes JSON API contains the rest of preset content (i.e. from UI call)
  //    "preset direct" JSON can contain "ps" API (i.e. call from UI to cycle presets) in such case stateChanged has to be false (i.e. no "win" or "seg" API)
  // b) "preset select" can be cycling ("1~5~""), random ("r" or "1~5r"), ID, etc. value allowed from JSON API. This type of call assumes no state changing content in API call
  byte presetToRestore = 0;
  if (!root[F("pd")].isNull() && stateChanged) {
    // a) already applied preset content (requires "seg" or "win" but will ignore the rest)
    currentPreset = root[F("pd")] | currentPreset;
    if (root["win"].isNull()) presetCycCurr = currentPreset; // otherwise presetCycCurr was set in handleSet() [set.cpp]
    presetToRestore = currentPreset; // stateUpdated() will clear the preset, so we need to restore it after
    DEBUG_PRINTF_P(PSTR("Preset direct: %d\n"), currentPreset);
  } else if (!root["ps"].isNull()) {
    // we have "ps" call (i.e. from button or external API call) or "pd" that includes "ps" (i.e. from UI call)
    if (root["win"].isNull() && getVal(root["ps"], presetCycCurr, 1, 250) && presetCycCurr > 0 && presetCycCurr < 251 && presetCycCurr != currentPreset) {
      DEBUG_PRINTF_P(PSTR("Preset select: %d\n"), presetCycCurr);
      // b) preset ID only or preset that does not change state (use embedded cycling limits if they exist in getVal())
      // async load from file system (only preset ID was specified)
      // avoid propogating CALL_MODE_INIT, which may cause accidental recursion
      applyPreset(presetCycCurr, callMode == CALL_MODE_INIT ? CALL_MODE_DIRECT_CHANGE : callMode);
      return stateResponse;
    } else presetCycCurr = currentPreset; // restore presetCycCurr
  }

  JsonObject playlist = root[F("playlist")];
  if (!playlist.isNull() && loadPlaylist(playlist, presetId)) {
    //do not notify here, because the first playlist entry will do
    if (root["on"].isNull()) callMode = CALL_MODE_NO_NOTIFY;
    else callMode = CALL_MODE_DIRECT_CHANGE;  // possible bugfix for playlist only containing HTTP API preset FX=~
  }

  if (root.containsKey(F("rmcpal"))) {
    char fileName[32];
    sprintf_P(fileName, PSTR("/palette%d.json"), root[F("rmcpal")].as<uint8_t>());
    if (WLED_FS.exists(fileName)) WLED_FS.remove(fileName);
    loadCustomPalettes();
  }

  doAdvancePlaylist = root[F("np")] | doAdvancePlaylist; //advances to next preset in playlist when true

  JsonObject wifi = root[F("wifi")];
  if (!wifi.isNull()) {
    bool apMode = getBoolVal(wifi[F("ap")], apActive);
    if (!apActive && apMode) WLED::instance().initAP();  // start AP mode immediately
    else if (apActive && !apMode) { // stop AP mode immediately
      dnsServer.stop();
      WiFi.softAPdisconnect(true);
      apActive = false;
    }
    //bool restart = wifi[F("restart")] | false;
    //if (restart) forceReconnect = true;
  }

  if (stateChanged) stateUpdated(callMode);
  if (presetToRestore) currentPreset = presetToRestore;

  return stateResponse;
}

static void serializeSegment(JsonObject& root, const Segment& seg, byte id, bool forPreset, bool segmentBounds)
{
  root["id"] = id;
  if (segmentBounds) {
    root["start"] = seg.start;
    root["stop"] = seg.stop;
    #ifndef WLED_DISABLE_2D
    if (strip.isMatrix) {
      root[F("startY")] = seg.startY;
      root[F("stopY")]  = seg.stopY;
    }
    #endif
  }
  if (!forPreset) root["len"] = seg.stop - seg.start;
  root["grp"]    = seg.grouping;
  root[F("spc")] = seg.spacing;
  root[F("of")]  = seg.offset;
  root["on"]     = seg.on;
  root["frz"]    = seg.freeze;
  byte segbri    = seg.opacity;
  root["bri"]    = (segbri) ? segbri : 255;
  root["cct"]    = seg.cct;
  root[F("set")] = seg.set;
  root["lc"]     = seg.getLightCapabilities();

  if (seg.name != nullptr) root["n"] = reinterpret_cast<const char *>(seg.name); //not good practice, but decreases required JSON buffer
  else if (forPreset) root["n"] = "";

  // to conserve RAM we will serialize the col array manually
  // this will reduce RAM footprint from ~300 bytes to 84 bytes per segment
  char colstr[70]; colstr[0] = '['; colstr[1] = '\0';  //max len 68 (5 chan, all 255)
  const char *format = strip.hasWhiteChannel() ? PSTR("[%u,%u,%u,%u]") : PSTR("[%u,%u,%u]");
  for (size_t i = 0; i < 3; i++)
  {
    byte segcol[4]; byte* c = segcol;
    segcol[0] = R(seg.colors[i]);
    segcol[1] = G(seg.colors[i]);
    segcol[2] = B(seg.colors[i]);
    segcol[3] = W(seg.colors[i]);
    char tmpcol[22];
    sprintf_P(tmpcol, format, (unsigned)c[0], (unsigned)c[1], (unsigned)c[2], (unsigned)c[3]);
    strcat(colstr, i<2 ? strcat(tmpcol, ",") : tmpcol);
  }
  strcat(colstr, "]");
  root["col"] = serialized(colstr);

  root["fx"]  = seg.mode;
  root["sx"]  = seg.speed;
  root["ix"]  = seg.intensity;
  root["pal"] = seg.palette;
  root["c1"]  = seg.custom1;
  root["c2"]  = seg.custom2;
  root["c3"]  = seg.custom3;
  root["sel"] = seg.isSelected();
  root["rev"] = seg.reverse;
  root["mi"]  = seg.mirror;
  #ifndef WLED_DISABLE_2D
  if (strip.isMatrix) {
    root["rY"] = seg.reverse_y;
    root["mY"] = seg.mirror_y;
    root[F("tp")] = seg.transpose;
  }
  #endif
  root["o1"]  = seg.check1;
  root["o2"]  = seg.check2;
  root["o3"]  = seg.check3;
  root["si"]  = seg.soundSim;
  root["m12"] = seg.map1D2D;
  root["bm"]  = seg.blendMode;
}

void serializeState(JsonObject root, bool forPreset, bool includeBri, bool segmentBounds, bool selectedSegmentsOnly)
{
  if (includeBri) {
    root["on"] = (bri > 0);
    root["bri"] = briLast;
    root[F("transition")] = transitionDelay/100; //in 100ms
    root[F("bs")] = blendingStyle;
  }

  if (!forPreset) {
    if (errorFlag) {root[F("error")] = errorFlag; errorFlag = ERR_NONE;} //prevent error message to persist on screen

    root["ps"] = (currentPreset > 0) ? currentPreset : -1;
    root[F("pl")] = currentPlaylist;
    root[F("ledmap")] = currentLedmap;

    UsermodManager::addToJsonState(root);

    JsonObject nl = root.createNestedObject("nl");
    nl["on"] = nightlightActive;
    nl["dur"] = nightlightDelayMins;
    nl["mode"] = nightlightMode;
    nl[F("tbri")] = nightlightTargetBri;
    nl[F("rem")] = nightlightActive ? (int)(nightlightDelayMs - (millis() - nightlightStartTime)) / 1000 : -1; // seconds remaining

    JsonObject udpn = root.createNestedObject("udpn");
    udpn[F("send")] = sendNotificationsRT;
    udpn[F("recv")] = receiveGroups != 0;
    udpn[F("sgrp")] = syncGroups;
    udpn[F("rgrp")] = receiveGroups;

    root[F("lor")] = realtimeOverride;
  }

  root[F("mainseg")] = strip.getMainSegmentId();

  JsonArray seg = root.createNestedArray("seg");
  for (size_t s = 0; s < WS2812FX::getMaxSegments(); s++) {
    if (s >= strip.getSegmentsNum()) {
      if (forPreset && segmentBounds && !selectedSegmentsOnly) { //disable segments not part of preset
        JsonObject seg0 = seg.createNestedObject();
        seg0["stop"] = 0;
        continue;
      } else
        break;
    }
    const Segment &sg = strip.getSegment(s);
    if (forPreset && selectedSegmentsOnly && !sg.isSelected()) continue;
    if (sg.isActive()) {
      JsonObject seg0 = seg.createNestedObject();
      serializeSegment(seg0, sg, s, forPreset, segmentBounds);
    } else if (forPreset && segmentBounds) { //disable segments not part of preset
      JsonObject seg0 = seg.createNestedObject();
      seg0["stop"] = 0;
    }
  }
}


void serializeInfo(JsonObject root)
{
  root[F("ver")] = versionString;
  root[F("vid")] = VERSION;
  root[F("cn")] = F(WLED_CODENAME);
  root[F("release")] = releaseString;
  root[F("repo")] = repoString;
  root[F("deviceId")] = getDeviceId();

  JsonObject leds = root.createNestedObject(F("leds"));
  leds[F("count")] = strip.getLengthTotal();
  leds[F("pwr")] = BusManager::currentMilliamps();
  leds["fps"] = strip.getFps();
  leds[F("maxpwr")] = BusManager::currentMilliamps()>0 ? BusManager::ablMilliampsMax() : 0;
  leds[F("maxseg")] = WS2812FX::getMaxSegments();
  //leds[F("actseg")] = strip.getActiveSegmentsNum();
  //leds[F("seglock")] = false; //might be used in the future to prevent modifications to segment config
  leds[F("bootps")] = bootPreset;

  #ifndef WLED_DISABLE_2D
  if (strip.isMatrix) {
    JsonObject matrix = leds.createNestedObject(F("matrix"));
    matrix["w"] = Segment::maxWidth;
    matrix["h"] = Segment::maxHeight;
  }
  #endif

  unsigned totalLC = 0;
  JsonArray lcarr = leds.createNestedArray(F("seglc")); // deprecated, use state.seg[].lc
  size_t nSegs = strip.getSegmentsNum();
  for (size_t s = 0; s < nSegs; s++) {
    if (!strip.getSegment(s).isActive()) continue;
    unsigned lc = strip.getSegment(s).getLightCapabilities();
    totalLC |= lc;
    lcarr.add(lc); // deprecated, use state.seg[].lc
  }

  leds["lc"] = totalLC;

  leds[F("rgbw")] = strip.hasRGBWBus(); // deprecated, use info.leds.lc
  leds[F("wv")]   = totalLC & 0x02;     // deprecated, true if white slider should be displayed for any segment
  leds["cct"]     = totalLC & 0x04;     // deprecated, use info.leds.lc

  #ifdef WLED_DEBUG
  JsonArray i2c = root.createNestedArray(F("i2c"));
  i2c.add(i2c_sda);
  i2c.add(i2c_scl);
  JsonArray spi = root.createNestedArray(F("spi"));
  spi.add(spi_mosi);
  spi.add(spi_sclk);
  spi.add(spi_miso);
  #endif

  root[F("str")] = false; // sync toggle receive

  root[F("name")] = serverDescription;
  root[F("udpport")] = udpPort;
  root[F("simplifiedui")] = simplifiedUI;
  root["live"] = (bool)realtimeMode;
  root[F("liveseg")] = useMainSegmentOnly ? strip.getMainSegmentId() : -1;  // if using main segment only for live

  switch (realtimeMode) {
    case REALTIME_MODE_INACTIVE: root["lm"] = ""; break;
    case REALTIME_MODE_GENERIC:  root["lm"] = ""; break;
    case REALTIME_MODE_UDP:      root["lm"] = F("UDP"); break;
    case REALTIME_MODE_HYPERION: root["lm"] = F("Hyperion"); break;
    case REALTIME_MODE_E131:     root["lm"] = F("E1.31"); break;
    case REALTIME_MODE_ADALIGHT: root["lm"] = F("USB Adalight/TPM2"); break;
    case REALTIME_MODE_ARTNET:   root["lm"] = F("Art-Net"); break;
    case REALTIME_MODE_TPM2NET:  root["lm"] = F("tpm2.net"); break;
    case REALTIME_MODE_DDP:      root["lm"] = F("DDP"); break;
    case REALTIME_MODE_DMX:      root["lm"] = F("DMX"); break;
  }

  root[F("lip")] = realtimeIP[0] == 0 ? "" : realtimeIP.toString();

  #ifdef WLED_ENABLE_WEBSOCKETS
  root[F("ws")] = ws.count();
  #else
  root[F("ws")] = -1;
  #endif

  root[F("fxcount")] = strip.getModeCount();
  root[F("palcount")] = getPaletteCount();
  root[F("cpalcount")] = customPalettes.size();   // number of user custom palettes (includes gray placeholders)
  root[F("umpalcount")] = usermodPalettes.size(); // number of usermod-registered palettes
  root[F("cpalmax")] = WLED_MAX_CUSTOM_PALETTES;  // maximum number of custom palettes
  // send usermod palette names so the UI can label them correctly
  if (usermodPalettes.size() > 0) {
    JsonArray umpalnames = root.createNestedArray(F("umpalnames"));
    for (size_t j = 0; j < usermodPalettes.size(); j++) {
      char buf[34];
      extractModeName(WLED_USERMOD_PALETTE_ID_BASE - j, JSON_palette_names, buf, sizeof(buf) - 1);
      umpalnames.add(buf);
    }
  }

  JsonArray ledmaps = root.createNestedArray(F("maps"));
  for (size_t i=0; i<WLED_MAX_LEDMAPS; i++) {
    if ((ledMaps>>i) & 0x00000001U) {
      JsonObject ledmaps0 = ledmaps.createNestedObject();
      ledmaps0["id"] = i;
      #ifndef ESP8266
      if (i && ledmapNames[i-1]) ledmaps0["n"] = ledmapNames[i-1];
      #endif
    }
  }

  JsonObject wifi_info = root.createNestedObject(F("wifi"));
  wifi_info[F("bssid")] = WiFi.BSSIDstr();
  int qrssi = WiFi.RSSI();
  wifi_info[F("rssi")] = qrssi;
  wifi_info[F("signal")] = getSignalQuality(qrssi);
  wifi_info[F("channel")] = WiFi.channel();
  wifi_info[F("ap")] = apActive;

  JsonObject fs_info = root.createNestedObject("fs");
  fs_info["u"] = fsBytesUsed / 1000;
  fs_info["t"] = fsBytesTotal / 1000;
  fs_info[F("pmt")] = presetsModifiedTime;

  root[F("ndc")] = nodeListEnabled ? (int)Nodes.size() : -1;

#ifdef ARDUINO_ARCH_ESP32
  #ifdef WLED_DEBUG
    wifi_info[F("txPower")] = (int) WiFi.getTxPower();
    wifi_info[F("sleep")] = (bool) WiFi.getSleep();
  #endif
  #if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_IDF_TARGET_ESP32) // classic esp32 only: report "esp32" without package details
    root[F("arch")] = "esp32";
  #else
    root[F("arch")] = ESP.getChipModel();
  #endif
  root[F("core")] = ESP.getSdkVersion();
  root[F("clock")] = ESP.getCpuFreqMHz();
  root[F("flash")] = (ESP.getFlashChipSize()/1024)/1024;
  #ifdef WLED_DEBUG
  root[F("maxalloc")] = getContiguousFreeHeap();
  root[F("resetReason0")] = (int)rtc_get_reset_reason(0);
  root[F("resetReason1")] = (int)rtc_get_reset_reason(1);
  #endif
  root[F("lwip")] = 0; //deprecated
  #ifndef WLED_DISABLE_OTA
  root[F("bootloaderSHA256")] = getBootloaderSHA256Hex();
  #endif
#else
  root[F("arch")] = "esp8266";
  root[F("core")] = ESP.getCoreVersion();
  root[F("clock")] = ESP.getCpuFreqMHz();
  root[F("flash")] = (ESP.getFlashChipSize()/1024)/1024;
  #ifdef WLED_DEBUG
  root[F("maxalloc")] = getContiguousFreeHeap();
  root[F("resetReason")] = (int)ESP.getResetInfoPtr()->reason;
  #endif
  root[F("lwip")] = LWIP_VERSION_MAJOR;
#endif

  root[F("freeheap")] = getFreeHeapSize();
  #if defined(ARDUINO_ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
  // Report PSRAM information
  // Free PSRAM in bytes (backward compatibility)
  root[F("psram")] = ESP.getFreePsram(); 
  // Total PSRAM size in MB, round up to correct for allocator overhead
  root[F("psrSz")] = (ESP.getPsramSize() + (1024U * 1024U - 1)) / (1024U * 1024U); 
  #endif
  root[F("uptime")] = millis()/1000 + rolloverMillis*4294967;

  char time[32];
  getTimeString(time);
  root[F("time")] = time;

  UsermodManager::addToJsonInfo(root);

  uint16_t os = 0;
  #ifdef WLED_DEBUG
  os  = 0x80;
    #ifdef WLED_DEBUG_HOST
    os |= 0x0100;
    if (!netDebugEnabled) os &= ~0x0080;
    #endif
  #endif
  #ifndef WLED_DISABLE_ALEXA
  os += 0x40;
  #endif

  //os += 0x20; // indicated now removed Blynk support, may be reused to indicate another build-time option

  #ifdef USERMOD_CRONIXIE
  os += 0x10;
  #endif
  #ifndef WLED_DISABLE_FILESYSTEM
  os += 0x08;
  #endif
  #ifndef WLED_DISABLE_HUESYNC
  os += 0x04;
  #endif
  #ifdef WLED_ENABLE_ADALIGHT
  os += 0x02;
  #endif
  #ifndef WLED_DISABLE_OTA
  os += 0x01;
  #endif
  root[F("opt")] = os;

  root[F("brand")] = F(WLED_BRAND);
  root[F("product")] = F(WLED_PRODUCT_NAME);
  root["mac"] = escapedMac;
  char s[16] = "";
  if (Network.isConnected())
  {
    IPAddress localIP = Network.localIP();
    sprintf(s, "%d.%d.%d.%d", localIP[0], localIP[1], localIP[2], localIP[3]);
  }
  root["ip"] = s;
}

// streamPalette16: streams a CRGBPalette16 as [[pos,r,g,b], ...].
// palette must outlive the returned Element (all call sites pass stable globals/members).
static json_chunked::Element streamPalette16(const CRGBPalette16& palette) {
  return writeJSONList(0, 16,
    // Indexes are ints because CRGBPalette16 operator[] doesn't accept size_t
    [&palette](int idx) -> Element {
      std::array<uint8_t,4> e = {uint8_t(idx<<4), palette[idx].red, palette[idx].green, palette[idx].blue};
      return writeJSONList(size_t(0), size_t(4),
        [e](size_t j) -> Element { return int32_t(e[j]); });
    });
}

// makePaletteArrayWriter: builds and streams the color array for one palette entry.
static json_chunked::Element makePaletteArrayWriter(size_t i, size_t palettesCount, size_t umPalettesCount) {
  // Dynamic palettes
  switch (i) {
    case 0: return streamPalette16(PartyColors_gc22);
    case 1: { static const char* s[] = {"r","r","r","r"};                                                                        return writeJSONList(s, s+4,  [](const char** p) -> Element { return *p; }); }
    case 2: { static const char* s[] = {"c1"};                                                                                   return writeJSONList(s, s+1,  [](const char** p) -> Element { return *p; }); }
    case 3: { static const char* s[] = {"c1","c1","c2","c2"};                                                                    return writeJSONList(s, s+4,  [](const char** p) -> Element { return *p; }); }
    case 4: { static const char* s[] = {"c3","c2","c1"};                                                                         return writeJSONList(s, s+3,  [](const char** p) -> Element { return *p; }); }
    case 5: { static const char* s[] = {"c1","c1","c1","c1","c1","c2","c2","c2","c2","c2","c3","c3","c3","c3","c3","c1"};       return writeJSONList(s, s+16, [](const char** p) -> Element { return *p; }); }
  }

  // Custom palettes
  if (i >= palettesCount + umPalettesCount)   return streamPalette16(customPalettes[i - palettesCount - umPalettesCount]);
  // Usermod palettes
  if (i >= palettesCount)                      return streamPalette16(usermodPalettes[i - palettesCount].palette);
  // FastLED fixed palettes
  if (i < DYNAMIC_PALETTE_COUNT + FASTLED_PALETTE_COUNT) return streamPalette16(*fastledPalettes[i - DYNAMIC_PALETTE_COUNT]);

  // Gradient palette: packed uint8_t entries in PROGMEM — may be unaligned.
  // pgm_read_dword reads the pointer out of the PROGMEM array; the pointer itself
  // is used only for iteration (++ advances by sizeof==4, no dereference).
  // Each field is read safely with pgm_read_byte in the callback.
  {
    // The pointer to the palette is dword aligned, so no need for pgm_read_dword here
    const TRGBGradientPaletteEntryUnion* begin_ent = (const TRGBGradientPaletteEntryUnion*) gGradientPalettes[i - (DYNAMIC_PALETTE_COUNT + FASTLED_PALETTE_COUNT)];
    const TRGBGradientPaletteEntryUnion* end_ent = begin_ent;
    while (pgm_read_byte(&end_ent->index) != 255) end_ent++;
    end_ent++;  // advance past the terminator so [begin_ent, end_ent) is inclusive
    return writeJSONList(begin_ent, end_ent,
      [](const TRGBGradientPaletteEntryUnion* ent) -> Element {
        TRGBGradientPaletteEntryUnion e;
        e.dword = pgm_read_dword(ent); // read the whole entry at once
        return writeJSONList(size_t(0), size_t(4),
          [e](size_t j) -> Element { return int32_t(e.bytes[j]); });
      });
  }
}

void respondPalettes(AsyncWebServerRequest* request, size_t page) {
  const size_t customPalettesCount = customPalettes.size();
  const size_t umPalettesCount     = usermodPalettes.size();
  const size_t palettesCount       = FIXED_PALETTE_COUNT;
  #ifdef ESP8266
  constexpr int itemPerPage = 5;
  #else
  constexpr size_t itemPerPage = 8;
  #endif
  const size_t total = palettesCount + umPalettesCount + customPalettesCount;
  const size_t maxPage = total / itemPerPage;
  if (page > maxPage) page = maxPage;
  const size_t start = itemPerPage * page;
  const size_t end   = start + itemPerPage < total ? start + itemPerPage : total;

  respondJSONObject(request, size_t(0), size_t(2),
    [maxPage, start, end, palettesCount, umPalettesCount](size_t idx) -> KeyValuePair {
      if (idx == 0) {
        return KeyValuePair{ "m", maxPage };
      }
      // idx == 1: "p" -> nested object of palette arrays
      return KeyValuePair{
        "p",
        json_chunked::Element(writeJSONObject(start, end,
          [palettesCount, umPalettesCount](size_t i) -> KeyValuePair {
            size_t paletteId;
            if (i >= palettesCount + umPalettesCount)
              paletteId = WLED_CUSTOM_PALETTE_ID_BASE  - (i - palettesCount - umPalettesCount);
            else if (i >= palettesCount)
              paletteId = WLED_USERMOD_PALETTE_ID_BASE - (i - palettesCount);
            else
              paletteId = i;
            return KeyValuePair{
              String(paletteId),
              makePaletteArrayWriter(i, palettesCount, umPalettesCount)
            };
          }))
      };
    });
}


static void serializeNetworks(JsonObject root)
{
  JsonArray networks = root.createNestedArray(F("networks"));
  int16_t status = WiFi.scanComplete();

  switch (status) {
    case WIFI_SCAN_FAILED:
      WiFi.scanNetworks(true);
      return;
    case WIFI_SCAN_RUNNING:
      return;
  }

  for (int i = 0; i < status; i++) {
    JsonObject node = networks.createNestedObject();
    node[F("ssid")]    = WiFi.SSID(i);
    node[F("rssi")]    = WiFi.RSSI(i);
    node[F("bssid")]   = WiFi.BSSIDstr(i);
    node[F("channel")] = WiFi.channel(i);
    node[F("enc")]     = WiFi.encryptionType(i);
  }

  WiFi.scanDelete();

  if (WiFi.scanComplete() == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
  }
}

// writePinItem: build a streaming writer for one GPIO pin object.
// Returns an immediately-done writer for pins that should be skipped.
static json_chunked::Element writePinItem(int gpio) {
  bool canInput    = PinManager::isPinOk(gpio, false);
  bool canOutput   = PinManager::isPinOk(gpio, true);
  bool isAllocated = PinManager::isPinAllocated(gpio);
  if (!canInput && !canOutput && !isAllocated)
    return [](uint8_t*, size_t) -> WriteResult { return {true, 0}; };

  auto doc = std::make_shared<StaticJsonDocument<384>>();
  JsonObject pinObj = doc->to<JsonObject>();
  pinObj["p"] = gpio;

  uint8_t caps = 0;
  #ifdef ARDUINO_ARCH_ESP32
  if (PinManager::isAnalogPin(gpio)) caps |= PIN_CAP_ADC;
  if (canInput && !canOutput) caps |= PIN_CAP_INPUT_ONLY;
  #if defined(CONFIG_IDF_TARGET_ESP32S3)
  if (gpio == 0) caps |= PIN_CAP_BOOT;
  if (gpio == 45 || gpio == 46) caps |= PIN_CAP_BOOTSTRAP;
  #elif defined(CONFIG_IDF_TARGET_ESP32S2)
  if (gpio == 0) caps |= PIN_CAP_BOOT;
  if (gpio == 45 || gpio == 46) caps |= PIN_CAP_BOOTSTRAP;
  #elif defined(CONFIG_IDF_TARGET_ESP32C3)
  if (gpio == 9) caps |= PIN_CAP_BOOT;
  if (gpio == 2 || gpio == 8) caps |= PIN_CAP_BOOTSTRAP;
  #elif defined(CONFIG_IDF_TARGET_ESP32)
  if (gpio == 0) caps |= PIN_CAP_BOOT;
  if (gpio == 2 || gpio == 12) caps |= PIN_CAP_BOOTSTRAP;
  #endif
  #else
  if (gpio == 0) caps |= PIN_CAP_BOOT;
  if (gpio == 2 || gpio == 15) caps |= PIN_CAP_BOOTSTRAP;
  if (gpio == 17) caps = PIN_CAP_INPUT_ONLY | PIN_CAP_ADC;
  #endif
  pinObj["c"] = caps;
  pinObj["a"] = isAllocated;

  int buttonIndex = PinManager::getButtonIndex(gpio);
  PinOwner owner  = PinManager::getPinOwner(gpio);
  if (isAllocated) {
    pinObj["o"] = static_cast<uint8_t>(owner);
    pinObj["n"] = PinManager::getPinOwnerName(gpio);
    if (owner == PinOwner::Relay) {
      pinObj["m"] = 1;
      pinObj["s"] = digitalRead(rlyPin);
    } else if (buttonIndex >= 0) {
      pinObj["m"] = 0;
      pinObj["t"] = buttons[buttonIndex].type;
      pinObj["s"] = isButtonPressed(buttonIndex) ? 1 : 0;
      #if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
      if (buttons[buttonIndex].type == BTN_TYPE_TOUCH || buttons[buttonIndex].type == BTN_TYPE_TOUCH_SWITCH) {
        if (digitalPinToTouchChannel(gpio) >= 0) {
          #ifdef SOC_TOUCH_VERSION_2
          pinObj["r"] = touchRead(gpio) >> 4;
          #else
          pinObj["r"] = touchRead(gpio);
          #endif
        }
      }
      #endif
      if (buttons[buttonIndex].type == BTN_TYPE_ANALOG || buttons[buttonIndex].type == BTN_TYPE_ANALOG_INVERTED) {
        int analogRaw = 0;
        #ifdef ESP8266
        analogRaw = analogRead(A0) >> 2;
        #else
        if (digitalPinToAnalogChannel(gpio) >= 0) analogRaw = (analogRead(gpio) >> 4);
        #endif
        if (buttons[buttonIndex].type == BTN_TYPE_ANALOG_INVERTED) analogRaw = 255 - analogRaw;
        pinObj["r"] = analogRaw;
      }
    } else if (owner == PinOwner::BusOnOff || owner == PinOwner::UM_MultiRelay) {
      pinObj["m"] = 1;
      pinObj["s"] = digitalRead(gpio);
    }
  }

  size_t total = measureJson(*doc);
  size_t sent  = 0;
  return json_chunked::Element(
    [doc, total, sent](uint8_t* buf, size_t maxLen) mutable -> WriteResult {
      if (sent >= total) return {true, 0};
      size_t n = total - sent < maxLen ? total - sent : maxLen;
      ChunkPrint cp(buf, sent, n);
      serializeJson(*doc, cp);
      sent += n;
      return {sent >= total, n};
    });
}

static Element writePins() {
  constexpr int ENUM_PINS = WLED_NUM_PINS;
  return writeJSONObject(size_t(0), size_t(1),
    [](size_t) -> KeyValuePair {
      return KeyValuePair{ "pins", writeJSONList(int(0), ENUM_PINS, writePinItem) };
    });
}

static Element writeModeData() {
  return writeJSONList(size_t(0), size_t(strip.getModeCount()),
    [](size_t i, uint8_t* dest, size_t maxLen) -> size_t {
      // Extract the portion of ModeData after the '@' character
      char buf[256];
      strncpy_P(buf, strip.getModeData(i), sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
      const char* p = strchr(buf, '@');
      return quoteJsonString(dest, maxLen, p ? p + 1 : "");
    });
}

static Element writeNodes() {
  // Produces {"nodes":[{"name":..,"type":N,"ip":..,"age":N,"vid":N},...]}
  return writeJSONObject({
    { "nodes", writeJSONList(Nodes.begin(), Nodes.end(),
        [](NodesMap::iterator it) -> json_chunked::Element {
          if (it->second.ip[0] == 0)
            return json_chunked::Element([](uint8_t*, size_t) -> WriteResult { return {true, 0}; });
          return writeJSONObject({
            { "name",   String(it->second.nodeName) },
            { "type",   it->second.nodeType         },
            { "ip",     it->second.ip.toString()    },
            { F("age"), it->second.age              },
            { F("vid"), it->second.build            }
          });
        })
    }
  });
}

static Element writeModeNames() {
  return writeJSONList(size_t(0), size_t(strip.getModeCount()),
    [](size_t i, uint8_t* dest, size_t maxLen) -> size_t {
      // Extract the portion of ModeData before the '@' character, which is the user-friendly name
      char buf[256];
      strncpy_P(buf, strip.getModeData(i), sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
      char* p = strchr(buf, '@');
      if (p) *p = '\0';
      return quoteJsonString(dest, maxLen, buf);
    });
}

static void respondJsonAll(AsyncWebServerRequest* request) {
  if (!requestJSONBufferLock(JSON_LOCK_SERVEJSON)) {
    request->deferResponse();
    return;
  }

  // Populate state and info into pDoc sub-objects.
  // The lock is held until the chunked response finishes streaming.
  pDoc->clear();
  JsonObject stateObj = pDoc->createNestedObject("state");
  serializeState(stateObj);
  JsonObject infoObj = pDoc->createNestedObject("info");
  serializeInfo(infoObj);

  // Capture JsonVariants by value (pointer into pDoc, valid while lock held).
  JsonVariant stateVar = stateObj;
  JsonVariant infoVar  = infoObj;

  auto writer = writeJSONObject({
    { "state",       stateVar },
    { "info",        infoVar  },
    { F("effects"),  writeModeNames() },
    { F("palettes"), makeProgmemRawWriter(JSON_palette_names) }
  });

  bool lock_released = false; // this variable is declared purely to live in the lambda below
  request->sendChunked(FPSTR(CONTENT_TYPE_JSON),
    [writer, lock_released](uint8_t* data, size_t len, size_t) mutable -> size_t {
      WriteResult r = writer(data, len);
      if (r.done && !lock_released) {
        releaseJSONBufferLock();
        lock_released = true;
      }
      return r.count;
    });
}

// Global buffer locking response helper class (to make sure lock is released when AsyncJsonResponse is destroyed)
class LockedJsonResponse: public AsyncJsonResponse {
  bool _holding_lock;
  public:
  // WARNING: constructor assumes requestJSONBufferLock() was successfully acquired externally/prior to constructing the instance
  // Not a good practice with C++. Unfortunately AsyncJsonResponse only has 2 constructors - for dynamic buffer or existing buffer,
  // with existing buffer it clears its content during construction
  // if the lock was not acquired (using JSONBufferGuard class) previous implementation still cleared existing buffer
  inline LockedJsonResponse(JsonDocument* doc, bool isArray) : AsyncJsonResponse(doc, isArray), _holding_lock(true) {};

  virtual size_t _fillBuffer(uint8_t *buf, size_t maxLen) { 
    size_t result = AsyncJsonResponse::_fillBuffer(buf, maxLen);
    // Release lock as soon as we're done filling content
    if (((result + _sentLength) >= (_contentLength)) && _holding_lock) {
      releaseJSONBufferLock();
      _holding_lock = false;
    }
    return result;
  }

  // destructor will remove JSON buffer lock when response is destroyed in AsyncWebServer
  virtual ~LockedJsonResponse() { if (_holding_lock) releaseJSONBufferLock(); };
};

void serveJson(AsyncWebServerRequest* request)
{
  enum class json_target {
    state, info, state_info, networks, config
  };
  // Default: bare /json → streaming all-in-one response, no buffer lock needed.
  bool isAll = true;
  json_target subJson = json_target::state; // placeholder; only used when isAll==false

  const String& url = request->url();
  if      (url.indexOf("state")    > 0) { isAll = false; subJson = json_target::state; }
  else if (url.indexOf("info")     > 0) { isAll = false; subJson = json_target::info; }
  else if (url.indexOf("si")       > 0) { isAll = false; subJson = json_target::state_info; }
  else if (url.indexOf(F("nodes")) > 0) { respondJSONChunked(request, writeNodes()); return; }
  else if (url.indexOf(F("eff"))   > 0) { respondJSONChunked(request, writeModeNames()); return; }
  else if (url.indexOf(F("palx"))  > 0) { respondPalettes(request, request->hasParam(F("page")) ? request->getParam(F("page"))->value().toInt() : 0); return; }
  else if (url.indexOf(F("fxda"))  > 0) { respondJSONChunked(request, writeModeData()); return; }
  else if (url.indexOf(F("net"))   > 0) { isAll = false; subJson = json_target::networks; }
  else if (url.indexOf(F("cfg"))   > 0) { isAll = false; subJson = json_target::config; }
  else if (url.indexOf(F("pins"))  > 0) { respondJSONChunked(request, writePins()); return; }
  #ifdef WLED_ENABLE_JSONLIVE
  else if (url.indexOf("live")     > 0) {
    serveLiveLeds(request);
    return;
  }
  #endif
  else if (url.indexOf("pal") > 0) {
    request->send_P(200, FPSTR(CONTENT_TYPE_JSON), JSON_palette_names);
    return;
  }
  else if (url.length() > 6) { //not just /json
    serveJsonError(request, 501, ERR_NOT_IMPL);
    return;
  }

  if (isAll) { respondJsonAll(request); return; }

  if (!requestJSONBufferLock(JSON_LOCK_SERVEJSON)) {
    request->deferResponse();
    return;
  }
  // releaseJSONBufferLock() will be called when "response" is destroyed (from AsyncWebServer)
  // make sure you delete "response" if no "request->send(response);" is made
  LockedJsonResponse *response = new LockedJsonResponse(pDoc, false); // will clear and convert JsonDocument into JsonObject if necessary

  JsonVariant lDoc = response->getRoot();

  switch (subJson)
  {
    case json_target::state:
      serializeState(lDoc); break;
    case json_target::info:
      serializeInfo(lDoc); break;
    case json_target::networks:
      serializeNetworks(lDoc); break;
    case json_target::config:
      serializeConfig(lDoc); break;
    case json_target::state_info:
      {
        JsonObject state = lDoc.createNestedObject("state");
        serializeState(state);
        JsonObject info = lDoc.createNestedObject("info");
        serializeInfo(info);
      }
      break;
  }

  DEBUG_PRINTF_P(PSTR("JSON buffer size: %u for request: %d\n"), lDoc.memoryUsage(), subJson);

  [[maybe_unused]] size_t len = response->setLength();
  DEBUG_PRINTF_P(PSTR("JSON content length: %u\n"), len);

  request->send(response);
}

#ifdef WLED_ENABLE_JSONLIVE
#define MAX_LIVE_LEDS 256

bool serveLiveLeds(AsyncWebServerRequest* request, uint32_t wsClient)
{
  #ifdef WLED_ENABLE_WEBSOCKETS
  AsyncWebSocketClient * wsc = nullptr;
  if (!request) { //not HTTP, use Websockets
    wsc = ws.client(wsClient);
    if (!wsc || wsc->queueLength() > 0) return false; //only send if queue free
  }
  #endif

  unsigned used = strip.getLengthTotal();
  unsigned n = (used -1) /MAX_LIVE_LEDS +1; //only serve every n'th LED if count over MAX_LIVE_LEDS
#ifndef WLED_DISABLE_2D
  if (strip.isMatrix) {
    // ignore anything behid matrix (i.e. extra strip)
    used = Segment::maxWidth*Segment::maxHeight; // always the size of matrix (more or less than strip.getLengthTotal())
    n = 1;
    if (used > MAX_LIVE_LEDS) n = 2;
    if (used > MAX_LIVE_LEDS*4) n = 4;
  }
#endif

  DynamicBuffer buffer(9 + (9*(1+(used/n))) + 7 + 5 + 6 + 5 + 6 + 5 + 2);  
  char* buf = buffer.data();      // assign buffer for oappnd() functions
  strncpy_P(buffer.data(), PSTR("{\"leds\":["), buffer.size());
  buf += 9; // sizeof(PSTR()) from last line

  for (size_t i = 0; i < used; i += n)
  {
#ifndef WLED_DISABLE_2D
    if (strip.isMatrix && n>1 && (i/Segment::maxWidth)%n) i += Segment::maxWidth * (n-1);
#endif
    uint32_t c = strip.getPixelColor(i);
    uint8_t r = R(c);
    uint8_t g = G(c);
    uint8_t b = B(c);
    uint8_t w = W(c);
    r = scale8(qadd8(w, r), strip.getBrightness()); //R, add white channel to RGB channels as a simple RGBW -> RGB map
    g = scale8(qadd8(w, g), strip.getBrightness()); //G
    b = scale8(qadd8(w, b), strip.getBrightness()); //B
    buf += sprintf_P(buf, PSTR("\"%06X\","), RGBW32(r,g,b,0));
  }
  buf--;  // remove last comma
  buf += sprintf_P(buf, PSTR("],\"n\":%d"), n);
#ifndef WLED_DISABLE_2D
  if (strip.isMatrix) {
    buf += sprintf_P(buf, PSTR(",\"w\":%d"), Segment::maxWidth/n);
    buf += sprintf_P(buf, PSTR(",\"h\":%d"), Segment::maxHeight/n);
  }
#endif
  (*buf++) = '}';
  (*buf++) = 0;
  
  if (request) {
    request->send(200, FPSTR(CONTENT_TYPE_JSON), toString(std::move(buffer)));
  }
  #ifdef WLED_ENABLE_WEBSOCKETS
  else {
    wsc->text(toString(std::move(buffer)));
  }
  #endif  
  return true;
}
#endif
