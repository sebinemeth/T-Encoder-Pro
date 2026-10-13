# AGENTS.md

Home Assistant controller firmware for the LilyGO T-Encoder-Pro (ESP32-S3, round AMOLED, rotary encoder).
This repo started as a fork of [Xinyuan-LilyGO/T-Encoder-Pro](https://github.com/Xinyuan-LilyGO/T-Encoder-Pro) (GPL-3.0, keep `LICENSE` and credit upstream). The app lives in `examples/HomeAssistant_LVGL`. See `WARP.md` for build and WebSocket troubleshooting until it is merged here.

## Hardware (verified)

- Owner's device is the **`DXQ120MYB2416A`** variant: SH8601 AMOLED + CHSC5816 touch. Verified by I2C probe: only address `0x2E` responds (SDA=5, SCL=6, touch RST=8, panel power `SCREEN_EN`=GPIO3 high).
- The other upstream variant, `TFD12MASBCTB4_V0_07` (CO5300 + CST816, I2C `0x15`), is **not** supported by the app yet. It needs `Arduino_CO5300` (Arduino_GFX >= 1.5) and `Arduino_DriveBus` for touch.
- Variant is selected by one macro at the top of `libraries/Mylibrary/pin_config.h`. Never hardcode pins; use that header.
- 390x390 round display over QSPI, ESP32-S3R8, 16 MB flash, octal PSRAM. Encoder: A=GPIO1, B=GPIO2, key=GPIO0.
- Board probe (to re-identify a device): flash a sketch that sets GPIO3 high, pulses GPIO8, runs `Wire.begin(5, 6)` and scans. `0x2E` = SH8601 variant, `0x15` = CO5300 variant.

## Upstream reference

Upstream stays the reference for hardware examples and library versions. Last synced upstream commit:

- **Last synced upstream SHA: `eb878b19ce493c9cc4da7769daa384b7227d88d7`** (2026-06-24, branch `arduino-esp32-libs_V3.0.7`). Update this line on every review.
- Merge base with our fork: `df033f0`.

Pinned links (replace the SHA when you re-sync):

- Repo: https://github.com/Xinyuan-LilyGO/T-Encoder-Pro
- Pins and variants: https://github.com/Xinyuan-LilyGO/T-Encoder-Pro/blob/eb878b19ce493c9cc4da7769daa384b7227d88d7/libraries/Mylibrary/pin_config.h
- Rotary encoder state machine: https://github.com/Xinyuan-LilyGO/T-Encoder-Pro/tree/eb878b19ce493c9cc4da7769daa384b7227d88d7/examples/Rotary_Encoder
- Display init (Arduino_GFX): https://github.com/Xinyuan-LilyGO/T-Encoder-Pro/tree/eb878b19ce493c9cc4da7769daa384b7227d88d7/examples/GFX
- Touch (CHSC5816): https://github.com/Xinyuan-LilyGO/T-Encoder-Pro/tree/eb878b19ce493c9cc4da7769daa384b7227d88d7/examples/CHSC5816
- LVGL 8 factory test, both variants: https://github.com/Xinyuan-LilyGO/T-Encoder-Pro/tree/eb878b19ce493c9cc4da7769daa384b7227d88d7/examples/Lvgl_CIT
- LVGL 9 example: https://github.com/Xinyuan-LilyGO/T-Encoder-Pro/tree/eb878b19ce493c9cc4da7769daa384b7227d88d7/examples/lvgl_9
- CST816 and other I2C chips (Arduino_DriveBus): https://github.com/Xinyuan-LilyGO/T-Encoder-Pro/tree/eb878b19ce493c9cc4da7769daa384b7227d88d7/libraries/Arduino_DriveBus
- Debug sketches (I2C scan, WS2812B, buzzer, IO): https://github.com/Xinyuan-LilyGO/T-Encoder-Pro/tree/eb878b19ce493c9cc4da7769daa384b7227d88d7/debug
- Schematics, datasheets, mechanical files: `information/`, `project/`, `structure/` in the same repo.

Browse without vendoring: `git remote add upstream https://github.com/Xinyuan-LilyGO/T-Encoder-Pro.git` (if missing), `git fetch upstream`, then `git show upstream/arduino-esp32-libs_V3.0.7:<path>`.

### Regularly check upstream

At the start of any display, touch, pin or library task, and at least monthly:

1. `git fetch upstream`
2. `git log --oneline <last-synced-sha>..upstream/arduino-esp32-libs_V3.0.7`
3. `git diff --stat <last-synced-sha>..upstream/arduino-esp32-libs_V3.0.7 -- libraries/Mylibrary examples libraries/Arduino_GFX* libraries/Arduino_DriveBus README.md`
4. Look for pin or macro renames (`LCD_*` became `SCREEN_*` in 2026), new hardware variants, library bumps, and README errata. Port what matters by hand. Do not merge wholesale; this tree is deliberately slimmed down.
5. Update the synced SHA above and the date.

History so far: upstream added the CO5300 + CST816 variant, renamed `LCD_*` to `SCREEN_*` (`LCD_VCI_EN` became `SCREEN_EN`), added Arduino_GFX 1.5.0, Arduino_DriveBus and LVGL 9.2.2, and removed `platformio.ini` and `UI_demo`. PlatformIO support in this repo is ours, not upstream's.

## Developing for the encoder

- **Input model** (`port/encoder.*` + `handleEncoderInput()` in the sketch): one interrupt-driven decoder, one detent per click (clockwise positive). Rotation switches tile. Short press: toggles a light/switch, or enters edit mode on cover, thermostat and fan tiles (knob then changes the value, 5 s idle or another press exits; touching or swiping also exits). Long press (600 ms): stops a cover. Touch works in parallel. There is no LVGL encoder indev or focus group.
- **Never block `loop()`**: no long `delay`, no blocking WiFi or HTTP. All LVGL calls stay on the main task.
- **Round display**: keep content inside the inscribed circle; avoid magic pixel offsets.
- **Memory**: `LV_MEM_SIZE` is 48K; avoid per-message `String` and `DynamicJsonDocument` churn; PSRAM is available.
- **Secrets**: WiFi and HA token go in `ha_config_private.h` (gitignored). Never print the token on Serial.
- **Build**: `venv/bin/pio run` (single env `HomeAssistant_LVGL`). Upload with `-t upload --upload-port /dev/cu.usbmodem*`. The ESP32-S3 appears as "USB JTAG/serial debug unit" (VID 303A).
- **Tests**: `pio test -e native` runs host-side Unity tests (`test/test_logic`). Keep logic that can be tested without hardware in the Arduino-free headers `app_logic.h`, `service_call.h` and `port/encoder_decoder.h`, and add tests when changing it.
- **CI** (`.github/workflows`): `ci.yml` runs the unit tests and builds the firmware on every push and PR (the CI binary has placeholder credentials, build check only). `upstream-check.yml` runs weekly and opens an "Upstream changes to review" issue when LilyGO changes files that matter here (see "Regularly check upstream").
- **Formatting**: `.clang-format` is provided for new and edited files; it is not enforced and existing files are not reformatted wholesale.
