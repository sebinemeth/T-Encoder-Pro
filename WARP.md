# WARP.md

This file provides guidance to WARP (warp.dev) when working with code in this repository.

## Project Overview

This is a hardware project for the **LilyGo T-Encoder Pro**, a smart control knob based on ESP32-S3R8 with:
- 1.2-inch round AMOLED touchscreen (SH8601A driver)
- CHSC5816 touch controller
- Rotary encoder with button
- Buzzer and WS2812B RGB LEDs
- Wi-Fi connectivity

## Development Environment

### Primary Build System: PlatformIO
The project uses PlatformIO as the primary build system with `platformio.ini` configuration.

**Build and upload commands:**
```bash
# Build the project
pio run

# Upload to device
pio run --target upload

# Build specific example (change default_envs in platformio.ini first)
pio run -e UI_demo

# Monitor serial output
pio device monitor --baud 115200
```

**To switch examples:** Edit `platformio.ini` and uncomment the desired `default_envs` line (currently set to `UI_demo`).

### Secondary: Arduino IDE
Also supports Arduino IDE development. Examples are in `examples/` directory with `.ino` files.

**Arduino IDE settings required:**
- Board: ESP32S3 Dev Module
- Upload Speed: 921600
- Flash Size: 16MB (128Mb)
- Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)
- PSRAM: OPI PSRAM

## Architecture and Code Structure

### Hardware Abstraction
- **Pin definitions:** `libraries/Mylibrary/pin_config.h` - Central pin configuration file
- **Display:** Uses Arduino_GFX library with QSPI interface to SH8601A AMOLED controller
- **Touch:** CHSC5816 via I2C (SDA=5, SCL=6)
- **Rotary Encoder:** GPIO1, GPIO2 (data A/B), GPIO0 (button)
- **Buzzer:** GPIO17 with PWM control

### Example Projects
- **GFX:** Basic display testing using Arduino_GFX
- **CHSC5816:** Touch controller testing
- **Rotary_Encoder:** Encoder input handling with state machine
- **Lvgl_CIT:** Factory testing program with LVGL GUI
- **UI_demo:** Advanced demo with Wi-Fi, WS2812B, servo motor, EEPROM
- **HomeAssistant_LVGL:** Multi-entity Home Assistant controller with touch and encoder input

### Key Libraries
- `Arduino_GFX-1.3.7`: Display driver
- `lvgl-8.3.5`: GUI framework for advanced examples
- `SensorLib-0.1.4`: Touch controller support
- `FastLED-3.6.0`: WS2812B RGB LED control
- `ESP32Servo`: Servo motor control

### Display and Graphics
- **Resolution:** 390x390 pixels
- **Color format:** 16-bit RGB565
- **LVGL integration:** Used in advanced examples for GUI
- **Brightness control:** Via `gfx->Display_Brightness(0-255)`

### Hardware Interfaces
- **Touch handling:** Polling-based in LVGL examples, interrupt support available
- **Rotary encoder:** Custom state machine implementation for rotation detection
- **Wi-Fi:** Standard ESP32 WiFi library with NTP time sync
- **EEPROM:** Settings persistence using ESP32 EEPROM library

## Development Patterns

### State Machine Pattern
The rotary encoder uses a state machine pattern to detect clockwise/counterclockwise rotation by monitoring the quadrature signals.

### LVGL Message System
Advanced examples use LVGL's message system (`lv_msg_`) for component communication, allowing decoupled UI updates.

### Hardware Initialization Sequence
1. Enable LCD power (LCD_VCI_EN high)
2. Initialize display bus and controller
3. Initialize touch controller via I2C
4. Set up input devices (encoder, touch)
5. Initialize LVGL if using GUI

### Memory Management
- Uses `heap_caps_malloc()` for LVGL display buffers with MALLOC_CAP_INTERNAL
- EEPROM used for persistent settings (brightness, touch settings, etc.)

## Common Development Tasks

### Testing Hardware Components
```bash
# Test display functionality
pio run -e GFX

# Test touch input
pio run -e CHSC5816

# Test rotary encoder
pio run -e Rotary_Encoder

# Factory test (comprehensive)
pio run -e Lvgl_CIT
```

### Debugging
- Serial output at 115200 baud
- USB CDC enabled by default (`ARDUINO_USB_CDC_ON_BOOT=1`)
- Use external UART by setting `ARDUINO_USB_CDC_ON_BOOT=0`

### Firmware Flashing
Pre-built firmware available in `firmware/` directory. Use ESP32 flash tool in `tools/` directory or:
```bash
# Hold BOOT button if upload fails
pio run --target upload
```

### Adding New Features
- Pin definitions go in `libraries/Mylibrary/pin_config.h`
- New examples should follow the existing structure in `examples/`
- Use the established hardware initialization patterns
- For LVGL projects, follow the message-based communication pattern

## Hardware-Specific Notes

- **Power management:** LCD_VCI_EN (GPIO3) must be HIGH to power the display
- **Touch sensitivity:** CHSC5816 supports multi-touch but examples typically use single touch
- **Encoder debouncing:** 20ms polling cycle used in examples for stable operation
- **WS2812B LEDs:** 12 LEDs on GPIO43, uses GRB color order
- **Servo control:** Uses ESP32PWM library with 50Hz frequency (1000-2000μs pulse width)

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
# Set default environment in platformio.ini
default_envs = HomeAssistant_LVGL

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
