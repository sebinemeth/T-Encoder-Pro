# T-Encoder HomeAssistant Controller

Firmware that turns the [LilyGO T-Encoder-Pro](https://github.com/Xinyuan-LilyGO/T-Encoder-Pro) (ESP32-S3, 390x390 round AMOLED, touch, rotary encoder) into a Home Assistant controller. Swipe or turn the knob to move between tiles for lights, switches, covers, climate, fans and sensors.

Based on LilyGO's T-Encoder-Pro examples (GPL-3.0). See [AGENTS.md](AGENTS.md) for hardware details, upstream references and development guidelines.

## Build

1. Copy `examples/HomeAssistant_LVGL/ha_config.h` values into a gitignored `examples/HomeAssistant_LVGL/ha_config_private.h` (WiFi SSID/password, HA URL, long-lived token). See `.env.example` for the fields.
2. Edit the entity list in `examples/HomeAssistant_LVGL/entities.h`.
3. `pio run -t upload` (PlatformIO). Select the board variant in `libraries/Mylibrary/pin_config.h` (default `DXQ120MYB2416A`).

More: `examples/HomeAssistant_LVGL/README.md`, `WARP.md` (WebSocket troubleshooting).

## License

GPL-3.0, see `LICENSE`.
