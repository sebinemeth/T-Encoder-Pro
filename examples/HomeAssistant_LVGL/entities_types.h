/*
 * @Description: Entity struct and domain helpers (no entity list: see entities.h)
 * @Author: WARP
 * @Date: 2024-10-06
 * @License: GPL 3.0
 */
#pragma once

#include <Arduino.h>
#include "lvgl.h"
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

extern HAEntity entities[];  // defined in entities.h (included by the sketch only)
