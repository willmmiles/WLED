#include "wled.h"
/*
 * Register your v2 usermods here!
 *   (for v1 usermods using just usermod.cpp, you can ignore this file)
 */

/*
 * Add/uncomment your usermod filename here (and once more below)
 * || || ||
 * \/ \/ \/
 */
//#include "../usermods/EXAMPLE_v2/usermod_v2_example.h"

#ifdef USERMOD_BATTERY
  #include "../usermods/Battery/usermod_v2_Battery.h"
#endif

#ifdef USERMOD_DALLASTEMPERATURE
  #include "../usermods/Temperature/usermod_temperature.h"
#endif

#ifdef USERMOD_SHT
#include "../usermods/sht/usermod_sht.h"
#endif

#ifdef USERMOD_SN_PHOTORESISTOR
  #include "../usermods/SN_Photoresistor/usermod_sn_photoresistor.h"
#endif

#ifdef USERMOD_PWM_FAN
  // requires DALLASTEMPERATURE or SHT included before it
  #include "../usermods/PWM_fan/usermod_PWM_fan.h"
#endif

#ifdef USERMOD_BUZZER
  #include "../usermods/buzzer/usermod_v2_buzzer.h"
#endif

#ifdef USERMOD_SENSORSTOMQTT
  #include "../usermods/sensors_to_mqtt/usermod_v2_SensorsToMqtt.h"
#endif

#ifdef USERMOD_PIRSWITCH
  #include "../usermods/PIR_sensor_switch/usermod_PIR_sensor_switch.h"
#endif

#ifdef USERMOD_MODE_SORT
  #include "../usermods/usermod_v2_mode_sort/usermod_v2_mode_sort.h"
#endif

#ifdef USERMOD_BH1750
  #include "../usermods/BH1750_v2/usermod_BH1750.h"
#endif

// BME280 v2 usermod. Define "USERMOD_BME280" in my_config.h
#ifdef USERMOD_BME280
  #include "../usermods/BME280_v2/usermod_bme280.h"
#endif

#ifdef USERMOD_FOUR_LINE_DISPLAY
  #ifdef USE_ALT_DISPlAY
    #include "../usermods/usermod_v2_four_line_display_ALT/usermod_v2_four_line_display_ALT.h"
  #else
    #include "../usermods/usermod_v2_four_line_display/usermod_v2_four_line_display.h"
  #endif
#endif

#ifdef USERMOD_ROTARY_ENCODER_UI
  #ifdef USE_ALT_DISPlAY
    #include "../usermods/usermod_v2_rotary_encoder_ui_ALT/usermod_v2_rotary_encoder_ui_ALT.h"
  #else
    #include "../usermods/usermod_v2_rotary_encoder_ui/usermod_v2_rotary_encoder_ui.h"
  #endif
#endif

#ifdef USERMOD_AUTO_SAVE
  #include "../usermods/usermod_v2_auto_save/usermod_v2_auto_save.h"
#endif

#ifdef USERMOD_DHT
  #include "../usermods/DHT/usermod_dht.h"
#endif

#ifdef USERMOD_VL53L0X_GESTURES
  #include "../usermods/VL53L0X_gestures/usermod_vl53l0x_gestures.h"
#endif

#ifdef USERMOD_ANIMATED_STAIRCASE
  #include "../usermods/Animated_Staircase/Animated_Staircase.h"
#endif

#ifdef USERMOD_MULTI_RELAY
  #include "../usermods/multi_relay/usermod_multi_relay.h"
#endif

#ifdef USERMOD_RTC
  #include "../usermods/RTC/usermod_rtc.h"
#endif

#ifdef USERMOD_ELEKSTUBE_IPS
  #include "../usermods/EleksTube_IPS/usermod_elekstube_ips.h"
#endif

#ifdef USERMOD_ROTARY_ENCODER_BRIGHTNESS_COLOR
  #include "../usermods/usermod_rotary_brightness_color/usermod_rotary_brightness_color.h"
#endif

#ifdef RGB_ROTARY_ENCODER
  #include "../usermods/rgb-rotary-encoder/rgb-rotary-encoder.h"
#endif

#ifdef USERMOD_ST7789_DISPLAY
  #include "../usermods/ST7789_display/ST7789_Display.h"
#endif

#ifdef USERMOD_SEVEN_SEGMENT
  #include "../usermods/seven_segment_display/usermod_v2_seven_segment_display.h"
#endif

#ifdef USERMOD_SSDR
  #include "../usermods/seven_segment_display_reloaded/usermod_seven_segment_reloaded.h"
#endif

#ifdef USERMOD_CRONIXIE
  #include "../usermods/Cronixie/usermod_cronixie.h"
#endif

#ifdef QUINLED_AN_PENTA
  #include "../usermods/quinled-an-penta/quinled-an-penta.h"
#endif

#ifdef USERMOD_WIZLIGHTS
  #include "../usermods/wizlights/wizlights.h"
#endif

#ifdef USERMOD_WIREGUARD
  #include "../usermods/wireguard/wireguard.h"
#endif

#ifdef USERMOD_WORDCLOCK
  #include "../usermods/usermod_v2_word_clock/usermod_v2_word_clock.h"
#endif

#ifdef USERMOD_MY9291
  #include "../usermods/MY9291/usermode_MY9291.h"
#endif

#ifdef USERMOD_SI7021_MQTT_HA
  #include "../usermods/Si7021_MQTT_HA/usermod_si7021_mqtt_ha.h"
#endif

#ifdef USERMOD_SMARTNEST
  #include "../usermods/smartnest/usermod_smartnest.h"
#endif

#ifdef USERMOD_AUDIOREACTIVE
  #include "../usermods/audioreactive/audio_reactive.h"
#endif

#ifdef USERMOD_ANALOG_CLOCK
  #include "../usermods/Analog_Clock/Analog_Clock.h"
#endif

#ifdef USERMOD_PING_PONG_CLOCK
  #include "../usermods/usermod_v2_ping_pong_clock/usermod_v2_ping_pong_clock.h"
#endif

#ifdef USERMOD_ADS1115
  #include "../usermods/ADS1115_v2/usermod_ads1115.h"
#endif

#ifdef USERMOD_KLIPPER_PERCENTAGE
  #include "..\usermods\usermod_v2_klipper_percentage\usermod_v2_klipper_percentage.h"
#endif

#ifdef USERMOD_BOBLIGHT
  #include "../usermods/boblight/boblight.h"
#endif

#ifdef USERMOD_INTERNAL_TEMPERATURE
  #include "../usermods/Internal_Temperature_v2/usermod_internal_temperature.h"
#endif

#if defined(WLED_USE_SD_MMC) || defined(WLED_USE_SD_SPI)
// This include of SD.h and SD_MMC.h must happen here, else they won't be
// resolved correctly (when included in mod's header only)
  #ifdef WLED_USE_SD_MMC
    #include "SD_MMC.h"
  #elif defined(WLED_USE_SD_SPI)
    #include "SD.h"
    #include "SPI.h"
  #endif
  #include "../usermods/sd_card/usermod_sd_card.h"
#endif

#ifdef USERMOD_PWM_OUTPUTS
#include "../usermods/pwm_outputs/usermod_pwm_outputs.h"
#endif


void registerUsermods()
{
/*
   * Add your usermod class name here
   * || || ||
   * \/ \/ \/
   */
  //static MyExampleUsermod _MyExampleUsermod; usermods.add(&_MyExampleUsermod);
  #ifdef USERMOD_BATTERY
  static UsermodBattery _UsermodBattery; usermods.add(&_UsermodBattery);
  #endif

  #ifdef USERMOD_DALLASTEMPERATURE
  static UsermodTemperature _UsermodTemperature; usermods.add(&_UsermodTemperature);
  #endif

  #ifdef USERMOD_SN_PHOTORESISTOR
  static Usermod_SN_Photoresistor _Usermod_SN_Photoresistor; usermods.add(&_Usermod_SN_Photoresistor);
  #endif

  #ifdef USERMOD_PWM_FAN
  static PWMFanUsermod _PWMFanUsermod; usermods.add(&_PWMFanUsermod);
  #endif

  #ifdef USERMOD_BUZZER
  static BuzzerUsermod _BuzzerUsermod; usermods.add(&_BuzzerUsermod);
  #endif

  #ifdef USERMOD_BH1750
  static Usermod_BH1750 _Usermod_BH1750; usermods.add(&_Usermod_BH1750);
  #endif

  #ifdef USERMOD_BME280
  static UsermodBME280 _UsermodBME280; usermods.add(&_UsermodBME280);
  #endif

  #ifdef USERMOD_SENSORSTOMQTT
  static UserMod_SensorsToMQTT _UserMod_SensorsToMQTT; usermods.add(&_UserMod_SensorsToMQTT);
  #endif

  #ifdef USERMOD_PIRSWITCH
  static PIRsensorSwitch _PIRsensorSwitch; usermods.add(&_PIRsensorSwitch);
  #endif

  #ifdef USERMOD_MODE_SORT
  static ModeSortUsermod _ModeSortUsermod; usermods.add(&_ModeSortUsermod);
  #endif

  #ifdef USERMOD_FOUR_LINE_DISPLAY
  static FourLineDisplayUsermod _FourLineDisplayUsermod; usermods.add(&_FourLineDisplayUsermod);
  #endif

  #ifdef USERMOD_ROTARY_ENCODER_UI
  static RotaryEncoderUIUsermod _RotaryEncoderUIUsermod; usermods.add(&_RotaryEncoderUIUsermod); // can use USERMOD_FOUR_LINE_DISPLAY
  #endif

  #ifdef USERMOD_AUTO_SAVE
  static AutoSaveUsermod _AutoSaveUsermod; usermods.add(&_AutoSaveUsermod);  // can use USERMOD_FOUR_LINE_DISPLAY
  #endif

  #ifdef USERMOD_DHT
  static UsermodDHT _UsermodDHT; usermods.add(&_UsermodDHT);
  #endif

  #ifdef USERMOD_VL53L0X_GESTURES
  static UsermodVL53L0XGestures _UsermodVL53L0XGestures; usermods.add(&_UsermodVL53L0XGestures);
  #endif

  #ifdef USERMOD_ANIMATED_STAIRCASE
  static Animated_Staircase _Animated_Staircase; usermods.add(&_Animated_Staircase);
  #endif

  #ifdef USERMOD_MULTI_RELAY
  static MultiRelay _MultiRelay; usermods.add(&_MultiRelay);
  #endif

  #ifdef USERMOD_RTC
  static RTCUsermod _RTCUsermod; usermods.add(&_RTCUsermod);
  #endif

  #ifdef USERMOD_ELEKSTUBE_IPS
  static ElekstubeIPSUsermod _ElekstubeIPSUsermod; usermods.add(&_ElekstubeIPSUsermod);
  #endif

  #ifdef USERMOD_ROTARY_ENCODER_BRIGHTNESS_COLOR
  static RotaryEncoderBrightnessColor _RotaryEncoderBrightnessColor; usermods.add(&_RotaryEncoderBrightnessColor);
  #endif

  #ifdef RGB_ROTARY_ENCODER
  static RgbRotaryEncoderUsermod _RgbRotaryEncoderUsermod; usermods.add(&_RgbRotaryEncoderUsermod);
  #endif

  #ifdef USERMOD_ST7789_DISPLAY
  static St7789DisplayUsermod _St7789DisplayUsermod; usermods.add(&_St7789DisplayUsermod);
  #endif

  #ifdef USERMOD_SEVEN_SEGMENT
  static SevenSegmentDisplay _SevenSegmentDisplay; usermods.add(&_SevenSegmentDisplay);
  #endif

  #ifdef USERMOD_SSDR
  static UsermodSSDR _UsermodSSDR; usermods.add(&_UsermodSSDR);
  #endif

  #ifdef USERMOD_CRONIXIE
  static UsermodCronixie _UsermodCronixie; usermods.add(&_UsermodCronixie);
  #endif

  #ifdef QUINLED_AN_PENTA
  static QuinLEDAnPentaUsermod _QuinLEDAnPentaUsermod; usermods.add(&_QuinLEDAnPentaUsermod);
  #endif

  #ifdef USERMOD_WIZLIGHTS
  static WizLightsUsermod _WizLightsUsermod; usermods.add(&_WizLightsUsermod);
  #endif

  #ifdef USERMOD_WIREGUARD
  static WireguardUsermod _WireguardUsermod; usermods.add(&_WireguardUsermod);
  #endif

  #ifdef USERMOD_WORDCLOCK
  static WordClockUsermod _WordClockUsermod; usermods.add(&_WordClockUsermod);
  #endif

  #ifdef USERMOD_MY9291
  static MY9291Usermod _MY9291Usermod; usermods.add(&_MY9291Usermod);
  #endif

  #ifdef USERMOD_SI7021_MQTT_HA
  static Si7021_MQTT_HA _Si7021_MQTT_HA; usermods.add(&_Si7021_MQTT_HA);
  #endif

  #ifdef USERMOD_SMARTNEST
  static Smartnest _Smartnest; usermods.add(&_Smartnest);
  #endif

  #ifdef USERMOD_AUDIOREACTIVE
  static AudioReactive _AudioReactive; usermods.add(&_AudioReactive);
  #endif

  #ifdef USERMOD_ANALOG_CLOCK
  static AnalogClockUsermod _AnalogClockUsermod; usermods.add(&_AnalogClockUsermod);
  #endif

  #ifdef USERMOD_PING_PONG_CLOCK
  static PingPongClockUsermod _PingPongClockUsermod; usermods.add(&_PingPongClockUsermod);
  #endif

  #ifdef USERMOD_ADS1115
  static ADS1115Usermod _ADS1115Usermod; usermods.add(&_ADS1115Usermod);
  #endif

  #ifdef USERMOD_KLIPPER_PERCENTAGE
  static klipper_percentage _klipper_percentage; usermods.add(&_klipper_percentage);
  #endif

  #ifdef USERMOD_BOBLIGHT
  static BobLightUsermod _BobLightUsermod; usermods.add(&_BobLightUsermod);
  #endif

  #ifdef SD_ADAPTER
  static UsermodSdCard _UsermodSdCard; usermods.add(&_UsermodSdCard);
  #endif

  #ifdef USERMOD_PWM_OUTPUTS
  static PwmOutputsUsermod _PwmOutputsUsermod; usermods.add(&_PwmOutputsUsermod);
  #endif

  #ifdef USERMOD_SHT
  static ShtUsermod _ShtUsermod; usermods.add(&_ShtUsermod);
  #endif

  #ifdef USERMOD_INTERNAL_TEMPERATURE
  static InternalTemperatureUsermod _InternalTemperatureUsermod; usermods.add(&_InternalTemperatureUsermod);
  #endif
}
