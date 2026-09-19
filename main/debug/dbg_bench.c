#include "dbg_bench.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "ui/display.h"
#include "ui/fonts/fonts.h"

static const char *TAG = "bench";

static volatile uint32_t s_frames;
static volatile int64_t  s_last_frame_us;
static volatile int64_t  s_worst_gap_us;
static lv_timer_t *s_inval;

static void refr_ready_cb(lv_event_t *e)
{
    (void)e;
    s_frames++;
    int64_t now = esp_timer_get_time();
    if (s_last_frame_us != 0) {
        int64_t gap = now - s_last_frame_us;
        if (gap > s_worst_gap_us) {
            s_worst_gap_us = gap;
        }
    }
    s_last_frame_us = now;
}

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

/* ---- Tearing characterisation (PLAN.md M4) ------------------------------ */

#include "nvs.h"
#include "nvs_flash.h"

/* One NVS commit of a realistic payload. The route cache is what will actually
 * be writing here, so the blob is sized like it rather than like a token. */
static void nvs_write_burst(int commits, size_t blob_bytes)
{
    uint8_t *blob = heap_caps_malloc(blob_bytes, MALLOC_CAP_SPIRAM);
    if (blob == NULL) {
        ESP_LOGE(TAG, "no memory for the tearing test blob");
        return;
    }
    memset(blob, 0xA5, blob_bytes);

    nvs_handle_t h;
    if (nvs_open("teartest", NVS_READWRITE, &h) != ESP_OK) {
        free(blob);
        return;
    }
    for (int i = 0; i < commits; i++) {
        /* Vary the payload so NVS cannot elide the write as a no-op. */
        blob[0] = (uint8_t)i;
        nvs_set_blob(h, "blob", blob, blob_bytes);
        nvs_commit(h);              /* the actual flash write */
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    nvs_close(h);
    free(blob);
}

static int64_t measure_window(int seconds, int commits, size_t blob_bytes, bool pause_lvgl)
{
    s_worst_gap_us = 0;
    s_last_frame_us = 0;
    int64_t t_end = esp_timer_get_time() + (int64_t)seconds * 1000000;

    if (commits > 0) {
        if (pause_lvgl) {
            /* The mitigation AGENTS.md §7 prescribes: hold the LVGL lock across
             * the commit so nothing is mid-render while flash is busy. */
            display_lock(0);
            nvs_write_burst(commits, blob_bytes);
            display_unlock();
        } else {
            nvs_write_burst(commits, blob_bytes);
        }
    }
    while (esp_timer_get_time() < t_end) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return s_worst_gap_us;
}

void dbg_bench_tearing(void)
{
    const size_t blob = 2048;   /* about what a trimmed route cache costs */
    const int commits = 20;

    display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0B0D), 0);
    lv_obj_t *hero = lv_label_create(scr);
    lv_label_set_text(hero, "München");
    lv_obj_set_style_text_font(hero, &plex_sans_cond_100, 0);
    lv_obj_set_style_text_color(hero, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(hero);
    s_inval = lv_timer_create(invalidate_cb, 10, NULL);
    display_unlock();

    ESP_LOGW(TAG, "=== tearing test: %d NVS commits of %u B ===", commits, (unsigned)blob);

    int64_t idle      = measure_window(4, 0, 0, false);
    int64_t during    = measure_window(6, commits, blob, false);
    int64_t mitigated = measure_window(6, commits, blob, true);

    display_lock(0);
    lv_timer_delete(s_inval);
    s_inval = NULL;
    display_unlock();

    /* A frame is ~35 ms at the measured 28.5 FPS ceiling, so a gap far beyond
     * that is the window in which the panel has nothing fresh to scan out. */
    ESP_LOGW(TAG, "worst frame gap  idle        : %6lld us", (long long)idle);
    ESP_LOGW(TAG, "worst frame gap  NVS commits : %6lld us", (long long)during);
    ESP_LOGW(TAG, "worst frame gap  + LVGL held : %6lld us", (long long)mitigated);
    ESP_LOGW(TAG, "delta from NVS: %lld us (%.1fx idle)",
             (long long)(during - idle),
             idle > 0 ? (double)during / (double)idle : 0.0);

    /* What the numbers above do NOT settle.
     *
     * Those gaps are LVGL's refresh cadence. The tearing mechanism in
     * espressif/esp-bsp#570 is the LCD peripheral's DMA starving while it reads
     * the framebuffer out of PSRAM during a flash write — and with bb_mode = 0
     * the panel DMAs straight from PSRAM, so a tear leaves no software trace at
     * all. It cannot be measured from in here; it has to be seen.
     *
     * So: draw something a tear is obvious on — hard horizontal edges sweeping
     * vertically — and hammer NVS underneath it for ten seconds. */
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, ">>> WATCH THE PANEL for the next ~10 s <<<");
    ESP_LOGW(TAG, "    High-contrast bars, with NVS commits underneath.");
    ESP_LOGW(TAG, "    A tear looks like a horizontal slice jumping sideways.");

    display_lock(0);
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_t *bars[6];
    for (int i = 0; i < 6; i++) {
        bars[i] = lv_obj_create(scr);
        lv_obj_set_size(bars[i], 480, 40);
        lv_obj_set_style_bg_color(bars[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(bars[i], 0, 0);
        lv_obj_set_style_radius(bars[i], 0, 0);
        lv_obj_set_pos(bars[i], 0, i * 80);
    }
    display_unlock();

    int64_t t_end = esp_timer_get_time() + 10000000;
    int offset = 0;
    while (esp_timer_get_time() < t_end) {
        display_lock(0);
        offset = (offset + 8) % 80;
        for (int i = 0; i < 6; i++) {
            lv_obj_set_pos(bars[i], 0, i * 80 + offset - 80);
        }
        display_unlock();
        nvs_write_burst(2, blob);          /* flash busy while the bars move */
    }

    ESP_LOGW(TAG, ">>> done. Did any bar edge tear or shear sideways? <<<");

    nvs_handle_t h;
    if (nvs_open("teartest", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);           /* leave no litter behind */
        nvs_commit(h);
        nvs_close(h);
    }
}
