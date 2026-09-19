/* M1 render benchmark.
 *
 * Measures the thing that actually matters for this product: a large glyph face
 * redrawing full-screen. CONFIG_SPIRAM_RODATA=y puts LVGL fonts in PSRAM, so
 * glyph reads share the bus the framebuffer writes to — the project's #1 risk
 * and its most distinctive design choice pulling on the same wire.
 * (AGENTS.md §7, PLAN.md M1.)
 */
#pragma once
#include "lvgl.h"

void dbg_bench_init(void);

/* Renders `text` in `font`, invalidating the whole screen every frame, for
 * `seconds`. Logs achieved FPS and the PSRAM cost. */
void dbg_bench_run(const lv_font_t *font, const char *font_name,
                   const char *text, int seconds);

/* Characterises espressif/esp-bsp#570 on THIS unit: flash and PSRAM share SPI1,
 * so an NVS commit can starve the RGB panel's refill and tear the display.
 * Tearing is a scan-out artifact — a framebuffer screenshot cannot see it — but
 * the starvation shows up as a frame-time spike, which can be measured.
 * Reports worst-case frame interval idle, during NVS commits, and during NVS
 * commits with LVGL paused around them. (PLAN.md M4.) */
void dbg_bench_tearing(void);
