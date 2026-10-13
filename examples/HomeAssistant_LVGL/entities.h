/*
 * @Description: Entity definitions for Home Assistant Controller
 * @Author: WARP
 * @Date: 2024-10-06
 * @License: GPL 3.0
 */
#pragma once

#include "app_logic.h"

struct HAEntity {
    String name;           // Display name for the entity
    String entity_id;      // Home Assistant entity ID (e.g., "light.living_room")
    EntityType type;       // Entity type (managed automatically based on domain)
    
    // Toggle entity fields
    bool state;            // Current state for toggle entities (managed automatically)
    bool lastState;        // Previous state for toggle entities (managed automatically)
    
    // Cover entity fields
    int position;          // Current position for cover entities (0-100, managed automatically)
    int lastPosition;      // Previous position for cover entities (managed automatically)
    String coverState;     // Cover state: "open", "closed", "opening", "closing", "stopped"
    
    // Thermostat entity fields
    float currentTemp;     // Current temperature
    float lastCurrentTemp; // Previous current temperature for UI updates
    float targetTemp;      // Target temperature
    float lastTargetTemp;  // Previous target temperature for UI updates
    float minTemp;         // Minimum temperature range from HA
    float maxTemp;         // Maximum temperature range from HA
    String hvacMode;       // HVAC mode: "off", "heat", "cool", "auto", "heat_cool"
    String hvacAction;     // Current action: "idle", "heating", "cooling"
    
    // Fan entity fields
    int fanSpeed;          // Fan speed percentage (0-100)
    int lastFanSpeed;      // Previous fan speed for UI updates
    
    // Sensor entity fields
    String sensorValue;    // Current sensor value
    String lastSensorValue;// Previous sensor value for UI updates
    String sensorUnit;     // Sensor unit of measurement
    
    // Common fields
    String icon;           // Material Design icon (e.g., "mdi:lightbulb") or "" for auto-detect
    bool iconFetched;      // Flag to track if icon has been retrieved (managed automatically)
    
    // Tile-specific fields
    lv_obj_t* tile;        // Pointer to the tile object for this entity
    bool tileCreated;      // Flag to track if tile UI has been created
    
    // Widget references, set when the tile is created
    lv_obj_t* wControl;    // switch / slider / arc
    lv_obj_t* wValue;      // main value label (position, current temp, sensor value, fan speed)
    lv_obj_t* wTarget;     // thermostat target label
    lv_obj_t* wStatus;     // status / HVAC label
    lv_obj_t* wUnit;       // sensor unit label
    lv_obj_t* wActual;     // cover: marker for the real position (wControl shows the target)
};

// ---- Domain helpers (single source of truth for type/icon mapping) ----

// Extract the domain from an entity_id ("light.desk" -> "light")
inline String getEntityType(const String& entityId) {
    char domain[24];
    domainOf(entityId.c_str(), domain, sizeof(domain));
    return String(domain);
}

inline EntityType entityTypeForDomain(const String& domain) { return entityTypeForDomain(domain.c_str()); }
inline const char* defaultIconForDomain(const String& domain) { return defaultIconForDomain(domain.c_str()); }

// Fill in the default icon once, if none was configured
inline void ensureEntityIcon(HAEntity& e) {
    if (!e.iconFetched) {
        if (e.icon.length() == 0) e.icon = defaultIconForDomain(getEntityType(e.entity_id));
        e.iconFetched = true;
    }
}

/*
 * Entity Type Support:
 * - ENTITY_TOGGLE: For lights, switches, input_boolean (uses switch widget)
 * - ENTITY_COVER: For covers like blinds, shutters (uses position slider and action buttons)
 * - ENTITY_THERMOSTAT: For climate entities (uses temperature controls)
 * - ENTITY_SENSOR: For sensors (read-only display with value)
 * - ENTITY_FAN: For fans (uses speed control slider)
 *
 * Icon Configuration Guide:
 * Two-priority system:
 * 1. User-configured icons: Provide a custom MDI icon (e.g., "mdi:lightbulb", "mdi:home-automation")
 * 2. Domain-based fallback: Leave empty "" to auto-assign based on entity domain:
 *    - input_boolean -> mdi:toggle-switch
 *    - switch -> mdi:light-switch  
 *    - light -> mdi:lightbulb
 *    - binary_sensor -> mdi:radiobox-marked
 *    - sensor -> mdi:gauge
 *    - fan -> mdi:fan
 *    - climate -> mdi:thermostat
 *    - cover -> mdi:window-shutter
 *    - unknown -> mdi:help-circle
 * 
 * Find icons at: https://materialdesignicons.com/
 * 
 * Each entity now gets its own tile in a swipeable tileview layout with
 * specialized controls based on the entity type.
 */

// Define your entities here - simply add/remove/modify as needed
// NOTE: When adding new entities, only specify the basic fields.
// All other fields are automatically initialized in initializeEntities()
const int ENTITY_COUNT = 4;

HAEntity entities[ENTITY_COUNT] = {
    // Basic initialization - extended fields are set automatically in initializeEntities()
    {"Desk Lamp", "light.desk_lamp", ENTITY_TOGGLE, false, false, 0, 0, "", 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, "", "", 0, 0, "", "", "", "", false, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"Bedroom Cover", "cover.bedroom_cover", ENTITY_COVER, false, false, 0, 0, "", 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, "", "", 0, 0, "", "", "", "mdi:blinds", false, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"Bedroom Radiator", "climate.bedroom_radiator", ENTITY_THERMOSTAT, false, false, 0, 0, "", 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, "", "", 0, 0, "", "", "", "", false, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"Temperature Sensor", "sensor.living_room_th_temperature", ENTITY_SENSOR, false, false, 0, 0, "", 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, "", "", 0, 0, "", "", "", "", false, nullptr, false, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}
};
