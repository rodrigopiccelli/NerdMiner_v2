#pragma once
#ifdef GEEKMAGIC_S3

#include <Arduino.h>

// API exposed by the GeekMagic S3 display driver (geekmagicS3DisplayDriver.cpp)

// Show a screen now (also stores it as the default screen when auto switch is off)
void gm_displayShowScreen(uint8_t screen);
// Go to the next enabled screen (touch button)
void gm_displayNextScreen();
// Display on/off (same as the triple tap)
void gm_displaySetOn(bool on);
bool gm_displayIsOn();
// Index of the screen currently shown
uint8_t gm_displayCurrentScreen();
// (Re)open the GIF selected in the config
void gm_displayReloadGif();
// Raw 240x240 frame buffer (RGB565, byte-swapped as sent to the panel) for screenshots
const uint16_t *gm_displayFrame();
// Show a centred popup (two lines) for `ms` milliseconds (thread-safe, drawn by the display task)
void gm_displayPopup(const char *line1, const char *line2, uint32_t ms = 2500);
// Poll the capacitive touch button (call from loop())
void gm_touchTick();
// Built-in GIFs (names without the "*" prefix)
size_t gm_builtinGifCount();
const char *gm_builtinGifName(size_t i);
const uint8_t *gm_builtinGifData(size_t i, size_t *len);
// Touch diagnostics
uint32_t gm_touchRaw();
uint32_t gm_touchBaseline();

#endif
