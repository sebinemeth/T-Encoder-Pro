/*
 * Cover, thermostat and fan tiles: a 270 degree ring around the face (gap at the bottom)
 * with the hero value inside. Geometry follows docs/ui-upgrade/mockups.html.
 */
#include "ui_tiles.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "ui_layout.h"
#include "ui_internal.h"
#include "app_logic.h"

#define UI_MAX_ENTITIES 32  // per-tile widget slots; tiles beyond this are not created

struct RingTile {
    lv_obj_t* ring;
    lv_obj_t* tick;       // cover: target marker, thermostat: current temperature marker
    lv_obj_t* icon;
    lv_obj_t* buttons[3]; // cover: open, stop, close
    lv_obj_t* dot;        // thermostat: state dot
    bool editing;
    int coverTarget;      // cover: position the user asked for, -1 if none
    uint32_t color;       // thermostat: current accent
};
static RingTile ring[UI_MAX_ENTITIES];

static RingTile* slot(int i) { return i >= 0 && i < UI_MAX_ENTITIES ? &ring[i] : nullptr; }

// ---- ring and tick widgets ----

// The ring keeps its centerline at UI_RING_RADIUS whatever the stroke: the object grows with it
static void setRingStroke(lv_obj_t* arc, int stroke) {
    const int size = 2 * UI_RING_RADIUS + stroke;
    lv_obj_set_size(arc, size, size);
    lv_obj_set_style_arc_width(arc, stroke, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, stroke, LV_PART_INDICATOR);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
}

static lv_obj_t* createRing(lv_obj_t* tile, int stroke, uint32_t accent, bool withKnob) {
    lv_obj_t* arc = lv_arc_create(tile);
    lv_arc_set_bg_angles(arc, UI_RING_START_ANGLE, (UI_RING_START_ANGLE + UI_RING_SWEEP) % 360);
    lv_arc_set_range(arc, 0, 100);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(UI_COLOR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    if (withKnob) {
        // 24 px knob: white ring around an accent core, like the mockup
        lv_obj_set_style_pad_all(arc, 5, LV_PART_KNOB);
        lv_obj_set_style_bg_color(arc, lv_color_hex(accent), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(arc, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_border_width(arc, 5, LV_PART_KNOB);
        lv_obj_set_style_border_color(arc, lv_color_hex(UI_COLOR_TEXT), LV_PART_KNOB);
    } else {
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    }
    lv_obj_add_flag(arc, LV_OBJ_FLAG_ADV_HITTEST);  // only the ring itself takes touches, so the face can swipe
    setRingStroke(arc, stroke);
    return arc;
}

// A 3 px marker across the ring, shown with setTick()
static lv_obj_t* createTick(lv_obj_t* tile, uint32_t color, int stroke) {
    lv_obj_t* arc = lv_arc_create(tile);
    lv_obj_remove_style_all(arc);
    const int size = 2 * UI_RING_RADIUS + stroke;
    lv_obj_set_size(arc, size, size);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, stroke, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
    return arc;
}

// fraction 0..1 along the ring
static void setTick(lv_obj_t* tick, float fraction) {
    fraction = constrain(fraction, 0.0f, 1.0f);
    const int a = (int)(UI_RING_START_ANGLE + UI_RING_SWEEP * fraction);
    lv_arc_set_bg_angles(tick, a % 360, (a + 1) % 360);
    lv_obj_clear_flag(tick, LV_OBJ_FLAG_HIDDEN);
}

static void setHeroValue(lv_obj_t* label, int value, uint32_t color) {
    lv_label_set_text_fmt(label, "%d%%", value);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

// ---- events ----

static void ring_event_handler(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    int entityIndex = (int)(uintptr_t)lv_event_get_user_data(e);
    int value = lv_arc_get_value(lv_event_get_target(e));
    switch (entities[entityIndex].type) {
        case ENTITY_COVER: setCoverPosition(entityIndex, value, true); break;
        case ENTITY_FAN: setFanSpeed(entityIndex, value, true); break;
        case ENTITY_THERMOSTAT: {
            HAEntity& entity = entities[entityIndex];
            setTargetTemp(entityIndex, arcValueToTemp(value, entity.minTemp, entity.maxTemp));
            break;
        }
        default: break;
    }
}

static void cover_button_event_handler(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* obj = lv_event_get_target(e);
    int entityIndex = (int)(uintptr_t)lv_event_get_user_data(e);
    String action = String((char*)lv_obj_get_user_data(obj));
    Serial.println("Cover " + action + " for entity " + String(entityIndex));
    handleEntityAction(entityIndex, "cover_" + action);
}

// ---- cover ----

static const char* const COVER_ACTIONS[3] = {"open", "stop", "close"};
static const char* const COVER_GLYPHS[3] = {"mdi:chevron-up", "mdi:stop", "mdi:chevron-down"};

static lv_obj_t* createRoundButton(lv_obj_t* tile, int index, int entityIndex) {
    lv_obj_t* btn = lv_obj_create(tile);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 48, 48);
    lv_obj_align(btn, LV_ALIGN_CENTER, (index - 1) * 60, 305 - UI_CENTER);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(UI_COLOR_ACCENT_COVER), 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 2, LV_STATE_CHECKED);   // CHECKED = this direction is moving
    lv_obj_set_style_border_width(btn, 2, LV_STATE_PRESSED);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cover_button_event_handler, LV_EVENT_CLICKED, (void*)(uintptr_t)entityIndex);
    lv_obj_set_user_data(btn, (void*)COVER_ACTIONS[index]);

    lv_obj_t* glyph = lv_label_create(btn);
    lv_label_set_text(glyph, uiIconGlyph(COVER_GLYPHS[index]));
    lv_obj_set_style_text_font(glyph, UI_FONT_ICON_XS, 0);
    lv_obj_set_style_text_color(glyph, lv_color_hex(UI_COLOR_TEXT_DIM), 0);
    lv_obj_set_style_text_color(glyph, lv_color_hex(UI_COLOR_ACCENT_COVER), LV_STATE_PRESSED);
    lv_obj_center(glyph);
    return btn;
}

void createCoverTile(int entityIndex) {
    RingTile* t = slot(entityIndex);
    if (!t) return;
    HAEntity& entity = entities[entityIndex];
    lv_obj_t* tile = entity.tile;
    *t = RingTile{};
    t->coverTarget = -1;

    t->ring = createRing(tile, UI_RING_STROKE, UI_COLOR_ACCENT_COVER, false);
    lv_arc_set_value(t->ring, entity.position);
    lv_obj_add_event_cb(t->ring, ring_event_handler, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)entityIndex);
    t->tick = createTick(tile, UI_COLOR_ACCENT_HEAT, UI_RING_STROKE + 8);

    uiCreateLine(tile, entity.name.c_str(), UI_FONT_NAME, UI_COLOR_TEXT_DIM, UI_Y_NAME_RING, UI_W_NAME_RING);
    t->icon = lv_label_create(tile);
    lv_label_set_text(t->icon, uiIconGlyph(entity.icon.c_str()));
    lv_obj_set_style_text_font(t->icon, UI_FONT_ICON_S, 0);
    lv_obj_set_style_text_color(t->icon, lv_color_hex(UI_COLOR_ACCENT_COVER), 0);
    lv_obj_align(t->icon, LV_ALIGN_CENTER, 0, 142 - UI_CENTER);

    lv_obj_t* hero = uiCreateLine(tile, "", UI_FONT_HERO, UI_COLOR_TEXT, 224, UI_W_HERO);
    setHeroValue(hero, entity.position, UI_COLOR_TEXT);
    lv_obj_t* caption = uiCreateLine(tile, "--", UI_FONT_CAPTION, UI_COLOR_ACCENT_COVER, 252, UI_W_CAPTION);
    for (int k = 0; k < 3; k++) t->buttons[k] = createRoundButton(tile, k, entityIndex);

    entity.wControl = t->ring;
    entity.wValue = hero;
    entity.wStatus = caption;
}

void uiUpdateCover(int entityIndex) {
    RingTile* t = slot(entityIndex);
    HAEntity& entity = entities[entityIndex];
    if (!t || !t->ring) return;

    // The ring follows HA only when the user isn't adjusting it
    if (entity.position != entity.lastPosition && !userHoldsControl(entityIndex)) {
        uiSyncControl(entityIndex);
        entity.lastPosition = entity.position;
    }

    const bool opening = entity.coverState == "opening";
    const bool closing = entity.coverState == "closing";
    if (!opening && !closing) t->coverTarget = -1;  // nothing moving: the request is done or was stopped

    // Target tick only while the cover is on its way
    if (t->coverTarget >= 0 && abs(t->coverTarget - entity.position) > 1) setTick(t->tick, t->coverTarget / 100.0f);
    else lv_obj_add_flag(t->tick, LV_OBJ_FLAG_HIDDEN);

    // The direction that is moving gets a ring on its button
    if (opening) lv_obj_add_state(t->buttons[0], LV_STATE_CHECKED); else lv_obj_clear_state(t->buttons[0], LV_STATE_CHECKED);
    if (closing) lv_obj_add_state(t->buttons[2], LV_STATE_CHECKED); else lv_obj_clear_state(t->buttons[2], LV_STATE_CHECKED);

    if (entity.wStatus) {
        String text = t->editing ? String("Editing")
                    : entity.coverState.length() && entity.coverState != "unknown" ? entity.coverState : String("--");
        text.setCharAt(0, toupper(text.charAt(0)));
        uiSetLineText(entity.wStatus, text.c_str());
    }
}

void uiSetCoverTarget(int entityIndex, int value) {
    if (RingTile* t = slot(entityIndex)) t->coverTarget = value;
}

// ---- fan ----

void createFanTile(int entityIndex) {
    RingTile* t = slot(entityIndex);
    if (!t) return;
    HAEntity& entity = entities[entityIndex];
    lv_obj_t* tile = entity.tile;
    *t = RingTile{};

    t->ring = createRing(tile, UI_RING_STROKE, UI_COLOR_ACCENT_LIGHT, false);
    lv_arc_set_value(t->ring, entity.fanSpeed);
    lv_obj_add_event_cb(t->ring, ring_event_handler, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)entityIndex);

    uiCreateLine(tile, entity.name.c_str(), UI_FONT_NAME, UI_COLOR_TEXT_DIM, UI_Y_NAME_RING, UI_W_NAME_RING);
    t->icon = lv_label_create(tile);
    lv_label_set_text(t->icon, uiIconGlyph("mdi:fan"));
    lv_obj_set_style_text_font(t->icon, UI_FONT_ICON_S, 0);
    lv_obj_align(t->icon, LV_ALIGN_CENTER, 0, 146 - UI_CENTER);

    lv_obj_t* hero = uiCreateLine(tile, "", UI_FONT_HERO, UI_COLOR_TEXT, 224, UI_W_HERO);
    lv_obj_t* caption = uiCreateLine(tile, "", UI_FONT_CAPTION, UI_COLOR_TEXT_DIM, 252, UI_W_CAPTION);

    entity.wControl = t->ring;
    entity.wValue = hero;
    entity.wStatus = caption;
    uiSyncControl(entityIndex);
}

void uiUpdateFan(int entityIndex) {
    HAEntity& entity = entities[entityIndex];
    if (entity.fanSpeed != entity.lastFanSpeed && !userHoldsControl(entityIndex)) {
        uiSyncControl(entityIndex);
        entity.lastFanSpeed = entity.fanSpeed;
    }
}

// ---- thermostat ----

static uint32_t thermostatColor(const HAEntity& entity) {
    if (entity.hvacAction == "heating") return UI_COLOR_ACCENT_HEAT;
    if (entity.hvacAction == "cooling") return UI_COLOR_ACCENT_COOL;
    return UI_COLOR_INACTIVE;
}

static const char* thermostatStateText(const HAEntity& entity) {
    if (entity.hvacAction == "heating") return "Heating";
    if (entity.hvacAction == "cooling") return "Cooling";
    if (entity.hvacMode == "off") return "Off";
    return "Idle";
}

// Hero, target and caption depend on edit mode: editing makes the target the hero
static void refreshThermostatTexts(int entityIndex) {
    RingTile* t = slot(entityIndex);
    HAEntity& entity = entities[entityIndex];
    char buf[24];
    if (t->editing) {
        snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", entity.targetTemp);
        // an idle ring is dark grey, too dim for a hero number
        lv_obj_set_style_text_color(entity.wValue, lv_color_hex(t->color == UI_COLOR_INACTIVE ? UI_COLOR_TEXT : t->color), 0);
        lv_obj_set_style_text_font(entity.wValue, UI_FONT_HERO_BIG, 0);
        uiSetLineText(entity.wValue, buf);
        lv_obj_align(entity.wValue, LV_ALIGN_TOP_MID, 0, 215 - uiBaselineOffset(UI_FONT_HERO_BIG));

        snprintf(buf, sizeof(buf), "Now %.1f\xC2\xB0", entity.currentTemp);
        lv_obj_set_style_text_font(entity.wTarget, UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_color(entity.wTarget, lv_color_hex(UI_COLOR_TEXT_DIM), 0);
        uiSetLineText(entity.wTarget, buf);
        lv_obj_align(entity.wTarget, LV_ALIGN_TOP_MID, 0, 250 - uiBaselineOffset(UI_FONT_CAPTION));
    } else {
        snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", entity.currentTemp);
        lv_obj_set_style_text_color(entity.wValue, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_set_style_text_font(entity.wValue, UI_FONT_HERO_BIG, 0);
        uiSetLineText(entity.wValue, buf);
        lv_obj_align(entity.wValue, LV_ALIGN_TOP_MID, 0, 205 - uiBaselineOffset(UI_FONT_HERO_BIG));

        snprintf(buf, sizeof(buf), LV_SYMBOL_RIGHT " %.1f\xC2\xB0", entity.targetTemp);
        lv_obj_set_style_text_font(entity.wTarget, UI_FONT_SECOND, 0);
        lv_obj_set_style_text_color(entity.wTarget, lv_color_hex(t->color), 0);
        uiSetLineText(entity.wTarget, buf);
        lv_obj_align(entity.wTarget, LV_ALIGN_TOP_MID, 0, 245 - uiBaselineOffset(UI_FONT_SECOND));
    }
}

static void applyThermostatColor(int entityIndex) {
    RingTile* t = slot(entityIndex);
    HAEntity& entity = entities[entityIndex];
    const uint32_t color = thermostatColor(entity);
    t->color = color;
    lv_obj_set_style_arc_color(t->ring, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(t->ring, lv_color_hex(color), LV_PART_KNOB);
    lv_obj_set_style_bg_color(t->dot, lv_color_hex(color), 0);
    uiSetLineText(entity.wStatus, thermostatStateText(entity));
    refreshThermostatTexts(entityIndex);
}

static float tempFraction(const HAEntity& entity, float temp) {
    const float range = entity.maxTemp - entity.minTemp;
    return range > 0 ? (temp - entity.minTemp) / range : 0;
}

void createThermostatTile(int entityIndex) {
    RingTile* t = slot(entityIndex);
    if (!t) return;
    HAEntity& entity = entities[entityIndex];
    lv_obj_t* tile = entity.tile;
    *t = RingTile{};
    t->color = UI_COLOR_INACTIVE;

    t->ring = createRing(tile, 14, UI_COLOR_INACTIVE, true);
    lv_arc_set_value(t->ring, tempToArcValue(entity.targetTemp, entity.minTemp, entity.maxTemp));
    lv_obj_add_event_cb(t->ring, ring_event_handler, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)entityIndex);
    t->tick = createTick(tile, UI_COLOR_TEXT, 14 + 8);

    uiCreateLine(tile, entity.name.c_str(), UI_FONT_NAME, UI_COLOR_TEXT_DIM, UI_Y_NAME_RING, UI_W_NAME_RING);
    entity.wValue = uiCreateLine(tile, "", UI_FONT_HERO_BIG, UI_COLOR_TEXT, 205, UI_W_HERO);
    entity.wTarget = uiCreateLine(tile, "", UI_FONT_SECOND, UI_COLOR_INACTIVE, 245, UI_W_HERO);

    // State row: colored dot + text, centered, baseline at 304
    lv_obj_t* row = lv_obj_create(tile);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 304 - uiBaselineOffset(UI_FONT_CAPTION));
    t->dot = lv_obj_create(row);
    lv_obj_remove_style_all(t->dot);
    lv_obj_set_size(t->dot, 8, 8);
    lv_obj_set_style_radius(t->dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(t->dot, LV_OPA_COVER, 0);
    lv_obj_t* status = lv_label_create(row);
    lv_obj_set_style_text_font(status, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_label_set_text(status, "");

    entity.wControl = t->ring;
    entity.wStatus = status;
    applyThermostatColor(entityIndex);
    setTick(t->tick, tempFraction(entity, entity.currentTemp));
}

void uiUpdateThermostat(int entityIndex) {
    RingTile* t = slot(entityIndex);
    HAEntity& entity = entities[entityIndex];
    if (!t || !t->ring) return;

    if (fabsf(entity.currentTemp - entity.lastCurrentTemp) > 0.05f) {
        setTick(t->tick, tempFraction(entity, entity.currentTemp));
        refreshThermostatTexts(entityIndex);
        entity.lastCurrentTemp = entity.currentTemp;
    }
    if (fabsf(entity.targetTemp - entity.lastTargetTemp) > 0.01f && !userHoldsControl(entityIndex)) {
        uiSyncControl(entityIndex);
        entity.lastTargetTemp = entity.targetTemp;
    }
    // Mode and action drive color and state text (cheap: only touches LVGL when they changed)
    if (t->color != thermostatColor(entity) || strcmp(lv_label_get_text(entity.wStatus), thermostatStateText(entity)) != 0) {
        applyThermostatColor(entityIndex);
    }
}

// ---- shared with the sketch ----

// Redraws ring and hero from the entity values (user input and HA updates)
void uiSyncControl(int entityIndex) {
    RingTile* t = slot(entityIndex);
    if (!t || !t->ring) return;
    HAEntity& entity = entities[entityIndex];
    switch (entity.type) {
        case ENTITY_COVER:
            lv_arc_set_value(t->ring, entity.position);
            setHeroValue(entity.wValue, entity.position, t->editing ? UI_COLOR_ACCENT_COVER : UI_COLOR_TEXT);
            break;
        case ENTITY_FAN: {
            lv_arc_set_value(t->ring, entity.fanSpeed);
            setHeroValue(entity.wValue, entity.fanSpeed, t->editing ? UI_COLOR_ACCENT_LIGHT : UI_COLOR_TEXT);
            const bool on = entity.fanSpeed > 0;
            lv_obj_set_style_text_color(t->icon, lv_color_hex(on ? UI_COLOR_ACCENT_LIGHT : UI_COLOR_INACTIVE), 0);
            uiSetLineText(entity.wStatus, on ? "On" : "Off");
            break;
        }
        case ENTITY_THERMOSTAT:
            lv_arc_set_value(t->ring, tempToArcValue(entity.targetTemp, entity.minTemp, entity.maxTemp));
            refreshThermostatTexts(entityIndex);
            break;
        default:
            break;
    }
}

// Edit mode look: thicker ring and an accent hero (cover, fan); the target becomes the hero (thermostat)
void uiSetEditMode(int entityIndex, bool on) {
    RingTile* t = slot(entityIndex);
    if (!t || !t->ring || t->editing == on) return;
    t->editing = on;
    HAEntity& entity = entities[entityIndex];
    if (entity.type != ENTITY_THERMOSTAT) setRingStroke(t->ring, on ? UI_RING_STROKE_EDIT : UI_RING_STROKE);
    uiSyncControl(entityIndex);
    if (entity.type == ENTITY_COVER) uiUpdateCover(entityIndex);
}
