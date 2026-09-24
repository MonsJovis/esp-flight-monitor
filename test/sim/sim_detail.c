/* sim_detail.c — the detail layer (screen_overhead.c) on the host (D79).
 *
 * The same harness as sim_radar.c (sim_common.h): real LVGL in the device's
 * DIRECT mode, the real screen, a 480x480 framebuffer, checks from pixels.
 *
 * What it guards: the lines between the destination and the data band.
 * D79 added two of them (distance from the origin, the arrival estimate) and
 * replaced a give-way rule that DROPPED lines without closing up the space
 * they left — a private DV20 with a two-line type name lost its registration
 * along with its reason sentence and showed ~70 px of nothing. Every aircraft
 * below is real — the 15:14 capture over the Vienna preset on 2026-09-24,
 * routes from adsb.im's routeset — except the one built to overfill the
 * screen, which says so.
 *
 * How it reads the screen: the destination (hero) is THEME_WHITE, the
 * supporting lines are THEME_TEXT_PRIMARY, the data band is THEME_CYAN. So
 * "lines between hero and band" is a count of TEXT_PRIMARY row-groups
 * between the last white row and the first cyan row under it.
 */
#include <time.h>

#include "lvgl.h"
#include "screen_overhead.h"
#include "view_build.h"
#include "theme.h"
#include "flight_types.h"
#include "sim_common.h"

static void back(void) {}

#define CHECK_ROUTE_LINE(vm, want) \
    CHECK(strcmp((vm).route_line, (want)) == 0, "route line \"%s\", want \"%s\"", (vm).route_line, (want))

static aircraft_t mk(const char *hex, const char *flight, const char *reg, const char *type,
                     const char *cat, int32_t alt_ft, int gs, float track, float dst,
                     float dir, double lat, double lon)
{
    aircraft_t a;
    memset(&a, 0, sizeof a);
    snprintf(a.hex, sizeof a.hex, "%s", hex);
    snprintf(a.flight, sizeof a.flight, "%s", flight);
    snprintf(a.reg, sizeof a.reg, "%s", reg);
    snprintf(a.type, sizeof a.type, "%s", type);
    snprintf(a.category, sizeof a.category, "%s", cat);
    a.alt_ft = alt_ft; a.gs_kt = gs; a.track_deg = track; a.has_track = true;
    a.dst_nm = dst; a.dir_deg = dir; a.lat = lat; a.lon = lon;
    return a;
}

static route_t mkroute(const char *cs, const char *orig, const char *dest,
                       float olat, float olon, float dlat, float dlon)
{
    route_t r;
    memset(&r, 0, sizeof r);
    snprintf(r.callsign, sizeof r.callsign, "%s", cs);
    snprintf(r.airline_code, sizeof r.airline_code, "AUA");
    snprintf(r.orig_icao, sizeof r.orig_icao, "%s", orig);
    snprintf(r.dest_icao, sizeof r.dest_icao, "%s", dest);
    r.orig_lat = olat; r.orig_lon = olon; r.dest_lat = dlat; r.dest_lon = dlon;
    r.resolved = r.plausible = r.has_coords = true;
    return r;
}

static const struct tm k_now = { .tm_year = 126, .tm_mon = 8, .tm_mday = 24,
                                 .tm_hour = 15, .tm_min = 14, .tm_wday = 4 };

static void show(const view_model_t *vm, const char *png)
{
    screen_overhead_update(vm);
    run_ms(200);
    lv_obj_invalidate(lv_screen_active());
    render();
    png_write(png);
}

/* ---- reading the layout back out of the pixels -------------------------- */

typedef struct {
    int hero_bottom;       /* last row with THEME_WHITE (the destination)    */
    int band_top;          /* first row under the hero with THEME_CYAN       */
    int lines;             /* TEXT_PRIMARY row-groups between the two         */
    int max_gap;           /* largest empty run between two of those groups   */
    int primary_in_band;   /* TEXT_PRIMARY pixels at or below band_top        */
} layout_t;

static bool row_has(uint16_t c, int y, int min)
{
    int n = 0;
    for (int x = 0; x < W; x++) n += (s_fb[y * W + x] == c);
    return n >= min;
}

static layout_t read_layout(void)
{
    layout_t L = { -1, -1, 0, 0, 0 };
    uint16_t white = to565(THEME_WHITE), cyan = to565(THEME_CYAN), prim = to565(THEME_TEXT_PRIMARY);
    for (int y = 100; y < H; y++) if (row_has(white, y, 2)) L.hero_bottom = y;
    for (int y = L.hero_bottom + 1; y < H && L.band_top < 0; y++) if (row_has(cyan, y, 2)) L.band_top = y;
    if (L.hero_bottom < 0 || L.band_top < 0) return L;
    /* Group rows: a text line has ascender-to-descender rows with exact-colour
     * glyph cores, and gaps under 6 rows are inside one line. */
    int in = 0, last_end = -1, gap = 0;
    for (int y = L.hero_bottom + 1; y < L.band_top; y++) {
        bool has = row_has(prim, y, 2);
        if (has) {
            if (!in) {
                if (last_end >= 0 && y - last_end - 1 >= 6) {
                    L.lines++;
                    if (y - last_end - 1 > L.max_gap) L.max_gap = y - last_end - 1;
                } else if (last_end < 0) {
                    L.lines++;
                }
                in = 1;
            }
            last_end = y;
        } else if (in && y - last_end >= 6) {
            in = 0;
        }
    }
    (void)gap;
    for (int y = L.band_top; y < H; y++)
        for (int x = 0; x < W; x++) L.primary_in_band += (s_fb[y * W + x] == prim);
    return L;
}

/* Cyan clusters on the altitude row (the band's first 40 rows), split where
 * the ink pauses for 28 px or more: "739 m" alone is one cluster (the space
 * inside it is ~22 px), altitude + speed are two. */
static int altitude_row_clusters(int band_top)
{
    uint16_t cyan = to565(THEME_CYAN);
    int clusters = 0, last = -1000;
    for (int x = 0; x < W; x++) {
        bool col = false;
        for (int y = band_top; y < band_top + 40 && y < H; y++) col |= (s_fb[y * W + x] == cyan);
        if (col) {
            if (x - last >= 28) clusters++;
            last = x;
        }
    }
    return clusters;
}

int main(int argc, char **argv)
{
    if (argc > 1) s_outdir = argv[1];

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(W, H);
    lv_display_set_buffers(d, s_fb, NULL, sizeof s_fb, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flush_cb);
    lv_obj_set_style_bg_color(lv_screen_active(), THEME_GROUND, 0);
    screen_overhead_create(lv_screen_active());
    screen_overhead_set_back_cb(back);          /* a layer, as main.c builds it */

    view_model_t vm;

    /* ------------------------------------------------------------------ */
    GROUP("arriving (AUA110 Klagenfurt -> Wien, 2,325 ft): the arrival, and the identity");
    {
        aircraft_t a = mk("440823", "AUA110", "OE-LWG", "E195", "A3", 2325, 158, 344.55f,
                          17.8f, 133.2f, 47.994892, 16.630765);
        route_t r = mkroute("AUA110", "LOWK", "LOWW", 46.642502f, 14.3377f, 48.110298f, 16.5697f);
        view_build_ex(&a, &r, false, &k_now, 32, NET_OK, &vm);
        CHECK(strstr(vm.departed, "km von Klagenfurt entfernt") != NULL, "departed = \"%s\"", vm.departed);
        CHECK(strncmp(vm.arrival, "Landung in", 10) == 0, "arrival = \"%s\"", vm.arrival);
        show(&vm, "detail_01_arriving");
        layout_t L = read_layout();
        CHECK(L.band_top > 0, "no data band found");
        /* Two lines: the route line (the arrival — it wins over the origin
         * distance when there is one) and the identity. */
        CHECK_ROUTE_LINE(vm, vm.arrival);
        CHECK(L.lines == 2, "%d lines between destination and band, want 2", L.lines);
        CHECK(L.primary_in_band == 0, "%d supporting-text px inside the band", L.primary_in_band);
        CHECK(altitude_row_clusters(L.band_top) == 2, "speed not beside the altitude (%d clusters)",
              altitude_row_clusters(L.band_top));
    }

    /* ------------------------------------------------------------------ */
    GROUP("climbing out (AUA1Y Wien -> Frankfurt, 2,425 ft): no estimate, and 6 km is not worth saying");
    {
        aircraft_t a = mk("440c8d", "AUA1Y", "OE-LBB", "A321", "A3", 2425, 183, 255.43f,
                          8.61f, 123.6f, 48.118674, 16.486535);
        route_t r = mkroute("AUA1Y", "LOWW", "EDDF", 48.110298f, 16.5697f, 50.026402f, 8.54313f);
        view_build_ex(&a, &r, false, &k_now, 32, NET_OK, &vm);
        CHECK(vm.arrival[0] == '\0', "an estimate while climbing out: \"%s\"", vm.arrival);
        CHECK(vm.departed[0] == '\0', "a distance under 10 km: \"%s\"", vm.departed);
        show(&vm, "detail_02_climbing_out");
        layout_t L = read_layout();
        CHECK(L.lines == 1, "%d lines, want 1 (the identity)", L.lines);
        CHECK(altitude_row_clusters(L.band_top) == 2, "speed missing beside the altitude");
    }

    /* ------------------------------------------------------------------ */
    GROUP("climbing out, further along (CONSTRUCTED from AUA1Y: 9,000 ft, ~45 km west of Schwechat)");
    {
        /* AUA1Y's own route and type, moved on along its departure to where
         * the origin distance is worth saying. Position invented. */
        aircraft_t a = mk("440c8d", "AUA1Y", "OE-LBB", "A321", "A3", 9000, 290, 280.0f,
                          25.0f, 280.0f, 48.2, 16.0);
        route_t r = mkroute("AUA1Y", "LOWW", "EDDF", 48.110298f, 16.5697f, 50.026402f, 8.54313f);
        view_build_ex(&a, &r, false, &k_now, 32, NET_OK, &vm);
        CHECK(vm.arrival[0] == '\0', "an estimate while climbing out: \"%s\"", vm.arrival);
        CHECK_ROUTE_LINE(vm, vm.departed);
        CHECK(strstr(vm.route_line, "km von Wien entfernt") != NULL, "route line \"%s\"", vm.route_line);
        show(&vm, "detail_02b_climbing_further");
        layout_t L = read_layout();
        CHECK(L.lines == 2, "%d lines, want 2 (distance from Wien, identity)", L.lines);
    }

    /* ------------------------------------------------------------------ */
    GROUP("no route (OE-AHM, DV20): the registration survives a two-line hero");
    {
        aircraft_t a = mk("4404e3", "OEAHM", "OE-AHM", "DV20", "A1", 1800, 131, 74.48f,
                          23.49f, 281.2f, 48.273376, 15.729675);
        view_build_ex(&a, NULL, false, &k_now, 32, NET_OK, &vm);
        show(&vm, "detail_03_no_route");
        layout_t L = read_layout();
        /* Before D79 this was 0: the reason did not fit, was hidden, and the
         * identity under it was hidden too instead of moving up. */
        CHECK(L.lines >= 1, "no line at all between the type and the band — the registration is gone");
        CHECK(L.primary_in_band == 0, "%d supporting-text px inside the band", L.primary_in_band);
    }

    /* ------------------------------------------------------------------ */
    GROUP("overfull (CONSTRUCTED: Wien -> Santiago de Compostela at cruise): no holes, no overlap");
    {
        /* Built to have every line at once under a long destination. The
         * position is invented; the airports are real. */
        aircraft_t a = mk("abcdef", "AUA7", "OE-LBC", "A20N", "A3", 37000, 460, 250.0f,
                          25.0f, 250.0f, 47.0, 10.0);
        route_t r = mkroute("AUA7", "LOWW", "LEST", 48.110298f, 16.5697f, 42.896301f, -8.415140f);
        view_build_ex(&a, &r, false, &k_now, 32, NET_OK, &vm);
        CHECK(vm.route_line[0] && vm.identity[0], "precondition: both lines built");
        show(&vm, "detail_04_overfull");
        layout_t L = read_layout();
        CHECK(L.lines >= 1, "everything dropped");
        CHECK(L.primary_in_band == 0, "%d supporting-text px inside the band", L.primary_in_band);
        /* The old failure's signature: a line dropped and the next one left
         * where it was, with a hole above it. Lines here are GAP_SM (8 px)
         * apart plus the font's own padding. */
        CHECK(L.max_gap <= 22, "a %d px hole between two supporting lines", L.max_gap);
    }

    /* ------------------------------------------------------------------ */
    GROUP("empty sky with a last-seen aircraft: its altitude, but never a stale speed");
    {
        aircraft_t a = mk("440c8d", "AUA1Y", "OE-LBB", "A321", "A3", 2425, 183, 255.43f,
                          8.61f, 123.6f, 48.118674, 16.486535);
        view_build_empty(&k_now, &a, NET_OK, &vm);
        show(&vm, "detail_05_empty_sky");
        uint16_t cyan = to565(THEME_CYAN);
        int band_top = -1;
        for (int y = 200; y < H && band_top < 0; y++) if (row_has(cyan, y, 2)) band_top = y;
        CHECK(band_top > 0, "no altitude row on the empty sky");
        if (band_top > 0)
            CHECK(altitude_row_clusters(band_top) == 1, "a speed on the empty sky (%d clusters)",
                  altitude_row_clusters(band_top));
    }

    printf("\n  %d checks, %d failed\n", t_run, t_fail);
    return t_fail ? 1 : 0;
}
