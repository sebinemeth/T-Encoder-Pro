#include "encoder.h"
#include "pin_config.h"

static const unsigned long BUTTON_DEBOUNCE_MS = 30;
static const unsigned long BUTTON_LONG_PRESS_MS = 600;

static volatile int detents = 0;
static volatile uint8_t lastRest = 0; // last resting state: 0b00 or 0b11
static volatile int8_t direction = 0; // direction seen since leaving lastRest

// State is (A << 1) | B. Clockwise: 00 -> 10 -> 11 -> 01 -> 00
static void IRAM_ATTR encoder_isr() {
    uint8_t state = (digitalRead(KNOB_DATA_A) << 1) | digitalRead(KNOB_DATA_B);

    if (state == 0b00 || state == 0b11) {
        // Reached a resting state: count a detent only if it differs from the last one
        if (state != lastRest && direction != 0) detents += direction;
        lastRest = state;
        direction = 0;
    } else if (state == 0b10) {
        direction = (lastRest == 0b00) ? 1 : -1;
    } else { // 0b01
        direction = (lastRest == 0b00) ? -1 : 1;
    }
}

void encoder_init() {
    pinMode(KNOB_DATA_A, INPUT_PULLUP);
    pinMode(KNOB_DATA_B, INPUT_PULLUP);
    pinMode(KNOB_KEY, INPUT_PULLUP);

    uint8_t state = (digitalRead(KNOB_DATA_A) << 1) | digitalRead(KNOB_DATA_B);
    lastRest = (state == 0b11) ? 0b11 : 0b00;

    attachInterrupt(digitalPinToInterrupt(KNOB_DATA_A), encoder_isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(KNOB_DATA_B), encoder_isr, CHANGE);
}

int encoder_take_detents() {
    noInterrupts();
    int value = detents;
    detents = 0;
    interrupts();
    return value;
}

EncoderButtonEvent encoder_poll_button() {
    static bool pressed = false;
    static bool longFired = false;
    static unsigned long pressedAt = 0;
    static unsigned long releasedAt = 0;

    unsigned long now = millis();
    bool down = digitalRead(KNOB_KEY) == LOW;

    if (down && !pressed && now - releasedAt > BUTTON_DEBOUNCE_MS) {
        pressed = true;
        longFired = false;
        pressedAt = now;
    } else if (down && pressed && !longFired && now - pressedAt >= BUTTON_LONG_PRESS_MS) {
        longFired = true;
        return ENC_BTN_LONG;
    } else if (!down && pressed) {
        pressed = false;
        releasedAt = now;
        if (!longFired && now - pressedAt > BUTTON_DEBOUNCE_MS) return ENC_BTN_SHORT;
    }
    return ENC_BTN_NONE;
}
