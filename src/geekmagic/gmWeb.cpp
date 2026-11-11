#ifdef GEEKMAGIC_S3

#include "gm.h"
#include "gmConfig.h"
#include "gmWeather.h"
#include "gmTime.h"
#include "gmDisplay.h"
#include "gmWebPage.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "version.h"
#include "wManager.h"
#include "drivers/storage/storage.h"
#include "drivers/storage/nvMemory.h"

extern TSettings Settings;
extern nvMemory nvMem;
extern uint32_t elapsedKHs;
extern uint32_t shares;
extern uint32_t valids;
extern uint64_t upTime;

static WebServer server(80);
static bool s_started = false;   // routes registered
static bool s_enabled = false;   // server listening
static uint32_t s_lastActivity = 0;
static bool s_idleOff = false;   // auto-off only when enabled manually (double tap)
#define GM_WEB_IDLE_OFF_MS (15UL * 60UL * 1000UL)   // auto-off after 15 min without requests

static void activity() { s_lastActivity = millis(); }

enum PendingAction { PA_NONE, PA_REBOOT, PA_WIFI, PA_RESET };
static PendingAction s_pending = PA_NONE;
static uint32_t s_pendingAt = 0;
static String s_newSsid, s_newPass;

static fs::File s_uploadFile;
static String s_uploadError;

static void schedule(PendingAction a)
{
  s_pending = a;
  s_pendingAt = millis() + 800;
}

static void sendOk() { server.send(200, "application/json", "{\"ok\":true}"); }
static void sendErr(const String &msg, int code = 400)
{
  String out = "{\"ok\":false,\"error\":\"" + msg + "\"}";
  server.send(code, "application/json", out);
}

static String colorHex(uint32_t c)
{
  char b[8];
  snprintf(b, sizeof(b), "#%06lX", (unsigned long)(c & 0xFFFFFF));
  return String(b);
}

static uint32_t parseColor(const String &s, uint32_t def)
{
  String h = s;
  h.trim();
  if (h.startsWith("#")) h = h.substring(1);
  if (h.length() != 6) return def;
  return strtoul(h.c_str(), nullptr, 16) & 0xFFFFFF;
}

// ------------------------------------------------------------------ state

static void handleState()
{
  activity();
  GmConfig c = gm_cfgGet();
  GmWeather w = gm_weatherGet();
  DynamicJsonDocument doc(4096);

  JsonObject sys = doc.createNestedObject("sys");
  sys["version"] = CURRENT_VERSION;
  sys["ip"] = WiFi.localIP().toString();
  sys["ssid"] = WiFi.SSID();
  sys["rssi"] = WiFi.RSSI();
  sys["mac"] = WiFi.macAddress();
  sys["uptime"] = (uint32_t)upTime;
  sys["heap"] = ESP.getFreeHeap();
  sys["psram"] = ESP.getFreePsram();
  sys["khs"] = elapsedKHs;
  sys["shares"] = shares;
  sys["valids"] = valids;
  sys["screen"] = gm_displayCurrentScreen();
  sys["displayOn"] = gm_displayIsOn();
  sys["touchRaw"] = gm_touchRaw();
  sys["touchBase"] = gm_touchBaseline();
  sys["timeValid"] = gm_timeValid();
  sys["utcOffset"] = gm_currentOffset();
  sys["time"] = gm_timeValid() ? gm_localEpoch() : 0;

  JsonObject cfg = doc.createNestedObject("cfg");
  cfg["city"] = c.city;
  cfg["owmKey"] = c.owmKey;
  cfg["tempUnit"] = c.tempUnit;
  cfg["windUnit"] = c.windUnit;
  cfg["pressUnit"] = c.pressUnit;
  cfg["weatherMin"] = c.weatherMin;
  cfg["lang"] = c.lang;
  cfg["tzAuto"] = c.tzAuto;
  cfg["tzOffsetMin"] = c.tzOffsetMin;
  cfg["hour12"] = c.hour12;
  cfg["dateFmt"] = c.dateFmt;
  cfg["colonBlink"] = c.colonBlink;
  cfg["font"] = c.font;
  cfg["ntp"] = c.ntp;
  cfg["hourColor"] = colorHex(c.hourColor);
  cfg["minColor"] = colorHex(c.minColor);
  cfg["secColor"] = colorHex(c.secColor);
  cfg["screen"] = c.screen;
  cfg["screenMask"] = c.screenMask;
  cfg["autoSwitch"] = c.autoSwitch;
  cfg["switchSec"] = c.switchSec;
  cfg["brightness"] = c.brightness;
  cfg["nightEn"] = c.nightEn;
  cfg["nightStart"] = c.nightStart;
  cfg["nightEnd"] = c.nightEnd;
  cfg["nightBrightness"] = c.nightBrightness;
  cfg["gif"] = c.gif;

  JsonObject miner = doc.createNestedObject("miner");
  miner["pool"] = Settings.PoolAddress;
  miner["port"] = Settings.PoolPort;
  miner["wallet"] = Settings.BtcWallet;
  miner["pass"] = Settings.PoolPassword;
  miner["saveStats"] = Settings.saveStats;

  JsonObject wj = doc.createNestedObject("weather");
  wj["valid"] = w.valid;
  wj["city"] = w.city;
  wj["cc"] = w.cc;
  wj["temp"] = w.temp;
  wj["feels"] = w.feels;
  wj["tmin"] = w.tmin;
  wj["tmax"] = w.tmax;
  wj["hum"] = w.humidity;
  wj["wind"] = w.windKmh;
  wj["windDeg"] = w.windDeg;
  wj["press"] = w.pressHpa;
  wj["cond"] = w.cond;
  wj["icon"] = w.icon;
  wj["desc"] = w.desc;
  wj["sunrise"] = w.sunriseMin;
  wj["sunset"] = w.sunsetMin;
  wj["day"] = w.isDay;
  wj["source"] = w.source;
  wj["status"] = w.status;
  wj["age"] = w.valid ? (millis() - w.updatedMs) / 1000 : -1;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// ------------------------------------------------------------------ config

static bool argInt(const char *name, long &v)
{
  if (!server.hasArg(name)) return false;
  v = server.arg(name).toInt();
  return true;
}

static bool argBool(const char *name, bool &v)
{
  if (!server.hasArg(name)) return false;
  String s = server.arg(name);
  v = (s == "1" || s == "true" || s == "on");
  return true;
}

static bool argStr(const char *name, char *dst, size_t len)
{
  if (!server.hasArg(name)) return false;
  String s = server.arg(name);
  s.trim();
  strlcpy(dst, s.c_str(), len);
  return true;
}

static void handleCfg()
{
  activity();
  GmConfig c = gm_cfgGet();
  GmConfig before = c;
  long v;
  bool b;

  argStr("city", c.city, sizeof(c.city));
  argStr("owmKey", c.owmKey, sizeof(c.owmKey));
  if (argInt("tempUnit", v)) c.tempUnit = constrain(v, 0, 1);
  if (argInt("windUnit", v)) c.windUnit = constrain(v, 0, 3);
  if (argInt("pressUnit", v)) c.pressUnit = constrain(v, 0, 2);
  if (argInt("weatherMin", v)) c.weatherMin = constrain(v, 5, 1440);
  if (argInt("lang", v)) c.lang = constrain(v, 0, 1);

  if (argBool("tzAuto", b)) c.tzAuto = b;
  if (argInt("tzOffsetMin", v)) c.tzOffsetMin = constrain(v, -720, 840);
  if (argBool("hour12", b)) c.hour12 = b;
  if (argInt("dateFmt", v)) c.dateFmt = constrain(v, 0, GM_DATE_COUNT - 1);
  if (argBool("colonBlink", b)) c.colonBlink = b;
  if (argInt("font", v)) c.font = constrain(v, 0, GM_FONT_COUNT - 1);
  argStr("ntp", c.ntp, sizeof(c.ntp));
  if (server.hasArg("hourColor")) c.hourColor = parseColor(server.arg("hourColor"), c.hourColor);
  if (server.hasArg("minColor")) c.minColor = parseColor(server.arg("minColor"), c.minColor);
  if (server.hasArg("secColor")) c.secColor = parseColor(server.arg("secColor"), c.secColor);

  if (argInt("screenMask", v)) c.screenMask = (v & 0x1F) ? (v & 0x1F) : 0x1F;
  if (argBool("autoSwitch", b)) c.autoSwitch = b;
  if (argInt("switchSec", v)) c.switchSec = constrain(v, 5, 3600);

  if (argInt("brightness", v)) { c.brightness = constrain(v, 0, 100); gm_displaySetOn(true); }
  if (argBool("nightEn", b)) c.nightEn = b;
  if (argInt("nightStart", v)) c.nightStart = constrain(v, 0, 23);
  if (argInt("nightEnd", v)) c.nightEnd = constrain(v, 0, 23);
  if (argInt("nightBrightness", v)) c.nightBrightness = constrain(v, 0, 100);

  if (server.hasArg("gif")) {
    String g = server.arg("gif");
    bool valid = g.length() == 0 || (!g.startsWith("*") && SPIFFS.exists("/gif/" + g));
    for (size_t i = 0; !valid && i < gm_builtinGifCount(); i++) valid = g == String("*") + gm_builtinGifName(i);
    if (valid) strlcpy(c.gif, g.c_str(), sizeof(c.gif));
  }

  gm_cfgSet(c);

  if (strcmp(before.city, c.city) || strcmp(before.owmKey, c.owmKey) || before.lang != c.lang || before.weatherMin != c.weatherMin)
    gm_weatherRequestUpdate();
  if (before.tzAuto != c.tzAuto || before.tzOffsetMin != c.tzOffsetMin || strcmp(before.ntp, c.ntp))
    gm_applyTimeSettings();
  if (strcmp(before.gif, c.gif)) gm_displayReloadGif();

  sendOk();
}

static void handleScreen()
{
  long v;
  if (!argInt("s", v) || v < 0 || v >= GM_SCREEN_COUNT) return sendErr("invalid screen");
  gm_displayShowScreen((uint8_t)v);
  sendOk();
}

static void handleWeatherRefresh()
{
  gm_weatherRequestUpdate();
  sendOk();
}

// ------------------------------------------------------------------ miner + network

static void handleMiner()
{
  if (server.hasArg("pool")) {
    String p = server.arg("pool");
    p.trim();
    if (p.length()) Settings.PoolAddress = p;
  }
  long v;
  if (argInt("port", v) && v > 0 && v < 65536) Settings.PoolPort = v;
  argStr("wallet", Settings.BtcWallet, sizeof(Settings.BtcWallet));
  argStr("pass", Settings.PoolPassword, sizeof(Settings.PoolPassword));
  bool b;
  if (argBool("saveStats", b)) Settings.saveStats = b;
  nvMem.saveConfig(&Settings);
  sendOk();
  schedule(PA_REBOOT);
}

static void handleScan()
{
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
    server.send(200, "application/json", "{\"scanning\":true}");
    return;
  }
  if (n == WIFI_SCAN_RUNNING) {
    server.send(200, "application/json", "{\"scanning\":true}");
    return;
  }
  DynamicJsonDocument doc(4096);
  doc["scanning"] = false;
  JsonArray arr = doc.createNestedArray("nets");
  for (int i = 0; i < n && i < 30; i++) {
    JsonObject o = arr.createNestedObject();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["ch"] = WiFi.channel(i);
    o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleWifi()
{
  if (!server.hasArg("ssid") || server.arg("ssid").length() == 0) return sendErr("empty SSID");
  s_newSsid = server.arg("ssid");
  s_newPass = server.arg("pass");
  sendOk();
  schedule(PA_WIFI);
}

static void handleReboot()
{
  sendOk();
  schedule(PA_REBOOT);
}

static void handleReset()
{
  sendOk();
  schedule(PA_RESET);
}

// ------------------------------------------------------------------ GIF files

static String sanitizeName(const String &in)
{
  String out;
  for (size_t i = 0; i < in.length() && out.length() < 26; i++) {
    char ch = in[i];
    if (isalnum((unsigned char)ch) || ch == '-' || ch == '_') out += ch;
    else if (ch == '.') break;
  }
  if (!out.length()) out = "gif";
  return out + ".gif";
}

static void handleGifList()
{
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.createNestedArray("files");
  fs::File root = SPIFFS.open("/");
  fs::File f = root.openNextFile();
  while (f) {
    String name = f.path();
    if (name.startsWith("/gif/")) {
      JsonObject o = arr.createNestedObject();
      o["name"] = name.substring(5);
      o["size"] = f.size();
    }
    f = root.openNextFile();
  }
  JsonArray builtin = doc.createNestedArray("builtin");
  for (size_t i = 0; i < gm_builtinGifCount(); i++) builtin.add(gm_builtinGifName(i));
  doc["total"] = SPIFFS.totalBytes();
  doc["used"] = SPIFFS.usedBytes();
  doc["selected"] = gm_cfgGet().gif;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleGifUploadDone()
{
  if (s_uploadError.length()) return sendErr(s_uploadError);
  sendOk();
}

static void handleGifUpload()
{
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    s_uploadError = "";
    String path = "/gif/" + sanitizeName(up.filename);
    s_uploadFile = SPIFFS.open(path, FILE_WRITE);
    if (!s_uploadFile) s_uploadError = "could not create file";
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (s_uploadFile && s_uploadFile.write(up.buf, up.currentSize) != up.currentSize) {
      s_uploadError = "not enough SPIFFS space";
      String p = s_uploadFile.path();
      s_uploadFile.close();
      SPIFFS.remove(p);
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (s_uploadFile) s_uploadFile.close();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (s_uploadFile) {
      String p = s_uploadFile.path();
      s_uploadFile.close();
      SPIFFS.remove(p);
    }
    s_uploadError = "upload aborted";
  }
}

static void handleGifDelete()
{
  String name = server.arg("f");
  if (!name.length() || name.indexOf('/') >= 0) return sendErr("invalid file");
  GmConfig c = gm_cfgGet();
  if (strcmp(c.gif, name.c_str()) == 0) {
    c.gif[0] = 0;
    gm_cfgSet(c);
    gm_displayReloadGif();
    delay(300); // let the display task close the file
  }
  SPIFFS.remove("/gif/" + name);
  sendOk();
}

// Serve a GIF (built-in "*name" or uploaded file) for previews in the UI
static void handleGifRaw()
{
  String name = server.arg("f");
  server.sendHeader("Cache-Control", "max-age=86400");
  if (name.startsWith("*")) {
    for (size_t i = 0; i < gm_builtinGifCount(); i++) {
      if (name.substring(1) == gm_builtinGifName(i)) {
        size_t len;
        const uint8_t *data = gm_builtinGifData(i, &len);
        server.send_P(200, "image/gif", (const char *)data, len);
        return;
      }
    }
  } else if (name.length() && name.indexOf('/') < 0) {
    fs::File f = SPIFFS.open("/gif/" + name, "r");
    if (f) {
      server.streamFile(f, "image/gif");
      f.close();
      return;
    }
  }
  server.send(404, "text/plain", "404");
}

// ------------------------------------------------------------------ OTA

static void handleUpdateDone()
{
  bool ok = !Update.hasError();
  server.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"update failed\"}");
  if (ok) schedule(PA_REBOOT);
}

static void handleUpdateUpload()
{
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("[GM] OTA start: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("[GM] OTA done: %u bytes\n", up.totalSize);
    else Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
  }
}

// ------------------------------------------------------------------ screenshot

static void handleScreenshot()
{
  activity();
  const uint16_t *fb = gm_displayFrame();
  if (!fb) return sendErr("no framebuffer", 500);

  const int W = 240, H = 240;
  const uint32_t rowBytes = W * 3;
  const uint32_t dataSize = rowBytes * H;
  const uint32_t fileSize = 54 + dataSize;
  uint8_t hdr[54] = {'B', 'M'};
  auto put32 = [&](int off, uint32_t v) { hdr[off] = v; hdr[off + 1] = v >> 8; hdr[off + 2] = v >> 16; hdr[off + 3] = v >> 24; };
  put32(2, fileSize);
  put32(10, 54);
  put32(14, 40);
  put32(18, W);
  put32(22, H);
  hdr[26] = 1;
  hdr[28] = 24;
  put32(34, dataSize);

  server.setContentLength(fileSize);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "image/bmp", "");
  server.sendContent((const char *)hdr, sizeof(hdr));

  static uint8_t buf[240 * 3 * 8];
  int n = 0;
  for (int y = H - 1; y >= 0; y--) {
    const uint16_t *row = fb + y * W;
    uint8_t *p = buf + n * rowBytes;
    for (int x = 0; x < W; x++) {
      uint16_t v = (row[x] >> 8) | (row[x] << 8); // sprite stores byte-swapped RGB565
      uint8_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
      *p++ = (b << 3) | (b >> 2);
      *p++ = (g << 2) | (g >> 4);
      *p++ = (r << 3) | (r >> 2);
    }
    if (++n == 8 || y == 0) {
      server.sendContent((const char *)buf, n * rowBytes);
      n = 0;
    }
  }
}

// ------------------------------------------------------------------ public

void gm_webStart()
{
  if (s_started) return;
  SPIFFS.begin(true);

  server.on("/", HTTP_GET, []() { activity(); server.send_P(200, "text/html", GM_WEB_PAGE); });
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/cfg", HTTP_POST, handleCfg);
  server.on("/api/screen", HTTP_POST, handleScreen);
  server.on("/api/display", HTTP_POST, []() {
    activity();
    if (!server.hasArg("on")) return sendErr("on=0|1 required");
    gm_displaySetOn(server.arg("on") == "1");
    sendOk();
  });
  server.on("/api/weather/refresh", HTTP_POST, handleWeatherRefresh);
  server.on("/api/miner", HTTP_POST, handleMiner);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_POST, handleWifi);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/reset", HTTP_POST, handleReset);
  server.on("/api/gifs", HTTP_GET, handleGifList);
  server.on("/api/gif/upload", HTTP_POST, handleGifUploadDone, handleGifUpload);
  server.on("/api/gif/delete", HTTP_POST, handleGifDelete);
  server.on("/api/gif/raw", HTTP_GET, handleGifRaw);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on("/screenshot.bmp", HTTP_GET, handleScreenshot);
  server.onNotFound([]() { server.send(404, "text/plain", "404"); });
  s_started = true;
}

bool gm_webEnabled() { return s_enabled; }

static void webSetEnabled(bool on, bool idleOff)
{
  if (on == s_enabled) return;
  if (on) {
    if (WiFi.status() != WL_CONNECTED) {
      gm_displayPopup("Web Config", "No WiFi connection");
      return;
    }
    gm_webStart();
    server.begin();
    s_enabled = true;
    s_idleOff = idleOff;
    activity();
    String url = "http://" + WiFi.localIP().toString();
    gm_displayPopup("Web Config On", url.c_str(), 4000);
    Serial.printf("[GM] Web UI on: %s/\n", url.c_str());
  } else {
    server.stop();
    s_enabled = false;
    gm_displayPopup("Web Config Off", "");
    Serial.println("[GM] Web UI off");
  }
}

void gm_webSetEnabled(bool on) { webSetEnabled(on, true); }
void gm_webEnableAtBoot() { webSetEnabled(true, false); }
void gm_webToggle() { webSetEnabled(!s_enabled, true); }

void gm_webLoop()
{
  if (!s_enabled) return;
  server.handleClient();
  if (s_idleOff && s_pending == PA_NONE && millis() - s_lastActivity > GM_WEB_IDLE_OFF_MS) {
    gm_webSetEnabled(false);
    return;
  }

  if (s_pending != PA_NONE && (int32_t)(millis() - s_pendingAt) >= 0) {
    PendingAction a = s_pending;
    s_pending = PA_NONE;
    switch (a) {
      case PA_WIFI:
        Serial.printf("[GM] Switching WiFi to '%s'\n", s_newSsid.c_str());
        WiFi.persistent(true);
        WiFi.disconnect(false, false);
        WiFi.begin(s_newSsid.c_str(), s_newPass.c_str());
        delay(500);
        ESP.restart();
        break;
      case PA_RESET:
        gm_cfgErase();
        reset_configuration(); // erases NerdMiner config + WiFi credentials and restarts
        break;
      case PA_REBOOT:
        ESP.restart();
        break;
      default:
        break;
    }
  }
}

#endif
