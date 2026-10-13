#pragma once

// Debug HTTP endpoint: GET http://<device-ip>/screenshot.bmp returns the active
// LVGL screen as a 24-bit BMP. Unauthenticated, so LAN debugging only.
// Disable with -D ENABLE_SCREENSHOT_SERVER=0.
#ifndef ENABLE_SCREENSHOT_SERVER
#define ENABLE_SCREENSHOT_SERVER 1
#endif

#if ENABLE_SCREENSHOT_SERVER
void screenshotBegin();  // call once WiFi is connected
void screenshotLoop();   // call from loop(), main task only
#else
inline void screenshotBegin() {}
inline void screenshotLoop() {}
#endif
