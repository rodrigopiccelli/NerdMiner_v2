#pragma once
#ifdef GEEKMAGIC_S3

#include <Arduino.h>

// Local time helpers on top of NerdMiner's NTPClient (monitor.cpp)

// true once NTP returned a plausible time
bool gm_timeValid();
// Local epoch (UTC + configured offset), seconds
uint32_t gm_localEpoch();
// Current UTC offset applied to the clock (seconds)
int32_t gm_currentOffset();
// Apply NTP server + UTC offset from the GeekMagic config / weather location
void gm_applyTimeSettings();

#endif
