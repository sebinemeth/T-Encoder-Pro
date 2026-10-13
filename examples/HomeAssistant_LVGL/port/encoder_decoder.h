/*
 * @Description: Quadrature decoder state machine (no Arduino dependencies, unit tested
 *               with `pio test -e native`).
 *
 * State is (A << 1) | B. Clockwise: 00 -> 10 -> 11 -> 01 -> 00. One detent is counted
 * each time the encoder moves from one resting state (00 or 11) to the other.
 * update() is safe to call from an interrupt; take() must run with interrupts disabled
 * or on the same core when races matter.
 */
#pragma once

#include <stdint.h>

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

class EncoderDecoder {
public:
    void reset(uint8_t state) {
        lastRest_ = (state == 0b11) ? 0b11 : 0b00;
        direction_ = 0;
        detents_ = 0;
    }

    inline void IRAM_ATTR update(uint8_t state) __attribute__((always_inline)) {
        if (state == 0b00 || state == 0b11) {
            // Reached a resting state: count a detent only if it differs from the last one
            if (state != lastRest_ && direction_ != 0) detents_ += direction_;
            lastRest_ = state;
            direction_ = 0;
        } else if (state == 0b10) {
            direction_ = (lastRest_ == 0b00) ? 1 : -1;
        } else { // 0b01
            direction_ = (lastRest_ == 0b00) ? -1 : 1;
        }
    }

    // Detents since the last call; clockwise positive.
    int take() {
        int value = detents_;
        detents_ = 0;
        return value;
    }

private:
    volatile int detents_ = 0;
    volatile uint8_t lastRest_ = 0;
    volatile int8_t direction_ = 0;
};
