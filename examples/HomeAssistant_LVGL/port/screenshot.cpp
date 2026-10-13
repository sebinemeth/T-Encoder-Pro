#include "screenshot.h"

#if ENABLE_SCREENSHOT_SERVER

#include <Arduino.h>
#include <WebServer.h>
#include "lvgl.h"

// The whole frame (~300 KB) does not fit in internal RAM, and PSRAM is not
// usable on this unit, so the screen is rendered in horizontal strips. Each
// strip is a clipped lv_obj_redraw(), the same technique lv_snapshot uses.
static const int STRIP_ROWS = 26;
static const int MAX_WIDTH = 390;

static WebServer server(80);
static bool started = false;

static void put32(uint8_t *p, uint32_t v) {
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

// Render rows [y0, y0+rows) of obj into buf (rows * width pixels)
static bool renderStrip(lv_obj_t *obj, lv_color_t *buf, int y0, int rows) {
    lv_disp_t *objDisp = lv_obj_get_disp(obj);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    area.y1 += y0;
    area.y2 = area.y1 + rows - 1;

    lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = lv_disp_get_hor_res(objDisp);
    driver.ver_res = lv_disp_get_ver_res(objDisp);
    lv_disp_drv_use_generic_set_px_cb(&driver, LV_IMG_CF_TRUE_COLOR);

    lv_disp_t fakeDisp;
    lv_memset_00(&fakeDisp, sizeof(lv_disp_t));
    fakeDisp.driver = &driver;

    lv_draw_ctx_t *ctx = (lv_draw_ctx_t *)lv_mem_alloc(objDisp->driver->draw_ctx_size);
    if (!ctx) return false;
    objDisp->driver->draw_ctx_init(&driver, ctx);
    driver.draw_ctx = ctx;
    ctx->clip_area = &area;
    ctx->buf_area = &area;
    ctx->buf = buf;
    memset(buf, 0, rows * lv_obj_get_width(obj) * sizeof(lv_color_t));

    lv_disp_t *refrOrig = _lv_refr_get_disp_refreshing();
    _lv_refr_set_disp_refreshing(&fakeDisp);
    lv_obj_redraw(ctx, obj);
    _lv_refr_set_disp_refreshing(refrOrig);

    objDisp->driver->draw_ctx_deinit(&driver, ctx);
    lv_mem_free(ctx);
    return true;
}

static void handleScreenshot() {
    lv_obj_t *scr = lv_scr_act();
    const int w = lv_obj_get_width(scr);
    const int h = lv_obj_get_height(scr);
    if (w > MAX_WIDTH) {
        server.send(500, "text/plain", "screen too wide");
        return;
    }

    lv_color_t *strip = (lv_color_t *)malloc(STRIP_ROWS * w * sizeof(lv_color_t));
    uint8_t *out = (uint8_t *)malloc(STRIP_ROWS * ((w * 3 + 3) & ~3));
    if (!strip || !out) {
        free(strip);
        free(out);
        server.send(500, "text/plain", "out of memory");
        return;
    }

    // BMP rows are stored bottom-up, so emit strips from the bottom of the screen
    const uint32_t rowSize = (w * 3 + 3) & ~3u;
    const uint32_t imgSize = rowSize * h;
    uint8_t hdr[54] = {'B', 'M'};
    put32(hdr + 2, 54 + imgSize);
    put32(hdr + 10, 54);
    put32(hdr + 14, 40);
    put32(hdr + 18, w);
    put32(hdr + 22, h);
    hdr[26] = 1;
    hdr[28] = 24;
    put32(hdr + 34, imgSize);

    server.setContentLength(54 + imgSize);
    server.send(200, "image/bmp", "");
    server.sendContent((const char *)hdr, sizeof(hdr));

    for (int yEnd = h; yEnd > 0; yEnd -= STRIP_ROWS) {
        const int rows = min(STRIP_ROWS, yEnd);
        const int y0 = yEnd - rows;
        if (!renderStrip(scr, strip, y0, rows)) break;
        for (int r = rows - 1; r >= 0; r--) {
            uint8_t *row = out + (rows - 1 - r) * rowSize;
            memset(row, 0, rowSize);
            for (int x = 0; x < w; x++) {
                uint32_t c = lv_color_to32(strip[r * w + x]);
                row[x * 3 + 0] = c;        // B
                row[x * 3 + 1] = c >> 8;   // G
                row[x * 3 + 2] = c >> 16;  // R
            }
        }
        server.sendContent((const char *)out, rows * rowSize);
    }
    free(strip);
    free(out);
}

static void handleTile() {
    if (!server.hasArg("i")) {
        server.send(400, "text/plain", "missing i");
        return;
    }
    debugGoToTile(server.arg("i").toInt(), server.arg("edit") == "1");
    server.send(200, "text/plain", "ok");
}

void screenshotBegin() {
    if (started) return;
    server.on("/screenshot.bmp", HTTP_GET, handleScreenshot);
    server.on("/tile", HTTP_GET, handleTile);
    server.begin();
    started = true;
    Serial.printf("Screenshot: http://%s/screenshot.bmp\n", WiFi.localIP().toString().c_str());
}

void screenshotLoop() {
    if (started) server.handleClient();
}

#endif
