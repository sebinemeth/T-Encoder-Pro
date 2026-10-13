#pragma once
// Pure layout helpers (no LVGL, no Arduino) so they can be unit tested on the host.

#include <math.h>
#include <string.h>

// Position of indicator dot i of n on the bottom bezel arc: centered at 90 degrees
// (straight down), `spacingDeg` apart, first dot on the left, on a circle of `radius` around (cx, cy).
inline void uiDotPosition(int i, int n, int cx, int cy, int radius, int spacingDeg, int* x, int* y) {
    const float angle = (90.0f - (i - (n - 1) / 2.0f) * spacingDeg) * 3.14159265f / 180.0f;
    *x = cx + (int)lroundf(radius * cosf(angle));
    *y = cy + (int)lroundf(radius * sinf(angle));
}

// True if every character of s is in the digits-only hero fonts' glyph set
// ("0123456789.-+ %/" and the degree sign, UTF-8 C2 B0). Anything else (for example
// "Unavailable" or "N/A") must use a full font.
inline bool uiFitsDigitsFont(const char* s) {
    if (!s || !*s) return false;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        if (*p == 0xC2 && p[1] == 0xB0) { p++; continue; }
        if (!strchr("0123456789.-+ %/", *p)) return false;
    }
    return true;
}
