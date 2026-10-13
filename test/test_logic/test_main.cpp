#include <unity.h>
#include <string.h>
#include "app_logic.h"
#include "service_call.h"
#include "encoder_decoder.h"
#include "ui_icons.h"
#include "ui_layout.h"

void setUp() {}
void tearDown() {}

// ---- domain mapping ----

void test_domain_of() {
    char out[24];
    TEST_ASSERT_TRUE(domainOf("light.desk_lamp", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("light", out);
    TEST_ASSERT_TRUE(domainOf("input_boolean.test", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("input_boolean", out);
    TEST_ASSERT_FALSE(domainOf("nodot", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("unknown", out);
    TEST_ASSERT_FALSE(domainOf(".leading", out, sizeof(out)));
    TEST_ASSERT_FALSE(domainOf("a_very_long_domain_name_that_overflows.x", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("unknown", out);
}

void test_entity_type_for_domain() {
    TEST_ASSERT_EQUAL(ENTITY_COVER, entityTypeForDomain("cover"));
    TEST_ASSERT_EQUAL(ENTITY_THERMOSTAT, entityTypeForDomain("climate"));
    TEST_ASSERT_EQUAL(ENTITY_SENSOR, entityTypeForDomain("sensor"));
    TEST_ASSERT_EQUAL(ENTITY_FAN, entityTypeForDomain("fan"));
    TEST_ASSERT_EQUAL(ENTITY_TOGGLE, entityTypeForDomain("light"));
    TEST_ASSERT_EQUAL(ENTITY_TOGGLE, entityTypeForDomain("switch"));
    TEST_ASSERT_EQUAL(ENTITY_TOGGLE, entityTypeForDomain("input_boolean"));
    TEST_ASSERT_EQUAL(ENTITY_TOGGLE, entityTypeForDomain("something_else"));
}

void test_default_icons() {
    TEST_ASSERT_EQUAL_STRING("mdi:lightbulb", defaultIconForDomain("light"));
    TEST_ASSERT_EQUAL_STRING("mdi:window-shutter", defaultIconForDomain("cover"));
    TEST_ASSERT_EQUAL_STRING("mdi:thermostat", defaultIconForDomain("climate"));
    TEST_ASSERT_EQUAL_STRING("mdi:help-circle", defaultIconForDomain("unknown"));
}

// ---- thermostat math ----

// ---- icon mapping ----

void test_icon_glyphs() {
    TEST_ASSERT_EQUAL_STRING("\xF3\xB0\x8C\xB5", uiIconGlyph("mdi:lightbulb"));  // U+F0335
    // every default icon of a known domain has its own glyph
    const char* domains[] = {"light", "switch", "input_boolean", "sensor", "fan", "climate", "cover"};
    for (const char* d : domains) {
        TEST_ASSERT_NOT_EQUAL(0, strcmp(UI_ICON_FALLBACK, uiIconGlyph(defaultIconForDomain(d))));
    }
}

void test_icon_fallback() {
    TEST_ASSERT_EQUAL_STRING(UI_ICON_FALLBACK, uiIconGlyph("mdi:does-not-exist"));
    TEST_ASSERT_EQUAL_STRING(UI_ICON_FALLBACK, uiIconGlyph("lightbulb"));
    TEST_ASSERT_EQUAL_STRING(UI_ICON_FALLBACK, uiIconGlyph(""));
    TEST_ASSERT_EQUAL_STRING(UI_ICON_FALLBACK, uiIconGlyph(nullptr));
}

// ---- UI layout ----

void test_dot_positions() {
    int x, y;
    // single dot sits straight below the center, on the radius
    uiDotPosition(0, 1, 195, 195, 172, 7, &x, &y);
    TEST_ASSERT_EQUAL_INT(195, x);
    TEST_ASSERT_EQUAL_INT(367, y);
    // dots run left to right
    uiDotPosition(0, 3, 195, 195, 172, 7, &x, &y);
    int xr, yr;
    uiDotPosition(2, 3, 195, 195, 172, 7, &xr, &yr);
    TEST_ASSERT_TRUE(x < xr);
    // even count is symmetric around the vertical axis
    int xa, ya, xb, yb;
    uiDotPosition(0, 6, 195, 195, 172, 7, &xa, &ya);
    uiDotPosition(5, 6, 195, 195, 172, 7, &xb, &yb);
    TEST_ASSERT_EQUAL_INT(390, xa + xb);
    TEST_ASSERT_EQUAL_INT(ya, yb);
    // the 12-dot limit spans 77 degrees and stays inside the screen
    uiDotPosition(0, 12, 195, 195, 172, 7, &x, &y);
    TEST_ASSERT_TRUE(x > 60 && y < 390);
}

void test_digits_font_coverage() {
    TEST_ASSERT_TRUE(uiFitsDigitsFont("24.9"));
    TEST_ASSERT_TRUE(uiFitsDigitsFont("-3.5"));
    TEST_ASSERT_TRUE(uiFitsDigitsFont("21.8\xC2\xB0"));
    TEST_ASSERT_TRUE(uiFitsDigitsFont("50%"));
    TEST_ASSERT_FALSE(uiFitsDigitsFont("N/A"));
    TEST_ASSERT_FALSE(uiFitsDigitsFont("Unavailable"));
    TEST_ASSERT_FALSE(uiFitsDigitsFont("24.9C"));
    TEST_ASSERT_FALSE(uiFitsDigitsFont(""));
    TEST_ASSERT_FALSE(uiFitsDigitsFont(nullptr));
}

void test_snap_to_step() {
    TEST_ASSERT_EQUAL_FLOAT(21.5f, snapToStep(21.6f, 0.5f));
    TEST_ASSERT_EQUAL_FLOAT(21.0f, snapToStep(21.2f, 0.5f));
    TEST_ASSERT_EQUAL_FLOAT(22.0f, snapToStep(21.8f, 0.5f));
}

void test_temp_arc_mapping() {
    TEST_ASSERT_EQUAL_INT(0, tempToArcValue(4.0f, 4.0f, 35.0f));
    TEST_ASSERT_EQUAL_INT(100, tempToArcValue(35.0f, 4.0f, 35.0f));
    TEST_ASSERT_EQUAL_INT(50, tempToArcValue(19.5f, 4.0f, 35.0f));
    // clamped outside the range and safe for a degenerate range
    TEST_ASSERT_EQUAL_INT(0, tempToArcValue(-10.0f, 4.0f, 35.0f));
    TEST_ASSERT_EQUAL_INT(100, tempToArcValue(99.0f, 4.0f, 35.0f));
    TEST_ASSERT_EQUAL_INT(0, tempToArcValue(20.0f, 20.0f, 20.0f));

    TEST_ASSERT_EQUAL_FLOAT(4.0f, arcValueToTemp(0, 4.0f, 35.0f));
    TEST_ASSERT_EQUAL_FLOAT(35.0f, arcValueToTemp(100, 4.0f, 35.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 19.5f, arcValueToTemp(50, 4.0f, 35.0f));
}

void test_temp_arc_roundtrip_within_a_step() {
    // The arc has 101 positions; going temp -> arc -> temp must stay within one arc step
    float minT = 4.0f, maxT = 35.0f, arcStep = (maxT - minT) / 100.0f;
    for (float t = minT; t <= maxT; t += 0.5f) {
        float back = arcValueToTemp(tempToArcValue(t, minT, maxT), minT, maxT);
        TEST_ASSERT_FLOAT_WITHIN(arcStep, t, back);
    }
}

// ---- service calls ----

void test_toggle_uses_explicit_state() {
    ServiceCall on = buildServiceCall("light.desk", "toggle", "1");
    TEST_ASSERT_TRUE(on.valid);
    TEST_ASSERT_EQUAL_STRING("light", on.domain);
    TEST_ASSERT_EQUAL_STRING("turn_on", on.service);
    TEST_ASSERT_NULL(on.key);

    ServiceCall off = buildServiceCall("input_boolean.test", "toggle", "0");
    TEST_ASSERT_EQUAL_STRING("input_boolean", off.domain);
    TEST_ASSERT_EQUAL_STRING("turn_off", off.service);

    ServiceCall onTrue = buildServiceCall("switch.plug", "toggle", "true");
    TEST_ASSERT_EQUAL_STRING("turn_on", onTrue.service);
}

void test_toggle_rejects_unsupported_domain() {
    TEST_ASSERT_FALSE(buildServiceCall("sensor.temp", "toggle", "1").valid);
    TEST_ASSERT_FALSE(buildServiceCall("nodot", "toggle", "1").valid);
}

void test_cover_calls() {
    ServiceCall open = buildServiceCall("cover.x", "cover_open", "");
    TEST_ASSERT_TRUE(open.valid);
    TEST_ASSERT_EQUAL_STRING("cover", open.domain);
    TEST_ASSERT_EQUAL_STRING("open_cover", open.service);
    TEST_ASSERT_EQUAL_STRING("close_cover", buildServiceCall("cover.x", "cover_close", "").service);
    TEST_ASSERT_EQUAL_STRING("stop_cover", buildServiceCall("cover.x", "cover_stop", "").service);

    ServiceCall pos = buildServiceCall("cover.x", "cover_set_position", "60");
    TEST_ASSERT_EQUAL_STRING("set_cover_position", pos.service);
    TEST_ASSERT_EQUAL_STRING("position", pos.key);
    TEST_ASSERT_FALSE(pos.isFloat);
    TEST_ASSERT_EQUAL_INT(60, pos.intValue);
}

void test_fan_and_climate_calls() {
    ServiceCall fan = buildServiceCall("fan.x", "fan_set_speed", "40");
    TEST_ASSERT_EQUAL_STRING("fan", fan.domain);
    TEST_ASSERT_EQUAL_STRING("set_percentage", fan.service);
    TEST_ASSERT_EQUAL_STRING("percentage", fan.key);
    TEST_ASSERT_EQUAL_INT(40, fan.intValue);

    ServiceCall temp = buildServiceCall("climate.x", "set_temperature", "21.5");
    TEST_ASSERT_EQUAL_STRING("climate", temp.domain);
    TEST_ASSERT_EQUAL_STRING("set_temperature", temp.service);
    TEST_ASSERT_EQUAL_STRING("temperature", temp.key);
    TEST_ASSERT_TRUE(temp.isFloat);
    TEST_ASSERT_EQUAL_FLOAT(21.5f, temp.floatValue);
}

void test_unknown_action_is_invalid() {
    TEST_ASSERT_FALSE(buildServiceCall("light.x", "explode", "").valid);
}

// ---- encoder decoder ----

static void feed(EncoderDecoder& d, const uint8_t* states, size_t n) {
    for (size_t i = 0; i < n; i++) d.update(states[i]);
}

void test_encoder_clockwise_detents() {
    EncoderDecoder d;
    d.reset(0b00);
    // 00 -> 10 -> 11 (detent) -> 01 -> 00 (detent)
    const uint8_t cw[] = {0b10, 0b11, 0b01, 0b00};
    feed(d, cw, sizeof(cw));
    TEST_ASSERT_EQUAL_INT(2, d.take());
    TEST_ASSERT_EQUAL_INT(0, d.take());
}

void test_encoder_counterclockwise_detents() {
    EncoderDecoder d;
    d.reset(0b00);
    // 00 -> 01 -> 11 (detent) -> 10 -> 00 (detent)
    const uint8_t ccw[] = {0b01, 0b11, 0b10, 0b00};
    feed(d, ccw, sizeof(ccw));
    TEST_ASSERT_EQUAL_INT(-2, d.take());
}

void test_encoder_starting_from_other_rest_state() {
    EncoderDecoder d;
    d.reset(0b11);
    const uint8_t cw[] = {0b01, 0b00}; // 11 -> 01 -> 00 is clockwise
    feed(d, cw, sizeof(cw));
    TEST_ASSERT_EQUAL_INT(1, d.take());
}

void test_encoder_ignores_bounce_back_to_same_rest() {
    EncoderDecoder d;
    d.reset(0b00);
    const uint8_t bounce[] = {0b10, 0b00, 0b01, 0b00, 0b10, 0b00};
    feed(d, bounce, sizeof(bounce));
    TEST_ASSERT_EQUAL_INT(0, d.take());
}

void test_encoder_direction_change_mid_turn() {
    EncoderDecoder d;
    d.reset(0b00);
    // starts clockwise, reverses before reaching the next rest state
    const uint8_t reverse[] = {0b10, 0b00, 0b01, 0b11};
    feed(d, reverse, sizeof(reverse));
    TEST_ASSERT_EQUAL_INT(-1, d.take());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_domain_of);
    RUN_TEST(test_entity_type_for_domain);
    RUN_TEST(test_default_icons);
    RUN_TEST(test_icon_glyphs);
    RUN_TEST(test_icon_fallback);
    RUN_TEST(test_dot_positions);
    RUN_TEST(test_digits_font_coverage);
    RUN_TEST(test_snap_to_step);
    RUN_TEST(test_temp_arc_mapping);
    RUN_TEST(test_temp_arc_roundtrip_within_a_step);
    RUN_TEST(test_toggle_uses_explicit_state);
    RUN_TEST(test_toggle_rejects_unsupported_domain);
    RUN_TEST(test_cover_calls);
    RUN_TEST(test_fan_and_climate_calls);
    RUN_TEST(test_unknown_action_is_invalid);
    RUN_TEST(test_encoder_clockwise_detents);
    RUN_TEST(test_encoder_counterclockwise_detents);
    RUN_TEST(test_encoder_starting_from_other_rest_state);
    RUN_TEST(test_encoder_ignores_bounce_back_to_same_rest);
    RUN_TEST(test_encoder_direction_change_mid_turn);
    return UNITY_END();
}
