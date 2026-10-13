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


/**********************
 *  STATIC VARIABLES
 **********************/
lv_indev_t * indev_touchpad;


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

}

/**********************
 *   STATIC FUNCTIONS
 **********************/

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
