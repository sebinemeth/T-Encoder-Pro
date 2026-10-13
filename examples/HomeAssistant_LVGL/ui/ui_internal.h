#pragma once
// Helpers shared by the tile implementations (ui_tiles.cpp, ui_rings.cpp). Not part of the
// sketch-facing API in ui_tiles.h.

#include "lvgl.h"
#include "entities_types.h"

// Distance from the top of a label to its text baseline
int uiBaselineOffset(const lv_font_t* font);

// Single-line, centered, ellipsized label whose text baseline sits at y (device pixels,
// 0 = top of the 390 px face) and which is at most `width` wide.
lv_obj_t* uiCreateLine(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color,
                       int baselineY, int width);
void uiSetLineText(lv_obj_t* label, const char* text);

// Ring tiles (ui_rings.cpp)
void createCoverTile(int entityIndex);
void createThermostatTile(int entityIndex);
void createFanTile(int entityIndex);
void uiUpdateCover(int entityIndex);
void uiUpdateThermostat(int entityIndex);
void uiUpdateFan(int entityIndex);
