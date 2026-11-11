#include "displayDriver.h"

#ifdef GEEKMAGIC_S3_DISPLAY

// GeekMagic S3 display driver (ST7789 240x240 + capacitive touch button)
//
//  - Screen 0: "Weather Clock" (layout inspired by the GeekMagic SmallTV weather clock theme)
//  - Screens 1-4: the classic NerdMiner 320x170 screens, rendered off-screen and scaled to 240x128
//  - Everything is composed in a 240x240 PSRAM sprite (also used for /screenshot.bmp)

#include <TFT_eSPI.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <OneButton.h>
#include <AnimatedGIF.h>
#include "media/images_320_170.h"
#include "media/myFonts.h"
#include "media/gmFonts.h"
#include "media/gmGifs.h"
#include "media/gmIcons.h"
#include "media/gmWeatherIcons.h"
#include "media/gmVlwFonts.h"
#include "media/Free_Fonts.h"
#include "version.h"
#include "monitor.h"
#include "wManager.h"
#include "OpenFontRender.h"
#include "drivers/storage/storage.h"
#include "geekmagic/gmConfig.h"
#include "geekmagic/gmWeather.h"
#include "geekmagic/gmTime.h"
#include "geekmagic/gmDisplay.h"
#include "geekmagic/gm.h"

#define WIDTH 240
#define HEIGHT 240
#define LEGACY_W 320
#define LEGACY_H 170
#define LEGACY_OUT_H 128   // 170 * 0.75
#define LEGACY_Y 56        // (240 - 128) / 2
#define GIF_X 160
#define GIF_Y 156
#define GIF_SIZE 80
#define GM_TOUCH_DELTA 3000   // measured: idle noise < 200, finger ~ +6000
#define GM_TOUCH_STABLE 3      // consecutive samples (20 ms each) before a state change is accepted

String getCurrentHashRate(unsigned long mElapsed);
void getTime(unsigned long *currentHours, unsigned long *currentMinutes, unsigned long *currentSeconds);

static TFT_eSPI tft = TFT_eSPI();
static TFT_eSprite frame = TFT_eSprite(&tft);   // 240x240 composed screen
static TFT_eSprite legacy = TFT_eSprite(&tft);  // 320x170 classic NerdMiner screens

static OpenFontRender lrender;   // DigitalNumbers -> legacy sprite (classic screens)
static OpenFontRender txt;       // Ubuntu Bold    -> frame
static OpenFontRender num;       // selected clock font -> frame
static uint8_t s_numFont = 0xFF;
static unsigned int s_timeSize = 90;

// Colours
#define C_BLACK      TFT_BLACK
#define C_WHITE      TFT_WHITE
#define C_ORANGE     0xFD80   // city name
#define C_GREEN      0x5FE5   // badge / weekday
#define C_BADGE_TXT  0x0220
#define C_LABEL_TXT  0x1110   // dark blue text in the condition label
#define C_GREY       0x8410

// ------------------------------------------------------------------ state

static uint8_t s_curScreen = GM_SCREEN_WEATHER;
static volatile bool s_nextRequested = false;
static volatile int s_showRequested = -1;
static uint32_t s_lastSwitchMs = 0;
static int s_appliedBrightness = -1;
static bool s_screenOff = false;

// ------------------------------------------------------------------ texts (pt / en)

struct GmTexts {
  const char *weekdays[7];
  const char *cond[9];
  const char *feels;
  const char *tmax;
  const char *tmin;
  const char *wind;
  const char *dirs[8];
  const char *press;
  const char *sunrise;
  const char *sunset;
  const char *noCity;
  const char *webOff;
  const char *weatherTab;
  const char *example;
};

static const GmTexts TXT_PT = {
    {"Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sáb"},
    {"--", "Limpo", "Parcial", "Nuvem", "Névoa", "Garoa", "Chuva", "Neve", "Raios"},
    "Sensação", "Máx", "Mín", "Vento",
    {"Vento Norte", "Vento Nordeste", "Vento Leste", "Vento Sudeste", "Vento Sul", "Vento Sudoeste", "Vento Oeste", "Vento Noroeste"},
    "Pressão", "Amanhecer", "Pôr do sol", "Configure a cidade",
    "2 toques: Web Config", "Aba Weather", "ex: Santo Andre,BR"};

static const GmTexts TXT_EN = {
    {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"},
    {"--", "Clear", "Clouds", "Clouds", "Mist", "Drizzle", "Rain", "Snow", "Storm"},
    "Feels like", "Max temp", "Min Temp", "Wind",
    {"Northerly", "Northeasterly", "Easterly", "Southeasterly", "Southerly", "Southwesterly", "Westerly", "Northwesterly"},
    "Atm", "Sunrise", "Sunset", "Set your city",
    "Double tap: Web Config", "Weather tab", "e.g. Santo Andre,BR"};

static const GmTexts &texts(const GmConfig &c) { return c.lang == GM_LANG_EN ? TXT_EN : TXT_PT; }

// Label of the white box under the icon. The atmosphere group (OWM 7xx) shows the specific phenomenon like the
// SmallTV does; every word is measured to fit the 70 px box in the SmallTV font.
static const char *condLabel(const GmWeather &w, const GmConfig &c)
{
  bool pt = c.lang == GM_LANG_PT;
  if (w.cond == GM_COND_FOG) {
    switch (w.owmId) {
      case 711: return pt ? "Fumo" : "Smoke";
      case 721: return pt ? "Bruma" : "Haze";
      case 731: case 761: return pt ? "Poeira" : "Dust";
      case 741: return pt ? "Névoa" : "Fog";
      case 751: return pt ? "Areia" : "Sand";
      case 762: return pt ? "Cinzas" : "Ash";
      case 771: return pt ? "Rajada" : "Squall";
      case 781: return pt ? "Raios" : "Storm";
      case 0: return pt ? "Névoa" : "Fog";      // Open-Meteo fog (WMO 45/48)
      default: break;                           // 701 mist
    }
  }
  return texts(c).cond[w.cond];
}

// ------------------------------------------------------------------ units

static String fmtTemp(const GmConfig &c, float tC, bool unit = true)
{
  float v = c.tempUnit == GM_TEMP_F ? tC * 9.0f / 5.0f + 32.0f : tC;
  String s = String((int)lroundf(v));
  if (unit) s += c.tempUnit == GM_TEMP_F ? "°F" : "°C";
  return s;
}

static String fmtWind(const GmConfig &c, float kmh)
{
  switch (c.windUnit) {
    case GM_WIND_MS: return String((int)lroundf(kmh / 3.6f)) + "m/s";
    case GM_WIND_MPH: return String((int)lroundf(kmh * 0.621371f)) + "mph";
    case GM_WIND_KN: return String((int)lroundf(kmh * 0.539957f)) + "kn";
    default: return String((int)lroundf(kmh)) + "km/h";
  }
}

static String fmtPress(const GmConfig &c, float hpa)
{
  switch (c.pressUnit) {
    case GM_PRESS_MMHG: return String((int)lroundf(hpa * 0.750062f)) + "mmHg";
    case GM_PRESS_INHG: return String(hpa * 0.02953f, 2) + "inHg";
    default: return String((int)lroundf(hpa)) + "hPa";
  }
}

// ------------------------------------------------------------------ backlight

// Backlight: GPIO14, ACTIVE LOW (low = on). Brightness uses inverted PWM on the backlight.
// A software pixel-dimming path (gmPush with s_swDim < 100) is kept as a fallback but disabled (GM_SOFT_DIM 0).
#define GM_SOFT_DIM 0
#define BL_PWM_CHANNEL 7
#define BL_PWM_BITS 12
#define BL_PWM_MAX ((1u << BL_PWM_BITS) - 1)
static uint8_t s_lut5[32], s_lut6[64];
static volatile int s_swDim = 100;           // 1..100, 100 = no scaling
static uint16_t *s_dimBuf = nullptr;          // 240x240 RGB565 scratch (PSRAM)

static void setBacklight(int pct)
{
  pct = constrain(pct, 0, 100);
  if (pct == s_appliedBrightness) return;
  s_appliedBrightness = pct;
#if GM_SOFT_DIM
  digitalWrite(TFT_BL, pct > 0 ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
  if (pct > 0 && pct != s_swDim) {
    float k = pct / 100.0f;   // linear on the colour values: closer to perceived brightness
    for (int i = 0; i < 32; i++) s_lut5[i] = (uint8_t)lroundf(i * k);
    for (int i = 0; i < 64; i++) s_lut6[i] = (uint8_t)lroundf(i * k);
    s_swDim = pct;
  }
#else
  // 12-bit PWM + gamma 2.2 curve: every 1 % is a visible step and 1 % is really dim
  uint32_t level = pct == 0 ? 0 : max<uint32_t>(1, (uint32_t)lroundf(BL_PWM_MAX * powf(pct / 100.0f, 2.2f)));
  ledcWrite(BL_PWM_CHANNEL, BL_PWM_MAX - level);   // active low
#endif
}

// Push a region of the frame sprite to the panel, applying the software brightness
static void gmPush(int x, int y, int w, int h)
{
  if (s_swDim >= 100 || !s_dimBuf) {
    if (x == 0 && y == 0 && w == WIDTH && h == HEIGHT) frame.pushSprite(0, 0);
    else frame.pushSprite(x, y, x, y, w, h);
    return;
  }
  const uint16_t *src = (const uint16_t *)frame.getPointer();
  uint16_t *dst = s_dimBuf;
  for (int row = 0; row < h; row++) {
    const uint16_t *p = src + (y + row) * WIDTH + x;
    for (int col = 0; col < w; col++) {
      uint16_t v = (p[col] >> 8) | (p[col] << 8);  // sprite stores byte-swapped RGB565
      *dst++ = (s_lut5[v >> 11] << 11) | (s_lut6[(v >> 5) & 0x3F] << 5) | s_lut5[v & 0x1F];
    }
  }
  tft.pushImage(x, y, w, h, s_dimBuf);           // tft.setSwapBytes(true): native RGB565 in
}

static void gmPushAll() { gmPush(0, 0, WIDTH, HEIGHT); }


static void updateBacklight(const GmConfig &c, int hour)
{
  if (s_screenOff) { setBacklight(0); return; }
  bool night = false;
  if (c.nightEn && hour >= 0) {
    if (c.nightStart == c.nightEnd) night = false;
    else if (c.nightStart < c.nightEnd) night = hour >= c.nightStart && hour < c.nightEnd;
    else night = hour >= c.nightStart || hour < c.nightEnd;
  }
  setBacklight(night ? c.nightBrightness : c.brightness);
}

// ------------------------------------------------------------------ fonts

static void ensureNumFont(const GmConfig &c)
{
  if (s_numFont == c.font) return;
  if (s_numFont != 0xFF) num.unloadFont(); // OpenFontRender crashes when unloading before the first load
  switch (c.font) {
    case GM_FONT_ROUND: num.loadFont(gm_fredoka_font, sizeof(gm_fredoka_font)); break;
    case GM_FONT_DIGITAL: num.loadFont(DigitalNumbers, sizeof(DigitalNumbers)); break;
    default: num.loadFont(gm_bebas_font, sizeof(gm_bebas_font)); break;
  }
  s_numFont = c.font;
  // Biggest size where "00:00" fits the clock area (158 x 70 px)
  unsigned int size = 120;
  while (size > 30) {
    num.setFontSize(size);
    if (num.getTextWidth("00:00") <= 156 && num.getTextHeight("00:00") <= 70) break;
    size -= 2;
  }
  s_timeSize = size;
}

// Draw text and return its width
static int drawTxt(OpenFontRender &r, const char *s, int x, int y, unsigned size, uint16_t fg, uint16_t bg = C_BLACK)
{
  r.setFontSize(size);
  r.drawString(s, x, y, fg, bg);
  return r.getTextWidth("%s", s);
}

// Fit font size for a text into a maximum width
static unsigned fitSize(OpenFontRender &r, const char *s, unsigned size, unsigned minSize, int maxW)
{
  while (size > minSize) {
    r.setFontSize(size);
    if ((int)r.getTextWidth("%s", s) <= maxW) break;
    size--;
  }
  return size;
}

// ------------------------------------------------------------------ weather icons (bitmaps, src/media/gmWeatherIcons.h)

// Draw the condition icon with its top-left corner at (x, y). Clear night uses the moon icon.
static void drawWeatherIcon(const GmWeather &w, int x, int y, bool big)
{
  if (!w.valid || w.icon == 0) return;
  uint8_t code = (w.icon == 1 && !w.isDay) ? 0 : w.icon;
  for (size_t i = 0; i < GM_WEATHER_ICON_COUNT; i++) {
    if (GM_WEATHER_ICONS[i].code == code) {
      if (big) frame.pushImage(x, y, GM_WICON_BIG, GM_WICON_BIG, GM_WEATHER_ICONS[i].big);
      else frame.pushImage(x, y, GM_WICON_SMALL, GM_WICON_SMALL, GM_WEATHER_ICONS[i].small);
      return;
    }
  }
}

// Bitmaps from src/media/gmIcons.h (SmallTV originals)
static void drawThermometer(int x, int y)
{
  frame.pushImage(x, y, GM_ICON_THERMO_W, GM_ICON_THERMO_H, gm_icon_thermo);
}

static void drawHumidityIcon(int x, int y)
{
  frame.pushImage(x, y, GM_ICON_HUMIDITY_W, GM_ICON_HUMIDITY_H, gm_icon_humidity);
}

static void drawBar(int x, int y, int w, int h, int pct, uint16_t col)
{
  pct = constrain(pct, 0, 100);
  frame.drawRoundRect(x, y, w, h, h / 2, C_WHITE);
  int fw = (w - 4) * pct / 100;
  if (fw > 2) frame.fillRoundRect(x + 2, y + 2, fw, h - 4, (h - 4) / 2, col);
}

#define GM_TEMP_MIN 0.0f    // temperature bar range (°C)
#define GM_TEMP_MAX 50.0f

// Temperature bar colour: humidity blue (cold) -> cyan -> short green -> yellow -> amber -> strong orange (hot)
struct GmColorStop { float t; uint8_t r, g, b; };
static const GmColorStop TEMP_STOPS[] = {
    {5, 40, 172, 248},   // same blue as the humidity bar
    {15, 40, 210, 220},  // cyan
    {22, 150, 220, 90},  // lime (kept short)
    {28, 240, 220, 60},  // yellow
    {34, 255, 160, 20},  // amber
    {40, 255, 120, 0},   // orange
    {48, 255, 80, 0},    // strong orange
};

static uint16_t tempColor(float t)
{
  const size_t n = sizeof(TEMP_STOPS) / sizeof(TEMP_STOPS[0]);
  if (t <= TEMP_STOPS[0].t) return tft.color565(TEMP_STOPS[0].r, TEMP_STOPS[0].g, TEMP_STOPS[0].b);
  for (size_t i = 1; i < n; i++) {
    const GmColorStop &a = TEMP_STOPS[i - 1], &b = TEMP_STOPS[i];
    if (t <= b.t) {
      float k = (t - a.t) / (b.t - a.t);
      return tft.color565(a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k);
    }
  }
  return tft.color565(TEMP_STOPS[n - 1].r, TEMP_STOPS[n - 1].g, TEMP_STOPS[n - 1].b);
}

static uint16_t humColor(int h)
{
  if (h < 40) return 0xEF6B;   // yellow (dry)
  if (h < 70) return 0x3E8A;   // green
  return 0x2D7F;               // blue (humid)
}

// ------------------------------------------------------------------ GIF (80x80 slot)

static AnimatedGIF *s_gif = nullptr;
static uint16_t *s_gifCanvas = nullptr;
static bool s_gifOpen = false;
static uint32_t s_gifNextMs = 0;
static volatile bool s_gifReload = true;
static int s_gifOffX = 0, s_gifOffY = 0;

static void *gifOpen(const char *fname, int32_t *pSize)
{
  fs::File *f = new fs::File(SPIFFS.open(fname, "r"));
  if (!*f) { delete f; return nullptr; }
  *pSize = f->size();
  return f;
}
static void gifClose(void *h)
{
  fs::File *f = (fs::File *)h;
  if (f) { f->close(); delete f; }
}
static int32_t gifRead(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
  fs::File *f = (fs::File *)pFile->fHandle;
  int32_t n = iLen;
  if ((pFile->iSize - pFile->iPos) < iLen) n = pFile->iSize - pFile->iPos - 1; // library quirk
  if (n <= 0) return 0;
  n = (int32_t)f->read(pBuf, n);
  pFile->iPos = f->position();
  return n;
}
static int32_t gifSeek(GIFFILE *pFile, int32_t iPosition)
{
  fs::File *f = (fs::File *)pFile->fHandle;
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  return pFile->iPos;
}
static void gifDraw(GIFDRAW *d)
{
  int y = d->iY + d->y - s_gifOffY;
  if (y < 0 || y >= GIF_SIZE) return;
  uint8_t *s = d->pPixels;
  uint16_t *pal = d->pPalette;
  uint16_t *dst = s_gifCanvas + y * GIF_SIZE;
  for (int i = 0; i < d->iWidth; i++) {
    int x = d->iX + i - s_gifOffX;
    if (x < 0) continue;
    if (x >= GIF_SIZE) break;
    uint8_t c = s[i];
    if (d->ucHasTransparency && c == d->ucTransparent) {
      if (d->ucDisposalMethod == 2) dst[x] = C_BLACK;
      continue;
    }
    dst[x] = pal[c];
  }
}

static void gifReloadIfNeeded(const GmConfig &c)
{
  if (!s_gifReload) return;
  s_gifReload = false;
  if (s_gifOpen) { s_gif->close(); s_gifOpen = false; }
  if (!c.gif[0] || !s_gif || !s_gifCanvas) return;
  memset(s_gifCanvas, 0, GIF_SIZE * GIF_SIZE * 2);

  int ok = 0;
  String path;
  if (c.gif[0] == '*') {
    // built-in GIF embedded in the firmware (memory mapped flash)
    path = c.gif;
    for (size_t i = 0; i < GM_BUILTIN_GIF_COUNT; i++) {
      if (strcmp(GM_BUILTIN_GIFS[i].name, c.gif + 1) == 0) {
        ok = s_gif->open((uint8_t *)GM_BUILTIN_GIFS[i].data, (int)GM_BUILTIN_GIFS[i].size, gifDraw);
        break;
      }
    }
  } else {
    path = String("/gif/") + c.gif;
    if (!SPIFFS.exists(path)) return;
    ok = s_gif->open(path.c_str(), gifOpen, gifClose, gifRead, gifSeek, gifDraw);
  }
  if (ok) {
    s_gifOpen = true;
    int cw = s_gif->getCanvasWidth(), ch = s_gif->getCanvasHeight();
    s_gifOffX = cw > GIF_SIZE ? (cw - GIF_SIZE) / 2 : 0;
    s_gifOffY = ch > GIF_SIZE ? (ch - GIF_SIZE) / 2 : 0;
    s_gifNextMs = 0;
    Serial.printf("[GM] GIF %s opened (%dx%d)\n", path.c_str(), cw, ch);
  } else {
    Serial.printf("[GM] GIF %s open failed (%d)\n", path.c_str(), s_gif->getLastError());
  }
}

// Advance the GIF; returns true if the canvas changed
// Frames follow the GIF's own timing: when the display task falls behind, overdue frames are decoded
// (they may be deltas) but only the latest one is shown, so the animation keeps its real speed.
static bool gifStep()
{
  if (!s_gifOpen) return false;
  uint32_t now = millis();
  if (s_gifNextMs == 0) s_gifNextMs = now;
  if ((int32_t)(now - s_gifNextMs) < 0) return false;
  for (int i = 0; i < 4 && (int32_t)(now - s_gifNextMs) >= 0; i++) {
    int delayMs = 100;
    if (!s_gif->playFrame(false, &delayMs)) s_gif->reset();
    s_gifNextMs += max(delayMs, 10);
  }
  if ((int32_t)(now - s_gifNextMs) > 500) s_gifNextMs = now; // way behind (e.g. after a full redraw): resync
  return true;
}

// Default content of the GIF slot: a Bitcoin coin + current hashrate
static void drawCoinSlot(const String &hashrate)
{
  int cx = GIF_X + 40, cy = GIF_Y + 30;
  frame.fillCircle(cx, cy, 25, 0xFC60);
  frame.drawCircle(cx, cy, 25, 0xFE60);
  frame.drawCircle(cx, cy, 21, 0xFE60);
  txt.setFontSize(30);
  txt.setAlignment(Align::MiddleCenter);
  txt.drawString("B", cx + 1, cy - 1, C_WHITE, 0xFC60);
  txt.setAlignment(Align::TopLeft);
  frame.fillRect(cx - 4, cy - 16, 2, 4, C_WHITE);
  frame.fillRect(cx + 2, cy - 16, 2, 4, C_WHITE);
  frame.fillRect(cx - 4, cy + 12, 2, 4, C_WHITE);
  frame.fillRect(cx + 2, cy + 12, 2, 4, C_WHITE);
  String hr = hashrate + " KH/s";
  unsigned sz = fitSize(txt, hr.c_str(), 15, 10, GIF_SIZE - 2);
  txt.setFontSize(sz);
  txt.setAlignment(Align::TopCenter);
  txt.drawString(hr.c_str(), cx, GIF_Y + 60, C_WHITE, C_BLACK);
  txt.setAlignment(Align::TopLeft);
}

// ------------------------------------------------------------------ time helpers

struct GmClock {
  int h, m, s, wday, day, mon, year;
  bool valid;
};

static GmClock readClock()
{
  unsigned long hh, mm, ss;
  getTime(&hh, &mm, &ss); // also drives the NTP sync of NerdMiner
  GmClock c;
  c.valid = gm_timeValid();
  time_t t = c.valid ? (time_t)gm_localEpoch() : 0;
  struct tm tmv;
  gmtime_r(&t, &tmv);
  c.h = hh; c.m = mm; c.s = ss;
  c.wday = tmv.tm_wday;
  c.day = tmv.tm_mday;
  c.mon = tmv.tm_mon + 1;
  c.year = tmv.tm_year + 1900;
  return c;
}

static String fmtDate(const GmConfig &cfg, const GmClock &c)
{
  char b[16];
  switch (cfg.dateFmt) {
    case GM_DATE_DD_MM_Y: snprintf(b, sizeof(b), "%02d/%02d/%04d", c.day, c.mon, c.year); break;
    case GM_DATE_M_D_Y: snprintf(b, sizeof(b), "%d/%d/%04d", c.mon, c.day, c.year); break;
    case GM_DATE_MM_DD_Y: snprintf(b, sizeof(b), "%02d/%02d/%04d", c.mon, c.day, c.year); break;
    case GM_DATE_Y_M_D: snprintf(b, sizeof(b), "%04d/%d/%d", c.year, c.mon, c.day); break;
    case GM_DATE_ISO: snprintf(b, sizeof(b), "%04d-%02d-%02d", c.year, c.mon, c.day); break;
    default: snprintf(b, sizeof(b), "%d/%d/%04d", c.day, c.mon, c.year); break;
  }
  return String(b);
}

static int displayHour(const GmConfig &cfg, int h)
{
  if (!cfg.hour12) return h;
  int h12 = h % 12;
  return h12 == 0 ? 12 : h12;
}

static String fmtHM(const GmConfig &cfg, int minutes)
{
  char b[8];
  snprintf(b, sizeof(b), "%02d:%02d", displayHour(cfg, minutes / 60), minutes % 60);
  return String(b);
}

// ------------------------------------------------------------------ GeekMagic smooth fonts (VLW)
//
// Layout of the weather screen, measured on photos of the SmallTV weather clock.
// Text positions are given as the TOP OF THE CAPITAL LETTERS / DIGITS.
#define L_CITY_AREA   176   // city + badge are centred in [0, L_CITY_AREA)
#define L_CITY_Y      15
#define L_BADGE_Y     9
#define L_BADGE_H     24
#define L_ICON_X      178
#define L_ICON_Y      2
#define L_INFO_CX     92
#define L_INFO_Y      46
#define L_INFO_W      176
#define L_TIME_RIGHT  166   // HH:MM is right-aligned to this x ("19:58" starts at x=18)
#define L_TIME_Y      74
#define L_LABEL_X     168
#define L_LABEL_Y     70
#define L_LABEL_W     72
#define L_LABEL_H     24
#define L_SEC_CX      204
#define L_SEC_Y       102
#define L_DATE_X      10
#define L_DATE_Y      142
#define L_ICONS_X     4
#define L_ROW1_Y      185   // temperature row (cap top of the value)
#define L_ROW2_Y      214   // humidity row
#define L_BAR_X       40
#define L_BAR_W       56
#define L_BAR_H       12
#define L_VAL_X       102

// Alibaba20: TFT_eSPI uses the header ascent (15) as maxAscent (glyph scan is disabled in Smooth_font.cpp),
// so glyph cells start ascent(15) - capHeight(14) = 1 px above the capitals
#define VLW_TXT_CAP_OFF 1
#define VLW_TXT_CAP_H   14

static const uint8_t *s_vlwLoaded = nullptr;

static void useVlw(const uint8_t *font)
{
  if (s_vlwLoaded == font) return;
  if (s_vlwLoaded) frame.unloadFont();
  frame.loadFont(font);
  s_vlwLoaded = font;
}

enum GmHAlign { H_LEFT, H_CENTER, H_RIGHT };

// Draw text in the SmallTV font (Alibaba20) with its capitals' top at capY.
// Falls back to a smaller Ubuntu Bold (OpenFontRender) when it does not fit maxW. Returns the drawn width.
static int gmText(const char *s, int x, int capY, uint16_t fg, uint16_t bg = C_BLACK, int maxW = 240, GmHAlign ha = H_LEFT)
{
  useVlw(gm_vlw_alibaba20);
  int w = frame.textWidth(s);
  if (w <= maxW) {
    int dx = ha == H_CENTER ? w / 2 : ha == H_RIGHT ? w : 0;
    frame.setTextColor(fg, bg);
    frame.setTextDatum(TL_DATUM);
    frame.drawString(s, x - dx, capY - VLW_TXT_CAP_OFF);
    return w;
  }
  unsigned sz = fitSize(txt, s, 19, 10, maxW);
  txt.setFontSize(sz);
  txt.setAlignment(ha == H_CENTER ? Align::MiddleCenter : ha == H_RIGHT ? Align::MiddleRight : Align::MiddleLeft);
  txt.drawString(s, x, capY + VLW_TXT_CAP_H / 2, fg, bg);
  txt.setAlignment(Align::TopLeft);
  return txt.getTextWidth("%s", s);
}

// Clock digits with their top at topY: SmallTV AF80/AF40 for the GeekMagic font, OpenFontRender otherwise.
static int clockText(const GmConfig &cfg, const char *s, int x, int topY, uint16_t fg, bool small, GmHAlign ha = H_LEFT)
{
  if (cfg.font == GM_FONT_GEEK) {
    useVlw(small ? gm_vlw_af40 : gm_vlw_af80);   // digits fill the whole ascent: top of cell = top of digits
    int w = frame.textWidth(s);
    int dx = ha == H_CENTER ? w / 2 : ha == H_RIGHT ? w : 0;
    frame.setTextColor(fg, C_BLACK);
    frame.setTextDatum(TL_DATUM);
    frame.drawString(s, x - dx, topY);
    return w;
  }
  ensureNumFont(cfg);
  num.setFontSize(small ? s_timeSize * 6 / 10 : s_timeSize);
  num.setAlignment(ha == H_CENTER ? Align::TopCenter : ha == H_RIGHT ? Align::TopRight : Align::TopLeft);
  num.drawString(s, x, topY, fg, C_BLACK);
  num.setAlignment(Align::TopLeft);
  return num.getTextWidth("%s", s);
}

static int clockTextWidth(const GmConfig &cfg, const char *s, bool small)
{
  if (cfg.font == GM_FONT_GEEK) {
    useVlw(small ? gm_vlw_af40 : gm_vlw_af80);
    return frame.textWidth(s);
  }
  ensureNumFont(cfg);
  num.setFontSize(small ? s_timeSize * 6 / 10 : s_timeSize);
  return num.getTextWidth("%s", s);
}

// Texts that overlap the GIF slot are remembered so the GIF animation can repaint them on top of each frame
struct GmOverlay { char s[24]; int16_t x, capY; uint16_t color; };
static GmOverlay s_ovl[4];
static volatile int s_ovlN = 0;

static int gmTextOv(const char *s, int x, int capY, uint16_t fg)
{
  int w = gmText(s, x, capY, fg);
  if (s_ovlN < 4 && x + w > GIF_X && capY + VLW_TXT_CAP_H + 6 > GIF_Y) {
    GmOverlay &o = s_ovl[s_ovlN++];
    strlcpy(o.s, s, sizeof(o.s));
    o.x = x; o.capY = capY; o.color = fg;
  }
  return w;
}

// ------------------------------------------------------------------ "Weather Clock" screen

static void drawWeatherScreen(unsigned long mElapsed, const GmConfig &cfg, const GmClock &clk)
{
  const GmTexts &T = texts(cfg);
  GmWeather w = gm_weatherGet();
  String hashrate = getCurrentHashRate(mElapsed);

  frame.fillSprite(C_BLACK);
  s_ovlN = 0;

  // --- Not configured / no data yet: ask for the configuration instead of showing empty values
  if (!w.valid) {
    String url = gm_webEnabled() ? "http://" + WiFi.localIP().toString() : String(T.webOff);
    if (!cfg.city[0]) {
      gmText(T.noCity, 120, L_CITY_Y, C_ORANGE, C_BLACK, 232, H_CENTER);
      gmText(url.c_str(), 120, L_INFO_Y, C_WHITE, C_BLACK, 232, H_CENTER);
    } else {
      gmText(cfg.city, 120, L_CITY_Y, C_ORANGE, C_BLACK, 232, H_CENTER);
      gmText(w.status, 120, L_INFO_Y, C_WHITE, C_BLACK, 232, H_CENTER);
    }
  }

  if (w.valid) {
    // --- Row 1: city + country badge
    const char *city = w.city;
    int badgeW = 0;
    useVlw(gm_vlw_alibaba20);
    if (w.cc[0]) badgeW = frame.textWidth(w.cc) + 14;
    const int gap = badgeW ? 10 : 0;
    int cityMaxW = L_CITY_AREA - 4 - gap - badgeW;
    int cityW = min((int)frame.textWidth(city), cityMaxW);
    int cityX = max(2, (L_CITY_AREA - (cityW + gap + badgeW)) / 2);
    cityW = gmText(city, cityX, L_CITY_Y, C_ORANGE, C_BLACK, cityMaxW);
    if (badgeW) {
      int bx = cityX + cityW + gap;
      frame.fillRoundRect(bx, L_BADGE_Y, badgeW, L_BADGE_H, 3, C_GREEN);
      gmText(w.cc, bx + badgeW / 2, L_BADGE_Y + (L_BADGE_H - VLW_TXT_CAP_H) / 2, C_BADGE_TXT, C_GREEN, badgeW, H_CENTER);
    }

    // --- Weather icon (top-right)
    drawWeatherIcon(w, L_ICON_X, L_ICON_Y, true);

    // --- Row 2: rotating info line (same sequence as the SmallTV weather clock + wind direction)
    String items[10];
    int n = 0;
    if (w.desc[0]) items[n++] = w.desc;
    items[n++] = WiFi.localIP().toString();
    items[n++] = String(T.wind) + " " + fmtWind(cfg, w.windKmh);
    items[n++] = T.dirs[((w.windDeg % 360) + 22) / 45 % 8];
    items[n++] = String(T.tmin) + " " + fmtTemp(cfg, w.tmin);
    items[n++] = String(T.tmax) + " " + fmtTemp(cfg, w.tmax);
    items[n++] = String(T.feels) + " " + fmtTemp(cfg, w.feels);
    items[n++] = String(T.press) + " " + fmtPress(cfg, w.pressHpa);
    if (w.sunriseMin >= 0) items[n++] = String(T.sunrise) + " " + fmtHM(cfg, w.sunriseMin);
    if (w.sunsetMin >= 0) items[n++] = String(T.sunset) + " " + fmtHM(cfg, w.sunsetMin);
    const String &info = items[(millis() / 4000) % n];
    gmText(info.c_str(), L_INFO_CX, L_INFO_Y, C_WHITE, C_BLACK, L_INFO_W, H_CENTER);
  }

  // --- Big clock HH:MM
  char hs[4], ms[4];
  snprintf(hs, sizeof(hs), "%02d", displayHour(cfg, clk.h));
  snprintf(ms, sizeof(ms), "%02d", clk.m);
  bool colon = !cfg.colonBlink || (clk.s % 2 == 0);
  uint16_t hc = gm_rgb565(cfg.hourColor), mc = gm_rgb565(cfg.minColor), sc = gm_rgb565(cfg.secColor);
  if (cfg.font == GM_FONT_GEEK) {
    // SmallTV style: every digit centred in a fixed-width cell (monospaced look, "06:14" spans x 8..163)
    const int CELL = 35, COLON = 15, X0 = 8;
    char d[2] = {0, 0};
    const int xs[4] = {X0, X0 + CELL, X0 + 2 * CELL + COLON, X0 + 3 * CELL + COLON};
    const char digits[4] = {hs[0], hs[1], ms[0], ms[1]};
    for (int i = 0; i < 4; i++) {
      d[0] = digits[i];
      clockText(cfg, d, xs[i] + CELL / 2, L_TIME_Y, i < 2 ? hc : mc, false, H_CENTER);
    }
    if (colon) clockText(cfg, ":", X0 + 2 * CELL + COLON / 2, L_TIME_Y, hc, false, H_CENTER);
  } else {
    int colonW = clockTextWidth(cfg, ":", false);
    int timeW = clockTextWidth(cfg, hs, false) + colonW + clockTextWidth(cfg, ms, false);
    int x = max(0, L_TIME_RIGHT - timeW);
    x += clockText(cfg, hs, x, L_TIME_Y, hc, false);
    if (colon) clockText(cfg, ":", x, L_TIME_Y, hc, false);
    x += colonW;
    clockText(cfg, ms, x, L_TIME_Y, mc, false);
  }

  // --- Condition label (white box) + seconds
  if (w.valid) {
    const char *label = condLabel(w, cfg);
    frame.fillRoundRect(L_LABEL_X, L_LABEL_Y, L_LABEL_W, L_LABEL_H, 3, C_WHITE);
    gmText(label, L_LABEL_X + L_LABEL_W / 2, L_LABEL_Y + (L_LABEL_H - VLW_TXT_CAP_H) / 2, C_LABEL_TXT, C_WHITE, L_LABEL_W - 2, H_CENTER);
  }

  char ss[4];
  snprintf(ss, sizeof(ss), "%02d", clk.s);
  clockText(cfg, ss, L_SEC_CX, L_SEC_Y, sc, true, H_CENTER);

  // --- GIF slot / Bitcoin coin (drawn before the texts below, which may overlap it)
  if (s_gifOpen) frame.pushImage(GIF_X, GIF_Y, GIF_SIZE, GIF_SIZE, s_gifCanvas);
  else drawCoinSlot(hashrate);

  // --- Date + weekday
  if (clk.valid) {
    String date = fmtDate(cfg, clk);
    int dw = gmTextOv(date.c_str(), L_DATE_X, L_DATE_Y, C_WHITE);
    gmTextOv(T.weekdays[clk.wday], L_DATE_X + dw + 13, L_DATE_Y, C_GREEN);
  } else {
    gmText("--/--/----", L_DATE_X, L_DATE_Y, C_GREY);
  }

  // --- Temperature + humidity rows (icons are 30x30, centred on the row)
  if (w.valid) {
    const int barOff = (VLW_TXT_CAP_H - L_BAR_H) / 2;
    drawThermometer(L_ICONS_X, L_ROW1_Y + VLW_TXT_CAP_H / 2 - 15);
    drawHumidityIcon(L_ICONS_X, L_ROW2_Y + VLW_TXT_CAP_H / 2 - 15);
    int tPct = (int)((w.temp - GM_TEMP_MIN) * 100.0f / (GM_TEMP_MAX - GM_TEMP_MIN));
    drawBar(L_BAR_X, L_ROW1_Y + barOff, L_BAR_W, L_BAR_H, tPct, tempColor(w.temp));
    drawBar(L_BAR_X, L_ROW2_Y + barOff, L_BAR_W, L_BAR_H, w.humidity, humColor(w.humidity));
    gmTextOv(fmtTemp(cfg, w.temp).c_str(), L_VAL_X, L_ROW1_Y, C_WHITE);
    gmTextOv((String(w.humidity) + "%").c_str(), L_VAL_X, L_ROW2_Y, C_WHITE);
  } else if (!cfg.city[0]) {
    gmText(T.weatherTab, L_ICONS_X + 4, L_ROW1_Y, C_WHITE, C_BLACK, GIF_X - 12);
    gmText(T.example, L_ICONS_X + 4, L_ROW2_Y, C_GREY, C_BLACK, GIF_X - 12);
  }

}

// ------------------------------------------------------------------ classic NerdMiner screens (320x170 -> 240x128)

static inline uint16_t be2le(uint16_t v) { return (v >> 8) | (v << 8); }

// Box-filter downscale of the legacy sprite into the frame (both sprites store byte-swapped RGB565)
static void scaleLegacyIntoFrame()
{
  uint16_t *src = (uint16_t *)legacy.getPointer();
  uint16_t *dst = (uint16_t *)frame.getPointer();
  if (!src || !dst) return;
  for (int oy = 0; oy < LEGACY_OUT_H; oy++) {
    int sy = (oy * (LEGACY_H - 1)) / (LEGACY_OUT_H - 1);
    int sy2 = min(sy + 1, LEGACY_H - 1);
    uint16_t *row = dst + (LEGACY_Y + oy) * WIDTH;
    for (int ox = 0; ox < WIDTH; ox++) {
      int sx = (ox * 4) / 3;
      int sx2 = min(sx + 1, LEGACY_W - 1);
      uint16_t p[4] = {be2le(src[sy * LEGACY_W + sx]), be2le(src[sy * LEGACY_W + sx2]),
                       be2le(src[sy2 * LEGACY_W + sx]), be2le(src[sy2 * LEGACY_W + sx2])};
      uint32_t r = 0, g = 0, b = 0;
      for (int i = 0; i < 4; i++) {
        r += p[i] >> 11;
        g += (p[i] >> 5) & 0x3F;
        b += p[i] & 0x1F;
      }
      row[ox] = be2le(((r / 4) << 11) | ((g / 4) << 5) | (b / 4));
    }
  }
}

// Header (time + date) and footer (IP + weather) around the scaled classic screens
// showClock = false on the Clock screen (it already shows the time in big digits)
static void drawLegacyBands(const GmConfig &cfg, const GmClock &clk, bool showClock)
{
  frame.fillRect(0, 0, WIDTH, LEGACY_Y, C_BLACK);
  frame.fillRect(0, LEGACY_Y + LEGACY_OUT_H, WIDTH, HEIGHT - LEGACY_Y - LEGACY_OUT_H, C_BLACK);

  // Header: HH:MM (small clock font, configured colours) + date / weekday
  char hs[4], ms[4];
  snprintf(hs, sizeof(hs), "%02d", displayHour(cfg, clk.h));
  snprintf(ms, sizeof(ms), "%02d", clk.m);
  const int hy = 12;
  if (showClock) {
    int x = 10;
    x += clockText(cfg, hs, x, hy, gm_rgb565(cfg.hourColor), true);
    int colonW = clockTextWidth(cfg, ":", true);
    if (!cfg.colonBlink || clk.s % 2 == 0) clockText(cfg, ":", x, hy, gm_rgb565(cfg.hourColor), true);
    x += colonW;
    clockText(cfg, ms, x, hy, gm_rgb565(cfg.minColor), true);
  }

  if (clk.valid) {
    String date = fmtDate(cfg, clk);
    gmText(date.c_str(), 232, 12, C_WHITE, C_BLACK, 130, H_RIGHT);
    gmText(texts(cfg).weekdays[clk.wday], 232, 32, C_GREEN, C_BLACK, 130, H_RIGHT);
  }

  // Footer: IP + current weather
  const int fy = LEGACY_Y + LEGACY_OUT_H;   // 184
  gmText(WiFi.localIP().toString().c_str(), 10, fy + 21, C_WHITE, C_BLACK, 136);
  GmWeather w = gm_weatherGet();
  if (w.valid) {
    drawWeatherIcon(w, 150, fy + 10, false);
    gmText(fmtTemp(cfg, w.temp).c_str(), 232, fy + 21, C_WHITE, C_BLACK, 60, H_RIGHT);
  }
}

static void composeLegacy(const GmConfig &cfg, const GmClock &clk, bool showClock = true)
{
  scaleLegacyIntoFrame();
  drawLegacyBands(cfg, clk, showClock);
}

static void legacyMinerScreen(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);
  legacy.pushImage(0, 0, MinerWidth, MinerHeight, MinerScreen);

  Serial.printf(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s\n",
                data.completedShares.c_str(), data.totalKHashes.c_str(), data.currentHashRate.c_str());

  lrender.setFontSize(35);
  lrender.setCursor(19, 118);
  lrender.setFontColor(TFT_BLACK);
  lrender.rdrawString(data.currentHashRate.c_str(), 118, 114, TFT_BLACK);
  lrender.setFontSize(18);
  lrender.rdrawString(data.totalMHashes.c_str(), 268, 138, TFT_BLACK);
  lrender.setFontSize(18);
  lrender.drawString(data.templates.c_str(), 186, 20, 0xDEDB);
  lrender.drawString(data.bestDiff.c_str(), 186, 48, 0xDEDB);
  lrender.setFontSize(18);
  lrender.drawString(data.completedShares.c_str(), 186, 76, 0xDEDB);
  lrender.setFontSize(14);
  lrender.rdrawString(data.timeMining.c_str(), 315, 104, 0xDEDB);
  lrender.setFontSize(24);
  lrender.drawString(data.valids.c_str(), 285, 56, 0xDEDB);
  lrender.setFontSize(10);
  lrender.rdrawString(data.temp.c_str(), 239, 1, TFT_BLACK);
  lrender.setFontSize(4);
  lrender.rdrawString(String(0).c_str(), 244, 3, TFT_BLACK);
  lrender.setFontSize(10);
  lrender.rdrawString(data.currentTime.c_str(), 286, 1, TFT_BLACK);
}

static void legacyClockScreen(unsigned long mElapsed)
{
  clock_data data = getClockData(mElapsed);
  legacy.pushImage(0, 0, minerClockWidth, minerClockHeight, minerClockScreen);

  lrender.setFontSize(25);
  lrender.setCursor(19, 122);
  lrender.setFontColor(TFT_BLACK);
  lrender.rdrawString(data.currentHashRate.c_str(), 94, 129, TFT_BLACK);

  legacy.setFreeFont(FSSB9);
  legacy.setTextSize(1);
  legacy.setTextDatum(TL_DATUM);
  legacy.setTextColor(TFT_BLACK);
  legacy.drawString(data.btcPrice.c_str(), 202, 3, GFXFF);

  lrender.setFontSize(18);
  lrender.rdrawString(data.blockHeight.c_str(), 254, 140, TFT_BLACK);

  legacy.setFreeFont(FF23);
  legacy.setTextSize(2);
  legacy.setTextColor(0xDEDB, TFT_BLACK);
  legacy.drawString(data.currentTime.c_str(), 130, 50, GFXFF);
}

static void legacyGlobalHashScreen(unsigned long mElapsed)
{
  coin_data data = getCoinData(mElapsed);
  legacy.pushImage(0, 0, globalHashWidth, globalHashHeight, globalHashScreen);

  legacy.setFreeFont(FSSB9);
  legacy.setTextSize(1);
  legacy.setTextDatum(TL_DATUM);
  legacy.setTextColor(TFT_BLACK);
  legacy.drawString(data.btcPrice.c_str(), 198, 3, GFXFF);
  legacy.drawString(data.currentTime.c_str(), 268, 3, GFXFF);

  legacy.setFreeFont(FSS9);
  legacy.setTextDatum(TR_DATUM);
  legacy.setTextColor(0x9C92);
  legacy.drawString(data.halfHourFee.c_str(), 302, 52, GFXFF);
  legacy.drawString(data.netwrokDifficulty.c_str(), 302, 88, GFXFF);

  lrender.setFontSize(17);
  lrender.rdrawString(data.globalHashRate.c_str(), 274, 145, TFT_BLACK);
  lrender.setFontSize(28);
  lrender.rdrawString(data.blockHeight.c_str(), 140, 104, 0xDEDB);

  int x2 = 2 + (138 * data.progressPercent / 100);
  legacy.fillRect(2, 149, x2, 168, 0xDEDB);

  legacy.setTextFont(FONT2);
  legacy.setTextSize(1);
  legacy.setTextDatum(MC_DATUM);
  legacy.setTextColor(TFT_BLACK);
  legacy.drawString(data.remainingBlocks.c_str(), 72, 159, FONT2);
}

static void legacyBTCprice(unsigned long mElapsed)
{
  clock_data data = getClockData(mElapsed);
  legacy.pushImage(0, 0, priceScreenWidth, priceScreenHeight, priceScreen);

  lrender.setFontSize(25);
  lrender.setCursor(19, 122);
  lrender.setFontColor(TFT_BLACK);
  lrender.rdrawString(data.currentHashRate.c_str(), 94, 129, TFT_BLACK);
  lrender.setFontSize(18);
  lrender.rdrawString(data.blockHeight.c_str(), 254, 138, TFT_WHITE);

  legacy.setFreeFont(FSSB9);
  legacy.setTextSize(1);
  legacy.setTextDatum(TL_DATUM);
  legacy.setTextColor(TFT_BLACK);
  legacy.drawString(data.currentTime.c_str(), 222, 3, GFXFF);

  legacy.setFreeFont(FF24);
  legacy.setTextDatum(TR_DATUM);
  legacy.setTextSize(1);
  legacy.setTextColor(0xDEDB, TFT_BLACK);
  legacy.drawString(data.btcPrice.c_str(), 300, 58, GFXFF);
}

// ------------------------------------------------------------------ popup

static char s_popup1[32] = "", s_popup2[40] = "";
static volatile uint32_t s_popupUntil = 0;
static volatile bool s_popupDirty = false;

void gm_displayPopup(const char *line1, const char *line2, uint32_t ms)
{
  strlcpy(s_popup1, line1 ? line1 : "", sizeof(s_popup1));
  strlcpy(s_popup2, line2 ? line2 : "", sizeof(s_popup2));
  s_popupUntil = millis() + ms;
  s_popupDirty = true;
}

#define POPUP_X 14
#define POPUP_Y 84
#define POPUP_W 212
#define POPUP_H 64
#define C_POPUP_BG 0x18E3

static bool popupActive() { return (int32_t)(s_popupUntil - millis()) > 0; }

static void drawPopup()
{
  if (!popupActive()) return;
  frame.fillRoundRect(POPUP_X, POPUP_Y, POPUP_W, POPUP_H, 8, C_POPUP_BG);
  frame.drawRoundRect(POPUP_X, POPUP_Y, POPUP_W, POPUP_H, 8, C_ORANGE);
  frame.drawRoundRect(POPUP_X + 1, POPUP_Y + 1, POPUP_W - 2, POPUP_H - 2, 7, C_ORANGE);
  bool two = s_popup2[0] != 0;
  gmText(s_popup1, 120, two ? POPUP_Y + 14 : POPUP_Y + (POPUP_H - VLW_TXT_CAP_H) / 2, C_ORANGE, C_POPUP_BG, POPUP_W - 12, H_CENTER);
  if (two) gmText(s_popup2, 120, POPUP_Y + 37, C_WHITE, C_POPUP_BG, POPUP_W - 12, H_CENTER);
}

// ------------------------------------------------------------------ screen dispatch

static int nextEnabled(const GmConfig &c, int from)
{
  for (int k = 1; k <= GM_SCREEN_COUNT; k++) {
    int i = (from + k) % GM_SCREEN_COUNT;
    if (c.screenMask & (1 << i)) return i;
  }
  return from;
}

static void gm_drawScreen(unsigned long mElapsed)
{
  GmConfig cfg = gm_cfgGet();
  GmClock clk = readClock();
  updateBacklight(cfg, clk.valid ? clk.h : -1);

  // Screen selection: explicit request > touch > auto switch > configured screen
  if (s_showRequested >= 0) {
    s_curScreen = s_showRequested;
    s_showRequested = -1;
    s_lastSwitchMs = millis();
  } else if (s_nextRequested) {
    s_nextRequested = false;
    s_curScreen = nextEnabled(cfg, s_curScreen);
    s_lastSwitchMs = millis();
    if (!cfg.autoSwitch) {
      cfg.screen = s_curScreen;
      gm_cfgSet(cfg);
    }
  } else if (cfg.autoSwitch) {
    if (!(cfg.screenMask & (1 << s_curScreen))) s_curScreen = nextEnabled(cfg, s_curScreen);
    uint32_t period = max<uint32_t>(cfg.switchSec, 5) * 1000UL;
    if (millis() - s_lastSwitchMs >= period) {
      s_curScreen = nextEnabled(cfg, s_curScreen);
      s_lastSwitchMs = millis();
    }
  } else {
    s_curScreen = cfg.screen < GM_SCREEN_COUNT ? cfg.screen : GM_SCREEN_WEATHER;
  }
  currentDisplayDriver->current_cyclic_screen = s_curScreen;

  switch (s_curScreen) {
    case GM_SCREEN_MINER: legacyMinerScreen(mElapsed); composeLegacy(cfg, clk); break;
    case GM_SCREEN_CLOCK: legacyClockScreen(mElapsed); composeLegacy(cfg, clk, false); break;
    case GM_SCREEN_GLOBAL: legacyGlobalHashScreen(mElapsed); composeLegacy(cfg, clk); break;
    case GM_SCREEN_BTC: legacyBTCprice(mElapsed); composeLegacy(cfg, clk); break;
    default:
      gifReloadIfNeeded(cfg);
      drawWeatherScreen(mElapsed, cfg, clk);
      break;
  }
  drawPopup();
  gmPushAll();
}

// ------------------------------------------------------------------ touch button

static OneButton s_touchBtn;
static uint32_t s_touchBase = 0;
static uint32_t s_touchRaw = 0;

static void onTouchClick() { gm_displayNextScreen(); }
static void onTouchDoubleClick() { gm_webToggle(); }

static void onTouchTripleClick()
{
  s_screenOff = !s_screenOff;
  setBacklight(s_screenOff ? 0 : gm_cfgGet().brightness);
}
static void onTouchLongPress() { reset_configuration(); }

static void touchInit()
{
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) { sum += touchRead(GM_TOUCH_PIN); delay(5); }
  s_touchBase = sum / 16;
  s_touchBtn.setClickMs(350);
  s_touchBtn.setPressMs(6000); // long touch (6 s) = factory reset of NerdMiner config, like the other boards
  s_touchBtn.attachClick(onTouchClick);
  s_touchBtn.attachDoubleClick(onTouchDoubleClick);
  s_touchBtn.attachMultiClick([]() { if (s_touchBtn.getNumberClicks() == 3) onTouchTripleClick(); });
  s_touchBtn.attachLongPressStart(onTouchLongPress);
  Serial.printf("[GM] Touch baseline %lu\n", (unsigned long)s_touchBase);
}

void gm_touchTick()
{
  static bool state = false;
  static uint8_t streak = 0;
  s_touchRaw = touchRead(GM_TOUCH_PIN);
  bool touched = s_touchRaw > s_touchBase + GM_TOUCH_DELTA;
  if (!touched) s_touchBase = (s_touchBase * 63 + s_touchRaw) / 64; // follow slow drift
  // ignore isolated spikes (e.g. backlight switching): the new state must hold for GM_TOUCH_STABLE samples
  if (touched != state) {
    if (++streak >= GM_TOUCH_STABLE) { state = touched; streak = 0; }
  } else {
    streak = 0;
  }
  if (s_screenOff && state) {
    // any touch wakes the display; swallow this press so it is not counted as a click
    s_screenOff = false;
    s_touchBtn.reset();
    while (touchRead(GM_TOUCH_PIN) > s_touchBase + GM_TOUCH_DELTA) delay(20);
    state = false;
    return;
  }
  s_touchBtn.tick(state);
}

uint32_t gm_touchRaw() { return s_touchRaw; }
uint32_t gm_touchBaseline() { return s_touchBase; }

// ------------------------------------------------------------------ public API

void gm_displayShowScreen(uint8_t screen)
{
  if (screen >= GM_SCREEN_COUNT) return;
  GmConfig c = gm_cfgGet();
  if (c.screen != screen) {
    c.screen = screen;
    gm_cfgSet(c);
  }
  s_showRequested = screen;
}

void gm_displayNextScreen() { s_nextRequested = true; }
uint8_t gm_displayCurrentScreen() { return s_curScreen; }
void gm_displaySetOn(bool on) { s_screenOff = !on; }   // applied on the next redraw (<= 1 s)
bool gm_displayIsOn() { return !s_screenOff; }
void gm_displayReloadGif() { s_gifReload = true; }
size_t gm_builtinGifCount() { return GM_BUILTIN_GIF_COUNT; }
const char *gm_builtinGifName(size_t i) { return i < GM_BUILTIN_GIF_COUNT ? GM_BUILTIN_GIFS[i].name : ""; }
const uint8_t *gm_builtinGifData(size_t i, size_t *len)
{
  if (i >= GM_BUILTIN_GIF_COUNT) { *len = 0; return nullptr; }
  *len = GM_BUILTIN_GIFS[i].size;
  return GM_BUILTIN_GIFS[i].data;
}
const uint16_t *gm_displayFrame() { return (const uint16_t *)frame.getPointer(); }

// ------------------------------------------------------------------ driver callbacks

static void gm_Init(void)
{
  gm_cfgLoad();
  GmConfig cfg = gm_cfgGet();

  tft.init();
  tft.setRotation(0);
  tft.setSwapBytes(true);
  tft.fillScreen(C_BLACK);

#if GM_SOFT_DIM
  pinMode(TFT_BL, OUTPUT);
  s_dimBuf = (uint16_t *)ps_malloc(WIDTH * HEIGHT * 2);
#else
  ledcSetup(BL_PWM_CHANNEL, 5000, BL_PWM_BITS);
  ledcAttachPin(TFT_BL, BL_PWM_CHANNEL);
#endif
  setBacklight(cfg.brightness);

  frame.setColorDepth(16);
  legacy.setColorDepth(16);
  if (!frame.createSprite(WIDTH, HEIGHT)) Serial.println("[GM] frame sprite alloc failed");
  if (!legacy.createSprite(LEGACY_W, LEGACY_H)) Serial.println("[GM] legacy sprite alloc failed");
  frame.setSwapBytes(true);
  legacy.setSwapBytes(true);

  lrender.setDrawer(legacy);
  lrender.setLineSpaceRatio(0.9);
  if (lrender.loadFont(DigitalNumbers, sizeof(DigitalNumbers))) Serial.println("[GM] font error (digital)");
  txt.setDrawer(frame);
  if (txt.loadFont(gm_text_font, sizeof(gm_text_font))) Serial.println("[GM] font error (text)");
  num.setDrawer(frame);
  ensureNumFont(cfg);

  s_gifCanvas = (uint16_t *)ps_malloc(GIF_SIZE * GIF_SIZE * 2);
  void *mem = ps_malloc(sizeof(AnimatedGIF));
  if (mem) {
    s_gif = new (mem) AnimatedGIF();
    s_gif->begin(GIF_PALETTE_RGB565_LE);
  }

  s_curScreen = cfg.screen < GM_SCREEN_COUNT ? cfg.screen : GM_SCREEN_WEATHER;
  touchInit();
}

static void gm_AlternateScreenState(void)
{
  onTouchTripleClick();
}

static void gm_AlternateRotation(void)
{
  tft.setRotation((tft.getRotation() + 2) % 4);
}

static void drawLegacyStatic(const unsigned short *img, uint16_t w, uint16_t h)
{
  legacy.fillSprite(C_BLACK);
  legacy.pushImage(0, 0, w, h, img);
  scaleLegacyIntoFrame();
  frame.fillRect(0, 0, WIDTH, LEGACY_Y, C_BLACK);
  frame.fillRect(0, LEGACY_Y + LEGACY_OUT_H, WIDTH, HEIGHT - LEGACY_Y - LEGACY_OUT_H, C_BLACK);
}

static void gm_LoadingScreen(void)
{
  drawLegacyStatic(initScreen, initWidth, initHeight);
  txt.setAlignment(Align::TopCenter);
  txt.setFontSize(22);
  txt.drawString("NerdMiner", 120, 14, C_ORANGE, C_BLACK);
  txt.setFontSize(16);
  txt.drawString(CURRENT_VERSION " - GeekMagic S3", 120, 196, C_WHITE, C_BLACK);
  txt.setAlignment(Align::TopLeft);
  gmPushAll();
}

static void gm_SetupScreen(void)
{
  drawLegacyStatic(setupModeScreen, setupModeWidth, setupModeHeight);
  txt.setAlignment(Align::TopCenter);
  txt.setFontSize(20);
  txt.drawString("WiFi setup", 120, 16, C_ORANGE, C_BLACK);
  txt.setFontSize(15);
  txt.drawString("AP: " DEFAULT_SSID "  Pass: " DEFAULT_WIFIPW, 120, 192, C_WHITE, C_BLACK);
  txt.drawString("Then open http://192.168.4.1", 120, 214, C_GREEN, C_BLACK);
  txt.setAlignment(Align::TopLeft);
  gmPushAll();
}

static void gm_AnimateCurrentScreen(unsigned long frameNo)
{
  if (s_popupDirty) {
    s_popupDirty = false;
    drawPopup();
    gmPush(POPUP_X, POPUP_Y, POPUP_W, POPUP_H);
  }
  if (s_curScreen != GM_SCREEN_WEATHER || !s_gifOpen) return;
  if (gifStep()) {
    frame.pushImage(GIF_X, GIF_Y, GIF_SIZE, GIF_SIZE, s_gifCanvas);
    for (int i = 0; i < s_ovlN; i++) gmText(s_ovl[i].s, s_ovl[i].x, s_ovl[i].capY, s_ovl[i].color);
    gmPush(GIF_X, GIF_Y, GIF_SIZE, GIF_SIZE);
  }
}

static void gm_DoLedStuff(unsigned long frameNo)
{
}

// Every slot points to the dispatcher; the real screen is chosen from the GeekMagic config
CyclicScreenFunction geekmagicS3CyclicScreens[] = {gm_drawScreen, gm_drawScreen, gm_drawScreen, gm_drawScreen, gm_drawScreen};

DisplayDriver geekmagicS3DisplayDriver = {
    gm_Init,
    gm_AlternateScreenState,
    gm_AlternateRotation,
    gm_LoadingScreen,
    gm_SetupScreen,
    geekmagicS3CyclicScreens,
    gm_AnimateCurrentScreen,
    gm_DoLedStuff,
    SCREENS_ARRAY_SIZE(geekmagicS3CyclicScreens),
    0,
    WIDTH,
    HEIGHT};

#endif
