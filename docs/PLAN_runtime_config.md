# Plan: runtime configuration (WiFi, Home Assistant, entities)

Status: planned, not started. Follow-up to Phases 1-5 of the takeover.

## Problem
WiFi credentials, the HA URL/token and the entity list are compiled in (`ha_config_private.h`, `entities.h`). Changing any of them means editing code and reflashing, and the token ends up inside the firmware binary.

## Goals
- Configure WiFi, HA URL, token and the entity list on the device without reflashing.
- First boot or a failed connection leads to a setup mode that needs no computer toolchain.
- Keep compile-time config as an optional default for development builds.
- Token never printed to Serial or shown on screen.

## Design
**Storage**: ESP32 `Preferences` (NVS), namespace `ha`.
- `wifi_ssid`, `wifi_pass`, `ha_url`, `ha_token` as strings.
- `entities` as one JSON string (array of `{name, entity_id, icon?}`), max ~2 KB, `version` int for migrations.
- Fallback: if NVS is empty and `ha_config_private.h` exists, seed NVS from it on first boot.

**Entity model**: change `HAEntity entities[ENTITY_COUNT]` (static array) to a runtime container.
- `ENTITY_COUNT` becomes `entityCount` (cap, for example 12 tiles).
- Tiles, indicator dots, pending-command slots, `refreshEntity[]` and the manager's subscription list are built from the runtime list. All currently use the compile-time constant, so the change touches `createUI`, `createIndicatorDots`, `pendingCommands[]`, `lastUserInteraction[]` and `HAWebSocketManagerV2`.
- Type and default icon already come from the domain (`entityTypeForDomain`, `defaultIconForDomain` in `entities.h`), so the JSON only needs name and entity_id.

**Setup mode** (SoftAP + captive portal):
- Enter when: no WiFi config, WiFi fails for N minutes, or the encoder button is held during boot (long press > 3 s).
- Device starts AP `T-Encoder-Setup`, shows the AP name, a QR/URL and a "Setup mode" screen on the display.
- `WebServer` + `DNSServer` (both in the Arduino core, no extra deps) serve one small page: WiFi scan list and password, HA URL, token, and an entity editor (add/remove/reorder rows, entity_id plus optional name).
- "Test connection" button: device tries WiFi plus an HA `/api/` call and reports the result before saving.
- Save writes NVS and reboots.
- Optional: entity picker populated from HA `GET /api/states` (filtered to supported domains) so the user does not type entity ids by hand.

**Security**
- Setup AP is open but only active in setup mode; setup page requires a short on-screen PIN shown on the display to save changes.
- Token stored in NVS (flash encryption optional, document it). Never logged; the form never echoes it back (show "set").
- Plain `http`/`ws` stays a known limitation; add `wss` as a separate follow-up.

## Steps
1. **Config layer**: `config_store.h/.cpp` with load/save/seed, unit-testable JSON (de)serialization. Replace `WIFI_SSID`/`HA_URL`/`HA_TOKEN` macro uses with `config.wifiSsid` etc. Build still works with compile-time defaults.
2. **Dynamic entities**: replace the static array and `ENTITY_COUNT`; load from config; verify with the current 4 entities (parity test on device).
3. **Setup mode backend**: SoftAP, DNS catch-all, `WebServer` routes (`/`, `/scan`, `/save`, `/test`, `/entities`).
4. **Setup page**: single embedded HTML string in PROGMEM, no external assets; works on phones.
5. **Display + input**: setup-mode screen, boot long-press entry, PIN.
6. **Entity picker from HA** (optional).
7. **Docs**: AGENTS.md and README (first-time setup, recovery, factory reset via long press at boot).

## Risks and open questions
- Heap: tiles per entity are LVGL objects; test the max entity count with `LV_MEM_SIZE` 48K, raise if needed.
- Changing the entity list at runtime requires rebuilding the UI; simplest is save-then-reboot.
- WiFi AP and STA modes together can be flaky on ESP32-S3; use AP-only during setup.
- Captive-portal behaviour differs between iOS/Android; keep the URL visible on screen as the fallback.

## Verification
- Fresh flash with empty NVS: boots into setup mode, phone configures WiFi/HA/entities, device reboots into the normal UI with the same behaviour as today.
- Wrong WiFi password: falls back to setup mode and keeps old values until saved.
- Token does not appear in Serial logs, setup page HTML or `/save` responses.
- Add and remove entities of each type (light, cover, climate, sensor, fan) and confirm tiles, subscription and commands follow.
