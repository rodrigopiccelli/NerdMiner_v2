#ifdef GEEKMAGIC_S3

#include "gm.h"
#include "gmConfig.h"
#include "gmWeather.h"
#include "gmTime.h"
#include "gmDisplay.h"
#include <WiFi.h>

void gm_begin()
{
  gm_cfgLoad();
  gm_applyTimeSettings();
  gm_weatherStart();
  gm_webStart();
  gm_webEnableAtBoot(); // shows "Web Config On" + IP; double tap toggles it
}

void gm_loop()
{
  gm_touchTick();
  gm_webLoop();
}

#endif
