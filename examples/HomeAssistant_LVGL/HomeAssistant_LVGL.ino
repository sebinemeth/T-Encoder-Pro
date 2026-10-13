/*
 * @Description: Multi-Entity LVGL-based Home Assistant Controller with Tileview for T-Encoder Pro
 * @Author: WARP
 * @Date: 2024-10-06
 * @License: GPL 3.0
 * @Features: Swipeable tileview layout with entity-specific controls
 */
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "lvgl.h"
#include "port/lv_port_disp.h"
#include "port/lv_port_indev.h"
#include "port/encoder.h"
#include "pin_config.h"
#include "ha_config.h"
#if __has_include("ha_config_private.h")
#include "ha_config_private.h" // Override defaults with actual values
#else
#warning "ha_config_private.h not found: copy ha_config_private.h.example and fill in your WiFi/HA credentials"
#endif
#include "ha_websocket_config.h"
#include "ha_websocket_manager_v2.h"
#include "entities.h"
#include "service_call.h"

// Global UI variables
lv_obj_t *tileview;
lv_obj_t *tiles[ENTITY_COUNT];
int currentTileIndex = 0;
int lastTileIndex = -1;

// WiFi overlay and indicator dots
lv_obj_t *wifi_overlay;
lv_obj_t *wifi_spinner;
lv_obj_t *wifi_status_label;
lv_obj_t *indicator_dots[ENTITY_COUNT];
lv_obj_t *indicator_container;

// Global connection variables  
bool connectionEstablished = false;
String lastError = "";
const unsigned long wifiRetryInterval = 30000;

// WebSocket manager
HAWebSocketManagerV2 wsManager;

// Pending commands: one slot per entity, latest value wins.
// Slider/arc changes are debounced so only the settled value is sent.
struct PendingCommand {
    bool active = false;
    String action;
    String value;
    unsigned long dueAt = 0;
};
PendingCommand pendingCommands[ENTITY_COUNT];
unsigned long lastUserInteraction[ENTITY_COUNT] = {};
const unsigned long valueCommandDebounce = 250;  // ms after the last slider/arc change
const unsigned long uiSyncHoldoff = 1500;        // ms: don't overwrite a control the user just touched
const float thermostatStep = 0.5f;               // arc resolution in degrees

// WiFi state (non-blocking)
unsigned long wifiAttemptStart = 0;
unsigned long wifiOverlayHideAt = 0;
bool wifiWasConnected = false;

// Encoder input: rotation navigates tiles; a short press toggles (switches) or enters
// edit mode (cover, thermostat, fan), where rotation changes the value. A long press
// stops a cover. Edit mode ends on press, tile change, or after a period of inactivity.
bool editMode = false;
unsigned long editLastActivity = 0;
const unsigned long editTimeout = 5000;
const int coverEditStep = 5;     // % per detent
const int fanEditStep = 10;      // % per detent
float editValue = 0;             // value being dialled in; HA updates must not move it

// Tileview scroll animations can emit a stale VALUE_CHANGED; ignore events that
// disagree with an encoder-initiated move for a short while.
int programmaticTile = -1;
unsigned long programmaticUntil = 0;

// Styles
lv_style_t style_tile_bg;
lv_style_t style_title;
lv_style_t style_icon;
lv_style_t style_value_large;
lv_style_t style_value_small;
lv_style_t style_sensor_value;  // Very large font for sensor values
lv_style_t style_sensor_unit;   // Large font for sensor units
lv_style_t style_button;
lv_style_t style_slider;

// Forward declarations
void createEntityTile(int entityIndex);
void updateEntityTile(int entityIndex);
void handleEntityAction(int entityIndex, String action, String value = "", unsigned long debounceMs = 0);
void processAPIRequest();
void startWiFi();
void serviceWiFi();
String getDisplaySymbol(String iconName);
void setEntityTypeFromDomain(int entityIndex);
void createWiFiOverlay();
void showWiFiOverlay(bool show);
void updateWiFiOverlayStatus(String status);
void createIndicatorDots();
void updateIndicatorDots();
void exitEditMode();

// Initialize entities with proper defaults
void initializeEntities() {
    Serial.println("Initializing entities with extended fields...");
    for (int i = 0; i < ENTITY_COUNT; i++) {
        Serial.println("Entity " + String(i) + ": " + String(entities[i].name) + " (" + String(entities[i].entity_id) + ")");
        
        // Set entity type and initialize defaults based on domain
        setEntityTypeFromDomain(i);
        
        // Initialize all entity fields to defaults
        entities[i].iconFetched = false;
        entities[i].tile = nullptr;
        entities[i].wControl = entities[i].wValue = entities[i].wTarget = entities[i].wStatus = entities[i].wUnit = entities[i].wActual = nullptr;
        entities[i].tileCreated = false;
        
        // Initialize type-specific fields
        switch (entities[i].type) {
            case ENTITY_TOGGLE:
                entities[i].state = false;
                entities[i].lastState = false;
                break;
            case ENTITY_COVER:
                entities[i].position = 0;
                entities[i].lastPosition = -1;
                entities[i].coverState = "unknown";
                break;
            case ENTITY_THERMOSTAT:
                entities[i].currentTemp = 20.0;
                entities[i].lastCurrentTemp = 20.0;
                entities[i].targetTemp = 21.0;
                entities[i].lastTargetTemp = 20.0;
                entities[i].minTemp = 4.0;  // Default minimum temperature
                entities[i].maxTemp = 35.0; // Default maximum temperature
                entities[i].hvacMode = "off";
                entities[i].hvacAction = "idle";
                break;
            case ENTITY_SENSOR:
                entities[i].sensorValue = "N/A";
                entities[i].lastSensorValue = "";
                entities[i].sensorUnit = "";
                break;
            case ENTITY_FAN:
                entities[i].fanSpeed = 0;
                entities[i].lastFanSpeed = -1;
                break;
        }
    }
    Serial.println("Loaded " + String(ENTITY_COUNT) + " entities from static configuration");
}

// Determine entity type from its domain
void setEntityTypeFromDomain(int entityIndex) {
    entities[entityIndex].type = entityTypeForDomain(getEntityType(entities[entityIndex].entity_id));
}

// Convert icon name to displayable symbol
String getDisplaySymbol(String iconName) {
    // Built-in symbol mapping for common Home Assistant icons
    if (iconName == "mdi:home-automation") return LV_SYMBOL_HOME;
    if (iconName == "mdi:toggle-switch") return LV_SYMBOL_SETTINGS;
    if (iconName == "mdi:light-switch") return LV_SYMBOL_POWER;
    if (iconName == "mdi:lightbulb") return LV_SYMBOL_EYE_OPEN;
    if (iconName == "mdi:radiobox-marked") return LV_SYMBOL_BULLET;
    if (iconName == "mdi:gauge") return LV_SYMBOL_CHARGE;
    if (iconName == "mdi:fan") return LV_SYMBOL_REFRESH;
    if (iconName == "mdi:thermostat") return LV_SYMBOL_LOOP;
    if (iconName == "mdi:help-circle") return LV_SYMBOL_WARNING;
    
    // Cover icons
    if (iconName == "mdi:window-shutter") return LV_SYMBOL_UP;
    if (iconName == "mdi:blinds") return LV_SYMBOL_UP;
    if (iconName == "mdi:garage") return LV_SYMBOL_UP;
    if (iconName == "mdi:door") return LV_SYMBOL_UP;
    
    // Thermostat icons
    if (iconName == "mdi:thermometer") return LV_SYMBOL_LOOP;
    
    // Default fallback symbol
    return LV_SYMBOL_DUMMY;
}

// Create styles for the tileview interface
void createStyles() {
    // Tile background style
    lv_style_init(&style_tile_bg);
    lv_style_set_bg_color(&style_tile_bg, lv_color_hex(0x000000));
    lv_style_set_pad_all(&style_tile_bg, 10);
    
    // Title style
    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, lv_color_white());
    lv_style_set_text_font(&style_title, &lv_font_montserrat_18);
    
    // Icon style
    lv_style_init(&style_icon);
    lv_style_set_text_color(&style_icon, lv_color_hex(0x4CAF50));
    lv_style_set_text_font(&style_icon, &lv_font_montserrat_18);
    
    // Large value style
    lv_style_init(&style_value_large);
    lv_style_set_text_color(&style_value_large, lv_color_white());
    lv_style_set_text_font(&style_value_large, &lv_font_montserrat_18);
    
    // Small value style
    lv_style_init(&style_value_small);
    lv_style_set_text_color(&style_value_small, lv_color_hex(0xCCCCCC));
    lv_style_set_text_font(&style_value_small, &lv_font_montserrat_14);
    
    // Very large sensor value style (use largest available font)
    lv_style_init(&style_sensor_value);
    lv_style_set_text_color(&style_sensor_value, lv_color_white());
    lv_style_set_text_font(&style_sensor_value, &lv_font_montserrat_26);
    
    // Large sensor unit style (use largest available smaller font)
    lv_style_init(&style_sensor_unit);
    lv_style_set_text_color(&style_sensor_unit, lv_color_hex(0x888888));
    lv_style_set_text_font(&style_sensor_unit, &lv_font_montserrat_18);
    
    // Button style
    lv_style_init(&style_button);
    lv_style_set_bg_color(&style_button, lv_color_hex(0x2196F3));
    lv_style_set_border_color(&style_button, lv_color_hex(0x1976D2));
    lv_style_set_border_width(&style_button, 2);
    lv_style_set_radius(&style_button, 8);
    
    // Slider style
    lv_style_init(&style_slider);
    lv_style_set_bg_color(&style_slider, lv_color_hex(0x3C3C3C));
    lv_style_set_border_color(&style_slider, lv_color_hex(0x666666));
    lv_style_set_border_width(&style_slider, 2);
}

// Event handler for tileview tile changes (swipe detection)
static void tileview_event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t * tileview_obj = lv_event_get_target(e);
        lv_obj_t * active_tile = lv_tileview_get_tile_act(tileview_obj);
        
        // Find which tile index is currently active
        for (int i = 0; i < ENTITY_COUNT; i++) {
            if (entities[i].tile == active_tile) {
                if (millis() < programmaticUntil && i != programmaticTile) break; // stale event
                if (i != currentTileIndex) exitEditMode();
                currentTileIndex = i;
                updateIndicatorDots(); // Update dots to show new active tile
                break;
            }
        }
    }
}

// Event handlers for different entity types
static void toggle_event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t * obj = lv_event_get_target(e);
        int entityIndex = (int)(uintptr_t)lv_event_get_user_data(e);
        
        bool is_checked = lv_obj_has_state(obj, LV_STATE_CHECKED);
        Serial.println("Toggle via TOUCH for entity " + String(entityIndex) + ": " + String(is_checked ? "ON" : "OFF"));
        
        entities[entityIndex].state = is_checked;
        handleEntityAction(entityIndex, "toggle", String(is_checked));
    }
}

static void cover_button_event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        lv_obj_t * obj = lv_event_get_target(e);
        int entityIndex = (int)(uintptr_t)lv_event_get_user_data(e);
        String action = String((char*)lv_obj_get_user_data(obj));
        
        Serial.println("Cover " + action + " for entity " + String(entityIndex));
        handleEntityAction(entityIndex, "cover_" + action);
    }
}

// ---- Value setters shared by touch handlers and the encoder ----
// fromTouch: the widget was already moved by the user, so don't touch it again.

static void setCoverPosition(int entityIndex, int value, bool fromTouch) {
    HAEntity& entity = entities[entityIndex];
    value = constrain(value, 0, 100);
    entity.position = value;
    if (!fromTouch && entity.wControl) lv_slider_set_value(entity.wControl, value, LV_ANIM_OFF);
    if (entity.wValue) lv_label_set_text_fmt(entity.wValue, "%d%%", value);
    handleEntityAction(entityIndex, "cover_set_position", String(value), valueCommandDebounce);
}

static void setFanSpeed(int entityIndex, int value, bool fromTouch) {
    HAEntity& entity = entities[entityIndex];
    value = constrain(value, 0, 100);
    entity.fanSpeed = value;
    if (!fromTouch && entity.wControl) lv_slider_set_value(entity.wControl, value, LV_ANIM_OFF);
    if (entity.wValue) lv_label_set_text_fmt(entity.wValue, "%d%%", value);
    handleEntityAction(entityIndex, "fan_set_speed", String(value), valueCommandDebounce);
}

static void setTargetTemp(int entityIndex, float temp) {
    HAEntity& entity = entities[entityIndex];
    temp = constrain(roundf(temp / thermostatStep) * thermostatStep, entity.minTemp, entity.maxTemp);
    entity.targetTemp = temp;
    if (entity.wTarget) lv_label_set_text_fmt(entity.wTarget, "Target: %.1f°C", temp);
    float range = entity.maxTemp - entity.minTemp;
    if (entity.wControl && range > 0) {
        lv_arc_set_value(entity.wControl, (int)lroundf((temp - entity.minTemp) / range * 100.0f));
    }
    handleEntityAction(entityIndex, "set_temperature", String(temp, 1), valueCommandDebounce);
}

static void slider_event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t * obj = lv_event_get_target(e);
        int entityIndex = (int)(uintptr_t)lv_event_get_user_data(e);
        int value = lv_slider_get_value(obj);
        
        if (entities[entityIndex].type == ENTITY_COVER) {
            setCoverPosition(entityIndex, value, true);
        } else if (entities[entityIndex].type == ENTITY_FAN) {
            setFanSpeed(entityIndex, value, true);
        }
    }
}

// Create UI for toggle entities (switches, lights)
void createToggleTile(int entityIndex) {
    lv_obj_t* tile = entities[entityIndex].tile;
    
    // Title
    lv_obj_t* title = lv_label_create(tile);
    lv_label_set_text(title, entities[entityIndex].name.c_str());
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Icon
    lv_obj_t* icon = lv_label_create(tile);
    String symbol = getDisplaySymbol(entities[entityIndex].icon);
    lv_label_set_text(icon, symbol.c_str());
    lv_obj_add_style(icon, &style_icon, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -40);
    
    // Switch
    lv_obj_t* sw = lv_switch_create(tile);
    lv_obj_set_size(sw, 80, 40);
    lv_obj_align(sw, LV_ALIGN_CENTER, 0, 20);
    lv_obj_add_event_cb(sw, toggle_event_handler, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)entityIndex);
    
    // Status label
    lv_obj_t* status = lv_label_create(tile);
    lv_label_set_text(status, "Ready");
    lv_obj_add_style(status, &style_value_small, 0);
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -10);
    
    entities[entityIndex].wControl = sw;
    entities[entityIndex].wStatus = status;
}

// Create UI for cover entities
void createCoverTile(int entityIndex) {
    lv_obj_t* tile = entities[entityIndex].tile;
    
    // Title
    lv_obj_t* title = lv_label_create(tile);
    lv_label_set_text(title, entities[entityIndex].name.c_str());
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Icon
    lv_obj_t* icon = lv_label_create(tile);
    String symbol = getDisplaySymbol(entities[entityIndex].icon);
    lv_label_set_text(icon, symbol.c_str());
    lv_obj_add_style(icon, &style_icon, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 40);
    
    // Position slider
    lv_obj_t* slider = lv_slider_create(tile);
    lv_obj_set_size(slider, 250, 25);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, entities[entityIndex].position, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, -40);
    lv_obj_add_style(slider, &style_slider, 0);
    lv_obj_add_event_cb(slider, slider_event_handler, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)entityIndex);
    
    // Real-position marker: a second, read-only slider whose small orange knob sits on top
    // of the main slider. The main knob shows the target, this one where the cover is.
    lv_obj_t* actual = lv_slider_create(tile);
    lv_obj_set_size(actual, 250, 25);
    lv_slider_set_range(actual, 0, 100);
    lv_slider_set_value(actual, entities[entityIndex].position, LV_ANIM_OFF);
    lv_obj_align(actual, LV_ALIGN_CENTER, 0, -40);
    lv_obj_clear_flag(actual, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(actual, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(actual, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(actual, lv_color_hex(0xFF9800), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(actual, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(actual, -5, LV_PART_KNOB); // smaller than the main knob
    lv_obj_add_flag(actual, LV_OBJ_FLAG_HIDDEN);         // shown only while it differs from the target
    
    // Position percentage label
    lv_obj_t* pos_label = lv_label_create(tile);
    lv_label_set_text_fmt(pos_label, "%d%%", entities[entityIndex].position);
    lv_obj_add_style(pos_label, &style_value_small, 0);
    lv_obj_align(pos_label, LV_ALIGN_CENTER, 0, -10);
    
    // Create button container for centered layout
    lv_obj_t* btn_container = lv_obj_create(tile);
    lv_obj_set_size(btn_container, 200, 50);
    lv_obj_align(btn_container, LV_ALIGN_CENTER, 0, 35);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_container, 0, LV_PART_MAIN);
    
    // Open button with up arrow icon
    lv_obj_t* btn_open = lv_btn_create(btn_container);
    lv_obj_set_size(btn_open, 50, 40);
    lv_obj_align(btn_open, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_style(btn_open, &style_button, 0);
    lv_obj_add_event_cb(btn_open, cover_button_event_handler, LV_EVENT_CLICKED, (void*)(uintptr_t)entityIndex);
    lv_obj_set_user_data(btn_open, (void*)"open");
    
    lv_obj_t* btn_open_icon = lv_label_create(btn_open);
    lv_label_set_text(btn_open_icon, LV_SYMBOL_UP);
    lv_obj_add_style(btn_open_icon, &style_value_large, 0);
    lv_obj_center(btn_open_icon);
    
    // Stop button with pause icon
    lv_obj_t* btn_stop = lv_btn_create(btn_container);
    lv_obj_set_size(btn_stop, 50, 40);
    lv_obj_align(btn_stop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_style(btn_stop, &style_button, 0);
    lv_obj_add_event_cb(btn_stop, cover_button_event_handler, LV_EVENT_CLICKED, (void*)(uintptr_t)entityIndex);
    lv_obj_set_user_data(btn_stop, (void*)"stop");
    
    lv_obj_t* btn_stop_icon = lv_label_create(btn_stop);
    lv_label_set_text(btn_stop_icon, LV_SYMBOL_PAUSE);
    lv_obj_add_style(btn_stop_icon, &style_value_large, 0);
    lv_obj_center(btn_stop_icon);
    
    // Close button with down arrow icon
    lv_obj_t* btn_close = lv_btn_create(btn_container);
    lv_obj_set_size(btn_close, 50, 40);
    lv_obj_align(btn_close, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_style(btn_close, &style_button, 0);
    lv_obj_add_event_cb(btn_close, cover_button_event_handler, LV_EVENT_CLICKED, (void*)(uintptr_t)entityIndex);
    lv_obj_set_user_data(btn_close, (void*)"close");
    
    lv_obj_t* btn_close_icon = lv_label_create(btn_close);
    lv_label_set_text(btn_close_icon, LV_SYMBOL_DOWN);
    lv_obj_add_style(btn_close_icon, &style_value_large, 0);
    lv_obj_center(btn_close_icon);
    
    // Store slider and label references for updates (using a simple container approach)
    // We'll store the slider as the main user data, and find other elements when updating
    // State line: "Opening - 44%"
    lv_obj_t* state_label = lv_label_create(tile);
    lv_label_set_text(state_label, "");
    lv_obj_add_style(state_label, &style_value_small, 0);
    lv_obj_align(state_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    
    entities[entityIndex].wControl = slider;
    entities[entityIndex].wValue = pos_label;
    entities[entityIndex].wStatus = state_label;
    entities[entityIndex].wActual = actual;
}

// Event handler for thermostat arc
static void thermostat_arc_event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t * arc = lv_event_get_target(e);
        int entityIndex = (int)(uintptr_t)lv_event_get_user_data(e);
        HAEntity& entity = entities[entityIndex];
        
        // Convert arc value (0-100) to a temperature in the entity's min/max range
        float tempRange = entity.maxTemp - entity.minTemp;
        setTargetTemp(entityIndex, entity.minTemp + (lv_arc_get_value(arc) / 100.0f) * tempRange);
    }
}

// Create UI for thermostat entities
void createThermostatTile(int entityIndex) {
    lv_obj_t* tile = entities[entityIndex].tile;
    
    // Title
    lv_obj_t* title = lv_label_create(tile);
    lv_label_set_text(title, entities[entityIndex].name.c_str());
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Current temperature - large display at top center
    lv_obj_t* current_temp = lv_label_create(tile);
    lv_label_set_text_fmt(current_temp, "%.1f°C", entities[entityIndex].currentTemp);
    lv_obj_add_style(current_temp, &style_sensor_value, 0);
    lv_obj_align(current_temp, LV_ALIGN_CENTER, 0, -80);
    
    // Target temperature - smaller display in center of arc
    lv_obj_t* target_temp = lv_label_create(tile);
    lv_label_set_text_fmt(target_temp, "Target: %.1f°C", entities[entityIndex].targetTemp);
    lv_obj_add_style(target_temp, &style_sensor_unit, 0);
    lv_obj_align(target_temp, LV_ALIGN_CENTER, 0, 0);
    
    // Arc for temperature control - larger size to avoid overlap
    lv_obj_t* temp_arc = lv_arc_create(tile);
    lv_obj_set_size(temp_arc, 240, 240);
    lv_arc_set_range(temp_arc, 0, 100); // 0-100 maps to 10-30°C
    
    // Set initial arc value based on target temperature using dynamic range
    float tempRange = entities[entityIndex].maxTemp - entities[entityIndex].minTemp;
    int arc_value = (int)((entities[entityIndex].targetTemp - entities[entityIndex].minTemp) / tempRange * 100.0);
    // Clamp to valid range
    arc_value = (arc_value < 0) ? 0 : ((arc_value > 100) ? 100 : arc_value);
    lv_arc_set_value(temp_arc, arc_value);
    
    lv_obj_align(temp_arc, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(temp_arc, thermostat_arc_event_handler, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)entityIndex);
    
    // Style the arc
    lv_obj_set_style_arc_color(temp_arc, lv_color_hex(0x666666), LV_PART_MAIN);
    lv_obj_set_style_arc_color(temp_arc, lv_color_hex(0xFF6B35), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(temp_arc, lv_color_hex(0x4CAF50), LV_PART_KNOB);
    lv_obj_set_style_border_color(temp_arc, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_border_width(temp_arc, 2, LV_PART_KNOB);
    
    // HVAC mode/action status - small text at bottom
    lv_obj_t* status = lv_label_create(tile);
    lv_label_set_text(status, entities[entityIndex].hvacMode.c_str());
    lv_obj_add_style(status, &style_value_small, 0);
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -10);
    
    entities[entityIndex].wControl = temp_arc;
    entities[entityIndex].wValue = current_temp;
    entities[entityIndex].wTarget = target_temp;
    entities[entityIndex].wStatus = status;
}

// Create UI for sensor entities
void createSensorTile(int entityIndex) {
    lv_obj_t* tile = entities[entityIndex].tile;
    
    // Title
    lv_obj_t* title = lv_label_create(tile);
    lv_label_set_text(title, entities[entityIndex].name.c_str());
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Icon - moved higher to make room for larger value
    lv_obj_t* icon = lv_label_create(tile);
    String symbol = getDisplaySymbol(entities[entityIndex].icon);
    lv_label_set_text(icon, symbol.c_str());
    lv_obj_add_style(icon, &style_icon, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -80);
    
    // Very large sensor value label
    lv_obj_t* valueLabel = lv_label_create(tile);
    lv_label_set_text(valueLabel, entities[entityIndex].sensorValue.c_str());
    lv_obj_add_style(valueLabel, &style_sensor_value, 0);
    lv_obj_align(valueLabel, LV_ALIGN_CENTER, 0, 0);
    
    // Large sensor unit label positioned next to value
    lv_obj_t* unitLabel = lv_label_create(tile);
    lv_label_set_text(unitLabel, entities[entityIndex].sensorUnit.c_str());
    lv_obj_add_style(unitLabel, &style_sensor_unit, 0);
    lv_obj_align_to(unitLabel, valueLabel, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    
    entities[entityIndex].wValue = valueLabel;
    entities[entityIndex].wUnit = unitLabel;
}

// Create UI for fan entities
void createFanTile(int entityIndex) {
    lv_obj_t* tile = entities[entityIndex].tile;
    
    // Title
    lv_obj_t* title = lv_label_create(tile);
    lv_label_set_text(title, entities[entityIndex].name.c_str());
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Icon
    lv_obj_t* icon = lv_label_create(tile);
    lv_label_set_text(icon, LV_SYMBOL_REFRESH);
    lv_obj_add_style(icon, &style_icon, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 40);
    
    // Speed slider
    lv_obj_t* speed_slider = lv_slider_create(tile);
    lv_obj_set_size(speed_slider, 200, 20);
    lv_slider_set_range(speed_slider, 0, 100);
    lv_slider_set_value(speed_slider, entities[entityIndex].fanSpeed, LV_ANIM_OFF);
    lv_obj_align(speed_slider, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(speed_slider, slider_event_handler, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)entityIndex);
    
    // Speed label
    lv_obj_t* speed_label = lv_label_create(tile);
    lv_label_set_text_fmt(speed_label, "%d%%", entities[entityIndex].fanSpeed);
    lv_obj_add_style(speed_label, &style_value_small, 0);
    lv_obj_align(speed_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    
    entities[entityIndex].wControl = speed_slider;
    entities[entityIndex].wValue = speed_label;
}

// Create WiFi connection overlay
void createWiFiOverlay() {
    lv_obj_t* main_screen = lv_scr_act();
    
    // Create overlay background
    wifi_overlay = lv_obj_create(main_screen);
    lv_obj_set_size(wifi_overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(wifi_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(wifi_overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(wifi_overlay, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_width(wifi_overlay, 0, LV_PART_MAIN);
    
    // WiFi icon
    lv_obj_t* wifi_icon = lv_label_create(wifi_overlay);
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_add_style(wifi_icon, &style_icon, 0);
    lv_obj_align(wifi_icon, LV_ALIGN_CENTER, 0, -60);
    
    // Spinner
    wifi_spinner = lv_spinner_create(wifi_overlay, 1000, 90);
    lv_obj_set_size(wifi_spinner, 60, 60);
    lv_obj_align(wifi_spinner, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_arc_color(wifi_spinner, lv_color_hex(0x4CAF50), LV_PART_MAIN);
    lv_obj_set_style_arc_color(wifi_spinner, lv_color_hex(0x2196F3), LV_PART_INDICATOR);
    
    // Status label
    wifi_status_label = lv_label_create(wifi_overlay);
    lv_label_set_text(wifi_status_label, "Connecting to WiFi...");
    lv_obj_add_style(wifi_status_label, &style_value_large, 0);
    lv_obj_align(wifi_status_label, LV_ALIGN_CENTER, 0, 40);
    
    // Initially hide the overlay
    lv_obj_add_flag(wifi_overlay, LV_OBJ_FLAG_HIDDEN);
}

// Show or hide WiFi overlay
void showWiFiOverlay(bool show) {
    if (wifi_overlay) {
        if (show) {
            lv_obj_clear_flag(wifi_overlay, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(wifi_overlay, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// Update WiFi overlay status text
void updateWiFiOverlayStatus(String status) {
    if (wifi_status_label) {
        lv_label_set_text(wifi_status_label, status.c_str());
    }
}

// Create indicator dots at the bottom of the screen
void createIndicatorDots() {
    lv_obj_t* main_screen = lv_scr_act();
    
    // Create container for dots
    indicator_container = lv_obj_create(main_screen);
    lv_obj_set_size(indicator_container, LV_HOR_RES, 30);
    lv_obj_align(indicator_container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(indicator_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(indicator_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(indicator_container, 0, LV_PART_MAIN);
    
    // Calculate dot spacing
    int dotSize = 8;
    int dotSpacing = 16;
    int totalWidth = (ENTITY_COUNT * dotSpacing) - (dotSpacing - dotSize);
    int startX = (LV_HOR_RES - totalWidth) / 2;
    
    // Create individual dots
    for (int i = 0; i < ENTITY_COUNT; i++) {
        indicator_dots[i] = lv_obj_create(indicator_container);
        lv_obj_set_size(indicator_dots[i], dotSize, dotSize);
        lv_obj_set_pos(indicator_dots[i], startX + (i * dotSpacing), 11);
        lv_obj_set_style_bg_color(indicator_dots[i], lv_color_hex(0x666666), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(indicator_dots[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(indicator_dots[i], 0, LV_PART_MAIN);
        lv_obj_set_style_radius(indicator_dots[i], dotSize / 2, LV_PART_MAIN);
    }
    
    // Set first dot as active
    updateIndicatorDots();
}

// Update indicator dots to show current tile
void updateIndicatorDots() {
    for (int i = 0; i < ENTITY_COUNT; i++) {
        if (indicator_dots[i]) {
            if (i == currentTileIndex) {
                // Active dot - bright green
                lv_obj_set_style_bg_color(indicator_dots[i], lv_color_hex(0x4CAF50), LV_PART_MAIN);
            } else {
                // Inactive dot - gray
                lv_obj_set_style_bg_color(indicator_dots[i], lv_color_hex(0x666666), LV_PART_MAIN);
            }
        }
    }
}

// Create entity tile based on type
void createEntityTile(int entityIndex) {
    if (entities[entityIndex].tileCreated) return;
    
    // Create the tile
    entities[entityIndex].tile = lv_tileview_add_tile(tileview, entityIndex, 0, LV_DIR_HOR);
    lv_obj_add_style(entities[entityIndex].tile, &style_tile_bg, 0);
    
    // Create entity-specific UI based on type
    switch (entities[entityIndex].type) {
        case ENTITY_TOGGLE:
            createToggleTile(entityIndex);
            break;
        case ENTITY_COVER:
            createCoverTile(entityIndex);
            break;
        case ENTITY_THERMOSTAT:
            createThermostatTile(entityIndex);
            break;
        case ENTITY_SENSOR:
            createSensorTile(entityIndex);
            break;
        case ENTITY_FAN:
            createFanTile(entityIndex);
            break;
    }
    
    entities[entityIndex].tileCreated = true;
    Serial.println("Created tile for entity " + String(entityIndex) + " (" + entities[entityIndex].name + ")");
}

// Create the main tileview UI
void createUI() {
    // Set main screen background to black
    lv_obj_t* main_screen = lv_scr_act();
    lv_obj_set_style_bg_color(main_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(main_screen, LV_OPA_COVER, LV_PART_MAIN);
    
    // Create tileview
    tileview = lv_tileview_create(main_screen);
    lv_obj_set_size(tileview, LV_HOR_RES, LV_VER_RES - 30); // Leave space for indicator dots
    lv_obj_align(tileview, LV_ALIGN_TOP_MID, 0, 0);
    
    // Hide tileview scrollbar
    lv_obj_set_style_bg_opa(tileview, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);
    
    // Set tileview background to black
    lv_obj_set_style_bg_color(tileview, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tileview, LV_OPA_COVER, LV_PART_MAIN);
    
    // Register event handler for tileview tile changes (swipe detection)
    lv_obj_add_event_cb(tileview, tileview_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Create tiles for each entity
    for (int i = 0; i < ENTITY_COUNT; i++) {
        createEntityTile(i);
    }
    
    // Create indicator dots
    createIndicatorDots();
    
    // Create WiFi overlay
    createWiFiOverlay();
    
    // Set initial tile
    lv_obj_set_tile_id(tileview, 0, 0, LV_ANIM_OFF);
}

// Queue an action for an entity. One slot per entity, the latest action wins.
// debounceMs delays sending so rapid slider/arc changes collapse into the final value.
void handleEntityAction(int entityIndex, String action, String value, unsigned long debounceMs) {
    if (entityIndex < 0 || entityIndex >= ENTITY_COUNT) return;
    
    unsigned long now = millis();
    PendingCommand& cmd = pendingCommands[entityIndex];
    cmd.active = true;
    cmd.action = action;
    cmd.value = value;
    cmd.dueAt = now + debounceMs;
    lastUserInteraction[entityIndex] = now;
}

// Begin connecting to WiFi without blocking; serviceWiFi() tracks progress
void startWiFi() {
    Serial.println("Connecting to WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wifiAttemptStart = millis();
    showWiFiOverlay(true);
    updateWiFiOverlayStatus("Connecting to WiFi...");
}

// Track WiFi state from loop(): overlay, reconnect attempts
void serviceWiFi() {
    unsigned long now = millis();
    
    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiWasConnected) {
            wifiWasConnected = true;
            connectionEstablished = true;
            Serial.print("WiFi connected, IP address: ");
            Serial.println(WiFi.localIP());
            updateWiFiOverlayStatus("WiFi Connected!");
            wifiOverlayHideAt = now + 1000;
        }
        if (wifiOverlayHideAt && now >= wifiOverlayHideAt) {
            wifiOverlayHideAt = 0;
            showWiFiOverlay(false);
        }
        return;
    }
    
    if (wifiWasConnected) {
        wifiWasConnected = false;
        connectionEstablished = false;
        wifiOverlayHideAt = 0;
        Serial.println("WiFi connection lost!");
        showWiFiOverlay(true);
        updateWiFiOverlayStatus("WiFi lost, reconnecting...");
        wifiAttemptStart = now;
    }
    
    if (now - wifiAttemptStart > wifiRetryInterval) {
        Serial.println("Retrying WiFi connection...");
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        wifiAttemptStart = now;
        updateWiFiOverlayStatus("Retrying WiFi...");
    }
}

static ServiceCall buildServiceCall(int entityIndex, const String& action, const String& value) {
    ServiceCall call;
    String domain = getEntityType(entities[entityIndex].entity_id);
    
    if (action == "toggle") {
        // Desired state is passed explicitly, so repeating the call is harmless
        bool on = (value == "1" || value == "true");
        call.domain = domain;
        call.service = on ? "turn_on" : "turn_off";
        call.valid = (domain == "light" || domain == "switch" || domain == "input_boolean");
    } else if (action == "cover_open" || action == "cover_close" || action == "cover_stop") {
        call.domain = "cover";
        call.service = action == "cover_open" ? "open_cover" : (action == "cover_close" ? "close_cover" : "stop_cover");
        call.valid = true;
    } else if (action == "cover_set_position") {
        call.domain = "cover";
        call.service = "set_cover_position";
        call.key = "position";
        call.intValue = value.toInt();
        call.valid = true;
    } else if (action == "fan_set_speed") {
        call.domain = "fan";
        call.service = "set_percentage";
        call.key = "percentage";
        call.intValue = value.toInt();
        call.valid = true;
    } else if (action == "set_temperature") {
        call.domain = "climate";
        call.service = "set_temperature";
        call.key = "temperature";
        call.isFloat = true;
        call.floatValue = value.toFloat();
        call.valid = true;
    }
    return call;
}

// REST fallback used when the WebSocket is not ready
static bool postServiceViaREST(int entityIndex, const ServiceCall& call) {
    String baseUrl = String(HA_URL);
    if (baseUrl.endsWith("/")) baseUrl = baseUrl.substring(0, baseUrl.length() - 1);
    
    HTTPClient http;
    http.begin(baseUrl + "/api/services/" + call.domain + "/" + call.service);
    http.addHeader("Authorization", "Bearer " + String(HA_TOKEN));
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(2000);
    http.setConnectTimeout(1000);
    
    DynamicJsonDocument doc(256);
    doc["entity_id"] = entities[entityIndex].entity_id;
    if (call.key) {
        if (call.isFloat) doc[call.key] = call.floatValue;
        else doc[call.key] = call.intValue;
    }
    String body;
    serializeJson(doc, body);
    
    int code = http.POST(body);
    http.end();
    if (code != 200) Serial.println("REST service call failed: HTTP " + String(code));
    return code == 200;
}

// Send at most one due command per call. Prefers the WebSocket, falls back to REST.
void processAPIRequest() {
    unsigned long now = millis();
    
    for (int i = 0; i < ENTITY_COUNT; i++) {
        PendingCommand& cmd = pendingCommands[i];
        if (!cmd.active || now < cmd.dueAt) continue;
        
        // Free the slot first: changes made while sending queue up as a new command
        String action = cmd.action, value = cmd.value;
        cmd.active = false;
        
        ServiceCall call = buildServiceCall(i, action, value);
        if (!call.valid) {
            lastError = "Unsupported action: " + action;
            return;
        }
        if (WiFi.status() != WL_CONNECTED) {
            lastError = "WiFi disconnected";
            return;
        }
        
        bool sent;
        if (call.key == nullptr) {
            sent = wsManager.callService(call.domain.c_str(), call.service.c_str(), i);
        } else if (call.isFloat) {
            sent = wsManager.callService(call.domain.c_str(), call.service.c_str(), i, call.key, call.floatValue);
        } else {
            sent = wsManager.callService(call.domain.c_str(), call.service.c_str(), i, call.key, call.intValue);
        }
        
        if (sent) {
            Serial.println("Sent " + call.domain + "." + call.service + " for " + entities[i].entity_id + " via WebSocket");
        } else if (postServiceViaREST(i, call)) {
            Serial.println("Sent " + call.domain + "." + call.service + " for " + entities[i].entity_id + " via REST");
        } else {
            lastError = action + " failed";
            // Revert the optimistic toggle so the UI matches HA again
            if (action == "toggle") {
                entities[i].state = !entities[i].state;
                entities[i].lastState = !entities[i].state;
            }
        }
        return;
    }
}

// True while the user is interacting with an entity's control; incoming HA values
// are held back from the widget so they don't fight the drag (applied afterwards).
static bool userHoldsControl(int entityIndex) {
    if (editMode && entityIndex == currentTileIndex) return true; // whole edit session
    return millis() - lastUserInteraction[entityIndex] < uiSyncHoldoff;
}

// Update UI elements for specific entity
void updateEntityTile(int entityIndex) {
    if (entityIndex < 0 || entityIndex >= ENTITY_COUNT || !entities[entityIndex].tileCreated) {
        return;
    }
    
    HAEntity &entity = entities[entityIndex];
    if (!entity.tile) return;
    
    switch (entity.type) {
        case ENTITY_TOGGLE: {
            if (entity.state != entity.lastState && !userHoldsControl(entityIndex)) {
                if (entity.wControl) {
                    if (entity.state) lv_obj_add_state(entity.wControl, LV_STATE_CHECKED);
                    else lv_obj_clear_state(entity.wControl, LV_STATE_CHECKED);
                }
                entity.lastState = entity.state;
            }
            break;
        }
        case ENTITY_COVER: {
            // Target control follows HA only when the user isn't adjusting it
            if (entity.position != entity.lastPosition && !userHoldsControl(entityIndex)) {
                if (entity.wControl) lv_slider_set_value(entity.wControl, entity.position, LV_ANIM_OFF);
                if (entity.wValue) lv_label_set_text_fmt(entity.wValue, "%d%%", entity.position);
                entity.lastPosition = entity.position;
            }
            
            // Real position and state are always live, even while editing
            if (entity.wActual && entity.wControl) {
                lv_slider_set_value(entity.wActual, entity.position, LV_ANIM_OFF);
                bool differs = lv_slider_get_value(entity.wControl) != entity.position;
                if (differs) lv_obj_clear_flag(entity.wActual, LV_OBJ_FLAG_HIDDEN);
                else lv_obj_add_flag(entity.wActual, LV_OBJ_FLAG_HIDDEN);
            }
            if (entity.wStatus) {
                String text = entity.coverState.length() && entity.coverState != "unknown" ? entity.coverState : String("--");
                text.setCharAt(0, toupper(text.charAt(0)));
                text += " - " + String(entity.position) + "%";
                if (text != lv_label_get_text(entity.wStatus)) lv_label_set_text(entity.wStatus, text.c_str());
            }
            break;
        }
        case ENTITY_THERMOSTAT: {
            if (fabsf(entity.currentTemp - entity.lastCurrentTemp) > 0.05f) {
                if (entity.wValue) lv_label_set_text_fmt(entity.wValue, "%.1f°C", entity.currentTemp);
                entity.lastCurrentTemp = entity.currentTemp;
            }
            
            if (fabsf(entity.targetTemp - entity.lastTargetTemp) > 0.01f && !userHoldsControl(entityIndex)) {
                if (entity.wTarget) lv_label_set_text_fmt(entity.wTarget, "Target: %.1f°C", entity.targetTemp);
                
                // Map the target onto the arc using the entity's own min/max range
                float range = entity.maxTemp - entity.minTemp;
                if (entity.wControl && range > 0) {
                    int arc_value = (int)lroundf((entity.targetTemp - entity.minTemp) / range * 100.0f);
                    arc_value = (arc_value < 0) ? 0 : ((arc_value > 100) ? 100 : arc_value);
                    lv_arc_set_value(entity.wControl, arc_value);
                }
                entity.lastTargetTemp = entity.targetTemp;
            }
            
            if (entity.wStatus) {
                String statusText = entity.hvacMode;
                if (entity.hvacAction != "idle" && entity.hvacAction != entity.hvacMode) {
                    statusText += " (" + entity.hvacAction + ")";
                }
                if (statusText != lv_label_get_text(entity.wStatus)) {
                    lv_label_set_text(entity.wStatus, statusText.c_str());
                }
            }
            break;
        }
        case ENTITY_SENSOR: {
            if (entity.sensorValue != entity.lastSensorValue) {
                if (entity.wValue) lv_label_set_text(entity.wValue, entity.sensorValue.c_str());
                if (entity.wUnit) {
                    lv_label_set_text(entity.wUnit, entity.sensorUnit.c_str());
                    if (entity.wValue) lv_obj_align_to(entity.wUnit, entity.wValue, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
                }
                entity.lastSensorValue = entity.sensorValue;
            }
            break;
        }
        case ENTITY_FAN: {
            if (entity.fanSpeed != entity.lastFanSpeed && !userHoldsControl(entityIndex)) {
                if (entity.wControl) lv_slider_set_value(entity.wControl, entity.fanSpeed, LV_ANIM_OFF);
                if (entity.wValue) lv_label_set_text_fmt(entity.wValue, "%d%%", entity.fanSpeed);
                entity.lastFanSpeed = entity.fanSpeed;
            }
            break;
        }
    }
}

// ---- Toast (short on-screen feedback) ----
static lv_obj_t* toastLabel = nullptr;
static lv_timer_t* toastTimer = nullptr;

static void showToast(const char* text, uint32_t durationMs = 1200) {
    if (!toastLabel) {
        toastLabel = lv_label_create(lv_layer_top());
        lv_obj_add_style(toastLabel, &style_value_small, 0);
        lv_obj_set_style_bg_color(toastLabel, lv_color_hex(0x333333), 0);
        lv_obj_set_style_bg_opa(toastLabel, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(toastLabel, 12, 0);
        lv_obj_set_style_pad_all(toastLabel, 8, 0);
        lv_obj_align(toastLabel, LV_ALIGN_BOTTOM_MID, 0, -60);
        toastTimer = lv_timer_create([](lv_timer_t* t) {
            lv_obj_add_flag(toastLabel, LV_OBJ_FLAG_HIDDEN);
            lv_timer_pause(t);
        }, durationMs, nullptr);
    }
    lv_label_set_text(toastLabel, text);
    lv_obj_align(toastLabel, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_clear_flag(toastLabel, LV_OBJ_FLAG_HIDDEN);
    lv_timer_set_period(toastTimer, durationMs);
    lv_timer_reset(toastTimer);
    lv_timer_resume(toastTimer);
}

// ---- Encoder input ----

static bool isAdjustable(EntityType type) {
    return type == ENTITY_COVER || type == ENTITY_THERMOSTAT || type == ENTITY_FAN;
}

// Outline the control's knob while editing so it is obvious the knob changes the value
static void setEditHighlight(int entityIndex, bool on) {
    lv_obj_t* ctrl = entities[entityIndex].wControl;
    if (!ctrl) return;
    lv_obj_set_style_outline_width(ctrl, on ? 4 : 0, LV_PART_KNOB);
    lv_obj_set_style_outline_color(ctrl, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_outline_pad(ctrl, 2, LV_PART_KNOB);
}

static void enterEditMode() {
    editMode = true;
    editLastActivity = millis();
    const HAEntity& entity = entities[currentTileIndex];
    editValue = entity.type == ENTITY_COVER ? entity.position
              : entity.type == ENTITY_FAN ? entity.fanSpeed
              : entity.targetTemp;
    setEditHighlight(currentTileIndex, true);
    Serial.println("Edit mode: " + entities[currentTileIndex].name);
}

void exitEditMode() {
    if (!editMode) return;
    editMode = false;
    for (int i = 0; i < ENTITY_COUNT; i++) setEditHighlight(i, false);
    Serial.println("Edit mode off");
}

static void navigateTiles(int detents) {
    int target = ((currentTileIndex + detents) % ENTITY_COUNT + ENTITY_COUNT) % ENTITY_COUNT;
    programmaticTile = target;
    programmaticUntil = millis() + 500;
    currentTileIndex = target;
    lv_obj_set_tile_id(tileview, target, 0, LV_ANIM_ON);
    updateIndicatorDots();
}

static void adjustCurrentValue(int detents) {
    HAEntity& entity = entities[currentTileIndex];
    switch (entity.type) {
        case ENTITY_COVER:
            editValue = constrain(editValue + detents * coverEditStep, 0, 100);
            setCoverPosition(currentTileIndex, (int)editValue, false);
            break;
        case ENTITY_FAN:
            editValue = constrain(editValue + detents * fanEditStep, 0, 100);
            setFanSpeed(currentTileIndex, (int)editValue, false);
            break;
        case ENTITY_THERMOSTAT:
            editValue = constrain(editValue + detents * thermostatStep, entity.minTemp, entity.maxTemp);
            setTargetTemp(currentTileIndex, editValue);
            break;
        default:
            break;
    }
}

static void toggleCurrentEntity() {
    HAEntity& entity = entities[currentTileIndex];
    entity.state = !entity.state;
    handleEntityAction(currentTileIndex, "toggle", String(entity.state ? 1 : 0));
    if (entity.wControl) {
        if (entity.state) lv_obj_add_state(entity.wControl, LV_STATE_CHECKED);
        else lv_obj_clear_state(entity.wControl, LV_STATE_CHECKED);
    }
}

void handleEncoderInput() {
    unsigned long now = millis();
    
    int detents = encoder_take_detents();
    if (detents != 0) {
        if (editMode) {
            adjustCurrentValue(detents);
            editLastActivity = now;
        } else {
            navigateTiles(detents);
        }
    }
    
    EncoderButtonEvent button = encoder_poll_button();
    if (button == ENC_BTN_SHORT) {
        if (editMode) {
            exitEditMode();
        } else if (entities[currentTileIndex].type == ENTITY_TOGGLE) {
            toggleCurrentEntity();
        } else if (isAdjustable(entities[currentTileIndex].type)) {
            enterEditMode();
        }
    } else if (button == ENC_BTN_LONG) {
        exitEditMode();
        if (entities[currentTileIndex].type == ENTITY_COVER) {
            handleEntityAction(currentTileIndex, "cover_stop");
            showToast("Stopped");
        }
    }
    
    if (editMode && now - editLastActivity > editTimeout) {
        exitEditMode();
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("Multi-Entity LVGL Home Assistant Controller with Tileview Starting...");
    
    encoder_init();
    
    // Initialize entities
    initializeEntities();
    
    // Initialize LVGL
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    
    createStyles();
    createUI();
    
    lv_timer_handler();
    delay(500);
    
    // Connect to WiFi in the background; the overlay shows progress from loop()
    startWiFi();
    lv_timer_handler(); // Ensure overlay is drawn
    
    // Initialize WebSocket manager
    if (wsManager.begin()) {
        Serial.println("WebSocket manager initialized successfully");
    } else {
        Serial.println("Failed to initialize WebSocket manager");
    }
    
    Serial.println("Setup complete! WebSocket will provide real-time updates.");
    Serial.println("Use encoder to navigate between entity tiles or swipe on touchscreen.");
}

void loop() {
    lv_timer_handler();
    
    handleEncoderInput();
    
    // Send queued commands
    processAPIRequest();
    
    // WiFi supervision (non-blocking)
    serviceWiFi();
    
    if (WiFi.status() == WL_CONNECTED) {
        // Process WebSocket events and REST fallback
        wsManager.loop();
        
        // Update connection status based on WebSocket state
        if (wsManager.isReady()) {
            connectionEstablished = true;
            lastError = "";
        } else if (wsManager.isConnected()) {
            connectionEstablished = true;
            lastError = "Connecting to HA: " + wsManager.getStateString();
        } else {
            connectionEstablished = false;
            lastError = wsManager.getLastError();
            if (lastError.length() == 0) {
                lastError = "WebSocket: " + wsManager.getStateString();
            }
        }
    }
    
    // Update current tile UI
    updateEntityTile(currentTileIndex);
    
    delay(5);
}
