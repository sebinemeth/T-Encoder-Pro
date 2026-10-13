/*
 * @Description: Rotary encoder driver for the T-Encoder-Pro (rotation + push button)
 * @License: GPL 3.0
 *
 * Rotation is decoded in an interrupt so it is not missed while LVGL renders.
 * One detent is counted per move between the two resting states (00 <-> 11),
 * same logic as the upstream Rotary_Encoder example.
 */
#pragma once

#include <Arduino.h>

enum EncoderButtonEvent {
    ENC_BTN_NONE,
    ENC_BTN_SHORT, // released before the long-press threshold
    ENC_BTN_LONG   // held past the threshold (fires once, while still held)
};

void encoder_init();

// Detents turned since the last call. Clockwise is positive.
int encoder_take_detents();

// Poll from loop(); returns at most one event per press.
EncoderButtonEvent encoder_poll_button();
