/*
 * @Description: Pure application logic (no Arduino/LVGL dependencies) so it can be
 *               unit tested on the host with `pio test -e native`.
 * @License: GPL 3.0
 */
#pragma once

#include <math.h>
#include <stddef.h>
#include <string.h>

enum EntityType {
    ENTITY_TOGGLE,      // For lights, switches, input_boolean (uses switch widget)
    ENTITY_COVER,       // For covers like blinds, shutters (uses position slider and action buttons)
    ENTITY_THERMOSTAT,  // For climate entities (uses temperature controls)
    ENTITY_SENSOR,      // For sensors (read-only display)
    ENTITY_FAN          // For fans (uses speed control)
};

// Copy the domain of an entity_id ("light.desk" -> "light") into out.
// Returns false (and writes "unknown") if there is no domain or it does not fit.
inline bool domainOf(const char* entityId, char* out, size_t outSize) {
    const char* dot = entityId ? strchr(entityId, '.') : nullptr;
    size_t len = dot ? (size_t)(dot - entityId) : 0;
    if (len == 0 || len >= outSize) {
        strncpy(out, "unknown", outSize - 1);
        out[outSize - 1] = '\0';
        return false;
    }
    memcpy(out, entityId, len);
    out[len] = '\0';
    return true;
}

inline EntityType entityTypeForDomain(const char* domain) {
    if (strcmp(domain, "cover") == 0) return ENTITY_COVER;
    if (strcmp(domain, "climate") == 0) return ENTITY_THERMOSTAT;
    if (strcmp(domain, "sensor") == 0) return ENTITY_SENSOR;
    if (strcmp(domain, "fan") == 0) return ENTITY_FAN;
    return ENTITY_TOGGLE; // lights, switches, input_boolean, ...
}

inline const char* defaultIconForDomain(const char* domain) {
    if (strcmp(domain, "input_boolean") == 0) return "mdi:toggle-switch";
    if (strcmp(domain, "switch") == 0) return "mdi:light-switch";
    if (strcmp(domain, "light") == 0) return "mdi:lightbulb";
    if (strcmp(domain, "cover") == 0) return "mdi:window-shutter";
    if (strcmp(domain, "climate") == 0) return "mdi:thermostat";
    if (strcmp(domain, "sensor") == 0) return "mdi:gauge";
    if (strcmp(domain, "fan") == 0) return "mdi:fan";
    return "mdi:help-circle";
}

// ---- Thermostat arc math ----

inline float snapToStep(float value, float step) {
    return roundf(value / step) * step;
}

// Temperature -> arc value 0..100 within [minTemp, maxTemp] (clamped)
inline int tempToArcValue(float temp, float minTemp, float maxTemp) {
    float range = maxTemp - minTemp;
    if (range <= 0) return 0;
    int value = (int)lroundf((temp - minTemp) / range * 100.0f);
    return value < 0 ? 0 : (value > 100 ? 100 : value);
}

// Arc value 0..100 -> temperature within [minTemp, maxTemp]
inline float arcValueToTemp(int arcValue, float minTemp, float maxTemp) {
    return minTemp + (arcValue / 100.0f) * (maxTemp - minTemp);
}
