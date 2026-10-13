/*
 * Tile creation and update for every entity type. Moved out of the sketch in UI phase 1;
 * see docs/PLAN_ui_upgrade.md.
 */
#include "ui_tiles.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "ui_layout.h"
#include "ui_internal.h"
#include "entities_types.h"
#include "app_logic.h"

// Styles
lv_style_t style_tile_bg;
lv_style_t style_title;
lv_style_t style_icon;
lv_style_t style_value_large;
lv_style_t style_value_small;
lv_style_t style_sensor_value;  // Very large font for sensor values
lv_style_t style_sensor_unit;   // Large font for sensor units
lv_style_t style_button;
lv_style_t style_slider;
lv_style_t style_icon_mdi;

// Create styles for the tileview interface
void uiCreateStyles() {
    // Tile background style
    lv_style_init(&style_tile_bg);
    lv_style_set_bg_color(&style_tile_bg, lv_color_hex(0x000000));
    lv_style_set_pad_all(&style_tile_bg, 0);  // tiles are the full 390 px face; layout uses device coordinates
    
    // Title style
    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, lv_color_white());
    lv_style_set_text_font(&style_title, &lv_font_montserrat_18);
    
    // Icon style
    lv_style_init(&style_icon);
    lv_style_set_text_color(&style_icon, lv_color_hex(0x4CAF50));
    lv_style_set_text_font(&style_icon, &lv_font_montserrat_18);
    
    // Large value style
    lv_style_init(&style_value_large);
    lv_style_set_text_color(&style_value_large, lv_color_white());
    lv_style_set_text_font(&style_value_large, &lv_font_montserrat_18);
    
    // Small value style
    lv_style_init(&style_value_small);
    lv_style_set_text_color(&style_value_small, lv_color_hex(0xCCCCCC));
    lv_style_set_text_font(&style_value_small, &lv_font_montserrat_14);
    
    // Very large sensor value style (use largest available font)
    lv_style_init(&style_sensor_value);
    lv_style_set_text_color(&style_sensor_value, lv_color_white());
    lv_style_set_text_font(&style_sensor_value, &lv_font_montserrat_26);
    
    // Large sensor unit style (use largest available smaller font)
    lv_style_init(&style_sensor_unit);
    lv_style_set_text_color(&style_sensor_unit, lv_color_hex(0x888888));
    lv_style_set_text_font(&style_sensor_unit, &lv_font_montserrat_18);
    
    // Button style
    lv_style_init(&style_button);
    lv_style_set_bg_color(&style_button, lv_color_hex(0x2196F3));
    lv_style_set_border_color(&style_button, lv_color_hex(0x1976D2));
    lv_style_set_border_width(&style_button, 2);
    lv_style_set_radius(&style_button, 8);
    
    // MDI glyph (icon font)
    lv_style_init(&style_icon_mdi);
    lv_style_set_text_color(&style_icon_mdi, lv_color_hex(UI_COLOR_ACCENT_OK));
    lv_style_set_text_font(&style_icon_mdi, UI_FONT_ICON_S);

    // Slider style
    lv_style_init(&style_slider);
    lv_style_set_bg_color(&style_slider, lv_color_hex(0x3C3C3C));
    lv_style_set_border_color(&style_slider, lv_color_hex(0x666666));
    lv_style_set_border_width(&style_slider, 2);
}

// ---- Layout helpers ----

// Distance from the top of a label to its text baseline
int uiBaselineOffset(const lv_font_t* font) {
    return lv_font_get_line_height(font) - font->base_line;
}

// Single-line, centered, ellipsized label whose text baseline sits at y (device pixels,
// 0 = top of the 390 px face) and which is at most `width` wide.
lv_obj_t* uiCreateLine(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color,
                            int baselineY, int width) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, width);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, baselineY - uiBaselineOffset(font));
    return label;
}

void uiSetLineText(lv_obj_t* label, const char* text) {
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

// ---- Toggle tile: state disc ----

// Applies the checked state of the disc and the matching state text and colors
static void applyToggleState(int entityIndex, bool on) {
    HAEntity& entity = entities[entityIndex];
    if (entity.wControl) {
        if (on) lv_obj_add_state(entity.wControl, LV_STATE_CHECKED);
        else lv_obj_clear_state(entity.wControl, LV_STATE_CHECKED);
    }
    if (entity.wStatus) {
        lv_obj_set_style_text_color(entity.wStatus, lv_color_hex(on ? UI_COLOR_ACCENT_LIGHT : UI_COLOR_TEXT_DIM), 0);
        uiSetLineText(entity.wStatus, on ? "On" : "Off");
    }
}

void uiSetToggleState(int entityIndex, bool on) { applyToggleState(entityIndex, on); }

// Event handlers for different entity types
static void toggle_event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t * obj = lv_event_get_target(e);
        int entityIndex = (int)(uintptr_t)lv_event_get_user_data(e);
        
        bool is_checked = lv_obj_has_state(obj, LV_STATE_CHECKED);
        Serial.println("Toggle via TOUCH for entity " + String(entityIndex) + ": " + String(is_checked ? "ON" : "OFF"));
        
        entities[entityIndex].state = is_checked;
        applyToggleState(entityIndex, is_checked);
        handleEntityAction(entityIndex, "toggle", String(is_checked));
    }
}

// Create UI for toggle entities (switches, lights): name, state disc, state text.
// The disc is the touch target; it stays 128 px with room for a brightness ring around it
// (docs/PLAN_light_brightness.md).
void createToggleTile(int entityIndex) {
    lv_obj_t* tile = entities[entityIndex].tile;

    uiCreateLine(tile, entities[entityIndex].name.c_str(), UI_FONT_NAME, UI_COLOR_TEXT_DIM, UI_Y_NAME_PLAIN, UI_W_NAME_PLAIN);

    // One label is both the disc and the icon: background, border and glyph color follow CHECKED
    lv_obj_t* disc = lv_label_create(tile);
    lv_label_set_long_mode(disc, LV_LABEL_LONG_CLIP);
    lv_label_set_text(disc, uiIconGlyph(entities[entityIndex].icon.c_str()));
    lv_obj_set_size(disc, UI_DISC_RADIUS * 2, UI_DISC_RADIUS * 2);
    lv_obj_align(disc, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(disc, UI_FONT_ICON_L, 0);
    lv_obj_set_style_text_align(disc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(disc, (UI_DISC_RADIUS * 2 - lv_font_get_line_height(UI_FONT_ICON_L)) / 2, 0);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_text_color(disc, lv_color_hex(UI_COLOR_INACTIVE), 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(UI_COLOR_ACCENT_LIGHT), LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER * 14 / 100, LV_STATE_CHECKED);
    lv_obj_set_style_text_color(disc, lv_color_hex(UI_COLOR_ACCENT_LIGHT), LV_STATE_CHECKED);
    lv_obj_set_style_border_width(disc, 2, LV_STATE_CHECKED);
    lv_obj_set_style_border_color(disc, lv_color_hex(UI_COLOR_ACCENT_LIGHT), LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(disc, LV_OPA_COVER * 75 / 100, LV_STATE_CHECKED);
    lv_obj_add_flag(disc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(disc, toggle_event_handler, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)entityIndex);

    lv_obj_t* status = uiCreateLine(tile, "", UI_FONT_CAPTION, UI_COLOR_TEXT_DIM, 300, UI_W_CAPTION);

    entities[entityIndex].wControl = disc;
    entities[entityIndex].wStatus = status;
    applyToggleState(entityIndex, entities[entityIndex].state);
}

// Hero text: the digits-only 64 px font, or the 26 px font for values it has no glyphs for
static void setHeroText(lv_obj_t* label, const char* text) {
    lv_obj_set_style_text_font(label, uiFitsDigitsFont(text) ? UI_FONT_HERO_BIG : UI_FONT_SECOND, 0);
    uiSetLineText(label, text);
}

// Create UI for sensor entities: name, icon, value + unit (read-only)
void createSensorTile(int entityIndex) {
    lv_obj_t* tile = entities[entityIndex].tile;

    uiCreateLine(tile, entities[entityIndex].name.c_str(), UI_FONT_NAME, UI_COLOR_TEXT_DIM, 100, UI_W_NAME_RING);

    lv_obj_t* icon = lv_label_create(tile);
    lv_label_set_text(icon, uiIconGlyph(entities[entityIndex].icon.c_str()));
    lv_obj_set_style_text_font(icon, UI_FONT_ICON_S, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(UI_COLOR_ACCENT_OK), 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, 150 - UI_CENTER);

    // Value and unit share a row, bottom aligned, with the row's baseline at the hero baseline
    lv_obj_t* row = lv_obj_create(tile);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, UI_W_HERO, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 232 + UI_FONT_HERO_BIG->base_line - lv_font_get_line_height(UI_FONT_HERO_BIG));

    lv_obj_t* valueLabel = lv_label_create(row);
    lv_obj_set_style_text_color(valueLabel, lv_color_hex(UI_COLOR_TEXT), 0);
    setHeroText(valueLabel, entities[entityIndex].sensorValue.c_str());

    lv_obj_t* unitLabel = lv_label_create(row);
    lv_obj_set_style_text_font(unitLabel, UI_FONT_NAME, 0);
    lv_obj_set_style_text_color(unitLabel, lv_color_hex(UI_COLOR_TEXT_DIM), 0);
    // Bring the unit's baseline level with the value's baseline
    lv_obj_set_style_pad_bottom(unitLabel, UI_FONT_HERO_BIG->base_line - UI_FONT_NAME->base_line, 0);
    lv_label_set_text(unitLabel, entities[entityIndex].sensorUnit.c_str());

    entities[entityIndex].wValue = valueLabel;
    entities[entityIndex].wUnit = unitLabel;
}

// Create entity tile based on type
void createEntityTile(int entityIndex, lv_obj_t* tileview) {
    if (entities[entityIndex].tileCreated) return;

    // Resolve the default icon now: the tile builders read it once at creation time
    ensureEntityIcon(entities[entityIndex]);
    
    // Create the tile
    entities[entityIndex].tile = lv_tileview_add_tile(tileview, entityIndex, 0, LV_DIR_HOR);
    lv_obj_add_style(entities[entityIndex].tile, &style_tile_bg, 0);
    
    // Create entity-specific UI based on type
    switch (entities[entityIndex].type) {
        case ENTITY_TOGGLE:
            createToggleTile(entityIndex);
            break;
        case ENTITY_COVER:
            createCoverTile(entityIndex);
            break;
        case ENTITY_THERMOSTAT:
            createThermostatTile(entityIndex);
            break;
        case ENTITY_SENSOR:
            createSensorTile(entityIndex);
            break;
        case ENTITY_FAN:
            createFanTile(entityIndex);
            break;
    }
    
    entities[entityIndex].tileCreated = true;
    Serial.println("Created tile for entity " + String(entityIndex) + " (" + entities[entityIndex].name + ")");
}

// Update UI elements for specific entity
void updateEntityTile(int entityIndex) {
    if (entityIndex < 0 || !entities[entityIndex].tileCreated) {
        return;
    }
    
    HAEntity &entity = entities[entityIndex];
    if (!entity.tile) return;
    
    switch (entity.type) {
        case ENTITY_TOGGLE: {
            if (entity.state != entity.lastState && !userHoldsControl(entityIndex)) {
                applyToggleState(entityIndex, entity.state);
                entity.lastState = entity.state;
            }
            break;
        }
        case ENTITY_COVER:
            uiUpdateCover(entityIndex);
            break;
        case ENTITY_THERMOSTAT:
            uiUpdateThermostat(entityIndex);
            break;
        case ENTITY_SENSOR: {
            if (entity.sensorValue != entity.lastSensorValue) {
                if (entity.wValue) setHeroText(entity.wValue, entity.sensorValue.c_str());
                if (entity.wUnit) uiSetLineText(entity.wUnit, entity.sensorUnit.c_str());
                entity.lastSensorValue = entity.sensorValue;
            }
            break;
        }
        case ENTITY_FAN:
            uiUpdateFan(entityIndex);
            break;
    }
}
