#pragma once
#ifdef GEEKMAGIC_S3

// GeekMagic S3 extras: web configuration UI + weather service.
// Display/touch live in drivers/displays/geekmagicS3DisplayDriver.cpp

void gm_webStart();                // register routes (once)
void gm_webLoop();
void gm_webSetEnabled(bool on);     // start / stop the HTTP server (shows a popup)
void gm_webToggle();
void gm_webEnableAtBoot();         // on at boot, no idle auto-off
bool gm_webEnabled();

// Call once at the end of setup() (WiFi connected, monitor initialised)
void gm_begin();
// Call from loop()
void gm_loop();

#endif
