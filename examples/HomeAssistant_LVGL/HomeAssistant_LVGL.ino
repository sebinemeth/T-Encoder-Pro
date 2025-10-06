/*
 * @Description: Multi-Entity LVGL-based Home Assistant Controller for T-Encoder Pro
 * @Author: WARP
 * @Date: 2024-10-06
 * @License: GPL 3.0
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
#include "entities.h"

// Multi-entity support - entities defined in entities.h
int currentEntityIndex = 0;
int lastEntityIndex = -1;

// Global variables
bool connectionEstablished = false;
String lastError = "";
unsigned long lastWiFiRetry = 0;
const unsigned long wifiRetryInterval = 30000;
unsigned long lastUpdateTime = 0;

// Optimistic UI variables
bool pendingToggle = false;
bool pendingToggleState = false;
int pendingEntityIndex = -1;
unsigned long toggleRequestTime = 0;
unsigned long lastToggleProcessTime = 0;
const unsigned long toggleProcessInterval = 100;

// LVGL objects
lv_obj_t *main_screen;
lv_obj_t *switch_widget;
lv_obj_t *title_label;
lv_obj_t *status_label;
lv_obj_t *update_label;
lv_obj_t *entity_indicator;

// Navigation debounce
bool navigationCooldown = false;
unsigned long lastNavigationTime = 0;
const unsigned long navigationCooldownTime = 250; // 250ms cooldown

// Encoder navigation variables
unsigned long lastEncoderTime = 0;
const unsigned long encoderDebounceTime = 50;

// Styles
lv_style_t style_main_bg;
lv_style_t style_title;
lv_style_t style_switch;
lv_style_t style_switch_knob;
lv_style_t style_switch_checked;
lv_style_t style_labels;

// Initialize entities from static definitions
void initializeEntities() {
    Serial.println("Initializing entities...");
    for (int i = 0; i < ENTITY_COUNT; i++) {
        Serial.println("Entity " + String(i) + ": " + String(entities[i].name) + " (" + String(entities[i].entity_id) + ")");
        // Reset state flags
        entities[i].state = false;
        entities[i].lastState = false;
    }
    Serial.println("Loaded " + String(ENTITY_COUNT) + " entities from static configuration");
}

// Helper function to extract entity type from entity_id
String getEntityType(String entityId) {
    int dotIndex = entityId.indexOf('.');
    if (dotIndex > 0) {
        return entityId.substring(0, dotIndex);
    }
    return "unknown";
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
        
        String state = doc["state"];
        bool newState = (state == "on");
        
        if (entities[entityIndex].state != newState) {
            entities[entityIndex].state = newState;
            entities[entityIndex].lastState = !newState; // Force UI update
            Serial.println("Entity " + String(entityIndex) + " state changed: " + (newState ? "ON" : "OFF"));
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

// Non-blocking toggle request for specific entity
void requestToggle(int entityIndex, bool targetState) {
    if (entityIndex < 0 || entityIndex >= ENTITY_COUNT) return;
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Toggle blocked - no WiFi connection");
        return;
    }
    
    pendingToggle = true;
    pendingToggleState = targetState;
    pendingEntityIndex = entityIndex;
    toggleRequestTime = millis();
    Serial.println("Toggle requested for entity " + String(entityIndex) + ": " + String(targetState ? "ON" : "OFF"));
}

// Background process to handle API calls
void processToggleRequest() {
    if (!pendingToggle || WiFi.status() != WL_CONNECTED || pendingEntityIndex < 0) {
        return;
    }
    
    HTTPClient http;
    String entityType = getEntityType(entities[pendingEntityIndex].entity_id);
    String service;
    String serviceUrl;
    
    if (entityType == "input_boolean") {
        service = "toggle";
        serviceUrl = String(HA_URL) + "api/services/input_boolean/" + service;
    } else if (entityType == "switch") {
        service = pendingToggleState ? "turn_on" : "turn_off";
        serviceUrl = String(HA_URL) + "api/services/switch/" + service;
    } else if (entityType == "light") {
        service = pendingToggleState ? "turn_on" : "turn_off";
        serviceUrl = String(HA_URL) + "api/services/light/" + service;
    } else {
        lastError = "Unsupported entity type: " + entityType;
        pendingToggle = false;
        return;
    }
    
    http.begin(serviceUrl);
    http.addHeader("Authorization", "Bearer " + String(HA_TOKEN));
    http.addHeader("Content-Type", "application/json");
    
    http.setTimeout(1000);
    http.setConnectTimeout(500);
    
    DynamicJsonDocument doc(256);
    doc["entity_id"] = entities[pendingEntityIndex].entity_id;
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    unsigned long startTime = millis();
    int httpResponseCode = http.POST(jsonString);
    unsigned long elapsed = millis() - startTime;
    
    Serial.println("HTTP call took: " + String(elapsed) + "ms");
    
    if (httpResponseCode == 200) {
        Serial.println("Toggle API call successful");
        lastError = "";
    } else {
        lastError = "Toggle failed: " + String(httpResponseCode);
        Serial.println("Toggle API call failed: " + String(httpResponseCode));
        
        // Revert the UI state since API call failed
        entities[pendingEntityIndex].state = !pendingToggleState;
        entities[pendingEntityIndex].lastState = !entities[pendingEntityIndex].state;
        Serial.println("API failed - reverting entity " + String(pendingEntityIndex));
    }
    
    http.end();
    pendingToggle = false;
    pendingEntityIndex = -1;
}

// Change to next/previous entity with simple transition
void changeEntity(bool next) {
    if (navigationCooldown || ENTITY_COUNT <= 1) return;
    
    int newIndex = currentEntityIndex;
    if (next) {
        newIndex = (currentEntityIndex + 1) % ENTITY_COUNT;
    } else {
        newIndex = (currentEntityIndex - 1 + ENTITY_COUNT) % ENTITY_COUNT;
    }
    
    if (newIndex == currentEntityIndex) return;
    
    currentEntityIndex = newIndex;
    
    // Set navigation cooldown to prevent rapid switching
    navigationCooldown = true;
    lastNavigationTime = millis();
    
    // Simple fade effect - briefly dim the title during transition
    lv_obj_set_style_text_opa(title_label, LV_OPA_50, 0);
    
    // Update UI immediately
    update_ui();
    
    // Restore title opacity
    lv_obj_set_style_text_opa(title_label, LV_OPA_COVER, 0);
    
    Serial.println("Switched to entity " + String(currentEntityIndex) + ": " + entities[currentEntityIndex].name);
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

// LVGL event handler
static void switch_event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_VALUE_CHANGED) {
        if (!connectionEstablished || currentEntityIndex < 0 || currentEntityIndex >= ENTITY_COUNT) {
            return;
        }
        
        bool is_checked = lv_obj_has_state(switch_widget, LV_STATE_CHECKED);
        Serial.println("Switch toggled (OPTIMISTIC): " + String(is_checked ? "ON" : "OFF"));
        
        // Optimistically update the current entity state
        entities[currentEntityIndex].state = is_checked;
        
        // Queue the API request
        requestToggle(currentEntityIndex, is_checked);
    }
}

// Handle rotary encoder input for navigation with discrete steps
void handleEncoder() {
    static uint8_t lastEncodeValue = 0;
    static int8_t encoderCounter = 0;
    
    uint8_t encodeValue = 0;
    
    // Read encoder pins
    if (digitalRead(KNOB_DATA_A)) encodeValue |= 0x01;
    if (digitalRead(KNOB_DATA_B)) encodeValue |= 0x02;
    
    // Detect rotation using full step detection
    if (encodeValue != lastEncodeValue && (millis() - lastEncoderTime > encoderDebounceTime)) {
        
        // Clockwise rotation pattern: 00 -> 01 -> 11 -> 10 -> 00 (full cycle)
        if ((lastEncodeValue == 0x00 && encodeValue == 0x01) ||
            (lastEncodeValue == 0x01 && encodeValue == 0x03) ||
            (lastEncodeValue == 0x03 && encodeValue == 0x02) ||
            (lastEncodeValue == 0x02 && encodeValue == 0x00)) {
            encoderCounter++;
        }
        // Counter-clockwise rotation pattern: 00 -> 10 -> 11 -> 01 -> 00 (full cycle)
        else if ((lastEncodeValue == 0x00 && encodeValue == 0x02) ||
                 (lastEncodeValue == 0x02 && encodeValue == 0x03) ||
                 (lastEncodeValue == 0x03 && encodeValue == 0x01) ||
                 (lastEncodeValue == 0x01 && encodeValue == 0x00)) {
            encoderCounter--;
        }
        
        lastEncodeValue = encodeValue;
        lastEncoderTime = millis();
        
        // Check for full detent steps (every 2 state changes for this encoder)
        if (encoderCounter >= 2) {
            // Full clockwise detent - next entity
            changeEntity(true);
            encoderCounter = 0;
            Serial.println("Encoder: Clockwise detent detected");
        } else if (encoderCounter <= -2) {
            // Full counter-clockwise detent - previous entity  
            changeEntity(false);
            encoderCounter = 0;
            Serial.println("Encoder: Counter-clockwise detent detected");
        }
    }
}

// Create styles
void create_styles() {
    lv_style_init(&style_main_bg);
    lv_style_set_bg_color(&style_main_bg, lv_color_hex(0x000000));
    
    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, lv_color_white());
    lv_style_set_text_font(&style_title, &lv_font_montserrat_18);
    
    lv_style_init(&style_switch);
    lv_style_set_bg_color(&style_switch, lv_color_hex(0x3C3C3C));
    lv_style_set_border_color(&style_switch, lv_color_hex(0x666666));
    lv_style_set_border_width(&style_switch, 2);
    lv_style_set_pad_all(&style_switch, 4);
    
    lv_style_init(&style_switch_knob);
    lv_style_set_bg_color(&style_switch_knob, lv_color_white());
    lv_style_set_border_color(&style_switch_knob, lv_color_hex(0xCCCCCC));
    lv_style_set_border_width(&style_switch_knob, 1);
    
    lv_style_init(&style_switch_checked);
    lv_style_set_bg_color(&style_switch_checked, lv_color_hex(0x4CAF50));
    lv_style_set_border_color(&style_switch_checked, lv_color_hex(0x45A049));
    
    lv_style_init(&style_labels);
    lv_style_set_text_color(&style_labels, lv_color_hex(0xCCCCCC));
    lv_style_set_text_font(&style_labels, &lv_font_montserrat_14);
}

// Create UI
void create_ui() {
    main_screen = lv_obj_create(NULL);
    lv_obj_add_style(main_screen, &style_main_bg, 0);
    
    title_label = lv_label_create(main_screen);
    lv_obj_add_style(title_label, &style_title, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 20);
    
    status_label = lv_label_create(main_screen);
    lv_label_set_text(status_label, "Initializing...");
    lv_obj_add_style(status_label, &style_labels, 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 50);
    
    switch_widget = lv_switch_create(main_screen);
    lv_obj_set_size(switch_widget, 120, 60);
    lv_obj_add_style(switch_widget, &style_switch, LV_PART_MAIN);
    lv_obj_add_style(switch_widget, &style_switch_knob, LV_PART_KNOB);
    lv_obj_add_style(switch_widget, &style_switch_checked, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_align(switch_widget, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(switch_widget, switch_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Add switch to the default group for encoder navigation and touch input
    lv_group_t *default_group = lv_group_get_default();
    if (default_group) {
        lv_group_add_obj(default_group, switch_widget);
    }
    
    entity_indicator = lv_label_create(main_screen);
    lv_obj_add_style(entity_indicator, &style_labels, 0);
    lv_obj_align(entity_indicator, LV_ALIGN_BOTTOM_MID, 0, -50);
    
    update_label = lv_label_create(main_screen);
    lv_label_set_text(update_label, "Updated: 0s ago");
    lv_obj_add_style(update_label, &style_labels, 0);
    lv_obj_align(update_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    lv_scr_load(main_screen);
}

// Update UI
void update_ui() {
    if (currentEntityIndex < 0 || currentEntityIndex >= ENTITY_COUNT) return;
    
    HAEntity &current = entities[currentEntityIndex];
    
    // Update entity name
    lv_label_set_text(title_label, current.name.c_str());
    
    // Update switch state - check both state change AND entity change
    if (current.state != current.lastState || lastEntityIndex != currentEntityIndex) {
        Serial.println("UI Update: Entity " + String(currentEntityIndex) + " state=" + String(current.state ? "ON" : "OFF"));
        if (current.state) {
            lv_obj_add_state(switch_widget, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(switch_widget, LV_STATE_CHECKED);
        }
        current.lastState = current.state;
        lastEntityIndex = currentEntityIndex;
    }
    
    // Update status
    if (connectionEstablished) {
        lv_label_set_text(status_label, "Connected");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x4CAF50), 0);
    } else {
        lv_label_set_text(status_label, lastError.c_str());
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xF44336), 0);
    }
    
    // Update entity indicator
    if (ENTITY_COUNT > 1) {
        String indicator = String(currentEntityIndex + 1) + "/" + String(ENTITY_COUNT);
        lv_label_set_text(entity_indicator, indicator.c_str());
    } else {
        lv_label_set_text(entity_indicator, "");
    }
    
    // Update time label
    String updateText = "Updated: " + String((millis() - lastUpdateTime) / 1000) + "s ago";
    lv_label_set_text(update_label, updateText.c_str());
}

void setup() {
    Serial.begin(115200);
    Serial.println("Multi-Entity LVGL Home Assistant Controller Starting...");
    
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
    
    create_styles();
    create_ui();
    
    lv_timer_handler();
    delay(500);
    
    connectToWiFi();
    
    // Initial state fetch for all entities
    for (int i = 0; i < ENTITY_COUNT; i++) {
        fetchEntityState(i);
        delay(100); // Small delay between requests
    }
    
    lastUpdateTime = millis();
    lastWiFiRetry = millis();
    Serial.println("Setup complete!");
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
    
    // Process pending toggles
    if (pendingToggle && millis() - lastToggleProcessTime >= toggleProcessInterval) {
        processToggleRequest();
        lastToggleProcessTime = millis();
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
        
        // Periodic state updates - fetch all entities to keep them synchronized
        if (!pendingToggle && millis() - lastUpdateTime > UPDATE_INTERVAL_MS) {
            // Fetch all entities to keep states synchronized
            for (int i = 0; i < ENTITY_COUNT; i++) {
                fetchEntityState(i);
                delay(50); // Small delay between requests
            }
            lastUpdateTime = millis();
        }
    }
    
    update_ui();
    delay(5);
}
