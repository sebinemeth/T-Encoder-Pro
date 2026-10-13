/*
 * Page indicator (dots on the bottom bezel, or "n / N") and toast.
 */
#include "ui_theme.h"
#include "ui_layout.h"

static lv_obj_t* dots[UI_DOTS_MAX];
static lv_obj_t* counter = nullptr;
static int dotCount = 0;
static int totalCount = 0;

void uiCreateDots(int count) {
    totalCount = count;
    lv_obj_t* parent = lv_scr_act();  // above the tileview (created before), below the WiFi overlay
    if (count > UI_DOTS_MAX) {
        counter = lv_label_create(parent);
        lv_obj_set_style_text_font(counter, UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_color(counter, lv_color_hex(UI_COLOR_TEXT_DIM), 0);
        lv_label_set_text(counter, "");
        lv_obj_align(counter, LV_ALIGN_TOP_MID, 0, UI_Y_COUNTER - lv_font_get_line_height(UI_FONT_CAPTION) + UI_FONT_CAPTION->base_line);
        return;
    }
    dotCount = count;
    for (int i = 0; i < count; i++) {
        int x, y;
        uiDotPosition(i, count, UI_CENTER, UI_CENTER, UI_DOTS_RADIUS, UI_DOTS_SPACING_DEG, &x, &y);
        lv_obj_t* dot = lv_obj_create(parent);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, UI_DOT_SIZE, UI_DOT_SIZE);
        lv_obj_set_pos(dot, x - UI_DOT_SIZE / 2, y - UI_DOT_SIZE / 2);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        dots[i] = dot;
    }
    uiUpdateDots(0);
}

void uiUpdateDots(int active) {
    if (counter) {
        lv_label_set_text_fmt(counter, "%d / %d", active + 1, totalCount);
        lv_obj_align(counter, LV_ALIGN_TOP_MID, 0, UI_Y_COUNTER - lv_font_get_line_height(UI_FONT_CAPTION) + UI_FONT_CAPTION->base_line);
        return;
    }
    for (int i = 0; i < dotCount; i++) {
        lv_obj_set_style_bg_color(dots[i], lv_color_hex(i == active ? UI_COLOR_TEXT_DIM : UI_COLOR_DOT_IDLE), 0);
    }
}

// ---- Toast: pill on the bottom chord ----
static lv_obj_t* toastLabel = nullptr;
static lv_timer_t* toastTimer = nullptr;
static const int TOAST_TOP = 318;

void uiShowToast(const char* text, uint32_t durationMs) {
    if (!toastLabel) {
        toastLabel = lv_label_create(lv_layer_top());
        lv_obj_set_style_text_font(toastLabel, UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_color(toastLabel, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_set_style_bg_color(toastLabel, lv_color_hex(UI_COLOR_SURFACE), 0);
        lv_obj_set_style_bg_opa(toastLabel, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(toastLabel, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_hor(toastLabel, 20, 0);
        lv_obj_set_style_pad_ver(toastLabel, 8, 0);
        toastTimer = lv_timer_create([](lv_timer_t* t) {
            lv_obj_add_flag(toastLabel, LV_OBJ_FLAG_HIDDEN);
            lv_timer_pause(t);
        }, durationMs, nullptr);
    }
    lv_label_set_text(toastLabel, text);
    lv_obj_align(toastLabel, LV_ALIGN_TOP_MID, 0, TOAST_TOP);
    lv_obj_clear_flag(toastLabel, LV_OBJ_FLAG_HIDDEN);
    lv_timer_set_period(toastTimer, durationMs);
    lv_timer_reset(toastTimer);
    lv_timer_resume(toastTimer);
}
