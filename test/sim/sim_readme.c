/* sim_readme.c — the pictures in the README, rendered from one real sky.
 *
 * The same harness as the other simulators (sim_common.h): real LVGL in the
 * device's DIRECT mode, the real screens, a 480x480 framebuffer. What is
 * different is the input. Nothing here is built by hand: the sky is the real
 * 30 nm capture over the Gloggnitz preset (test/fixtures, 2026-09-18 09:47,
 * the same one dbg_fixture.c replays on the panel), run through the device's
 * own parsers, and every screen is fed the same snapshot — so the Radar, the
 * Liste and the detail layer in the README show one moment, not three.
 *
 * What it does not draw: nav.c's page dots and the corner status meters, which
 * belong to the deck around these screens rather than to the screens.
 *
 *   make -C test/sim readme    renders, then writes docs/screens/ (compressed)
 *
 * It checks that the capture parsed and that each picture has ink where the
 * screen's own colour rules say it must; it is not a layout test. The layout
 * tests are sim_radar.c, sim_detail.c and sim_list.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"
#include "screen_radar.h"
#include "screen_list.h"
#include "screen_overhead.h"
#include "view_build.h"
#include "adsb_parse.h"
#include "route_parse.h"
#include "theme.h"
#include "flight_types.h"
#include "sim_common.h"

#define FIXTURES "../fixtures/"

/* The Gloggnitz preset's default, settings.c: ~55 km. */
#define RADIUS_NM 30

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    *len = fread(buf, 1, (size_t)n, f);
    buf[*len] = '\0';
    fclose(f);
    return buf;
}

/* 09:47 on Friday 18 September 2026 — when the capture was taken, as in
 * dbg_fixture.c, so the clock and the arrival estimate match the traffic. */
static const struct tm k_capture = { .tm_year = 126, .tm_mon = 8, .tm_mday = 18,
                                     .tm_hour = 9, .tm_min = 47, .tm_wday = 5 };

static void back(void) {}

static void shoot(lv_obj_t *scr, const char *png)
{
    lv_screen_load(scr);
    run_ms(400);
    lv_obj_invalidate(scr);
    render();
    png_write(png);
}

static int find_flight(const aircraft_t *ac, int n, const char *flight)
{
    for (int i = 0; i < n; i++)
        if (strcmp(ac[i].flight, flight) == 0) return i;
    return -1;
}

int main(int argc, char **argv)
{
    if (argc > 1) s_outdir = argv[1];

    size_t ac_len = 0, rt_len = 0;
    char *ac_json = slurp(FIXTURES "adsblol_gloggnitz_30nm.json", &ac_len);
    char *rt_json = slurp(FIXTURES "routeset_response.json", &rt_len);
    CHECK(ac_json != NULL && rt_json != NULL, "fixtures not found (run from test/sim)");
    if (ac_json == NULL || rt_json == NULL) return 1;

    static aircraft_t ac[MAX_AIRCRAFT];
    static route_t    rt[MAX_AIRCRAFT];
    int nac = adsb_parse(ac_json, ac_len, ac, MAX_AIRCRAFT);
    int nrt = route_parse(rt_json, rt_len, rt, MAX_AIRCRAFT);
    /* 13 in the feed; one is an MLAT ground beacon the parser drops (test_parse.c). */
    CHECK(nac == 12, "capture parsed to %d aircraft, want 12", nac);
    CHECK(nrt == 13, "routeset parsed to %d entries, want 13", nrt);
    if (nac <= 0 || nrt <= 0) return 1;

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(W, H);
    lv_display_set_buffers(d, s_fb, NULL, sizeof s_fb, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flush_cb);

    lv_obj_t *scr_radar  = lv_obj_create(NULL);
    lv_obj_t *scr_list   = lv_obj_create(NULL);
    lv_obj_t *scr_detail = lv_obj_create(NULL);
    lv_obj_t *all[] = { scr_radar, scr_list, scr_detail };
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_bg_color(all[i], THEME_GROUND, 0);
        lv_obj_set_scrollbar_mode(all[i], LV_SCROLLBAR_MODE_OFF);
    }

    char clock[8];
    snprintf(clock, sizeof clock, "%02d:%02d", k_capture.tm_hour, k_capture.tm_min);

    GROUP("radar: the capture, untouched");
    {
        screen_radar_create(scr_radar);
        screen_radar_set_net(NET_OK);
        screen_radar_set_has_data(true);
        screen_radar_set_clock(clock);
        screen_radar_update(ac, nac, rt, nrt, RADIUS_NM);
        shoot(scr_radar, "readme_radar");
        CHECK(find(THEME_MAGENTA, 0, 0, W - 1, H - 1).n > 20, "no nearest mark");
        CHECK(find(THEME_AMBER, 0, 0, W - 1, H - 1).n == 0, "amber on a live sky");
    }

    GROUP("liste: the same snapshot, nearest first");
    {
        screen_list_create(scr_list);
        screen_list_set_source(true, NET_OK);
        screen_list_update(ac, nac, rt, nrt);
        shoot(scr_list, "readme_liste");
        CHECK(find(THEME_TEXT_PRIMARY, 0, 60, W - 1, 380).n > 200, "no rows drawn");
    }

    GROUP("detail: a routed flight, and a light aircraft with no flight plan");
    {
        screen_overhead_create(scr_detail);
        screen_overhead_set_back_cb(back);      /* a layer, as main.c builds it */
        view_model_t vm;

        int i = find_flight(ac, nac, "AUA76PZ");
        CHECK(i >= 0, "AUA76PZ missing from the capture");
        if (i >= 0) {
            view_build_ex(&ac[i], route_find(rt, nrt, ac[i].flight), false,
                          &k_capture, nac, NET_OK, &vm);
            CHECK(vm.state == VIEW_OVERHEAD, "AUA76PZ is not routed");
            screen_overhead_update(&vm);
            shoot(scr_detail, "readme_detail");
            CHECK(find(THEME_WHITE, 0, 150, W - 1, 300).n > 500, "no destination headline");
        }

        /* A light aircraft with no flight plan: the case the README says is
         * not a failure — the ones he hears, low and slow. Not one of the
         * two DV20s: "Diamond DV20 Katana" takes two lines at hero size and
         * the reason sentence gives way to it (D79), which is correct and
         * says less. */
        int j = find_flight(ac, nac, "OEVSO");
        CHECK(j >= 0, "OEVSO missing from the capture");
        if (j < 0) return 1;
        view_build_ex(&ac[j], route_find(rt, nrt, ac[j].flight), false,
                      &k_capture, nac, NET_OK, &vm);
        CHECK(vm.state == VIEW_NO_ROUTE, "OEVSO has a route");
        screen_overhead_update(&vm);
        shoot(scr_detail, "readme_no_route");
    }

    free(ac_json);
    free(rt_json);
    printf("\n  %d checks, %d failed\n", t_run, t_fail);
    return t_fail ? 1 : 0;
}
