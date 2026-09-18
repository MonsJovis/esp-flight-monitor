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
