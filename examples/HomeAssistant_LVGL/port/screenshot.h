#pragma once

// Debug HTTP endpoint: GET http://<device-ip>/screenshot.bmp returns the active
// LVGL screen as a 24-bit BMP. Unauthenticated, so LAN debugging only.
// GET /tile?i=<n>[&edit=1] jumps to a tile (and optionally edit mode) through the
// sketch-provided debugGoToTile() hook, so UI work is testable remotely.
// Disable with -D ENABLE_SCREENSHOT_SERVER=0.
#ifndef ENABLE_SCREENSHOT_SERVER
#define ENABLE_SCREENSHOT_SERVER 1
#endif

#if ENABLE_SCREENSHOT_SERVER
void debugGoToTile(int index, bool edit);  // implemented by the sketch
void screenshotBegin();  // call once WiFi is connected
void screenshotLoop();   // call from loop(), main task only
#else
inline void screenshotBegin() {}
inline void screenshotLoop() {}
#endif
