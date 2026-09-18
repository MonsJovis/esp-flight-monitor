/* M1 bring-up: the panel, the touch controller, the memory budget and the font
 * gate. The numbers this logs go into the table at the bottom of docs/PLAN.md.
 *
 * Debug console (over USB serial):
 *   s  screenshot the live framebuffer
 *   f  draw the font card — the M1 "German renders at 100 px" gate
 *   b  run the render benchmark suite
 *   m  measure every place name against the hero shrink ladder
 *   w  provision WiFi (typed in over serial, stored in NVS — never in the repo)
 *   n  network status and a scan of what is in range
 *   1  replay §5.1 from the real capture   2  §5.2 Ohne Route
 *   3  §5.3 Himmel frei                    4  longest destination (shrink ladder)
 */
#include <stdio.h>
#include <string.h>
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
#include "nvs_flash.h"
#include "net/wifi.h"
#include "net/flight_source.h"
#include "net/timesync.h"
#include "esp_task_wdt.h"
#include "ui/screen_overhead.h"
#include "data/view_build.h"
#include "net/route_parse.h"
#include "debug/dbg_fixture.h"
#include <time.h>

static const char *TAG = "flight";

/* A replayed fixture screen must not be overwritten by the next live poll. */
static volatile bool s_fixture_mode = false;

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

/* Gloggnitz — Semmeringstraße 11. The Pattaya preset and the picker are M6. */
#define HOME_LAT   47.6691
#define HOME_LON   15.9303
#define HOME_RADIUS_NM 30

/* Credentials are typed in here and stored in NVS. They must never appear in a
 * source file: AGENTS.md §10, and "temporarily hard-coded" is exactly how they
 * end up committed. This is also the seed of the M6 provisioning screen, which
 * reads from the same place. */
static void provision_wifi(void)
{
    char line[160];
    printf("\nSSID then TAB then password, one line, ENTER to finish:\n> ");
    fflush(stdout);
    if (dbg_read_line(line, sizeof line, 120000) < 0) {
        ESP_LOGW(TAG, "provisioning timed out — nothing stored");
        return;
    }
    char *tab = strchr(line, '\t');
    if (tab == NULL) {
        ESP_LOGE(TAG, "expected SSID<TAB>password; nothing stored");
        return;
    }
    *tab = '\0';
    const char *ssid = line, *pass = tab + 1;
    if (wifi_creds_set(0, ssid, pass) == ESP_OK) {
        /* SSID only. Never log the password. */
        ESP_LOGW(TAG, "stored network \"%s\" in slot 0; restarting WiFi", ssid);
        wifi_start();
    } else {
        ESP_LOGE(TAG, "could not store credentials");
    }
}

static void network_status(void)
{
    char ssids[8][WIFI_SSID_LEN];
    ESP_LOGW(TAG, "connected=%d  source=%s  stale=%d  failures=%d  last_ok=%lld ms ago",
             wifi_is_connected(), flight_source_current_source_name(),
             flight_source_is_stale(), flight_source_consecutive_failures(),
             (long long)flight_source_last_success_age_ms());

    if (wifi_creds_list(ssids, 8) == ESP_OK) {
        for (int i = 0; i < 8; i++) {
            if (ssids[i][0]) ESP_LOGW(TAG, "  stored slot %d: %s", i, ssids[i]);
        }
    }
    int n = wifi_scan(ssids, 8);
    for (int i = 0; i < n; i++) {
        ESP_LOGW(TAG, "  in range: %s", ssids[i]);
    }
}

/* Reads the poller's snapshot and repaints. Owns no network state and does no
 * formatting — view_build turns raw API data into final German, this just moves
 * it onto the panel. All LVGL work happens here, behind the display lock. */
static void ui_task(void *arg)
{
    /* The heartbeat for the whole device. If rendering, the display mutex or the
     * snapshot copy ever wedges, this stops feeding and the watchdog reboots us
     * — which is strictly better than a panel frozen on a stale aircraft in a
     * room where nobody can tell the difference. */
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    static aircraft_t ac[MAX_AIRCRAFT];
    static route_t    rt[MAX_AIRCRAFT];
    static aircraft_t last_seen;
    static bool       have_last_seen = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_task_wdt_reset();
        if (s_fixture_mode) {
            continue;   /* a replayed screen stays up until dismissed */
        }

        int n = flight_source_snapshot(ac, MAX_AIRCRAFT, rt, MAX_AIRCRAFT);
        time_t raw = time(NULL);
        struct tm now;
        localtime_r(&raw, &now);

        view_model_t vm;
        if (n > 0) {
            last_seen = ac[0];
            have_last_seen = true;
            view_build(&ac[0], route_find(rt, n, ac[0].flight), &now, n,
                       !flight_source_is_stale(), &vm);
        } else {
            view_build_empty(&now, have_last_seen ? &last_seen : NULL,
                             !flight_source_is_stale(), &vm);
        }

        display_lock(0);
        screen_overhead_update(&vm);
        display_unlock();
    }
}

static void on_cmd(char c)
{
    if (c >= '1' && c <= '4') {
        s_fixture_mode = true;
        dbg_fixture_show(c - '0');
        return;
    }
    if (c == '0') {
        s_fixture_mode = false;      /* back to live data */
        ESP_LOGW(TAG, "fixture mode off; resuming live snapshots");
        return;
    }
    if (c == 'b') bench_suite();
    else if (c == 'm') dbg_metrics_hero();
    else if (c == 'w') provision_wifi();
    else if (c == 'n') network_status();
    else if (c == 'f') font_card();
}

void app_main(void)
{
    log_memory_budget("boot, before display init");

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    ESP_ERROR_CHECK(display_init());
    /* bsp_display_new() already ran brightness init; calling it again just
     * re-configures GPIO4 and logs an LEDC conflict. The BSP drives this
     * backlight active-low, so "flipped 0%" in the log means FULL brightness. */
    bsp_display_backlight_on();

    log_memory_budget("after display init (framebuffer up)");

    dbg_bench_init();
    dbg_screen_start(on_cmd);

    display_lock(0);
    screen_overhead_create(lv_screen_active());
    display_unlock();
    log_memory_budget("after screen built");

    /* Network last, and never fatal: the panel must come up and show something
     * even with no credentials stored, which is the state every new device is
     * in and the state he will be in when he lands in Thailand. */
    if (wifi_start() == ESP_OK) {
        /* Timezone follows the location preset — he never sets a clock. The
         * preset picker is M6; for now it is compiled in with the coordinates. */
        timesync_start(TZ_GLOGGNITZ);
        ESP_ERROR_CHECK(flight_source_start(HOME_LAT, HOME_LON, HOME_RADIUS_NM));
        log_memory_budget("after wifi + poller started");
    } else {
        ESP_LOGW(TAG, "no WiFi credentials stored — press 'w' to provision");
    }

    xTaskCreate(ui_task, "ui", 4096, NULL, 4, NULL);

    ESP_LOGW(TAG, "ready: s=shot f=fontcard b=bench m=metrics w=wifi n=net 1-4=fixture 0=live");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        log_memory_budget("steady state");
    }
}
