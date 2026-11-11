#ifdef GEEKMAGIC_S3

#include "gmTime.h"
#include "gmConfig.h"
#include "gmWeather.h"
#include <NTPClient.h>
#include <WiFi.h>
#include "drivers/storage/storage.h"

// Owned by monitor.cpp
extern NTPClient timeClient;
extern unsigned long mTriggerUpdate;
extern unsigned long initialTime;
extern TSettings Settings;

static int32_t s_appliedOffset = INT32_MIN;
static char s_ntpServer[48] = "";

bool gm_timeValid()
{
  return mTriggerUpdate != 0 && initialTime > 1600000000UL;
}

uint32_t gm_localEpoch()
{
  return initialTime + (millis() - mTriggerUpdate) / 1000;
}

int32_t gm_currentOffset()
{
  return s_appliedOffset == INT32_MIN ? Settings.Timezone * 3600 : s_appliedOffset;
}

void gm_applyTimeSettings()
{
  GmConfig cfg = gm_cfgGet();

  // NTP server (NTPClient keeps the pointer, so use a static buffer)
  if (strcmp(cfg.ntp, s_ntpServer) != 0) {
    strlcpy(s_ntpServer, cfg.ntp, sizeof(s_ntpServer));
    timeClient.setPoolServerName(s_ntpServer[0] ? s_ntpServer : "europe.pool.ntp.org");
    mTriggerUpdate = 0; // force a new NTP sync on next getTime()
  }

  int32_t offset;
  GmWeather w = gm_weatherGet();
  if (cfg.tzAuto) {
    offset = w.hasOffset ? w.utcOffset : Settings.Timezone * 3600;
  } else {
    offset = (int32_t)cfg.tzOffsetMin * 60;
  }

  if (offset != s_appliedOffset) {
    s_appliedOffset = offset;
    timeClient.setTimeOffset(offset);
    if (mTriggerUpdate != 0) {
      // Re-base NerdMiner's software clock without waiting for the next NTP sync
      initialTime = timeClient.getEpochTime();
      mTriggerUpdate = millis();
    }
    Serial.printf("[GM] UTC offset set to %ld s\n", (long)offset);
  }
}

#endif
