#!/bin/sh
# Regenerates the UI fonts in examples/HomeAssistant_LVGL/ui/fonts.
# Needs node; installs lv_font_conv and @mdi/font into a temp dir.
# Digits fonts: Montserrat Medium (from the LVGL tree), glyphs "0123456789.-+ %/°".
# Icon fonts: Material Design Icons subset; keep the ranges in sync with ui/ui_icons.h.
set -e
cd "$(dirname "$0")/.."
WORK=$(mktemp -d)
(cd "$WORK" && npm init -y >/dev/null && npm i lv_font_conv @mdi/font >/dev/null)
CONV="$WORK/node_modules/.bin/lv_font_conv"
MDI="$WORK/node_modules/@mdi/font/fonts/materialdesignicons-webfont.ttf"
MONT=libraries/lvgl-8.3.5/scripts/built_in_font/Montserrat-Medium.ttf
OUT=examples/HomeAssistant_LVGL/ui/fonts
for s in 56 64; do
    $CONV --font $MONT --size $s --bpp 4 --no-compress --format lvgl --lv-include lvgl.h \
        -r 0x20,0x25,0x2B,0x2D,0x2E,0x2F,0x30-0x39,0xB0 -o $OUT/ui_font_digits_$s.c --lv-font-name ui_font_digits_$s
done
ICONS=0xF0335,0xF0336,0xF097E,0xF0521,0xF06A5,0xF00AC,0xF1011,0xF111C,0xF06D9,0xF0210,0xF0393,0xF050F,0xF058E,0xF029A,0xF0241,0xF02D7,0xF081A,0xF043E,0xF07D1,0xF0143,0xF0140,0xF04DB
for s in 26 36 64; do
    $CONV --font $MDI --size $s --bpp 4 --no-compress --format lvgl --lv-include lvgl.h \
        -r $ICONS -o $OUT/ui_font_mdi_$s.c --lv-font-name ui_font_mdi_$s
done
rm -rf "$WORK"
