/**
 * @file lv_port_indev.cpp
 */

#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev.h"
#include "lvgl.h"
#include "pin_config.h"
#include "Arduino.h"
#include "TouchDrvCHSC5816.hpp"
#include "Wire.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void touchpad_init(void);
static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);

static void encoder_init(void);
static void encoder_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
static void encoder_handler(void);

/**********************
 *  STATIC VARIABLES
 **********************/
lv_indev_t * indev_touchpad;
lv_indev_t * indev_encoder;

static int32_t encoder_diff;
static lv_indev_state_t encoder_state;

// Touch controller variables
TouchDrvCHSC5816 touch;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_indev_init(void)
{
    /*------------------
     * Touchpad
     * -----------------*/

    /*Initialize your touchpad if you have*/
    touchpad_init();

    /*Register a touchpad input device*/
    static lv_indev_drv_t indev_drv_touchpad;
    lv_indev_drv_init(&indev_drv_touchpad);
    indev_drv_touchpad.type = LV_INDEV_TYPE_POINTER;
    indev_drv_touchpad.read_cb = touchpad_read;
    indev_touchpad = lv_indev_drv_register(&indev_drv_touchpad);

    /*------------------
     * Encoder
     * -----------------*/

    /*Initialize your encoder if you have*/
    encoder_init();

    /*Register an encoder input device*/
    static lv_indev_drv_t indev_drv_encoder;
    lv_indev_drv_init(&indev_drv_encoder);
    indev_drv_encoder.type = LV_INDEV_TYPE_ENCODER;
    indev_drv_encoder.read_cb = encoder_read;
    indev_encoder = lv_indev_drv_register(&indev_drv_encoder);
    
    /*Create a group for encoder navigation*/
    lv_group_t *group = lv_group_create();
    lv_indev_set_group(indev_encoder, group);
    lv_group_set_default(group);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*Initialize your encoder*/
static void encoder_init(void)
{
    /*Set encoder pins as inputs with pull-up*/
    pinMode(KNOB_DATA_A, INPUT_PULLUP);
    pinMode(KNOB_DATA_B, INPUT_PULLUP);  
    pinMode(KNOB_KEY, INPUT_PULLUP);
    
    /*Attach interrupt for encoder rotation*/
    attachInterrupt(digitalPinToInterrupt(KNOB_DATA_A), encoder_handler, CHANGE);
    
    /*Initialize encoder state*/
    encoder_diff = 0;
    encoder_state = LV_INDEV_STATE_REL;
}

/*Will be called by the library to read the encoder*/
static void encoder_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    data->enc_diff = encoder_diff;
    data->state = encoder_state;

    /*Handle encoder rotation*/
    if(encoder_diff != 0) {
        encoder_diff = 0; /*Reset diff after reading*/
    }
    
    /*Handle encoder button press*/
    if (digitalRead(KNOB_KEY) == LOW) {
        encoder_state = LV_INDEV_STATE_PR;
    } else {
        encoder_state = LV_INDEV_STATE_REL;
    }
}

/*Call this function in an interrupt to process encoder events (turn, press)*/
static void encoder_handler(void)
{
    static uint8_t encoder_last_a = 1;
    
    uint8_t a_val = digitalRead(KNOB_DATA_A);
    uint8_t b_val = digitalRead(KNOB_DATA_B);
    
    if ((encoder_last_a == 1) && (a_val == 0)) {
        if (b_val == 0) {
            encoder_diff++;
        } else {
            encoder_diff--;
        }
    }
    
    encoder_last_a = a_val;
}

/*------------------
 * Touchpad
 * -----------------*/

/*Initialize your touchpad*/
static void touchpad_init(void)
{
    /*Initialize the CHSC5816 touch controller*/
    touch.setPins(TOUCH_RST, TOUCH_INT);
    
    if (!touch.begin(Wire, CHSC5816_SLAVE_ADDRESS, IIC_SDA, IIC_SCL))
    {
        Serial.println("Failed to find CHSC5816 - check your wiring!");
        Serial.println("Touch functionality will be disabled");
        return;
    }
    
    Serial.println("CHSC5816 Touch device initialized successfully!");
}

/*Will be called by the library to read the touchpad*/
static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    int16_t x[2], y[2];
    uint8_t touchpad = touch.getPoint(x, y);

    if (touchpad > 0) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x[0];
        data->point.y = y[0];
        
        // Optional: Add debug output
        // Serial.printf("Touch: x=%d, y=%d\n", data->point.x, data->point.y);
    }
    else {
        data->state = LV_INDEV_STATE_REL;
    }
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
