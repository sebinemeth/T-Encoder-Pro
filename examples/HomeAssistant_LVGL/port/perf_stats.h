#pragma once

// Rendering instrumentation for UI phase 1b. Enable with -D ENABLE_PERF_STATS=1.
// Prints one line on Serial after every burst of screen updates (e.g. a tile swap):
//   [perf] burst_ms=N frames=N fps=N flushes=N px=N flush_ms=N lvgl_ms=N loop_max_gap_ms=N
// frames: completed screen refreshes; flush_ms: time inside disp_flush (bus transfer);
// lvgl_ms: time inside lv_timer_handler (render + flush); loop_max_gap_ms: longest time
// between two loop() iterations (anything blocking the main task shows up here).
#ifndef ENABLE_PERF_STATS
#define ENABLE_PERF_STATS 0
#endif

#if ENABLE_PERF_STATS
#include <Arduino.h>
void perfFlush(uint32_t us, uint32_t pixels, bool lastOfFrame);
void perfLvglTime(uint32_t us);
void perfLoopTick();
#else
#include <stdint.h>
inline void perfFlush(uint32_t, uint32_t, bool) {}
inline void perfLvglTime(uint32_t) {}
inline void perfLoopTick() {}
#endif
