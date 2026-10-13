/**
 * @file lv_port_disp.cpp
 */

#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp.h"
#include <stdbool.h>
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include "perf_stats.h"

/*********************
 *      DEFINES
 *********************/
// QSPI clock for the panel. Arduino_GFX defaults to 8 MHz; 80 MHz verified on the owner's unit; override with -D DISPLAY_BUS_HZ=...
// Draw buffer height in rows, and whether a second buffer is used. Flushing is synchronous,
// so a second buffer buys nothing; one larger buffer means fewer full-tree redraw passes.
#ifndef DISP_BUF_ROWS
#define DISP_BUF_ROWS 78   /* 5 passes per full frame, 61 KB */
#endif
#ifndef DISP_BUF_DOUBLE
#define DISP_BUF_DOUBLE 0
#endif
#ifndef DISPLAY_BUS_HZ
#define DISPLAY_BUS_HZ 80000000
#endif
#define MY_DISP_HOR_RES SCREEN_WIDTH
#define MY_DISP_VER_RES SCREEN_HEIGHT

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);

/**********************
 *  STATIC VARIABLES
 **********************/
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    SCREEN_CS /* CS */, SCREEN_SCLK /* SCK */, SCREEN_SDIO0 /* SDIO0 */,
    SCREEN_SDIO1 /* SDIO1 */, SCREEN_SDIO2 /* SDIO2 */, SCREEN_SDIO3 /* SDIO3 */);

Arduino_GFX *gfx = new Arduino_SH8601(bus, SCREEN_RST /* RST */, 0 /* rotation */,
                                      false /* IPS */, SCREEN_WIDTH, SCREEN_HEIGHT);

volatile bool disp_flush_enabled = true;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_disp_init(void)
{
    /*-------------------------
     * Initialize your display
     * -----------------------*/
    disp_init();

    /*-----------------------------
     * Create a buffer for drawing
     *----------------------------*/
    
    static lv_disp_draw_buf_t draw_buf_dsc;
    static lv_color_t buf_1[MY_DISP_HOR_RES * DISP_BUF_ROWS];
#if DISP_BUF_DOUBLE
    static lv_color_t buf_2[MY_DISP_HOR_RES * DISP_BUF_ROWS];
    lv_disp_draw_buf_init(&draw_buf_dsc, buf_1, buf_2, MY_DISP_HOR_RES * DISP_BUF_ROWS);
#else
    lv_disp_draw_buf_init(&draw_buf_dsc, buf_1, NULL, MY_DISP_HOR_RES * DISP_BUF_ROWS);
#endif

    /*-----------------------------------
     * Register the display in LVGL
     *----------------------------------*/

    static lv_disp_drv_t disp_drv;                         /*Descriptor of a display driver*/
    lv_disp_drv_init(&disp_drv);                    /*Basic initialization*/

    /*Set the resolution of the display*/
    disp_drv.hor_res = MY_DISP_HOR_RES;
    disp_drv.ver_res = MY_DISP_VER_RES;

    /*Used to copy the buffer's content to the display*/
    disp_drv.flush_cb = disp_flush;

    /*Set a display buffer*/
    disp_drv.draw_buf = &draw_buf_dsc;

    /*Finally register the driver*/
    lv_disp_drv_register(&disp_drv);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    // Enable LCD power
    pinMode(SCREEN_EN, OUTPUT);
    digitalWrite(SCREEN_EN, HIGH);

    // Initialize display
    if (!gfx->begin(DISPLAY_BUS_HZ))
    {
        Serial.println("gfx->begin() failed!");
    }
    gfx->fillScreen(BLACK);

    // Fade in display brightness
    for (uint8_t i = 0; i < 255; i++)
    {
        gfx->Display_Brightness(i);
        delay(3);
    }
}

/* Enable updating the screen (the flushing process) when disp_flush() is called by LVGL */
void disp_enable_update(void)
{
    disp_flush_enabled = true;
}

/* Disable updating the screen (the flushing process) when disp_flush() is called by LVGL */
void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

/*Flush the content of the internal buffer the specific area on the display*/
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    uint32_t t0 = micros();
    if(disp_flush_enabled) {
        uint32_t w = (area->x2 - area->x1 + 1);
        uint32_t h = (area->y2 - area->y1 + 1);

#if (LV_COLOR_16_SWAP != 0)
        gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
        gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif
    }

    perfFlush(micros() - t0, (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1), lv_disp_flush_is_last(disp_drv));

    /*IMPORTANT!!!
     *Inform the graphics library that you are ready with the flushing*/
    lv_disp_flush_ready(disp_drv);
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
