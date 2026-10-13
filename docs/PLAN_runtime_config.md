# Plan: runtime configuration (WiFi, Home Assistant, entities)

Status: planned, not started. Follow-up to Phases 1-5 of the takeover.

## Problem
WiFi credentials, the HA URL/token and the entity list are compiled in (`ha_config_private.h`, `entities.h`). Changing any of them means editing code and reflashing, and the token ends up inside the firmware binary.

The device itself has no usable text input (one knob, one button, small round touch screen), so configuration has to be typed on a phone and sent to the device over the network.

## Goals
- Configure WiFi, HA URL, token and the entity list from a phone browser, without reflashing.
- First boot or a failed connection leads to a setup mode that needs no computer toolchain.
- Keep build-time configuration as a first-class option: a build with compiled-in config skips the runtime flow entirely.
- Two PlatformIO environments, one per mode, so either can be built without editing files.
- Token never printed to Serial or shown on screen.

## Build modes

Two environments in `platformio.ini`, sharing one `[env]`-style base section for the common flags.

| Env | Config source | Runtime flow |
|---|---|---|
| `HomeAssistant_LVGL` (default) | NVS, set via the phone setup flow | Setup mode, NVS, web server compiled in |
| `HomeAssistant_LVGL_static` | `ha_config_private.h` + `entities.h`, compiled in | None: no NVS reads, no SoftAP, no web server, no setup screen |

- The static env sets `-D STATIC_CONFIG`. The `config_store` layer then returns the compile-time values and the setup code is not compiled (`#ifndef STATIC_CONFIG`), which also saves flash and heap.
- The static env fails the build with a clear `#error` if `ha_config_private.h` is missing, instead of today's `#warning` plus placeholder values. CI builds it against `ha_config_private.h.example`.
- The runtime env ignores `ha_config_private.h` completely, so a stray private header can never leak a token into a binary that is meant to be configured on the device.
- A device flashed with the static build keeps working exactly as today. Switching modes is a reflash; NVS contents from the other mode are untouched and ignored.
- `ha_config.h` placeholder defaults are only used by the `native` test env.

## Design
**Storage** (runtime env): ESP32 `Preferences` (NVS), namespace `ha`.
- `wifi_ssid`, `wifi_pass`, `ha_url`, `ha_token` as strings.
- `entities` as one JSON string (array of `{name, entity_id, icon?}`), max ~2 KB, `version` int for migrations.
- No seeding from `ha_config_private.h`: that is what the static env is for.

**Config layer**: `config_store.h/.cpp` exposes one `Config` struct (`wifiSsid`, `wifiPass`, `haUrl`, `haToken`, entities, `phase` flags). In the static env it is filled from macros at startup; in the runtime env from NVS. The rest of the app does not know which.

**Entity model**: change `HAEntity entities[ENTITY_COUNT]` (static array) to a runtime container.
- `ENTITY_COUNT` becomes `entityCount` (cap, for example 12 tiles).
- Tiles, indicator dots, pending-command slots, `refreshEntity[]` and the manager's subscription list are built from the runtime list. All currently use the compile-time constant, so the change touches `createUI`, `createIndicatorDots`, `pendingCommands[]`, `lastUserInteraction[]` and `HAWebSocketManagerV2`.
- In the static env the runtime list is initialised from the compile-time table in `entities.h`, so there is one code path.
- Type and default icon already come from the domain (`entityTypeForDomain`, `defaultIconForDomain` in `entities.h`), so the JSON only needs name and entity_id.

## Setup flow (runtime env only): two phases

Why two phases: the device cannot test WiFi, reach HA or list entities while it is a SoftAP only, and AP+STA at the same time is flaky on ESP32-S3. So phase 1 only gets the device onto the home network; phase 2 does everything else from the phone on that same network, where the user can also copy the token from the HA companion app.

**Entering setup**
- Phase 1 when: no WiFi config in NVS, or the encoder button is held during boot (long press > 3 s, doubles as factory reset of WiFi).
- Phase 2 when: WiFi config exists but HA URL/token/entities are missing, HA is unreachable or rejects the token for N minutes, or a long press at boot while WiFi still works (skips phase 1).
- If WiFi itself fails for N minutes, fall back to phase 1 and keep the old values until a new save succeeds.

**Phase 1: WiFi (SoftAP, AP-only)**
1. Device starts open AP `T-Encoder-Setup`. The display shows "Setup 1/2" and a QR code `WIFI:S:T-Encoder-Setup;T:nopass;;` so the phone joins with one scan.
2. Captive portal (`DNSServer` catch-all + `WebServer`, both in the Arduino core) serves one page: WiFi scan list plus password. A fallback URL `http://192.168.4.1` is also shown on screen because captive-portal behaviour differs between iOS and Android.
3. On submit the device stores SSID and password, tears down the AP and reboots into STA mode. No connection test in this phase; a bad password is caught by the fall-back rule above.

**Phase 2: Home Assistant and entities (on the home LAN)**
1. Device joins the home WiFi and serves the same web server on its LAN IP and as `t-encoder.local` (mDNS). The display shows "Setup 2/2", a QR code with the URL and a short PIN.
2. The phone is on the normal WiFi again, so the user can copy a long-lived token from the HA companion app or web UI and paste it.
3. Page fields: HA URL (prefilled by probing `homeassistant.local:8123`), token, and the entity picker.
4. **Entity picker (core, not optional)**: after URL and token are entered, the device calls `GET /api/states`, filters to supported domains (light, switch, cover, climate, fan, sensor) and the page shows a searchable checklist with names, plus up-down reordering. A manual "add entity_id" row stays as a fallback. The device proxies the call, so the page (plain http) has no mixed-content problem.
5. "Test connection" runs the HA `/api/` call and reports success or the HTTP status before saving.
6. Save writes NVS and reboots into the normal UI.

**Security**
- Setup AP is open but only active in phase 1, which only accepts WiFi credentials. Phase 2 is on the LAN and requires the PIN shown on the display for every save and every `/states` proxy call.
- Phase 2 server only runs while in setup mode (timeout, then reboot to normal operation), never during normal use.
- Token stored in NVS (flash encryption optional, document it). Never logged; the form never echoes it back (show "set"); the `/states` proxy and `/save` responses never contain it.
- Plain `http`/`ws` stays a known limitation; add `wss` as a separate follow-up.

## Steps
1. **Build modes + config layer**: split `platformio.ini` into the two envs, add `STATIC_CONFIG`, `config_store.h/.cpp` with load/save and unit-testable JSON (de)serialization. Replace `WIFI_SSID`/`HA_URL`/`HA_TOKEN` macro uses with `config.*`. Both envs build and behave as today (runtime env just boots to "not configured" for now).
2. **Dynamic entities**: replace the static array and `ENTITY_COUNT`; load from config; verify with the current 4 entities in both envs (parity test on device).
3. **Phase 1 backend**: SoftAP, DNS catch-all, `WebServer` routes (`/`, `/scan`, `/wifi`).
4. **Phase 2 backend**: STA mode server, mDNS, PIN check, routes (`/`, `/test`, `/states`, `/save`), HA URL probe.
5. **Setup page**: single embedded HTML string in PROGMEM, no external assets, works on phones; phase 1 and phase 2 views, entity picker with search and reorder.
6. **Display + input**: setup screens (QR codes, PIN, "Setup 1/2" and "2/2"), boot long-press entry, timeout back to normal mode.
7. **CI**: build both envs (static against the `.example` header), run `native` tests; add JSON (de)serialization and phase-selection logic tests to `test/test_logic`.
8. **Docs**: AGENTS.md (build section lists both envs), README (first-time setup walkthrough, recovery, factory reset via long press at boot, when to use the static env).

## Risks and open questions
- Heap: tiles per entity are LVGL objects; test the max entity count with `LV_MEM_SIZE` 48K, raise if needed. The web server and `/api/states` response (can be large) also need memory: stream-filter the JSON instead of loading it whole, and free everything before starting the UI.
- Changing the entity list at runtime requires rebuilding the UI; simplest is save-then-reboot.
- Captive-portal behaviour differs between iOS/Android; the visible URL and the WiFi QR code are the fallback.
- mDNS (`t-encoder.local`) is unreliable on some Android versions; always show the raw IP too.
- Existing users of the single env name `HomeAssistant_LVGL`: it changes meaning to the runtime build. Call this out in the README, and mention `-e HomeAssistant_LVGL_static` for the old behaviour.

## Verification
- Fresh flash of the runtime env with empty NVS: phone scans the QR, joins the AP, picks WiFi; device reboots onto the LAN; phone opens the shown URL, pastes the token, picks entities from the list, tests, saves; device reboots into the normal UI with the same behaviour as today.
- Wrong WiFi password: falls back to phase 1 and keeps old values until saved.
- Wrong token: stays in or returns to phase 2 with a clear on-screen hint.
- Static env: builds and boots straight into the normal UI with no AP, no web server and no NVS access; build fails clearly without `ha_config_private.h`.
- Runtime env binary does not contain the token from `ha_config_private.h` (check with `strings`).
- Token does not appear in Serial logs, setup page HTML, `/states` or `/save` responses.
- Add and remove entities of each type (light, cover, climate, sensor, fan) and confirm tiles, subscription and commands follow.
