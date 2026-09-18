/* M1 bring-up: the panel, the touch controller, the memory budget and the font
 * gate. The numbers this logs go into the table at the bottom of docs/PLAN.md.
 *
 * Debug console (over USB serial):
 *   s  screenshot the live framebuffer
 *   f  draw the font card — the M1 "German renders at 100 px" gate
 *   b  run the render benchmark suite
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "ui/display.h"
#include "ui/theme.h"
#include "ui/fonts/fonts.h"
#include "debug/dbg_screen.h"
#include "debug/dbg_bench.h"
#include "debug/dbg_metrics.h"

static const char *TAG = "flight";

static void log_memory_budget(const char *when)
{
    ESP_LOGW(TAG, "MEM %-36s internal %7u (blk %6u)  psram %8u (blk %8u)",
             when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

/* The M1 font gate, drawn so a screenshot answers it: every tier of the scale,
 * with the glyphs that are NOT in the default ASCII subset. If the subsetting
 * is wrong these render as blanks, and blanks are the whole point of the check.
 * Magenta sits directly beside white and cyan because AC 25-11A flags that pair
 * specifically — the open question in AGENTS.md §8.2. */
static void font_card(void)
{
    display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, THEME_GROUND, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    struct { const lv_font_t *f; const char *txt; lv_color_t col; int y; } rows[] = {
        { &plex_sans_cond_100, "München",          THEME_WHITE,         10  },
        { &plex_sans_cond_76,  "Zürich",           THEME_MAGENTA,       118 },
        { &plex_sans_cond_56,  "Wien → Graz",      THEME_WHITE,         206 },
        { &plex_sans_cond_34,  "Großraum · 42°",   THEME_CYAN,          272 },
        { &plex_sans_cond_22,  "Straße, süß, Öl",  THEME_TEXT_PRIMARY,  318 },
        { &plex_mono_32,       "9.100 m  12,4 km", THEME_CYAN,          350 },
        { &plex_mono_17,       "ÄÖÜäöüß °·—→",     THEME_TEXT_LABEL,    396 },
        { &plex_mono_13,       "KEIN FLUGPLAN",    THEME_AMBER,         424 },
        { &plex_mono_12,       "AUA453 · BCS3",    THEME_TEXT_TERTIARY, 448 },
    };
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        lv_obj_t *l = lv_label_create(scr);
        lv_label_set_text(l, rows[i].txt);
        lv_obj_set_style_text_font(l, rows[i].f, 0);
        lv_obj_set_style_text_color(l, rows[i].col, 0);
        lv_obj_set_pos(l, THEME_SIDE_PADDING, rows[i].y);
    }
    display_unlock();
    ESP_LOGI(TAG, "font card drawn");
}

static void bench_suite(void)
{
    ESP_LOGW(TAG, "=== render benchmark: full-screen invalidate every frame ===");
    dbg_bench_run(&lv_font_montserrat_48, "montserrat_48 (flash)", "Muenchen", 8);
    dbg_bench_run(&plex_sans_cond_56,     "plex_cond_56  (psram)", "München",  8);
    dbg_bench_run(&plex_sans_cond_100,    "plex_cond_100 (psram)", "München",  8);
    ESP_LOGW(TAG, "=== benchmark done ===");
    font_card();
}

static void on_cmd(char c)
{
    if (c == 'b') bench_suite();
    else if (c == 'm') dbg_metrics_hero();
    else if (c == 'f') font_card();
}

void app_main(void)
{
    log_memory_budget("boot, before display init");

    ESP_ERROR_CHECK(display_init());
    /* bsp_display_new() already ran brightness init; calling it again just
     * re-configures GPIO4 and logs an LEDC conflict. The BSP drives this
     * backlight active-low, so "flipped 0%" in the log means FULL brightness. */
    bsp_display_backlight_on();

    log_memory_budget("after display init (framebuffer up)");

    dbg_bench_init();
    dbg_screen_start(on_cmd);
    font_card();
    log_memory_budget("after font card drawn");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        log_memory_budget("steady state");
    }
}
