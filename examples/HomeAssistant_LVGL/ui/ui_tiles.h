#pragma once
// Tile creation and update. Pure LVGL: no networking, no encoder state.

#include "lvgl.h"
#include <Arduino.h>

void uiCreateStyles();
void createEntityTile(int entityIndex, lv_obj_t* tileview);
void updateEntityTile(int entityIndex);
void uiSetToggleState(int entityIndex, bool on);  // visual + text, for encoder presses
void uiSyncControl(int entityIndex);              // ring and hero from the entity values
void uiSetCoverTarget(int entityIndex, int value);
void uiSetEditMode(int entityIndex, bool on);

// Provided by the sketch
void handleEntityAction(int entityIndex, String action, String value = "", unsigned long debounceMs = 0);
void setCoverPosition(int entityIndex, int value, bool fromTouch);
void setFanSpeed(int entityIndex, int value, bool fromTouch);
void setTargetTemp(int entityIndex, float temp);
bool userHoldsControl(int entityIndex);
