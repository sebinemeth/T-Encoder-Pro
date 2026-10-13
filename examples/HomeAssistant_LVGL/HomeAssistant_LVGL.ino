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
unsigned long lastWiFiRetry = 0;
const unsigned long wifiRetryInterval = 30000;

// WebSocket manager
HAWebSocketManagerV2 wsManager;

// API request management
bool pendingRequest = false;
int pendingEntityIndex = -1;
String pendingRequestType = "";
String pendingRequestValue = "";
unsigned long requestStartTime = 0;
const unsigned long requestProcessInterval = 100;

// Input handling
unsigned long lastEncoderTime = 0;
const unsigned long encoderDebounceTime = 50;
bool lastEncoderButtonState = HIGH;
unsigned long lastEncoderButtonTime = 0;
const unsigned long encoderButtonDebounceTime = 50;

// Navigation
bool navigationCooldown = false;
unsigned long lastNavigationTime = 0;
const unsigned long navigationCooldownTime = 300;

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
void handleEntityAction(int entityIndex, String action, String value = "");
void processAPIRequest();
void connectToWiFi();
String getDisplaySymbol(String iconName);
void setEntityTypeFromDomain(int entityIndex);
void createWiFiOverlay();
void showWiFiOverlay(bool show);
void updateWiFiOverlayStatus(String status);
void createIndicatorDots();
void updateIndicatorDots();

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
                currentTileIndex = i;
                updateIndicatorDots(); // Update dots to show new active tile
                Serial.println("Tileview: Swiped to tile " + String(currentTileIndex));
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

static void slider_event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t * obj = lv_event_get_target(e);
        int entityIndex = (int)(uintptr_t)lv_event_get_user_data(e);
        int value = lv_slider_get_value(obj);
        
        // Update the entity based on its type
        if (entities[entityIndex].type == ENTITY_COVER) {
            entities[entityIndex].position = value;
            handleEntityAction(entityIndex, "cover_set_position", String(value));
        } else if (entities[entityIndex].type == ENTITY_FAN) {
            entities[entityIndex].fanSpeed = value;
            handleEntityAction(entityIndex, "fan_set_speed", String(value));
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
    
    // Store references for updates
    lv_obj_set_user_data(tile, sw);
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
    lv_obj_set_user_data(tile, slider);
}

// Event handler for thermostat arc
static void thermostat_arc_event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t * arc = lv_event_get_target(e);
        int entityIndex = (int)(uintptr_t)lv_event_get_user_data(e);
        int arc_value = lv_arc_get_value(arc);
        
        // Convert arc value (0-100) to temperature using entity's dynamic min/max range
        float tempRange = entities[entityIndex].maxTemp - entities[entityIndex].minTemp;
        float target_temp = entities[entityIndex].minTemp + (arc_value / 100.0) * tempRange;
        entities[entityIndex].targetTemp = target_temp;
        
        Serial.println("Thermostat target temp changed to: " + String(target_temp, 1) + "°C (range: " + 
                      String(entities[entityIndex].minTemp, 1) + "-" + String(entities[entityIndex].maxTemp, 1) + "°C)");
        
        // Update target temperature display immediately for responsiveness
        lv_obj_t* tile = entities[entityIndex].tile;
        if (tile) {
            // Find target temp label (3rd label after title, current temp)
            uint32_t childCount = lv_obj_get_child_cnt(tile);
            int labelCount = 0;
            for (uint32_t i = 0; i < childCount; i++) {
                lv_obj_t* child = lv_obj_get_child(tile, i);
                if (child && lv_obj_check_type(child, &lv_label_class)) {
                    labelCount++;
                    if (labelCount == 1) { // Title - skip
                        continue;
                    } else if (labelCount == 2) { // Current temp - skip
                        continue;
                    } else if (labelCount == 3) { // Target temp label
                        lv_label_set_text_fmt(child, "%.1f°C", target_temp);
                        break;
                    }
                }
            }
        }
        
        // Send temperature change to Home Assistant
        handleEntityAction(entityIndex, "set_temperature", String(target_temp, 1));
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
    
    // Store arc reference for updates
    lv_obj_set_user_data(tile, temp_arc);
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
    
    // Store value label reference for updates (unit will be found by position)
    lv_obj_set_user_data(tile, valueLabel);
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
    lv_obj_align(speed_slider, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(speed_slider, slider_event_handler, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)entityIndex);
    
    // Speed label
    lv_obj_t* speed_label = lv_label_create(tile);
    lv_label_set_text(speed_label, "0%");
    lv_obj_add_style(speed_label, &style_value_small, 0);
    lv_obj_align(speed_label, LV_ALIGN_BOTTOM_MID, 0, -10);
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

// Handle entity actions (toggle, cover, etc.)
void handleEntityAction(int entityIndex, String action, String value) {
    if (!connectionEstablished || pendingRequest) return;
    
    pendingRequest = true;
    pendingEntityIndex = entityIndex;
    pendingRequestType = action;
    pendingRequestValue = value;
    requestStartTime = millis();
    
    Serial.println("Action queued for entity " + String(entityIndex) + ": " + action + " = " + value);
}

// WiFi connection function
void connectToWiFi() {
    Serial.println("Connecting to WiFi...");
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(1000);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.println("WiFi connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        connectionEstablished = true;
    } else {
        Serial.println("WiFi connection failed!");
        connectionEstablished = false;
    }
}

// Process API requests
void processAPIRequest() {
    if (!pendingRequest || WiFi.status() != WL_CONNECTED || pendingEntityIndex < 0) {
        return;
    }
    
    HTTPClient http;
    String entityType = getEntityType(entities[pendingEntityIndex].entity_id);
    String service;
    String serviceUrl;
    String domain;
    
    // Determine service and URL based on request type
    if (pendingRequestType == "toggle") {
        if (entityType == "input_boolean") {
            domain = "input_boolean";
            service = "toggle";
        } else if (entityType == "switch") {
            domain = "switch";
            service = (pendingRequestValue == "1" || pendingRequestValue == "true") ? "turn_on" : "turn_off";
        } else if (entityType == "light") {
            domain = "light";
            service = (pendingRequestValue == "1" || pendingRequestValue == "true") ? "turn_on" : "turn_off";
        } else {
            lastError = "Unsupported toggle entity: " + entityType;
            pendingRequest = false;
            return;
        }
        serviceUrl = String(HA_URL) + "api/services/" + domain + "/" + service;
    } else if (pendingRequestType.startsWith("cover_")) {
        domain = "cover";
        if (pendingRequestType == "cover_open") {
            service = "open_cover";
        } else if (pendingRequestType == "cover_close") {
            service = "close_cover";
        } else if (pendingRequestType == "cover_stop") {
            service = "stop_cover";
        } else if (pendingRequestType == "cover_set_position") {
            service = "set_cover_position";
        }
        serviceUrl = String(HA_URL) + "api/services/cover/" + service;
    } else if (pendingRequestType.startsWith("fan_")) {
        domain = "fan";
        if (pendingRequestType == "fan_set_speed") {
            service = "set_percentage";
        }
        serviceUrl = String(HA_URL) + "api/services/fan/" + service;
    } else if (pendingRequestType == "set_temperature") {
        domain = "climate";
        service = "set_temperature";
        serviceUrl = String(HA_URL) + "api/services/climate/" + service;
    } else {
        lastError = "Unknown request type: " + pendingRequestType;
        pendingRequest = false;
        return;
    }
    
    http.begin(serviceUrl);
    http.addHeader("Authorization", "Bearer " + String(HA_TOKEN));
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(1000);
    http.setConnectTimeout(500);
    
    DynamicJsonDocument doc(256);
    doc["entity_id"] = entities[pendingEntityIndex].entity_id;
    
    // Add additional parameters based on request type
    if (pendingRequestType == "cover_set_position") {
        doc["position"] = pendingRequestValue.toInt();
    } else if (pendingRequestType == "fan_set_speed") {
        doc["percentage"] = pendingRequestValue.toInt();
    } else if (pendingRequestType == "set_temperature") {
        doc["temperature"] = pendingRequestValue.toFloat();
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    unsigned long startTime = millis();
    int httpResponseCode = http.POST(jsonString);
    unsigned long elapsed = millis() - startTime;
    
    Serial.println("API call (" + pendingRequestType + ") took: " + String(elapsed) + "ms");
    
    if (httpResponseCode == 200) {
        Serial.println("API call successful: " + pendingRequestType);
        lastError = "";
    } else {
        lastError = pendingRequestType + " failed: " + String(httpResponseCode);
        Serial.println("API call failed: " + String(httpResponseCode));
        
        // Revert optimistic UI changes on failure
        if (pendingRequestType == "toggle") {
            entities[pendingEntityIndex].state = !entities[pendingEntityIndex].state;
            entities[pendingEntityIndex].lastState = !entities[pendingEntityIndex].state;
        }
    }
    
    http.end();
    pendingRequest = false;
    pendingEntityIndex = -1;
}

// Update UI elements for specific entity
void updateEntityTile(int entityIndex) {
    if (entityIndex < 0 || entityIndex >= ENTITY_COUNT || !entities[entityIndex].tileCreated) {
        return;
    }
    
    HAEntity &entity = entities[entityIndex];
    lv_obj_t* tile = entity.tile;
    
    if (!tile) return;
    
    switch (entity.type) {
        case ENTITY_TOGGLE: {
            // Update switch state
            if (entity.state != entity.lastState) {
                lv_obj_t* sw = (lv_obj_t*)lv_obj_get_user_data(tile);
                if (sw) {
                    if (entity.state) {
                        lv_obj_add_state(sw, LV_STATE_CHECKED);
                    } else {
                        lv_obj_clear_state(sw, LV_STATE_CHECKED);
                    }
                }
                entity.lastState = entity.state;
            }
            break;
        }
        case ENTITY_COVER: {
            // Update position slider and label
            if (entity.position != entity.lastPosition) {
                lv_obj_t* slider = (lv_obj_t*)lv_obj_get_user_data(tile);
                if (slider) {
                    // Update slider value without triggering events
                    lv_slider_set_value(slider, entity.position, LV_ANIM_OFF);
                    
                    // Find and update position label (iterate through all children)
                    uint32_t childCount = lv_obj_get_child_cnt(tile);
                    for (uint32_t i = 0; i < childCount; i++) {
                        lv_obj_t* child = lv_obj_get_child(tile, i);
                        if (child && lv_obj_check_type(child, &lv_label_class)) {
                            // Check if this label contains percentage text
                            const char* text = lv_label_get_text(child);
                            if (text && strstr(text, "%") != NULL) {
                                lv_label_set_text_fmt(child, "%d%%", entity.position);
                                break;
                            }
                        }
                    }
                }
                Serial.println("Cover position updated to: " + String(entity.position) + "%");
                entity.lastPosition = entity.position;
            }
            break;
        }
        case ENTITY_THERMOSTAT: {
            // Update current temperature display
            if (abs(entity.currentTemp - entity.lastCurrentTemp) > 0.1) {
                Serial.println("Thermostat current temp updated to: " + String(entity.currentTemp, 1) + "°C");
                
                // Find and update current temp label (1st label after title)
                uint32_t childCount = lv_obj_get_child_cnt(tile);
                int labelCount = 0;
                for (uint32_t i = 0; i < childCount; i++) {
                    lv_obj_t* child = lv_obj_get_child(tile, i);
                    if (child && lv_obj_check_type(child, &lv_label_class)) {
                        labelCount++;
                        if (labelCount == 1) { // Title - skip
                            continue;
                        } else if (labelCount == 2) { // Current temp label
                            lv_label_set_text_fmt(child, "%.1f°C", entity.currentTemp);
                            break;
                        }
                    }
                }
                entity.lastCurrentTemp = entity.currentTemp;
            }
            
            // Update target temperature display and arc position
            if (abs(entity.targetTemp - entity.lastTargetTemp) > 0.1) {
                Serial.println("Thermostat target temp updated to: " + String(entity.targetTemp, 1) + "°C");
                
                // Update target temperature label (3rd label after title)
                uint32_t childCount = lv_obj_get_child_cnt(tile);
                int labelCount = 0;
                for (uint32_t i = 0; i < childCount; i++) {
                    lv_obj_t* child = lv_obj_get_child(tile, i);
                    if (child && lv_obj_check_type(child, &lv_label_class)) {
                        labelCount++;
                        if (labelCount == 1) { // Title - skip
                            continue;
                        } else if (labelCount == 2) { // Current temp - skip
                            continue;
                        } else if (labelCount == 3) { // Target temp label
                            lv_label_set_text_fmt(child, "Target: %.1f°C", entity.targetTemp);
                            break;
                        }
                    }
                }
                
                // Update arc position to match target temperature
                lv_obj_t* arc = (lv_obj_t*)lv_obj_get_user_data(tile);
                if (arc && lv_obj_check_type(arc, &lv_arc_class)) {
                    // Convert temperature (10-30°C) to arc value (0-100)
                    int arc_value = (int)((entity.targetTemp - 10.0) / 20.0 * 100.0);
                    // Clamp to valid range
                    arc_value = (arc_value < 0) ? 0 : ((arc_value > 100) ? 100 : arc_value);
                    lv_arc_set_value(arc, arc_value);
                }
                
                entity.lastTargetTemp = entity.targetTemp;
            }
            
            // Update HVAC status display
            lv_obj_t* statusLabel = nullptr;
            uint32_t childCount = lv_obj_get_child_cnt(tile);
            int labelCount = 0;
            for (uint32_t i = 0; i < childCount; i++) {
                lv_obj_t* child = lv_obj_get_child(tile, i);
                if (child && lv_obj_check_type(child, &lv_label_class)) {
                    labelCount++;
                    if (labelCount == 4) { // Status label (4th label)
                        statusLabel = child;
                        break;
                    }
                }
            }
            if (statusLabel) {
                String statusText = entity.hvacMode;
                if (entity.hvacAction != "idle" && entity.hvacAction != entity.hvacMode) {
                    statusText += " (" + entity.hvacAction + ")";
                }
                lv_label_set_text(statusLabel, statusText.c_str());
            }
            
            break;
        }
        case ENTITY_SENSOR: {
            // Update separate sensor value and unit labels
            if (entity.sensorValue != entity.lastSensorValue) {
                lv_obj_t* valueLabel = (lv_obj_t*)lv_obj_get_user_data(tile);
                if (valueLabel) {
                    // Update the large sensor value
                    lv_label_set_text(valueLabel, entity.sensorValue.c_str());
                    
                    // Find and update the unit label - simple approach by checking label count
                    uint32_t childCount = lv_obj_get_child_cnt(tile);
                    lv_obj_t* unitLabel = nullptr;
                    int labelCount = 0;
                    for (uint32_t i = 0; i < childCount; i++) {
                        lv_obj_t* child = lv_obj_get_child(tile, i);
                        if (child && lv_obj_check_type(child, &lv_label_class)) {
                            labelCount++;
                            // The unit label is typically the 4th label (after title, icon, value)
                            if (labelCount >= 4 && child != valueLabel) {
                                unitLabel = child;
                                break;
                            }
                        }
                    }
                    
                    if (unitLabel) {
                        lv_label_set_text(unitLabel, entity.sensorUnit.c_str());
                        // Re-position unit label next to updated value label
                        lv_obj_align_to(unitLabel, valueLabel, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
                    }
                }
                Serial.println("Sensor value updated: " + entity.sensorValue + " " + entity.sensorUnit);
                entity.lastSensorValue = entity.sensorValue;
            }
            break;
        }
        case ENTITY_FAN: {
            // Update fan speed - simplified for compatibility
            if (entity.fanSpeed != entity.lastFanSpeed) {
                Serial.println("Fan speed updated to: " + String(entity.fanSpeed) + "%");
                entity.lastFanSpeed = entity.fanSpeed;
            }
            break;
        }
    }
}

// Handle encoder navigation
void handleEncoder() {
    static uint8_t lastEncodeValue = 0;
    static int8_t encoderCounter = 0;
    
    uint8_t encodeValue = 0;
    
    // Read encoder pins
    if (digitalRead(KNOB_DATA_A)) encodeValue |= 0x01;
    if (digitalRead(KNOB_DATA_B)) encodeValue |= 0x02;
    
    // Detect rotation using full step detection
    if (encodeValue != lastEncodeValue && (millis() - lastEncoderTime > encoderDebounceTime)) {
        
        // Clockwise rotation pattern
        if ((lastEncodeValue == 0x00 && encodeValue == 0x01) ||
            (lastEncodeValue == 0x01 && encodeValue == 0x03) ||
            (lastEncodeValue == 0x03 && encodeValue == 0x02) ||
            (lastEncodeValue == 0x02 && encodeValue == 0x00)) {
            encoderCounter++;
        }
        // Counter-clockwise rotation pattern
        else if ((lastEncodeValue == 0x00 && encodeValue == 0x02) ||
                 (lastEncodeValue == 0x02 && encodeValue == 0x03) ||
                 (lastEncodeValue == 0x03 && encodeValue == 0x01) ||
                 (lastEncodeValue == 0x01 && encodeValue == 0x00)) {
            encoderCounter--;
        }
        
        lastEncodeValue = encodeValue;
        lastEncoderTime = millis();
        
        // Check for full detent steps
        if (encoderCounter >= 2 && !navigationCooldown) {
            // Next tile
            int nextTile = (currentTileIndex + 1) % ENTITY_COUNT;
            lv_obj_set_tile_id(tileview, nextTile, 0, LV_ANIM_ON);
            currentTileIndex = nextTile;
            updateIndicatorDots(); // Update dots to show new active tile
            encoderCounter = 0;
            navigationCooldown = true;
            lastNavigationTime = millis();
            Serial.println("Encoder: Next tile - " + String(currentTileIndex));
        } else if (encoderCounter <= -2 && !navigationCooldown) {
            // Previous tile
            int prevTile = (currentTileIndex - 1 + ENTITY_COUNT) % ENTITY_COUNT;
            lv_obj_set_tile_id(tileview, prevTile, 0, LV_ANIM_ON);
            currentTileIndex = prevTile;
            updateIndicatorDots(); // Update dots to show new active tile
            encoderCounter = 0;
            navigationCooldown = true;
            lastNavigationTime = millis();
            Serial.println("Encoder: Previous tile - " + String(currentTileIndex));
        }
    }
}

// Handle encoder button press
void handleEncoderButton() {
    bool currentButtonState = digitalRead(KNOB_KEY);
    
    if (currentButtonState == LOW && lastEncoderButtonState == HIGH && 
        (millis() - lastEncoderButtonTime > encoderButtonDebounceTime)) {
        
        Serial.println("Encoder button pressed for entity " + String(currentTileIndex));
        
        // Handle button press based on current entity type
        switch (entities[currentTileIndex].type) {
            case ENTITY_TOGGLE:
                // Toggle the switch
                entities[currentTileIndex].state = !entities[currentTileIndex].state;
                handleEntityAction(currentTileIndex, "toggle", String(entities[currentTileIndex].state));
                // Update UI switch state
                if (entities[currentTileIndex].tile) {
                    lv_obj_t* sw = (lv_obj_t*)lv_obj_get_user_data(entities[currentTileIndex].tile);
                    if (sw) {
                        if (entities[currentTileIndex].state) {
                            lv_obj_add_state(sw, LV_STATE_CHECKED);
                        } else {
                            lv_obj_clear_state(sw, LV_STATE_CHECKED);
                        }
                    }
                }
                break;
            case ENTITY_COVER:
                // For covers, trigger stop action
                handleEntityAction(currentTileIndex, "cover_stop");
                break;
            default:
                // For other types, implement specific behavior as needed
                break;
        }
        
        lastEncoderButtonTime = millis();
    }
    
    lastEncoderButtonState = currentButtonState;
}

void setup() {
    Serial.begin(115200);
    Serial.println("Multi-Entity LVGL Home Assistant Controller with Tileview Starting...");
    
    // Initialize encoder pins
    pinMode(KNOB_DATA_A, INPUT_PULLUP);
    pinMode(KNOB_DATA_B, INPUT_PULLUP);
    pinMode(KNOB_KEY, INPUT_PULLUP);
    
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
    
    // Show WiFi overlay while connecting
    showWiFiOverlay(true);
    updateWiFiOverlayStatus("Connecting to WiFi...");
    lv_timer_handler(); // Ensure overlay is drawn
    
    connectToWiFi();
    
    // Hide WiFi overlay after connection attempt
    if (connectionEstablished) {
        updateWiFiOverlayStatus("WiFi Connected!");
        delay(1000); // Show success message briefly
    } else {
        updateWiFiOverlayStatus("WiFi Connection Failed");
        delay(2000); // Show error message longer
    }
    showWiFiOverlay(false);
    
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
    
    // Handle navigation cooldown
    if (navigationCooldown && millis() - lastNavigationTime > navigationCooldownTime) {
        navigationCooldown = false;
    }
    
    // Handle encoder navigation
    if (!navigationCooldown) {
        handleEncoder();
    }
    
    // Handle encoder button
    handleEncoderButton();
    
    // Process pending API requests
    if (pendingRequest && millis() - requestStartTime >= requestProcessInterval) {
        processAPIRequest();
    }
    
    // WiFi connection management
    if (WiFi.status() != WL_CONNECTED) {
        if (connectionEstablished) {
            connectionEstablished = false;
            Serial.println("WiFi connection lost!");
            lastWiFiRetry = millis();
        }
        
        if (millis() - lastWiFiRetry > wifiRetryInterval) {
            Serial.println("Attempting to reconnect to WiFi...");
            connectToWiFi();
            lastWiFiRetry = millis();
        }
    } else {
        if (!connectionEstablished) {
            connectionEstablished = true;
        }
        
        // Process WebSocket events (replaces periodic polling)
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
