/*
 * @Description: Maps a queued UI action to a Home Assistant service call.
 *               Pure logic, unit tested with `pio test -e native`.
 * @License: GPL 3.0
 */
#pragma once

#include <stdlib.h>
#include <string.h>
#include "app_logic.h"

struct ServiceCall {
    char domain[24];
    char service[24];
    const char* key;  // optional single data field, nullptr if none
    bool isFloat;
    int intValue;
    float floatValue;
    bool valid;
};

// action: "toggle" (value "1"/"true" = on), "cover_open", "cover_close", "cover_stop",
// "cover_set_position", "fan_set_speed", "set_temperature"
inline ServiceCall buildServiceCall(const char* entityId, const char* action, const char* value) {
    ServiceCall call = {};
    char entityDomain[24];
    domainOf(entityId, entityDomain, sizeof(entityDomain));

    auto set = [&call](const char* domain, const char* service) {
        strncpy(call.domain, domain, sizeof(call.domain) - 1);
        strncpy(call.service, service, sizeof(call.service) - 1);
        call.valid = true;
    };

    if (strcmp(action, "toggle") == 0) {
        // Desired state is passed explicitly, so repeating the call is harmless
        bool on = strcmp(value, "1") == 0 || strcmp(value, "true") == 0;
        bool supported = strcmp(entityDomain, "light") == 0 || strcmp(entityDomain, "switch") == 0 ||
                         strcmp(entityDomain, "input_boolean") == 0;
        if (supported) set(entityDomain, on ? "turn_on" : "turn_off");
    } else if (strcmp(action, "cover_open") == 0) {
        set("cover", "open_cover");
    } else if (strcmp(action, "cover_close") == 0) {
        set("cover", "close_cover");
    } else if (strcmp(action, "cover_stop") == 0) {
        set("cover", "stop_cover");
    } else if (strcmp(action, "cover_set_position") == 0) {
        set("cover", "set_cover_position");
        call.key = "position";
        call.intValue = atoi(value);
    } else if (strcmp(action, "fan_set_speed") == 0) {
        set("fan", "set_percentage");
        call.key = "percentage";
        call.intValue = atoi(value);
    } else if (strcmp(action, "set_temperature") == 0) {
        set("climate", "set_temperature");
        call.key = "temperature";
        call.isFloat = true;
        call.floatValue = (float)atof(value);
    }
    return call;
}
