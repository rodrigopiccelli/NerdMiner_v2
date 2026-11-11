#ifdef GEEKMAGIC_S3

#include "gmWeather.h"
#include "gmConfig.h"
#include "gmTime.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static GmWeather s_weather;
static SemaphoreHandle_t s_mutex = nullptr;
static volatile bool s_forceUpdate = false;
static TaskHandle_t s_task = nullptr;

// Geocoding cache (Open-Meteo)
static char s_geoKey[64] = "";
static float s_lat = 0, s_lon = 0;
static char s_geoName[40] = "";
static char s_geoCC[4] = "";

// ------------------------------------------------------------------ helpers

static String urlEncode(const char *s)
{
  static const char *hex = "0123456789ABCDEF";
  String out;
  for (const uint8_t *p = (const uint8_t *)s; *p; p++) {
    uint8_t c = *p;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}

static void setStatus(const char *fmt, ...)
{
  char buf[80];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  strlcpy(s_weather.status, buf, sizeof(s_weather.status));
  xSemaphoreGive(s_mutex);
  Serial.printf("[GM] Weather: %s\n", buf);
}

static bool httpGet(const String &url, String &body, int &code)
{
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.setConnectTimeout(8000);
  if (!http.begin(client, url)) { code = -1; return false; }
  code = http.GET();
  if (code > 0) body = http.getString();
  http.end();
  return code == 200;
}

static bool isNumeric(const char *s)
{
  if (!*s) return false;
  for (; *s; s++) if (!isdigit((unsigned char)*s)) return false;
  return true;
}

static void splitCity(const char *city, String &name, String &cc)
{
  String c(city);
  c.trim();
  int comma = c.lastIndexOf(',');
  if (comma > 0) {
    name = c.substring(0, comma);
    cc = c.substring(comma + 1);
    name.trim();
    cc.trim();
    cc.toUpperCase();
  } else {
    name = c;
    cc = "";
  }
}

static uint8_t condFromWmo(int code)
{
  if (code == 0) return GM_COND_CLEAR;
  if (code == 1 || code == 2) return GM_COND_FEW;
  if (code == 3) return GM_COND_CLOUDS;
  if (code == 45 || code == 48) return GM_COND_FOG;
  if (code >= 51 && code <= 57) return GM_COND_DRIZZLE;
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return GM_COND_RAIN;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return GM_COND_SNOW;
  if (code >= 95) return GM_COND_THUNDER;
  return GM_COND_UNKNOWN;
}

// WMO weather code (Open-Meteo) -> description, worded like OpenWeatherMap's
struct WmoDesc { uint8_t code; const char *en; const char *pt; };
static const WmoDesc WMO_DESC[] = {
    {0, "Clear Sky", "Céu Limpo"},           {1, "Mainly Clear", "Poucas Nuvens"},
    {2, "Partly Cloudy", "Parc. Nublado"},   {3, "Overcast Clouds", "Nublado"},
    {45, "Fog", "Neblina"},                  {48, "Rime Fog", "Neblina Gelada"},
    {51, "Light Drizzle", "Garoa Fraca"},    {53, "Drizzle", "Garoa"},
    {55, "Heavy Drizzle", "Garoa Forte"},    {56, "Freezing Drizzle", "Garoa Gelada"},
    {57, "Freezing Drizzle", "Garoa Gelada"}, {61, "Light Rain", "Chuva Fraca"},
    {63, "Moderate Rain", "Chuva Moderada"}, {65, "Heavy Rain", "Chuva Forte"},
    {66, "Freezing Rain", "Chuva Gelada"},   {67, "Freezing Rain", "Chuva Gelada"},
    {71, "Light Snow", "Neve Fraca"},        {73, "Snow", "Neve"},
    {75, "Heavy Snow", "Neve Forte"},        {77, "Snow Grains", "Grãos de Neve"},
    {80, "Light Showers", "Pancadas Fracas"}, {81, "Rain Showers", "Pancadas"},
    {82, "Heavy Showers", "Pancadas Fortes"}, {85, "Light Snow", "Neve Fraca"},
    {86, "Heavy Snow", "Neve Forte"}, {95, "Thunderstorm", "Trovoada"},
    {96, "Hailstorm", "Granizo"}, {99, "Hailstorm", "Granizo"},
};

// OpenWeatherMap condition id -> short description (fits the 176 px info line in the SmallTV font)
struct OwmDesc { uint16_t id; const char *en; const char *pt; };
static const OwmDesc OWM_DESC[] = {
    {200, "Storm, Light Rain", "Trovoada, Chuva"},
    {201, "Storm, Rain", "Trovoada, Chuva"},
    {202, "Storm, Downpour", "Trovoada Forte"},
    {210, "Light Storm", "Trovoada Fraca"},
    {211, "Thunderstorm", "Trovoada"},
    {212, "Heavy Storm", "Trovoada Forte"},
    {221, "Isolated Storms", "Trovoadas"},
    {230, "Storm, Drizzle", "Trovoada, Garoa"},
    {231, "Storm, Drizzle", "Trovoada, Garoa"},
    {232, "Storm, Drizzle", "Trovoada, Garoa"},
    {300, "Light Drizzle", "Garoa Fraca"},
    {301, "Drizzle", "Garoa"},
    {302, "Heavy Drizzle", "Garoa Forte"},
    {310, "Light Drizzle", "Garoa Fraca"},
    {311, "Drizzle, Rain", "Garoa e Chuva"},
    {312, "Heavy Drizzle", "Garoa Forte"},
    {313, "Showers, Drizzle", "Pancadas, Garoa"},
    {314, "Heavy Showers", "Pancadas Fortes"},
    {321, "Drizzle Showers", "Pancadas, Garoa"},
    {500, "Light Rain", "Chuva Fraca"},
    {501, "Moderate Rain", "Chuva Moderada"},
    {502, "Heavy Rain", "Chuva Forte"},
    {503, "Very Heavy Rain", "Chuva Intensa"},
    {504, "Extreme Rain", "Chuva Extrema"},
    {511, "Freezing Rain", "Chuva Gelada"},
    {520, "Light Showers", "Pancadas Fracas"},
    {521, "Showers", "Pancadas"},
    {522, "Heavy Showers", "Pancadas Fortes"},
    {531, "Isolated Showers", "Pancadas"},
    {600, "Light Snow", "Neve Fraca"},
    {601, "Snow", "Neve"},
    {602, "Heavy Snow", "Neve Forte"},
    {611, "Sleet", "Granizo Fino"},
    {612, "Light Sleet", "Granizo Fino"},
    {613, "Sleet Showers", "Granizo Fino"},
    {615, "Light Rain, Snow", "Chuva e Neve"},
    {616, "Rain And Snow", "Chuva e Neve"},
    {620, "Light Snow", "Neve Fraca"},
    {621, "Snow Showers", "Nevadas"},
    {622, "Heavy Snow", "Neve Forte"},
    {701, "Mist", "Névoa"},
    {711, "Smoke", "Fumaça"},
    {721, "Haze", "Névoa Seca"},
    {731, "Dust Whirls", "Redemoinhos"},
    {741, "Fog", "Neblina"},
    {751, "Sand", "Areia"},
    {761, "Dust", "Poeira"},
    {762, "Volcanic Ash", "Cinza Vulcânica"},
    {771, "Squalls", "Rajadas"},
    {781, "Tornado", "Tornado"},
    {800, "Clear Sky", "Céu Limpo"},
    {801, "Few Clouds", "Poucas Nuvens"},
    {802, "Scattered Clouds", "Nuvens Dispersas"},
    {803, "Broken Clouds", "Muitas Nuvens"},
    {804, "Overcast Clouds", "Nublado"},
};

static const char *descFromOwm(int id, uint8_t lang)
{
  for (const OwmDesc &d : OWM_DESC)
    if (d.id == id) return lang == GM_LANG_PT ? d.pt : d.en;
  return nullptr;
}

static const char *descFromWmo(int code, uint8_t lang)
{
  for (const WmoDesc &d : WMO_DESC)
    if (d.code == code) return lang == GM_LANG_PT ? d.pt : d.en;
  return "";
}

// "broken clouds" -> "Broken Clouds" (ASCII words; UTF-8 bytes are kept as-is)
static void titleCase(char *s)
{
  bool start = true;
  for (; *s; s++) {
    if (*s == ' ') { start = true; continue; }
    if (start && *s >= 'a' && *s <= 'z') *s -= 32;
    start = false;
  }
}

// "2026-11-11T05:12" -> 312
static int16_t isoToMinutes(const char *iso)
{
  const char *t = iso ? strchr(iso, 'T') : nullptr;
  if (!t || strlen(t) < 6) return -1;
  return atoi(t + 1) * 60 + atoi(t + 4);
}

// WMO weather code (Open-Meteo) -> OpenWeatherMap icon code, so both sources share the same icon set
static uint8_t iconFromWmo(int code)
{
  if (code == 0) return 1;
  if (code == 1) return 2;
  if (code == 2) return 3;
  if (code == 3) return 4;
  if (code == 45 || code == 48) return 50;
  if (code >= 51 && code <= 57) return 9;
  if (code == 66 || code == 67) return 9;
  if (code >= 61 && code <= 65) return 10;
  if (code >= 80 && code <= 82) return 9;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return 13;
  if (code >= 95) return 11;
  return 0;
}

static uint8_t condFromOwm(int id)
{
  if (id >= 200 && id < 300) return GM_COND_THUNDER;
  if (id >= 300 && id < 400) return GM_COND_DRIZZLE;
  if (id >= 500 && id < 600) return GM_COND_RAIN;
  if (id >= 600 && id < 700) return GM_COND_SNOW;
  if (id >= 700 && id < 800) return GM_COND_FOG;
  if (id == 800) return GM_COND_CLEAR;
  if (id == 801 || id == 802) return GM_COND_FEW;
  if (id > 802) return GM_COND_CLOUDS;
  return GM_COND_UNKNOWN;
}

// ------------------------------------------------------------------ Open-Meteo

static bool geocode(const GmConfig &cfg)
{
  if (strcmp(s_geoKey, cfg.city) == 0 && s_geoName[0]) return true;

  // Numeric OpenWeatherMap city ids are GeoNames ids, which Open-Meteo can resolve directly
  if (isNumeric(cfg.city)) {
    String url = String("https://geocoding-api.open-meteo.com/v1/get?id=") + cfg.city + "&language=" + (cfg.lang == GM_LANG_PT ? "pt" : "en");
    String body;
    int code;
    if (!httpGet(url, body, code)) {
      setStatus("City ID %s not found (HTTP %d)", cfg.city, code);
      return false;
    }
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, body) || !doc.containsKey("latitude")) {
      setStatus("City ID %s not found", cfg.city);
      return false;
    }
    s_lat = doc["latitude"] | 0.0f;
    s_lon = doc["longitude"] | 0.0f;
    strlcpy(s_geoName, doc["name"] | cfg.city, sizeof(s_geoName));
    strlcpy(s_geoCC, doc["country_code"] | "", sizeof(s_geoCC));
    strlcpy(s_geoKey, cfg.city, sizeof(s_geoKey));
    Serial.printf("[GM] City id %s -> %s,%s (%.4f, %.4f)\n", cfg.city, s_geoName, s_geoCC, s_lat, s_lon);
    return true;
  }

  String name, cc;
  splitCity(cfg.city, name, cc);
  String url = "https://geocoding-api.open-meteo.com/v1/search?count=10&format=json&language=";
  url += cfg.lang == GM_LANG_PT ? "pt" : "en";
  url += "&name=" + urlEncode(name.c_str());

  String body;
  int code;
  if (!httpGet(url, body, code)) {
    setStatus("City lookup failed (HTTP %d)", code);
    return false;
  }

  DynamicJsonDocument doc(12288);
  if (deserializeJson(doc, body)) {
    setStatus("Invalid city lookup response");
    return false;
  }
  JsonArray results = doc["results"].as<JsonArray>();
  JsonObject pick;
  for (JsonObject r : results) {
    if (cc.length() == 0 || cc.equalsIgnoreCase(r["country_code"] | "")) { pick = r; break; }
  }
  if (pick.isNull()) {
    setStatus("City not found: %s", cfg.city);
    return false;
  }
  s_lat = pick["latitude"] | 0.0f;
  s_lon = pick["longitude"] | 0.0f;
  strlcpy(s_geoName, pick["name"] | name.c_str(), sizeof(s_geoName));
  strlcpy(s_geoCC, pick["country_code"] | "", sizeof(s_geoCC));
  strlcpy(s_geoKey, cfg.city, sizeof(s_geoKey));
  Serial.printf("[GM] Geocoded '%s' -> %s,%s (%.4f, %.4f)\n", cfg.city, s_geoName, s_geoCC, s_lat, s_lon);
  return true;
}

static bool fetchOpenMeteo(const GmConfig &cfg, GmWeather &w)
{
  if (!geocode(cfg)) return false;

  char url[512];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,"
           "pressure_msl,wind_speed_10m,wind_direction_10m"
           "&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset&timezone=auto&forecast_days=1",
           s_lat, s_lon);

  String body;
  int code;
  if (!httpGet(url, body, code)) {
    setStatus("Open-Meteo error (HTTP %d)", code);
    return false;
  }
  DynamicJsonDocument doc(6144);
  if (deserializeJson(doc, body)) {
    setStatus("Invalid Open-Meteo response");
    return false;
  }
  JsonObject cur = doc["current"];
  if (cur.isNull()) {
    setStatus("Open-Meteo returned no current data");
    return false;
  }
  strlcpy(w.city, s_geoName, sizeof(w.city));
  strlcpy(w.cc, s_geoCC, sizeof(w.cc));
  w.temp = cur["temperature_2m"] | 0.0f;
  w.feels = cur["apparent_temperature"] | w.temp;
  w.humidity = cur["relative_humidity_2m"] | 0;
  w.isDay = (cur["is_day"] | 1) == 1;
  int wmo = cur["weather_code"] | -1;
  w.cond = condFromWmo(wmo);
  w.icon = iconFromWmo(wmo);
  strlcpy(w.desc, descFromWmo(wmo, cfg.lang), sizeof(w.desc));
  w.sunriseMin = isoToMinutes(doc["daily"]["sunrise"][0] | "");
  w.sunsetMin = isoToMinutes(doc["daily"]["sunset"][0] | "");
  w.pressHpa = cur["pressure_msl"] | 0.0f;
  w.windKmh = cur["wind_speed_10m"] | 0.0f;
  w.windDeg = cur["wind_direction_10m"] | 0;
  w.tmax = doc["daily"]["temperature_2m_max"][0] | w.temp;
  w.tmin = doc["daily"]["temperature_2m_min"][0] | w.temp;
  if (doc.containsKey("utc_offset_seconds")) {
    w.hasOffset = true;
    w.utcOffset = doc["utc_offset_seconds"];
  }
  strlcpy(w.source, "Open-Meteo", sizeof(w.source));
  return true;
}

// ------------------------------------------------------------------ OpenWeatherMap

static int s_owmHttpCode = 0;   // last HTTP status from OpenWeatherMap (for the fallback decision)

static bool fetchOwm(const GmConfig &cfg, GmWeather &w)
{
  String url = "https://api.openweathermap.org/data/2.5/weather?units=metric&appid=";
  url += urlEncode(cfg.owmKey);
  url += cfg.lang == GM_LANG_PT ? "&lang=pt_br" : "&lang=en";
  if (isNumeric(cfg.city)) url += String("&id=") + cfg.city;
  else url += "&q=" + urlEncode(cfg.city);

  String body;
  int code;
  bool got = httpGet(url, body, code);
  s_owmHttpCode = code;
  if (!got) {
    if (code == 401) setStatus("Invalid OpenWeatherMap key (401)");
    else if (code == 404) setStatus("City not found on OpenWeatherMap");
    else setStatus("OpenWeatherMap error (HTTP %d)", code);
    return false;
  }
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, body)) {
    setStatus("Invalid OpenWeatherMap response");
    return false;
  }
  strlcpy(w.city, doc["name"] | "", sizeof(w.city));
  strlcpy(w.cc, doc["sys"]["country"] | "", sizeof(w.cc));
  JsonObject m = doc["main"];
  w.temp = m["temp"] | 0.0f;
  w.feels = m["feels_like"] | w.temp;
  w.tmin = m["temp_min"] | w.temp;
  w.tmax = m["temp_max"] | w.temp;
  w.humidity = m["humidity"] | 0;
  w.pressHpa = m["pressure"] | 0.0f;
  w.windKmh = (doc["wind"]["speed"] | 0.0f) * 3.6f;
  w.windDeg = doc["wind"]["deg"] | 0;
  w.owmId = doc["weather"][0]["id"] | 0;
  w.cond = condFromOwm(w.owmId);
  const char *icon = doc["weather"][0]["icon"] | "01d";
  w.isDay = icon[strlen(icon) - 1] != 'n';
  w.icon = atoi(icon);
  const char *shortDesc = descFromOwm(doc["weather"][0]["id"] | 0, cfg.lang);
  if (shortDesc) {
    strlcpy(w.desc, shortDesc, sizeof(w.desc));
  } else {   // unknown id: use the API text
    strlcpy(w.desc, doc["weather"][0]["description"] | "", sizeof(w.desc));
    titleCase(w.desc);
  }
  if (doc.containsKey("timezone")) {
    w.hasOffset = true;
    w.utcOffset = doc["timezone"];
  }
  long rise = doc["sys"]["sunrise"] | 0L, set = doc["sys"]["sunset"] | 0L;
  if (rise > 0) w.sunriseMin = ((rise + w.utcOffset) % 86400) / 60;
  if (set > 0) w.sunsetMin = ((set + w.utcOffset) % 86400) / 60;
  strlcpy(w.source, "OpenWeatherMap", sizeof(w.source));
  return true;
}

// ------------------------------------------------------------------ task

static bool fetchWeather()
{
  GmConfig cfg = gm_cfgGet();
  if (!cfg.city[0]) {
    setStatus("Set your city in the Weather tab");
    return false;
  }

  GmWeather w;
  bool ok;
  const char *note = nullptr;
  if (cfg.owmKey[0]) {
    ok = fetchOwm(cfg, w);
    // Fallback: any OpenWeatherMap failure (key rejected, rate limit, network/TLS error, 5xx, unknown city)
    // -> use the keyless Open-Meteo for this update (numeric ids are resolved as GeoNames ids).
    // OWM is tried again on every update, so it takes over automatically once it works.
    if (!ok) {
      static char noteBuf[32];
      int code = s_owmHttpCode;
      Serial.printf("[GM] OpenWeatherMap failed (HTTP %d), falling back to Open-Meteo\n", code);
      if (code == 401 || code == 403) snprintf(noteBuf, sizeof(noteBuf), "OWM key invalid");
      else if (code == 429) snprintf(noteBuf, sizeof(noteBuf), "OWM rate limited");
      else snprintf(noteBuf, sizeof(noteBuf), "OWM error %d", code);
      w = GmWeather();
      ok = fetchOpenMeteo(cfg, w);
      note = noteBuf;
    }
  } else {
    ok = fetchOpenMeteo(cfg, w);
  }
  if (!ok) return false;

  w.valid = true;
  w.updatedMs = millis();
  if (note) snprintf(w.status, sizeof(w.status), "OK (%s, %s)", w.source, note);
  else snprintf(w.status, sizeof(w.status), "OK (%s)", w.source);

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_weather = w;
  xSemaphoreGive(s_mutex);

  Serial.printf("[GM] Weather %s,%s: %.1fC hum %d%% cond %d (%s)\n", w.city, w.cc, w.temp, w.humidity, w.cond, w.source);
  gm_applyTimeSettings(); // time zone may come from the location
  return true;
}

static void weatherTask(void *)
{
  uint32_t nextAttempt = 0;
  uint32_t retryDelay = 60000;
  char lastCity[64] = "";
  char lastKey[48] = "";

  for (;;) {
    GmConfig cfg = gm_cfgGet();
    // A different city/source invalidates the cached data
    if (strcmp(lastCity, cfg.city) != 0 || strcmp(lastKey, cfg.owmKey) != 0) {
      strlcpy(lastCity, cfg.city, sizeof(lastCity));
      strlcpy(lastKey, cfg.owmKey, sizeof(lastKey));
      s_geoKey[0] = 0;
      s_geoName[0] = 0;
      xSemaphoreTake(s_mutex, portMAX_DELAY);
      s_weather.valid = false;
      s_weather.hasOffset = false;
      xSemaphoreGive(s_mutex);
      s_forceUpdate = true;
    }

    bool due = s_forceUpdate || (int32_t)(millis() - nextAttempt) >= 0;
    if (due && WiFi.status() == WL_CONNECTED) {
      s_forceUpdate = false;
      if (fetchWeather()) {
        uint32_t minutes = cfg.weatherMin < 5 ? 5 : cfg.weatherMin;
        nextAttempt = millis() + minutes * 60000UL;
        retryDelay = 60000;
      } else {
        nextAttempt = millis() + retryDelay;
        retryDelay = min<uint32_t>(retryDelay * 2, 15 * 60000UL);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void gm_weatherStart()
{
  if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
  if (s_task) return;
  setStatus("Waiting for update...");
  // Same priority as the stratum task: the HW miner (prio 3) only yields when idle
  xTaskCreate(weatherTask, "GMWeather", 12288, nullptr, 4, &s_task);
}

GmWeather gm_weatherGet()
{
  if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  GmWeather copy = s_weather;
  xSemaphoreGive(s_mutex);
  return copy;
}

void gm_weatherRequestUpdate()
{
  s_forceUpdate = true;
}

#endif
