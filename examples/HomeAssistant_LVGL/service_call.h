/*
 * @Description: A Home Assistant service call derived from a queued UI action
 * @License: GPL 3.0
 */
#pragma once

#include <Arduino.h>

struct ServiceCall {
    String domain;
    String service;
    const char* key = nullptr;  // optional single data field
    bool isFloat = false;
    int intValue = 0;
    float floatValue = 0;
    bool valid = false;
};
