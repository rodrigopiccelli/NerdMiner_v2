#ifdef GEEKMAGIC_S3

#include "gmConfig.h"
#include <Preferences.h>

static const char *GM_NVS_NS = "gmcfg";
static const char *GM_NVS_KEY = "cfg";

static GmConfig s_cfg;
static SemaphoreHandle_t s_mutex = nullptr;
static bool s_loaded = false;

static void ensureMutex()
{
  if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
}

void gm_cfgLoad()
{
  ensureMutex();
  if (s_loaded) return;

  Preferences prefs;
  GmConfig cfg;
  if (prefs.begin(GM_NVS_NS, true)) {
    size_t len = prefs.getBytesLength(GM_NVS_KEY);
    if (len == sizeof(GmConfig)) {
      GmConfig stored;
      prefs.getBytes(GM_NVS_KEY, &stored, sizeof(stored));
      if (stored.version == GM_CFG_VERSION) cfg = stored;
    }
    prefs.end();
  }
  // Make sure strings are terminated even if NVS data is corrupted
  cfg.city[sizeof(cfg.city) - 1] = 0;
  cfg.owmKey[sizeof(cfg.owmKey) - 1] = 0;
  cfg.ntp[sizeof(cfg.ntp) - 1] = 0;
  cfg.gif[sizeof(cfg.gif) - 1] = 0;
  if (cfg.screen >= GM_SCREEN_COUNT) cfg.screen = GM_SCREEN_WEATHER;
  if ((cfg.screenMask & 0x1F) == 0) cfg.screenMask = 0x1F;
  if (cfg.font >= GM_FONT_COUNT) cfg.font = GM_FONT_GEEK;
  if (cfg.dateFmt >= GM_DATE_COUNT) cfg.dateFmt = GM_DATE_D_M_Y;

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_cfg = cfg;
  s_loaded = true;
  xSemaphoreGive(s_mutex);
}

GmConfig gm_cfgGet()
{
  ensureMutex();
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  GmConfig copy = s_cfg;
  xSemaphoreGive(s_mutex);
  return copy;
}

void gm_cfgSet(const GmConfig &cfg)
{
  ensureMutex();
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_cfg = cfg;
  s_cfg.version = GM_CFG_VERSION;
  GmConfig copy = s_cfg;
  xSemaphoreGive(s_mutex);

  Preferences prefs;
  if (prefs.begin(GM_NVS_NS, false)) {
    prefs.putBytes(GM_NVS_KEY, &copy, sizeof(copy));
    prefs.end();
  }
}

void gm_cfgErase()
{
  Preferences prefs;
  if (prefs.begin(GM_NVS_NS, false)) {
    prefs.clear();
    prefs.end();
  }
}

uint16_t gm_rgb565(uint32_t rgb)
{
  uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

#endif
