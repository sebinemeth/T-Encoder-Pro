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
#include "ha_config_private.h" // Override defaults with actual values
#include "ha_websocket_config.h"
#include "ha_websocket_manager_v2.h"
#include "entities.h"

// Global UI variables
lv_obj_t *tileview;
lv_obj_t *tiles[ENTITY_COUNT];
int currentTileIndex = 0;
int lastTileIndex = -1;

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
lv_style_t style_button;
lv_style_t style_slider;

// Forward declarations
void createEntityTile(int entityIndex);
void updateEntityTile(int entityIndex);
void handleEntityAction(int entityIndex, String action, String value = "");
void processAPIRequest();
bool fetchEntityState(int entityIndex);
void connectToWiFi();
String getDisplaySymbol(String iconName);
void setEntityTypeFromDomain(int entityIndex);

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
                entities[i].targetTemp = 21.0;
                entities[i].lastTargetTemp = 20.0;
                entities[i].hvacMode = "off";
                entities[i].hvacAction = "idle";
                break;
            case ENTITY_SENSOR:
                entities[i].sensorValue = "N/A";
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

// Determine entity type and set defaults based on domain
void setEntityTypeFromDomain(int entityIndex) {
    String entityType = getEntityType(entities[entityIndex].entity_id);
    
    if (entityType == "cover") {
        entities[entityIndex].type = ENTITY_COVER;
    } else if (entityType == "climate") {
        entities[entityIndex].type = ENTITY_THERMOSTAT;
    } else if (entityType == "sensor") {
        entities[entityIndex].type = ENTITY_SENSOR;
    } else if (entityType == "fan") {
        entities[entityIndex].type = ENTITY_FAN;
    } else {
        // Default to toggle type for lights, switches, input_boolean, etc.
        entities[entityIndex].type = ENTITY_TOGGLE;
    }
}

// Helper function to extract entity type from entity_id
String getEntityType(String entityId) {
    int dotIndex = entityId.indexOf('.');
    if (dotIndex > 0) {
        return entityId.substring(0, dotIndex);
    }
    return "unknown";
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
    lv_obj_set_size(slider, 200, 20);
    lv_slider_set_range(slider, 0, 100);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, -20);
    lv_obj_add_event_cb(slider, slider_event_handler, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)entityIndex);
    
    // Position label
    lv_obj_t* pos_label = lv_label_create(tile);
    lv_label_set_text(pos_label, "0%");
    lv_obj_add_style(pos_label, &style_value_small, 0);
    lv_obj_align(pos_label, LV_ALIGN_CENTER, 0, 10);
    
    // Action buttons
    lv_obj_t* btn_open = lv_btn_create(tile);
    lv_obj_set_size(btn_open, 60, 30);
    lv_obj_align(btn_open, LV_ALIGN_BOTTOM_LEFT, 20, -10);
    lv_obj_add_style(btn_open, &style_button, 0);
    lv_obj_add_event_cb(btn_open, cover_button_event_handler, LV_EVENT_CLICKED, (void*)(uintptr_t)entityIndex);
    lv_obj_set_user_data(btn_open, (void*)"open");
    
    lv_obj_t* btn_open_label = lv_label_create(btn_open);
    lv_label_set_text(btn_open_label, "OPEN");
    lv_obj_center(btn_open_label);
    
    lv_obj_t* btn_stop = lv_btn_create(tile);
    lv_obj_set_size(btn_stop, 60, 30);
    lv_obj_align(btn_stop, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_style(btn_stop, &style_button, 0);
    lv_obj_add_event_cb(btn_stop, cover_button_event_handler, LV_EVENT_CLICKED, (void*)(uintptr_t)entityIndex);
    lv_obj_set_user_data(btn_stop, (void*)"stop");
    
    lv_obj_t* btn_stop_label = lv_label_create(btn_stop);
    lv_label_set_text(btn_stop_label, "STOP");
    lv_obj_center(btn_stop_label);
    
    lv_obj_t* btn_close = lv_btn_create(tile);
    lv_obj_set_size(btn_close, 60, 30);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_add_style(btn_close, &style_button, 0);
    lv_obj_add_event_cb(btn_close, cover_button_event_handler, LV_EVENT_CLICKED, (void*)(uintptr_t)entityIndex);
    lv_obj_set_user_data(btn_close, (void*)"close");
    
    lv_obj_t* btn_close_label = lv_label_create(btn_close);
    lv_label_set_text(btn_close_label, "CLOSE");
    lv_obj_center(btn_close_label);
}

// Create UI for thermostat entities
void createThermostatTile(int entityIndex) {
    lv_obj_t* tile = entities[entityIndex].tile;
    
    // Title
    lv_obj_t* title = lv_label_create(tile);
    lv_label_set_text(title, entities[entityIndex].name.c_str());
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Icon
    lv_obj_t* icon = lv_label_create(tile);
    lv_label_set_text(icon, LV_SYMBOL_LOOP);
    lv_obj_add_style(icon, &style_icon, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 40);
    
    // Current temperature
    lv_obj_t* current_temp = lv_label_create(tile);
    lv_label_set_text(current_temp, "20.0°C");
    lv_obj_add_style(current_temp, &style_value_large, 0);
    lv_obj_align(current_temp, LV_ALIGN_CENTER, 0, -20);
    
    // Target temperature slider
    lv_obj_t* temp_slider = lv_slider_create(tile);
    lv_obj_set_size(temp_slider, 200, 20);
    lv_slider_set_range(temp_slider, 10, 30); // 10-30°C range
    lv_obj_align(temp_slider, LV_ALIGN_CENTER, 0, 20);
    
    // HVAC mode/action status
    lv_obj_t* status = lv_label_create(tile);
    lv_label_set_text(status, "Off");
    lv_obj_add_style(status, &style_value_small, 0);
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -10);
}

// Create UI for sensor entities
void createSensorTile(int entityIndex) {
    lv_obj_t* tile = entities[entityIndex].tile;
    
    // Title
    lv_obj_t* title = lv_label_create(tile);
    lv_label_set_text(title, entities[entityIndex].name.c_str());
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Icon
    lv_obj_t* icon = lv_label_create(tile);
    lv_label_set_text(icon, LV_SYMBOL_CHARGE);
    lv_obj_add_style(icon, &style_icon, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -40);
    
    // Sensor value
    lv_obj_t* value = lv_label_create(tile);
    lv_label_set_text(value, "N/A");
    lv_obj_add_style(value, &style_value_large, 0);
    lv_obj_align(value, LV_ALIGN_CENTER, 0, 20);
    
    // Unit
    lv_obj_t* unit = lv_label_create(tile);
    lv_label_set_text(unit, "");
    lv_obj_add_style(unit, &style_value_small, 0);
    lv_obj_align(unit, LV_ALIGN_BOTTOM_MID, 0, -10);
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
    lv_obj_set_size(tileview, LV_HOR_RES, LV_VER_RES);
    lv_obj_center(tileview);
    
    // Set tileview background to black
    lv_obj_set_style_bg_color(tileview, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tileview, LV_OPA_COVER, LV_PART_MAIN);
    
    // Create tiles for each entity
    for (int i = 0; i < ENTITY_COUNT; i++) {
        createEntityTile(i);
    }
    
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

// Fetch state for specific entity
bool fetchEntityState(int entityIndex) {
    if (entityIndex < 0 || entityIndex >= ENTITY_COUNT) return false;
    if (WiFi.status() != WL_CONNECTED) {
        lastError = "WiFi Disconnected";
        connectionEstablished = false;
        return false;
    }
    
    HTTPClient http;
    String url = String(HA_URL);
    if (!url.endsWith("/")) {
        url += "/";
    }
    url += "api/states/" + entities[entityIndex].entity_id;
    
    http.begin(url);
    http.addHeader("Authorization", "Bearer " + String(HA_TOKEN));
    http.addHeader("Content-Type", "application/json");
    
    int httpResponseCode = http.GET();
    
    if (httpResponseCode == 200) {
        String response = http.getString();
        
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, response);
        
        if (error) {
            lastError = "JSON Parse Error";
            http.end();
            return false;
        }
        
        // Parse entity state based on type
        String state = doc["state"];
        
        switch (entities[entityIndex].type) {
            case ENTITY_TOGGLE: {
                bool newState = (state == "on");
                if (entities[entityIndex].state != newState) {
                    entities[entityIndex].state = newState;
                    entities[entityIndex].lastState = !newState;
                }
                break;
            }
            case ENTITY_COVER: {
                entities[entityIndex].coverState = state;
                if (doc["attributes"]["current_position"].is<int>()) {
                    int newPosition = doc["attributes"]["current_position"];
                    if (entities[entityIndex].position != newPosition) {
                        entities[entityIndex].position = newPosition;
                        entities[entityIndex].lastPosition = -1; // Force UI update
                    }
                }
                break;
            }
            case ENTITY_THERMOSTAT: {
                if (doc["attributes"]["current_temperature"].is<float>()) {
                    entities[entityIndex].currentTemp = doc["attributes"]["current_temperature"];
                }
                if (doc["attributes"]["temperature"].is<float>()) {
                    float newTarget = doc["attributes"]["temperature"];
                    if (abs(entities[entityIndex].targetTemp - newTarget) > 0.1) {
                        entities[entityIndex].targetTemp = newTarget;
                        entities[entityIndex].lastTargetTemp = -1; // Force UI update
                    }
                }
                entities[entityIndex].hvacMode = doc["attributes"]["hvac_mode"] | "off";
                entities[entityIndex].hvacAction = doc["attributes"]["hvac_action"] | "idle";
                break;
            }
            case ENTITY_SENSOR: {
                entities[entityIndex].sensorValue = state;
                entities[entityIndex].sensorUnit = doc["attributes"]["unit_of_measurement"] | "";
                break;
            }
            case ENTITY_FAN: {
                bool fanOn = (state == "on");
                int newSpeed = 0;
                if (fanOn && doc["attributes"]["percentage"].is<int>()) {
                    newSpeed = doc["attributes"]["percentage"];
                }
                if (entities[entityIndex].fanSpeed != newSpeed) {
                    entities[entityIndex].fanSpeed = newSpeed;
                    entities[entityIndex].lastFanSpeed = -1; // Force UI update
                }
                break;
            }
        }
        
        // Set icon if not already processed
        if (!entities[entityIndex].iconFetched) {
            if (entities[entityIndex].icon.length() == 0) {
                String entityType = getEntityType(entities[entityIndex].entity_id);
                if (entityType == "input_boolean") {
                    entities[entityIndex].icon = "mdi:toggle-switch";
                } else if (entityType == "switch") {
                    entities[entityIndex].icon = "mdi:light-switch";
                } else if (entityType == "light") {
                    entities[entityIndex].icon = "mdi:lightbulb";
                } else if (entityType == "cover") {
                    entities[entityIndex].icon = "mdi:window-shutter";
                } else if (entityType == "climate") {
                    entities[entityIndex].icon = "mdi:thermostat";
                } else if (entityType == "sensor") {
                    entities[entityIndex].icon = "mdi:gauge";
                } else if (entityType == "fan") {
                    entities[entityIndex].icon = "mdi:fan";
                } else {
                    entities[entityIndex].icon = "mdi:help-circle";
                }
            }
            entities[entityIndex].iconFetched = true;
        }
        
        lastError = "";
        connectionEstablished = true;
        http.end();
        return true;
    } else {
        lastError = "HTTP " + String(httpResponseCode);
        connectionEstablished = false;
        http.end();
        return false;
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
            // Update position slider and label - simplified for compatibility
            if (entity.position != entity.lastPosition) {
                Serial.println("Cover position updated to: " + String(entity.position) + "%");
                entity.lastPosition = entity.position;
            }
            break;
        }
        case ENTITY_THERMOSTAT: {
            // Update temperature displays - simplified for compatibility
            Serial.println("Thermostat temperature: " + String(entity.currentTemp, 1) + "°C");
            break;
        }
        case ENTITY_SENSOR: {
            // Update sensor value - simplified for compatibility
            Serial.println("Sensor value: " + entity.sensorValue + " " + entity.sensorUnit);
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
            encoderCounter = 0;
            navigationCooldown = true;
            lastNavigationTime = millis();
            Serial.println("Encoder: Next tile - " + String(currentTileIndex));
        } else if (encoderCounter <= -2 && !navigationCooldown) {
            // Previous tile
            int prevTile = (currentTileIndex - 1 + ENTITY_COUNT) % ENTITY_COUNT;
            lv_obj_set_tile_id(tileview, prevTile, 0, LV_ANIM_ON);
            currentTileIndex = prevTile;
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
    
    connectToWiFi();
    
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
