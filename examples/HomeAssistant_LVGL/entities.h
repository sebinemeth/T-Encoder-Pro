/*
 * @Description: Entity definitions for Home Assistant Controller
 * @Author: WARP
 * @Date: 2024-10-06
 * @License: GPL 3.0
 */
#pragma once

#include "entities_types.h"

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
