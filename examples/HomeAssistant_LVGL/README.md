# HomeAssistant LVGL Multi-Entity Controller

A professional Home Assistant controller for the LilyGo T-Encoder Pro with dual input methods (touch + rotary encoder).

## Features

- **Multi-Entity Support**: Control multiple Home Assistant entities
- **Dual Input**: Touch screen and rotary encoder navigation  
- **Optimistic UI**: Immediate feedback with background API calls
- **Touch Integration**: Direct switch toggling via CHSC5816 touch controller
- **Secure Configuration**: Environment-based sensitive data management

## Quick Setup

### 1. Security Configuration

**IMPORTANT**: This project uses a secure configuration system to protect sensitive data.

```bash
# Copy the environment template
cp ../../.env.example ../../.env

# Edit the .env file with your actual credentials
# - WiFi SSID and password  
# - Home Assistant URL and long-lived access token
```

### 2. Entity Configuration

Edit `entities.h` to define your Home Assistant entities:

```cpp
HAEntity entities[] = {
    {"Living Room Light", "light.living_room"},
    {"Bedroom Switch", "switch.bedroom"},
    {"Test Boolean", "input_boolean.test"}
};
```

### 3. Build and Upload

```bash
# Set as default in platformio.ini
default_envs = HomeAssistant_LVGL

# Build and upload
pio run --target upload

# Monitor serial output
pio device monitor --baud 115200
```

## Configuration Files

- **`ha_config.h`**: Template configuration with safe defaults
- **`ha_config_private.h`**: Overrides with actual values (auto-generated, gitignored)
- **`entities.h`**: Entity definitions (customize this)
- **`.env`**: Environment variables (gitignored)

## Usage

### Navigation
- **Rotate encoder**: Switch between entities
- **Touch switch**: Toggle current entity
- **Encoder button**: Alternative toggle method

### Visual Feedback
- Entity name and state displayed
- Connection status indicator
- Update timestamp
- Entity counter (1/3, 2/3, etc.)

## Supported Entity Types

- **switch**: Standard Home Assistant switches
- **light**: Lights and lighting controls  
- **input_boolean**: Boolean helper entities

## API Integration

- **Authentication**: Bearer token from Home Assistant
- **Endpoints**: REST API for state and service calls
- **Error Handling**: Graceful fallback with UI state reversion
- **Performance**: ~35-127ms typical response times

## Security Features

- ✅ **No hardcoded credentials** in source code
- ✅ **Environment variables** for sensitive data
- ✅ **Gitignore protection** for private files
- ✅ **Template system** for safe sharing
- ✅ **Override pattern** for local customization

## Troubleshooting

### WiFi Connection Issues
- Verify SSID and password in .env file
- Check WiFi signal strength
- Monitor serial output for connection attempts

### Home Assistant Authentication
- Ensure long-lived access token is valid
- Verify Home Assistant URL is accessible
- Check entity IDs exist in Home Assistant

### Touch Not Working
- Verify CHSC5816 touch controller initialization
- Check I2C connections (SDA=5, SCL=6)
- Monitor touch events in serial output

## Hardware Requirements

- **LilyGo T-Encoder Pro** (ESP32-S3R8)
- **1.2" AMOLED Display** (SH8601A)
- **CHSC5816 Touch Controller**  
- **Rotary Encoder** with button
- **WiFi Connection**

## Memory Usage

- **RAM**: ~113KB (34.6% of 327KB available)
- **Flash**: ~1.1MB (17.2% of 6.5MB available)
- **Libraries**: ArduinoJson, LVGL, SensorLib, WiFi, HTTPClient
