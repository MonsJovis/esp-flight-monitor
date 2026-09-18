#include "dbg_bench.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "ui/display.h"

static const char *TAG = "bench";

static volatile uint32_t s_frames;
static lv_timer_t *s_inval;

static void refr_ready_cb(lv_event_t *e) { (void)e; s_frames++; }

/* Worst case on purpose: the real UI redraws everything when a poll lands, and
 * a partial-area benchmark would flatter the numbers. */
static void invalidate_cb(lv_timer_t *t)
{
    (void)t;
    lv_obj_invalidate(lv_screen_active());
}

void dbg_bench_init(void)
{
    lv_display_add_event_cb(display_lv(), refr_ready_cb, LV_EVENT_REFR_READY, NULL);
}

void dbg_bench_run(const lv_font_t *font, const char *font_name,
                   const char *text, int seconds)
{
    display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0B0D), 0);

    lv_obj_t *hero = lv_label_create(scr);
    lv_label_set_text(hero, text);
    lv_obj_set_style_text_font(hero, font, 0);
    lv_obj_set_style_text_color(hero, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(hero, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(hero);

    s_inval = lv_timer_create(invalidate_cb, 10, NULL);
    s_frames = 0;
    display_unlock();

    int64_t t0 = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(seconds * 1000));
    int64_t dt = esp_timer_get_time() - t0;
    uint32_t frames = s_frames;

    display_lock(0);
    lv_timer_delete(s_inval);
    s_inval = NULL;
    display_unlock();

    double fps = (double)frames * 1e6 / (double)dt;
    ESP_LOGW(TAG, "FPS %-22s %6.2f  (%lu frames / %.1fs)  psram_free=%u internal_free=%u",
             font_name, fps, (unsigned long)frames, dt / 1e6,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}
