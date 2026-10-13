#pragma once
// Design tokens, safe-area contract and shared styles for the round 390x390 AMOLED UI.
// Source of truth for geometry: docs/ui-upgrade/mockups.html. See docs/PLAN_ui_upgrade.md.

#include "lvgl.h"

// ---- Colors (flat on purpose: RGB565 bands on gradients) ----
#define UI_COLOR_BG           0x000000  // screen and tiles (true black on AMOLED)
#define UI_COLOR_SURFACE      0x16181B  // idle icon disc, inactive arc track
#define UI_COLOR_TEXT         0xE6E6E6  // hero value
#define UI_COLOR_TEXT_DIM     0x8A8F98  // name, secondary text
#define UI_COLOR_INACTIVE     0x4A4F57  // icon when off
#define UI_COLOR_ACCENT_LIGHT 0xFFB300  // light / switch / fan on
#define UI_COLOR_ACCENT_HEAT  0xFF7A1A  // climate heating
#define UI_COLOR_ACCENT_COOL  0x3FA7FF  // climate cooling
#define UI_COLOR_ACCENT_COVER 0x42A5F5  // cover
#define UI_COLOR_ACCENT_OK    0x4CAF50  // sensor / positive state
#define UI_COLOR_ERROR        0xE5484D  // unavailable / call failed

// ---- Safe area on the round panel (device pixels, center 195,195) ----
// Half-width of the circle at distance d from the center: sqrt(195^2 - d^2).
#define UI_SCREEN_SIZE        390
#define UI_CENTER             195
#define UI_RING_RADIUS        175   // control ring, 270 degrees with the gap at the bottom
#define UI_RING_START_ANGLE   135
#define UI_RING_SWEEP         270
#define UI_RING_STROKE        12
#define UI_RING_STROKE_EDIT   18
#define UI_DISC_RADIUS        64    // icon disc (128 px)
#define UI_DOTS_RADIUS        172   // indicator dots on the bottom bezel
#define UI_DOTS_SPACING_DEG   7
#define UI_DOT_SIZE           6
#ifndef UI_DOTS_MAX
#define UI_DOTS_MAX           12    // above this the dots become an "n / N" caption
#endif
#define UI_COLOR_DOT_IDLE     0x2A2D32

// Text baselines (y from the top of the 390 px face) and the width usable there
#define UI_Y_NAME_PLAIN       74    // name on tiles without a ring (light, switch), ~190 px wide
#define UI_Y_NAME_RING        98    // name on tiles with a ring (cover, thermostat, fan), ~270 px wide
#define UI_Y_HERO_MIN         205   // hero value zone 205..232, ~340 px wide
#define UI_Y_HERO_MAX         232
#define UI_Y_CAPTION_MIN      252   // state / caption zone 252..304, ~260 px wide
#define UI_Y_CAPTION_MAX      304
#define UI_Y_COUNTER          362   // "n / N" caption
#define UI_Y_MIN_TEXT         48    // no text above this
#define UI_W_NAME_PLAIN       190
#define UI_W_NAME_RING        270
#define UI_W_HERO             340
#define UI_W_CAPTION          260
#define UI_CONTROL_DIAMETER   300   // the main control stays inside this circle

// ---- Fonts ----
// Built-in Montserrat 14 / 18 / 26 for text; digits-only 56 / 64 for hero values
// (glyph set "0123456789.-+ %/°", anything else must fall back to 26); MDI subset for icons.
LV_FONT_DECLARE(ui_font_digits_56)
LV_FONT_DECLARE(ui_font_digits_64)
LV_FONT_DECLARE(ui_font_mdi_26)   // cover buttons: chevrons and stop only
LV_FONT_DECLARE(ui_font_mdi_36)   // tile header / sensor icons
LV_FONT_DECLARE(ui_font_mdi_64)   // disc icon

#define UI_FONT_CAPTION  (&lv_font_montserrat_14)
#define UI_FONT_NAME     (&lv_font_montserrat_18)
#define UI_FONT_SECOND   (&lv_font_montserrat_26)
#define UI_FONT_HERO     (&ui_font_digits_56)
#define UI_FONT_HERO_BIG (&ui_font_digits_64)
#define UI_FONT_ICON_XS  (&ui_font_mdi_26)
#define UI_FONT_ICON_S   (&ui_font_mdi_36)
#define UI_FONT_ICON_L   (&ui_font_mdi_64)

// ---- Shared styles (initialised by uiCreateStyles() in ui_tiles.cpp) ----
extern lv_style_t style_tile_bg;
extern lv_style_t style_title;
extern lv_style_t style_icon;
extern lv_style_t style_value_large;
extern lv_style_t style_value_small;
extern lv_style_t style_sensor_value;
extern lv_style_t style_sensor_unit;
extern lv_style_t style_button;
extern lv_style_t style_slider;
extern lv_style_t style_icon_mdi;   // MDI glyph, small

// ---- Chrome shared by all tiles (ui_chrome.cpp) ----
void uiCreateDots(int count);        // count decided once, at UI build time
void uiUpdateDots(int active);
void uiShowToast(const char* text, uint32_t durationMs = 1200);
