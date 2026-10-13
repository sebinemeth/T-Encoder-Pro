#include "perf_stats.h"

#if ENABLE_PERF_STATS

// A burst is a run of flushes with gaps below BURST_GAP_US; it ends (and is reported)
// when the screen has been idle for that long.
static const uint32_t BURST_GAP_US = 150000;

static uint32_t frames, flushes, pixels, flushUs, lvglUs, maxGapUs;
static uint32_t burstStart, lastFlushEnd, lastTick;
static bool inBurst;
static uint32_t idleWindowStart, idleMaxGapUs;

static void report() {
    uint32_t ms = (lastFlushEnd - burstStart) / 1000;
    Serial.printf("[perf] burst_ms=%u frames=%u fps=%u flushes=%u px=%u flush_ms=%u lvgl_ms=%u loop_max_gap_ms=%u\n",
                  (unsigned)ms, (unsigned)frames, (unsigned)(ms ? frames * 1000 / ms : 0), (unsigned)flushes,
                  (unsigned)pixels, (unsigned)(flushUs / 1000), (unsigned)(lvglUs / 1000), (unsigned)(maxGapUs / 1000));
    frames = flushes = pixels = flushUs = lvglUs = maxGapUs = 0;
    inBurst = false;
}

void perfFlush(uint32_t us, uint32_t px, bool lastOfFrame) {
    uint32_t now = micros();
    if (inBurst && now - lastFlushEnd > BURST_GAP_US) report();
    if (!inBurst) {
        inBurst = true;
        burstStart = now - us;
        lastTick = 0;
    }
    lastFlushEnd = now;
    flushes++;
    pixels += px;
    flushUs += us;
    if (lastOfFrame) frames++;
}

void perfLvglTime(uint32_t us) { if (inBurst) lvglUs += us; }

void perfLoopTick() {
    uint32_t now = micros();
    if (inBurst) {
        if (lastTick && now - lastTick > maxGapUs) maxGapUs = now - lastTick;
        if (now - lastFlushEnd > BURST_GAP_US) report();
    }
    // Outside bursts, report stalls over 50 ms once per 10 s (network calls blocking loop())
    if (!inBurst && lastTick && now - lastTick > idleMaxGapUs) idleMaxGapUs = now - lastTick;
    if (now - idleWindowStart > 10000000) {
        if (idleMaxGapUs > 50000) Serial.printf("[perf] idle loop gap max %u ms\n", (unsigned)(idleMaxGapUs / 1000));
        idleMaxGapUs = 0;
        idleWindowStart = now;
    }
    lastTick = now;
}

#endif
