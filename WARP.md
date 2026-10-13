# WARP.md

This file provides guidance to WARP (warp.dev) when working with code in this repository.

## Project Overview

Home Assistant controller for the LilyGO T-Encoder-Pro. Hardware, upstream references and encoder guidelines are in [AGENTS.md](AGENTS.md). Build: `venv/bin/pio run` (single env `HomeAssistant_LVGL`).

## HomeAssistant_LVGL Project

### Overview
A professional multi-entity Home Assistant controller with dual input methods (touch + encoder) and optimistic UI updates.

### Features
- **Multi-Entity Support:** Control multiple Home Assistant entities (switches, lights, input_boolean, covers)
- **Cover Control:** 3-action cover control (Open/Close/Stop) via encoder button cycling
- **Dual Input Methods:** Touch screen and rotary encoder navigation
- **Optimistic UI:** Immediate visual feedback with background API calls
- **Non-blocking Operations:** Asynchronous API calls with pending request queuing
- **WiFi Management:** Automatic connection with retry logic
- **Static Configuration:** Compile-time entity definitions in `entities.h`
- **Discrete Encoder Steps:** Precise detent detection for smooth navigation
- **Touch Integration:** Direct switch toggling via CHSC5816 touch controller
- **Input Conflict Prevention:** Lockout system prevents simultaneous touch/encoder conflicts

### Configuration Files
- **`ha_config.h`:** Default/template configuration with safe placeholder values
- **`ha_config_private.h`:** Override file with actual sensitive values (gitignored)
- **`entities.h`:** Static entity definitions (name, entity_id, type)
- **`platformio.ini`:** Build configuration with required libraries
- **`.env`:** Environment variables for sensitive data (gitignored)
- **`.env.example`:** Template for environment configuration

### Security Setup
1. **Copy environment template:** `cp .env.example .env`
2. **Edit .env:** Add your actual WiFi and Home Assistant credentials
3. **Verify .gitignore:** Ensure .env and *_private.h files are ignored
4. **Private config:** The ha_config_private.h file overrides default values
5. **Never commit:** Sensitive values should only exist in gitignored files

### Usage
1. **Navigation:** Rotate encoder to switch between entities
2. **Toggle Entities:** Touch the switch widget or press encoder button (for switches/lights)
3. **Cover Control:** For cover entities, encoder button cycles through Open → Close → Stop actions
4. **Visual Feedback:** Entity states synchronized with Home Assistant
5. **Status Display:** Connection status, update timestamps, and next cover action
6. **Action Feedback:** Status label shows next cover action for covers, connection status for toggles

### Build and Deploy
```bash

# Build and upload
pio run --target upload

# Monitor output
pio device monitor --baud 115200
```

### API Integration
- **Supported Entity Types:** switch, light, input_boolean, cover
- **Hybrid Architecture:** Combines REST API + WebSocket for optimal performance
  - **REST API:** Initial state fetching and 15-second fallback polling
  - **WebSocket API:** Real-time state change notifications and command sending
- **API Endpoints:** Uses Home Assistant REST API and WebSocket API
  - Toggle entities: `/api/services/{domain}/{service}` (turn_on/turn_off/toggle)
  - Cover entities: `/api/services/cover/{action}` (open_cover/close_cover/stop_cover)
  - WebSocket: `/api/websocket` for real-time event subscriptions
- **Authentication:** Bearer token authentication for both REST and WebSocket
- **Error Handling:** Graceful fallback with state reversion on API failures
- **Performance:** REST API response times 35-127ms, WebSocket real-time (<100ms)
- **Background Processing:** Non-blocking API calls and WebSocket events processed in main loop

### Input System
- **Encoder Navigation:** Discrete step detection (2 state changes per detent)
- **Touch Toggle:** Direct widget interaction via LVGL touch events (toggle entities only)
- **Encoder Button:** Context-sensitive behavior:
  - Toggle entities: Standard on/off toggle
  - Cover entities: Cycles through Open → Close → Stop actions
- **Debouncing:** 50ms encoder debounce, 250ms navigation cooldown
- **Input Lockout:** 500ms lockout prevents touch/encoder conflicts
- **Dual Mode:** Both input methods work simultaneously with conflict prevention

### Memory and Performance
- **RAM Usage:** ~113KB (34.6% of available)
- **Flash Usage:** ~1.1MB (17.3% of available) 
- **Libraries:** ArduinoJson, LVGL, SensorLib, WiFi, HTTPClient, WebSockets
- **Startup Time:** ~3-5 seconds including WiFi connection
- **Request Processing:** 100ms interval for background API call processing
- **Entity Support:** Handles multiple entity types with type-specific behaviors
- **WebSocket Buffer:** 16KB message buffer handles large Home Assistant event payloads

### WebSocket Troubleshooting

**Common Issue: ArduinoJson Type Checking Problems**

If WebSocket events show "event is not an object" errors despite valid JSON:
- **Problem:** ArduinoJson's `is<JsonObject>()` can fail on valid objects accessed via `JsonVariantConst`
- **Symptoms:** JSON parsing succeeds but nested object access fails with type errors
- **Solution:** Use `containsKey()` instead of `is<JsonObject>()` for existence checks
- **Fixed in:** `ha_websocket_manager_v2.h` - removed strict type checks, uses direct access

**Debugging WebSocket Events:**
```bash
# Monitor serial output to see WebSocket messages
pio device monitor --baud 115200

# Look for these log patterns:
# [WS] JSON parsed successfully - confirms message parsing
# [WS] Event type: state_changed - confirms event processing 
# [WS] State changed for entity: light.desk_lamp - confirms entity extraction
# [WS] MATCH! Processing entity 0 - confirms entity found and processed
```

**Message Buffer Sizing:**
- Set `WS_MAX_MESSAGE_SIZE` to 16384 bytes in `ha_websocket_config.h`
- Home Assistant state_changed events can exceed 1KB with full entity attributes
- Monitor memory usage in logs: "memory usage: 1117/16384 bytes"

**Current Status:** ✅ **Fully Operational**
- WebSocket connection established and authenticated
- Real-time event processing working correctly
- Entity state synchronization active
- Hybrid REST+WebSocket architecture complete
