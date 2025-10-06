/*
 * @Description: Entity definitions for Home Assistant Controller
 * @Author: WARP
 * @Date: 2024-10-06
 * @License: GPL 3.0
 */
#pragma once

struct HAEntity {
    String name;           // Display name for the entity
    String entity_id;      // Home Assistant entity ID (e.g., "light.living_room")
    bool state;            // Current state (managed automatically)
    bool lastState;        // Previous state (managed automatically)
    String icon;           // Material Design icon (e.g., "mdi:lightbulb") or "" for auto-detect
    bool iconFetched;      // Flag to track if icon has been retrieved (managed automatically)
};

/*
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
 *    - unknown -> mdi:help-circle
 * 
 * Find icons at: https://materialdesignicons.com/
 */

// Define your entities here - simply add/remove/modify as needed
const int ENTITY_COUNT = 2;

HAEntity entities[ENTITY_COUNT] = {
    {"LilyGo Test", "input_boolean.lilygo_test", false, false, "mdi:home-automation", false},  // Custom user-configured icon
    {"LilyGo Test 2", "input_boolean.lilygo_test_2", false, false, "", false}                   // Empty = use domain fallback
};
