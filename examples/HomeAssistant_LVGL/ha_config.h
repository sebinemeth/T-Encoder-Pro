/*
 * @Description: Home Assistant Configuration
 * @Author: WARP
 * @Date: 2024-10-06
 * @License: GPL 3.0
 */
#pragma once

// WiFi Configuration
// IMPORTANT: Update these values with your actual WiFi credentials
// Put real values in ha_config_private.h (gitignored), see ha_config_private.h.example
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
