#pragma once
#ifdef GEEKMAGIC_S3

#include <Arduino.h>

enum GmCond : uint8_t {
  GM_COND_UNKNOWN = 0,
  GM_COND_CLEAR,
  GM_COND_FEW,       // partly cloudy
  GM_COND_CLOUDS,
  GM_COND_FOG,
  GM_COND_DRIZZLE,
  GM_COND_RAIN,
  GM_COND_SNOW,
  GM_COND_THUNDER,
};

struct GmWeather {
  bool valid = false;
  char city[40] = "";
  char cc[4] = "";          // ISO country code ("BR")
  float temp = 0;           // always stored in metric (°C, km/h, hPa)
  float feels = 0;
  float tmin = 0;
  float tmax = 0;
  float windKmh = 0;
  int windDeg = 0;
  float pressHpa = 0;
  int humidity = 0;
  uint8_t cond = GM_COND_UNKNOWN;
  uint8_t icon = 0;         // OpenWeatherMap icon code (1,2,3,4,9,10,11,13,50), 0 = unknown
  uint16_t owmId = 0;       // OpenWeatherMap condition id (0 with Open-Meteo)
  char desc[32] = "";       // condition description, e.g. "Broken Clouds" (in the configured language)
  int16_t sunriseMin = -1;  // local time, minutes after midnight (-1 = unknown)
  int16_t sunsetMin = -1;
  bool isDay = true;
  bool hasOffset = false;
  int32_t utcOffset = 0;    // seconds
  uint32_t updatedMs = 0;   // millis() of last successful update
  char source[16] = "";     // "Open-Meteo" / "OpenWeatherMap"
  char status[80] = "";     // last status / error
};

// Start background weather task (call after WiFi is connected)
void gm_weatherStart();
// Thread-safe snapshot
GmWeather gm_weatherGet();
// Ask the task to refresh as soon as possible (e.g. after a config change)
void gm_weatherRequestUpdate();

#endif
