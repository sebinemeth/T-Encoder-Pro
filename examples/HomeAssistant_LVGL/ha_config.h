/*
 * @Description: Home Assistant Configuration
 * @Author: WARP
 * @Date: 2024-10-06
 * @License: GPL 3.0
 */
#pragma once

// WiFi Configuration
// IMPORTANT: Update these values with your actual WiFi credentials
// Consider using environment variables or a separate config file for sensitive data
#ifndef WIFI_SSID
#define WIFI_SSID "your_wifi_network"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "your_wifi_password"
#endif

// Home Assistant Configuration
// IMPORTANT: Update these values with your actual Home Assistant server details
#ifndef HA_URL
#define HA_URL "http://192.168.1.100:8123/"
#endif
#ifndef HA_TOKEN
#define HA_TOKEN "your_home_assistant_token_here"
#endif

// Entity Configuration
#define SWITCH_ENTITY_ID "input_boolean.lilygo_test"
#define SWITCH_FRIENDLY_NAME "LilyGo Test"

// Display Configuration
#define UPDATE_INTERVAL_MS 5000  // Update entity state every 5 seconds
#define DEBOUNCE_DELAY_MS 300    // Debounce for encoder button press
