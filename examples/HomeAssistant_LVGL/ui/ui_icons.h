#pragma once
// Maps Home Assistant icon names ("mdi:lightbulb") to glyphs of the MDI subset fonts
// (ui/fonts/ui_font_mdi_*.c). Arduino-free so it can be unit tested on the host.
// Regenerate the fonts with tools/gen_fonts.sh after adding an icon here.

#include <string.h>

struct UiIcon {
    const char* name;   // without the "mdi:" prefix
    const char* glyph;  // UTF-8 encoded private-use codepoint
};

static const UiIcon UI_ICONS[] = {
    {"lightbulb", "\xF3\xB0\x8C\xB5"},  // U+F0335
    {"lightbulb-outline", "\xF3\xB0\x8C\xB6"},  // U+F0336
    {"light-switch", "\xF3\xB0\xA5\xBE"},  // U+F097E
    {"toggle-switch", "\xF3\xB0\x94\xA1"},  // U+F0521
    {"power-plug", "\xF3\xB0\x9A\xA5"},  // U+F06A5
    {"blinds", "\xF3\xB0\x82\xAC"},  // U+F00AC
    {"blinds-open", "\xF3\xB1\x80\x91"},  // U+F1011
    {"window-shutter", "\xF3\xB1\x84\x9C"},  // U+F111C
    {"garage", "\xF3\xB0\x9B\x99"},  // U+F06D9
    {"fan", "\xF3\xB0\x88\x90"},  // U+F0210
    {"thermostat", "\xF3\xB0\x8E\x93"},  // U+F0393
    {"thermometer", "\xF3\xB0\x94\x8F"},  // U+F050F
    {"water-percent", "\xF3\xB0\x96\x8E"},  // U+F058E
    {"gauge", "\xF3\xB0\x8A\x9A"},  // U+F029A
    {"flash", "\xF3\xB0\x89\x81"},  // U+F0241
    {"help-circle", "\xF3\xB0\x8B\x97"},  // U+F02D7
    {"door", "\xF3\xB0\xA0\x9A"},  // U+F081A
    {"radiobox-marked", "\xF3\xB0\x90\xBE"},  // U+F043E
    {"home-automation", "\xF3\xB0\x9F\x91"},  // U+F07D1
};

#define UI_ICON_FALLBACK "\xF3\xB0\x8B\x97"  // mdi:help-circle (U+F02D7)

// Glyph for "mdi:<name>"; unknown names (and names without the prefix) give the help-circle glyph.
inline const char* uiIconGlyph(const char* mdiName) {
    if (!mdiName || strncmp(mdiName, "mdi:", 4) != 0) return UI_ICON_FALLBACK;
    for (const UiIcon& icon : UI_ICONS) {
        if (strcmp(icon.name, mdiName + 4) == 0) return icon.glyph;
    }
    return UI_ICON_FALLBACK;
}
