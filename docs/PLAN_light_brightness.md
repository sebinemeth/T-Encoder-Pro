# Plan: light brightness control

Status: planned, not started. Depends on the ring look from [PLAN_ui_upgrade.md](PLAN_ui_upgrade.md) (Phase 3, after the performance phase 1b); the data and input work (steps 1-3) can land earlier on the current UI.

## Problem
Light tiles are on/off only. Dimmable lights (most bulbs) report a brightness and accept one, but the device ignores it: the encoder press toggles, and there is no way to dim.

## Goals
- Show a light's brightness on its tile and change it with the encoder or touch.
- Keep today's behavior for lights that are not dimmable (on/off switch lights, plain `switch`, `input_boolean`): no ring, no edit mode.
- Short press still toggles. Dimming must not make toggling slower or ambiguous.

## Home Assistant side
- State: a light has `attributes.brightness` (0-255) **only while on**; it is `null` when off. Dimmable lights list a mode other than `onoff` in `attributes.supported_color_modes` (`brightness`, `color_temp`, `hs`, `rgb`, `xy`, `rgbw`, ...). `["onoff"]` means not dimmable.
- Command: `light.turn_on` with `brightness_pct` (0-100), e.g. `{"entity_id": "light.desk_lamp", "brightness_pct": 40}`. This also turns the light on. Do not send `brightness_pct: 0` for "off"; use `turn_off` (HA treats 0 as off anyway, but `turn_off` is explicit and keeps the last brightness for the next `turn_on`).
- The same call works over the WebSocket (`call_service`) and the REST fallback (`/api/services/light/turn_on`), through the existing `buildServiceCall()` path.

## Data model
New fields on `HAEntity` (in `entities.h`):

| Field | Meaning |
|---|---|
| `bool dimmable` | derived from `supported_color_modes`, default false |
| `int brightness` | 0-100 percent, last known (0 when off) |
| `int lastBrightness` | for UI change detection, same pattern as `lastPosition` |

**Beware**: `entities[]` is initialized positionally, one long row per entity. Adding fields means editing every row and is easy to get wrong. Do this first: switch the struct to default member initializers (`bool dimmable = false;` ...) so rows only list name, entity id and optional icon, and `initializeEntities()` keeps resetting the runtime fields. That also helps the runtime-config plan.

Parsing (`updateEntityFromState`, `ENTITY_TOGGLE` case in `ha_websocket_manager_v2.h`):
- `dimmable` = `supported_color_modes` exists and contains any mode other than `"onoff"`. Check with an array iteration, not `is<JsonObject>()` style type checks (see the ArduinoJson note in AGENTS.md).
- `brightness` = `lroundf(attrs["brightness"] * 100 / 255)` when on and present, else 0. Use a pure helper `brightnessToPct(int raw)` / `pctToBrightness` in `app_logic.h` so it is unit tested (0 -> 0, 1 -> 1 (not 0 for a very dim light), 255 -> 100).
- WebSocket events carry attributes, and `state_changed` events with large attribute lists (color lights) can be big: check the message size against `WS_MAX_MESSAGE_SIZE` (16384) with a real RGB bulb before shipping, since `supported_color_modes` plus `rgb_color`, `xy_color`, `hs_color` add up.

## Service call
In `service_call.h` add action `light_set_brightness` (value = percent): domain `light`, service `turn_on`, key `brightness_pct`, `intValue`. Only valid for `light` entities. Extend the native tests (`test/test_logic`) with: valid light call, rejected for `switch`, percent clamping to 0-100, 0 percent mapping decision.

## Input model

| Input | Not dimmable | Dimmable |
|---|---|---|
| Rotate | switch tile | switch tile (edit mode off) |
| Short press | toggle | toggle (unchanged) |
| Long press (600 ms) | nothing | **enter brightness edit mode** |
| Rotate in edit mode | n/a | brightness +/-5 % per detent |
| Press or 5 s idle in edit mode | n/a | exit edit mode |
| Touch | tap disc toggles | tap disc toggles, drag on the ring sets brightness |

Why long press: short press is already "toggle" on lights and must stay instant. Long press is unused on lights today (it only stops a cover). A hint ("Hold to dim") can be shown once in the toast the first time a dimmable light tile is focused.

Edit-mode rules reuse the existing machinery: `editValue`, `editLastActivity`, `editTimeout`, `userHoldsControl()`, debounce through `handleEntityAction(..., debounceMs)`. Constraints in edit mode: clamp 1-100 (going below 1 does not turn the light off; turning off is a press). If the light is off when edit mode starts, the first rotation turns it on at the new value (HA does this for `turn_on` with brightness anyway).

## UI
Per the ring language of the UI plan (accent `ACCENT_LIGHT`):
- Dimmable light, on: 270-degree ring at r=175, 12 px, fill = brightness; hero stays the icon disc (the percent appears as a caption under the disc: "On - 40%").
- Dimmable light, off: ring track only (empty), caption "Off".
- Edit mode: ring 18 px, the percent becomes the hero (56 pt, accent) in place of the icon; icon moves up to caption size. Same transition as the cover and fan.
- Disc fill opacity can follow brightness (15% to 30%) so the tile looks dimmer when the light is dim; keep contrast of the icon above the plan's minimum.
- Non-dimmable lights and switches: no ring, exactly the tile from the UI plan.

If this lands before the UI upgrade, use the current style: add a slider on the toggle tile only for dimmable lights, and the same edit highlight as the cover.

## Steps
1. `HAEntity` default member initializers (no behavior change); build and run on device.
2. Pure helpers (`brightnessToPct`, `pctToBrightness`, `light_set_brightness` in `buildServiceCall`) with native tests.
3. Parse `dimmable` and `brightness` in the WebSocket manager and in REST fetch (`fetchEntityViaREST` / `fetchAllViaREST` share the state parser; verify). Log `dimmable` once per entity on Serial.
4. Input: long press enters edit mode for dimmable lights; `adjustCurrentValue()` and `enterEditMode()` get a light case; `isAdjustable()` stays as is for short press (a light must not enter edit on short press), so add a separate `isDimmable()` check.
5. UI: ring, caption, edit look (on top of the new theme).
6. Hint toast, README and AGENTS.md input-model update.

## Testing
- Native: percent conversion round trips, service call mapping, rejection for non-lights.
- Device with a real dimmable light and with a plain switch-backed light: toggle still works on both; long press only does something on the dimmable one; slider/ring follows changes made from the HA app (and does not fight while dragging, via `userHoldsControl`).
- Edge cases: light unavailable (no attributes), light off (brightness null), color bulb (large event), brightness changed from HA while in edit mode (ignored until exit, then synced), rapid rotation (debounce collapses to the last value; one service call per burst).

## Risks and open points
- Long press discoverability: covered by the hint toast; if it proves annoying, an alternative is press-and-rotate (rotate while held).
- Some integrations report `brightness` for `onoff` lights or omit `supported_color_modes` (old integrations use `supported_features` bit 1). If seen, fall back to `supported_features & 1`.
- Transition time: pass `transition` only if a smoother change is wanted; not needed for a first version.
