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
#include "port/screenshot.h"
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
#include "ui/ui_theme.h"
#include "ui/ui_tiles.h"

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


// Forward declarations
void processAPIRequest();
void startWiFi();
void serviceWiFi();
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

// ---- Value setters shared by touch handlers and the encoder ----
// fromTouch: the widget was already moved by the user, so don't touch it again.

void setCoverPosition(int entityIndex, int value, bool fromTouch) {
    HAEntity& entity = entities[entityIndex];
    value = constrain(value, 0, 100);
    entity.position = value;
    if (!fromTouch && entity.wControl) lv_slider_set_value(entity.wControl, value, LV_ANIM_OFF);
    if (entity.wValue) lv_label_set_text_fmt(entity.wValue, "%d%%", value);
    handleEntityAction(entityIndex, "cover_set_position", String(value), valueCommandDebounce);
}

void setFanSpeed(int entityIndex, int value, bool fromTouch) {
    HAEntity& entity = entities[entityIndex];
    value = constrain(value, 0, 100);
    entity.fanSpeed = value;
    if (!fromTouch && entity.wControl) lv_slider_set_value(entity.wControl, value, LV_ANIM_OFF);
    if (entity.wValue) lv_label_set_text_fmt(entity.wValue, "%d%%", value);
    handleEntityAction(entityIndex, "fan_set_speed", String(value), valueCommandDebounce);
}

void setTargetTemp(int entityIndex, float temp) {
    HAEntity& entity = entities[entityIndex];
    temp = constrain(snapToStep(temp, thermostatStep), entity.minTemp, entity.maxTemp);
    entity.targetTemp = temp;
    if (entity.wTarget) lv_label_set_text_fmt(entity.wTarget, "Target: %.1f°C", temp);
    if (entity.wControl) lv_arc_set_value(entity.wControl, tempToArcValue(temp, entity.minTemp, entity.maxTemp));
    handleEntityAction(entityIndex, "set_temperature", String(temp, 1), valueCommandDebounce);
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
        createEntityTile(i, tileview);
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
            screenshotBegin();
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
        
        ServiceCall call = buildServiceCall(entities[i].entity_id.c_str(), action.c_str(), value.c_str());
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
            sent = wsManager.callService(call.domain, call.service, i);
        } else if (call.isFloat) {
            sent = wsManager.callService(call.domain, call.service, i, call.key, call.floatValue);
        } else {
            sent = wsManager.callService(call.domain, call.service, i, call.key, call.intValue);
        }
        
        if (sent) {
            Serial.println("Sent " + String(call.domain) + "." + call.service + " for " + entities[i].entity_id + " via WebSocket");
        } else if (postServiceViaREST(i, call)) {
            Serial.println("Sent " + String(call.domain) + "." + call.service + " for " + entities[i].entity_id + " via REST");
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
bool userHoldsControl(int entityIndex) {
    if (editMode && entityIndex == currentTileIndex) return true; // whole edit session
    return millis() - lastUserInteraction[entityIndex] < uiSyncHoldoff;
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

#if ENABLE_SCREENSHOT_SERVER
// Called from the screenshot server's /tile route (main task)
void debugGoToTile(int index, bool edit) {
    if (index < 0 || index >= ENTITY_COUNT) return;
    exitEditMode();
    navigateTiles(index - currentTileIndex);
    if (edit && isAdjustable(entities[index].type)) enterEditMode();
}
#endif

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
    
    uiCreateStyles();
    createUI();
    
    lv_timer_handler();
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    Serial.printf("LVGL heap after UI: free %u of %u bytes, biggest block %u\n", (unsigned)mon.free_size, (unsigned)(mon.total_size), (unsigned)mon.free_biggest_size);
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
    screenshotLoop();
    
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
