#include "encoder.h"
#include "encoder_decoder.h"
#include "pin_config.h"

static const unsigned long BUTTON_DEBOUNCE_MS = 30;
static const unsigned long BUTTON_LONG_PRESS_MS = 600;

static EncoderDecoder decoder;

static void IRAM_ATTR encoder_isr() {
    decoder.update((digitalRead(KNOB_DATA_A) << 1) | digitalRead(KNOB_DATA_B));
}

void encoder_init() {
    pinMode(KNOB_DATA_A, INPUT_PULLUP);
    pinMode(KNOB_DATA_B, INPUT_PULLUP);
    pinMode(KNOB_KEY, INPUT_PULLUP);

    decoder.reset((digitalRead(KNOB_DATA_A) << 1) | digitalRead(KNOB_DATA_B));

    attachInterrupt(digitalPinToInterrupt(KNOB_DATA_A), encoder_isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(KNOB_DATA_B), encoder_isr, CHANGE);
}

int encoder_take_detents() {
    noInterrupts();
    int value = decoder.take();
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
