#pragma once
#ifdef GEEKMAGIC_S3

#include <Arduino.h>

// Screens available on the GeekMagic S3 (order = index used by the display driver / web UI)
enum GmScreen : uint8_t {
  GM_SCREEN_WEATHER = 0,   // "Weather Clock" (GeekMagic style)
  GM_SCREEN_MINER,         // NerdMiner classic screens (scaled from 320x170)
  GM_SCREEN_CLOCK,
  GM_SCREEN_GLOBAL,
  GM_SCREEN_BTC,
  GM_SCREEN_COUNT
};

enum GmTempUnit : uint8_t { GM_TEMP_C = 0, GM_TEMP_F };
enum GmWindUnit : uint8_t { GM_WIND_KMH = 0, GM_WIND_MS, GM_WIND_MPH, GM_WIND_KN };
enum GmPressUnit : uint8_t { GM_PRESS_HPA = 0, GM_PRESS_MMHG, GM_PRESS_INHG };
enum GmLang : uint8_t { GM_LANG_PT = 0, GM_LANG_EN };
enum GmFont : uint8_t { GM_FONT_GEEK = 0, GM_FONT_BEBAS, GM_FONT_ROUND, GM_FONT_DIGITAL, GM_FONT_COUNT };

// Date formats shown on the weather screen
enum GmDateFmt : uint8_t {
  GM_DATE_D_M_Y = 0,   // 5/7/2025
  GM_DATE_DD_MM_Y,     // 05/07/2025
  GM_DATE_M_D_Y,       // 7/5/2025
  GM_DATE_MM_DD_Y,     // 07/05/2025
  GM_DATE_Y_M_D,       // 2025/7/5
  GM_DATE_ISO,         // 2025-07-05
  GM_DATE_COUNT
};

#define GM_CFG_VERSION 1

struct GmConfig {
  uint16_t version = GM_CFG_VERSION;

  // ---- Weather ----
  char city[64] = "";          // "City", "City,CC" or numeric OpenWeatherMap city id
  char owmKey[48] = "";        // empty = Open-Meteo (no key needed)
  uint8_t tempUnit = GM_TEMP_C;
  uint8_t windUnit = GM_WIND_KMH;
  uint8_t pressUnit = GM_PRESS_HPA;
  uint16_t weatherMin = 20;    // update interval (minutes)
  uint8_t lang = GM_LANG_EN;

  // ---- Time ----
  bool tzAuto = true;          // take UTC offset from the weather location
  int16_t tzOffsetMin = -180;  // manual UTC offset (minutes)
  bool hour12 = false;
  uint8_t dateFmt = GM_DATE_D_M_Y;
  bool colonBlink = false;
  uint8_t font = GM_FONT_GEEK;
  char ntp[48] = "";           // empty = NerdMiner default pool
  uint32_t hourColor = 0xFFFFFF;
  uint32_t minColor = 0xFEBA01;
  uint32_t secColor = 0xFF5900;

  // ---- Screens ----
  uint8_t screen = GM_SCREEN_WEATHER;   // screen shown when auto switch is off
  uint8_t screenMask = 0x1F;            // screens that take part in touch / auto switch cycling
  bool autoSwitch = false;
  uint16_t switchSec = 30;

  // ---- Display ----
  uint8_t brightness = 80;     // 0-100
  bool nightEn = false;
  uint8_t nightStart = 22;     // hour
  uint8_t nightEnd = 7;        // hour
  uint8_t nightBrightness = 15;

  char gif[48] = "*spaceman";   // weather screen GIF: "" = Bitcoin coin, "*name" = built-in, else /gif/<file>
};

// Load from NVS (call once, before using gm_cfg*). Safe to call multiple times.
void gm_cfgLoad();
// Thread-safe snapshot of the current configuration.
GmConfig gm_cfgGet();
// Replace the configuration and persist it to NVS.
void gm_cfgSet(const GmConfig &cfg);
// Erase the GeekMagic configuration (factory reset).
void gm_cfgErase();

uint16_t gm_rgb565(uint32_t rgb);

#endif
