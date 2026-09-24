/* sim_radar.c — the radar screen, run on the host, touched by a script.
 *
 * WHY THIS EXISTS. Everything D75 and D76 changed on the radar is either
 * drawing or touch routing, and neither can be reached by test/host: the
 * logic behind it is tested there (test_radar.c), but "does the ring land on
 * the mark he pressed", "does a long press on the empty scope still reach
 * Einstellungen now that the scope owns a click", and "is there any amber on
 * the screen while the data is live" are questions about LVGL doing what we
 * think it does. D59 records that the radar's taps "could not be verified from
 * here" and needed a finger. This is the finger.
 *
 * WHAT IT IS. Real LVGL 9.6 compiled for the host, the real screen_radar.c,
 * a 480x480 RGB565 framebuffer, a pointer device fed from a script, and a
 * clock that only moves when the script says so — so every run is identical.
 * The screen goes inside a real lv_tileview, exactly as nav.c builds the deck,
 * because touch routing (bubbling, scroll-steals-the-press) depends on that
 * parent chain and a bare screen would test something the device never runs.
 *
 * HOW IT CHECKS. From pixels, never from the screen's private state:
 *   - pure white is drawn by exactly one thing, the selection ring, so the
 *     centroid of white pixels on the scope IS where the ring is;
 *   - exact THEME_MAGENTA only appears on the nearest aircraft's mark;
 *   - exact THEME_AMBER only appears in the stale tag.
 * If any of those stops being true the tests that lean on it fail loudly
 * rather than pass quietly — each centroid check also asserts it found pixels.
 *
 * Screenshots of every step are written as PNG to the directory given as
 * argv[1], for a human to look at. They are not compared against anything.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "screen_radar.h"
#include "theme.h"
#include "view_model.h"
#include "flight_types.h"
#include "fonts.h"
#include "fmt_de.h"

#define W 480
#define H 480

/* ---- harness ----------------------------------------------------------- */

static int t_run, t_fail;
#define CHECK(cond, ...) do {                                              \
    t_run++;                                                               \
    if (!(cond)) {                                                         \
        t_fail++;                                                          \
        printf("    FAIL  %s:%d  %s — ", __FILE__, __LINE__, #cond);       \
        printf(__VA_ARGS__);                                               \
        printf("\n");                                                      \
    }                                                                      \
} while (0)
#define GROUP(name) printf("\n  %s\n", (name))

static uint16_t    s_fb[W * H];
static uint32_t    s_now_ms;
static int32_t     s_px, s_py;
static bool        s_down;
static const char *s_outdir = ".";

static uint32_t tick_cb(void) { return s_now_ms; }

/* DIRECT mode, rendering straight into s_fb — the device's mode
 * (CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=y, two framebuffers kept in sync by
 * esp_lvgl_port). Only INVALIDATED areas are redrawn and everything else
 * keeps last frame's pixels, which is the whole point: a first version used
 * FULL mode, repainted every pixel every frame, and so could not see a
 * forgotten lv_obj_invalidate() — a mutant that removed the trail layer's
 * invalidate survived it. Two synced buffers behave as one for this purpose. */
static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    (void)a; (void)px;
    lv_display_flush_ready(d);
}

static void read_cb(lv_indev_t *i, lv_indev_data_t *d)
{
    (void)i;
    d->point.x = s_px;
    d->point.y = s_py;
    d->state   = s_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* Advances the clock in 5 ms steps, running LVGL at each. */
static void run_ms(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 5) {
        s_now_ms += 5;
        lv_timer_handler();
    }
}

static void touch_down(int32_t x, int32_t y) { s_px = x; s_py = y; s_down = true;  run_ms(60); }
static void touch_up(void)                    {                      s_down = false; run_ms(60); }
static void tap(int32_t x, int32_t y)         { touch_down(x, y); run_ms(40); touch_up(); }
static void render(void)                      { lv_refr_now(NULL); }

/* Deterministic LCG for the stress run. A function, not a macro: two calls
 * in one expression as a macro modify the seed unsequenced, which is
 * undefined behaviour — the compiler said so, and it was right. */
static uint32_t s_seed;
static int rnd(int n)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return (int)((s_seed >> 8) % (uint32_t)n);
}

/* ---- pixels ------------------------------------------------------------ */

static uint16_t to565(lv_color_t c)
{
    return (uint16_t)(((c.red >> 3) << 11) | ((c.green >> 2) << 5) | (c.blue >> 3));
}

typedef struct { int n; double cx, cy; } blob_t;

/* Every pixel inside the box that is EXACTLY `c`. */
static blob_t find(lv_color_t c, int x0, int y0, int x1, int y1)
{
    uint16_t want = to565(c);
    blob_t   b    = { 0, 0, 0 };
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (s_fb[y * W + x] == want) {
                b.n++;
                b.cx += x;
                b.cy += y;
            }
        }
    }
    if (b.n > 0) {
        b.cx /= b.n;
        b.cy /= b.n;
    }
    return b;
}

#define R_OUTER_PX 140
#define RADIUS_NM_ 30

/* A point inside the rings that no aircraft's touch target reaches (each
 * mark is 34 px + 11 px of pad a side = 28 px from its centre): checked
 * against the whole sky below in "the empty point is empty". */
#define EMPTY_X 330
#define EMPTY_Y 180

/* Everything a ring can reach: an aircraft clamped to the outer ring
 * (r = 140) plus the largest selection ring (r = 20) plus a pixel. The first
 * version stopped at 150, cut the outer arc off rings around out-of-range
 * aircraft, and dragged their centroid 3-5 px inward — which the stress run
 * reported as "a ring on no aircraft". Still clear of the caption band (416+)
 * and of anything else drawn in pure white or exact magenta. */
#define SCOPE_X0 (240 - 162)
#define SCOPE_Y0 (240 - 162)
#define SCOPE_X1 (240 + 162)
#define SCOPE_Y1 (240 + 162)

static blob_t ring(void)    { return find(THEME_WHITE,   SCOPE_X0, SCOPE_Y0, SCOPE_X1, SCOPE_Y1); }
static blob_t magenta(void) { return find(THEME_MAGENTA, SCOPE_X0, SCOPE_Y0, SCOPE_X1, SCOPE_Y1); }

/* How far the white ring's pixels reach from its own centre — its radius. */
static double ring_radius(blob_t r)
{
    uint16_t want = to565(THEME_WHITE);
    double   max  = 0;
    for (int y = SCOPE_Y0; y <= SCOPE_Y1; y++) {
        for (int x = SCOPE_X0; x <= SCOPE_X1; x++) {
            if (s_fb[y * W + x] == want) {
                double d = hypot(x - r.cx, y - r.cy);
                if (d > max) max = d;
            }
        }
    }
    return max;
}

/* Pixels in a disc that are not the ground colour — "is anything drawn here". */
static int ink_near(double cx, double cy, double r)
{
    uint16_t ground = to565(THEME_GROUND);
    int      n      = 0;
    for (int y = (int)(cy - r); y <= (int)(cy + r); y++) {
        for (int x = (int)(cx - r); x <= (int)(cx + r); x++) {
            if (x < 0 || y < 0 || x >= W || y >= H) continue;
            if (hypot(x - cx, y - cy) > r) continue;
            if (s_fb[y * W + x] != ground) n++;
        }
    }
    return n;
}

/* Pixels in a disc where the colour leans magenta or cyan — used for trail
 * dots, which are blended and so never an exact token. */
static int tinted_near(double cx, double cy, double r, bool want_magenta)
{
    int n = 0;
    for (int y = (int)(cy - r); y <= (int)(cy + r); y++) {
        for (int x = (int)(cx - r); x <= (int)(cx + r); x++) {
            if (x < 0 || y < 0 || x >= W || y >= H) continue;
            if (hypot(x - cx, y - cy) > r) continue;
            uint16_t p  = s_fb[y * W + x];
            int      R  = ((p >> 11) & 31) << 3, G = ((p >> 5) & 63) << 2, B = (p & 31) << 3;
            /* Strict enough that the range rings' dim blue-grey hairlines
             * (THEME_HAIRLINE*) never count — only a trail dot at roughly
             * 40 % opacity or more, or a mark. */
            bool     mg = R > 90 && R > G + 40 && B > G + 40;
            bool     cy_ = G > 90 && B > 100 && B > R + 40 && G > R + 40;
            if (want_magenta ? mg : cy_) n++;
        }
    }
    return n;
}

/* Tinted pixels along C's orbit (r for 20 nm) between two bearings. */
static int tinted_near(double cx, double cy, double r, bool want_magenta);
static int arc_tint(double from_deg, double to_deg, bool magenta_)
{
    int n = 0;
    double r = R_OUTER_PX * (20.0 / RADIUS_NM_);
    for (double b = from_deg; b <= to_deg; b += 1.0) {
        double rad = b * M_PI / 180.0;
        n += tinted_near(240 + r * sin(rad), 240 - r * cos(rad), 3, magenta_);
    }
    return n;
}

/* Pixels of the two dim ring hairlines in a disc — subtracted from "ink" so
 * a check for "nothing drawn here" is not defeated by the range ring the
 * orbit happens to lie on. */
static int ring_ink(double cx, double cy, double r)
{
    uint16_t h1 = to565(THEME_HAIRLINE), h2 = to565(THEME_HAIRLINE_DIM), g = to565(THEME_GROUND);
    int      n  = 0;
    for (int y = (int)(cy - r); y <= (int)(cy + r); y++) {
        for (int x = (int)(cx - r); x <= (int)(cx + r); x++) {
            if (x < 0 || y < 0 || x >= W || y >= H || hypot(x - cx, y - cy) > r) continue;
            uint16_t p = s_fb[y * W + x];
            /* the hairlines, and their anti-aliased blends into the ground */
            int R = ((p >> 11) & 31) << 3, G = ((p >> 5) & 63) << 2, B = (p & 31) << 3;
            if (p != g && (p == h1 || p == h2 || (R < 30 && G < 60 && B < 72))) n++;
        }
    }
    return n;
}

/* Rendered width, unwrapped — the same call screen_radar.c's fit uses. */
static int text_w(const char *txt, const lv_font_t *f)
{
    lv_point_t p;
    lv_text_get_size(&p, txt, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return (int)p.x;
}

static void put32(FILE *f, uint32_t v)
{
    uint8_t q[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    fwrite(q, 1, 4, f);
}

static void png_chunk(FILE *f, const uint32_t *crc_tab, const char *type,
                      const uint8_t *data, size_t len)
{
    put32(f, (uint32_t)len);
    fwrite(type, 1, 4, f);
    if (len > 0) fwrite(data, 1, len, f);
    uint32_t c = 0xFFFFFFFFu;
    for (int k = 0; k < 4; k++) c = crc_tab[(c ^ (uint8_t)type[k]) & 0xFF] ^ (c >> 8);
    for (size_t k = 0; k < len; k++) c = crc_tab[(c ^ data[k]) & 0xFF] ^ (c >> 8);
    put32(f, c ^ 0xFFFFFFFFu);
}

static void png_write(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.png", s_outdir, name);
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        printf("    (could not write %s)\n", path);
        return;
    }
    /* Uncompressed PNG: zlib "stored" blocks. Big files, zero dependencies. */
    static uint32_t crc_tab[256];
    if (crc_tab[1] == 0) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_tab[n] = c;
        }
    }
    size_t   row = 1 + W * 3, raw_len = row * H;
    uint8_t *raw = malloc(raw_len);
    for (int y = 0; y < H; y++) {
        raw[y * row] = 0;
        for (int x = 0; x < W; x++) {
            uint16_t p = s_fb[y * W + x];
            uint8_t *o = &raw[y * row + 1 + x * 3];
            o[0] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
            o[1] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
            o[2] = (uint8_t)((p & 31) * 255 / 31);
        }
    }
    size_t   nblk = (raw_len + 65534) / 65535, z_len = 2 + raw_len + nblk * 5 + 4;
    uint8_t *z = malloc(z_len), *zp = z;
    *zp++ = 0x78; *zp++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw_len; off += 65535) {
        size_t n = raw_len - off < 65535 ? raw_len - off : 65535;
        *zp++ = (off + n == raw_len) ? 1 : 0;
        *zp++ = (uint8_t)n; *zp++ = (uint8_t)(n >> 8);
        *zp++ = (uint8_t)~n; *zp++ = (uint8_t)(~n >> 8);
        memcpy(zp, raw + off, n); zp += n;
    }
    for (size_t i = 0; i < raw_len; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    uint32_t ad = (b << 16) | a;
    *zp++ = (uint8_t)(ad >> 24); *zp++ = (uint8_t)(ad >> 16); *zp++ = (uint8_t)(ad >> 8); *zp++ = (uint8_t)ad;

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13] = { 0, 0, W >> 8, W & 255, 0, 0, H >> 8, H & 255, 8, 2, 0, 0, 0 };
    png_chunk(f, crc_tab, "IHDR", ihdr, 13);
    png_chunk(f, crc_tab, "IDAT", z, (size_t)(zp - z));
    png_chunk(f, crc_tab, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}

/* ---- the sky ------------------------------------------------------------ */

#define RADIUS_NM 30
#define R_OUTER   140     /* RADAR_R_OUTER in screen_radar.c */

enum { A, B, C, D, E, F, N_AC };

static aircraft_t s_ac[N_AC];
static route_t    s_rt[N_AC];
static int        s_n;

static void put(int i, const char *hex, const char *flight, const char *type, const char *reg,
                float dst, float dir, bool track, float trk, int32_t alt, int gs)
{
    aircraft_t *a = &s_ac[i];
    memset(a, 0, sizeof *a);
    snprintf(a->hex, sizeof a->hex, "%s", hex);
    snprintf(a->flight, sizeof a->flight, "%s", flight);
    snprintf(a->type, sizeof a->type, "%s", type);
    snprintf(a->reg, sizeof a->reg, "%s", reg);
    a->dst_nm = dst; a->dir_deg = dir; a->has_track = track; a->track_deg = trk;
    a->alt_ft = alt; a->gs_kt = gs;
}

static void routed(int i, const char *from, const char *to)
{
    route_t *r = &s_rt[i];
    memset(r, 0, sizeof *r);
    snprintf(r->callsign, sizeof r->callsign, "%s", s_ac[i].flight);
    snprintf(r->orig_icao, sizeof r->orig_icao, "%s", from);
    snprintf(r->dest_icao, sizeof r->dest_icao, "%s", to);
    r->resolved = r->plausible = true;
}

static void sky(void)
{
    memset(s_rt, 0, sizeof s_rt);
    /*  i   hex       flight    type    reg        dst    dir   trk?  trk   alt     gs */
    put(A, "a00001", "AUA123", "A320", "OE-LBA",   6.0f,  45, true,  90,  3000, 250);
    put(B, "b00002", "",       "C172", "OE-KAB",  12.0f, 200, true, 300,  2500, 100);
    put(C, "c00003", "DLH4",   "A320", "D-AIPA",  20.0f, 300, true,  10, 36000, 450);
    put(D, "d00004", "",       "",     "OE-9515", 15.0f, 120, false,  0, ALT_UNKNOWN, -1);
    put(E, "e00005", "RYR1",   "B38M", "9H-QAA",  18.0f, 250, true, 180, 36000, 440);
    put(F, "f00006", "AUA9",   "A320", "OE-LBB",  24.0f, 350, true, 180,  2000, 160);
    routed(A, "LOWW", "LKPR");
    routed(C, "LOWW", "EDDF");
    routed(E, "LOWW", "LEPA");
    routed(F, "LOWW", "LOWG");
    s_n = N_AC;
}

/* Where the screen puts aircraft i — screen_radar.c's bearing_to_xy(). */
static void where(int i, double *x, double *y)
{
    double r = R_OUTER * (s_ac[i].dst_nm / RADIUS_NM);
    if (r > R_OUTER) r = R_OUTER;
    double b = s_ac[i].dir_deg * M_PI / 180.0;
    *x = 240 + r * sin(b);
    *y = 240 - r * cos(b);
}

static void update(void)
{
    screen_radar_update(s_ac, s_n, s_rt, s_n, RADIUS_NM);
    render();
}

/* ---- what the rest of the device would be doing ------------------------ */

static int  s_tile_longpress;
static char s_selected[16];

static void on_tile_longpress(lv_event_t *e) { (void)e; s_tile_longpress++; }
static void on_select(const char *hex)       { snprintf(s_selected, sizeof s_selected, "%s", hex); }

/* nav.c's bubble_decorative(), verbatim in intent: anything with no callback
 * of its own passes presses up. Reproduced rather than linked because nav.c
 * pulls in the rest of the device. If nav.c's rule changes, change this. */
static void bubble_decorative(lv_obj_t *parent)
{
    uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        if (lv_obj_get_event_count(child) != 0) continue;
        lv_obj_set_event_bubble(child, true);
        bubble_decorative(child);
    }
}

/* ======================================================================== */

int main(int argc, char **argv)
{
    if (argc > 1) s_outdir = argv[1];

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(W, H);
    lv_display_set_buffers(disp, s_fb, NULL, sizeof s_fb, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);

    /* The deck, as nav.c builds it: a tileview, two tiles, the long-press
     * handlers on the tile. */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, THEME_GROUND, 0);
    lv_obj_t *tv  = lv_tileview_create(scr);
    lv_obj_set_style_bg_color(tv, THEME_GROUND, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);   /* as nav.c */
    lv_obj_t *t0  = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    lv_obj_t *t1  = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT);
    (void)t1;
    lv_obj_set_scrollbar_mode(t0, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollbar_mode(t1, LV_SCROLLBAR_MODE_OFF);
    screen_radar_create(t0);
    screen_radar_set_select_cb(on_select);
    bubble_decorative(t0);
    lv_obj_add_event_cb(t0, on_tile_longpress, LV_EVENT_LONG_PRESSED, NULL);

    sky();
    screen_radar_set_net(NET_OK);
    screen_radar_set_clock("10:39");
    update();
    run_ms(100);
    render();
    png_write("01_rest");

    double ax, ay, bx, by, cx_, cy_, dx, dy, fx, fy;
    where(A, &ax, &ay); where(B, &bx, &by); where(C, &cx_, &cy_);
    where(D, &dx, &dy); where(F, &fx, &fy);

    /* ------------------------------------------------------------------ */
    GROUP("the empty point is empty");
    {
        for (int i = 0; i < N_AC; i++) {
            double x, y; where(i, &x, &y);
            CHECK(fabs(x - EMPTY_X) > 30 || fabs(y - EMPTY_Y) > 30,
                  "aircraft %d at (%.0f,%.0f) covers the empty point", i, x, y);
        }
    }

    /* The magenta tolerance is 7 px, not 2 like the ring's: the ring is a
     * circle centred on the mark, but a triangle's pixel centroid sits a
     * third of the way from its base, not on the point it rotates about —
     * 4-5 px off for the largest band. The ring check is the tight one. */
    GROUP("at rest: the ring sits on the nearest, which is the magenta one");
    {
        blob_t m = magenta(), r = ring();
        CHECK(m.n > 20, "magenta pixels: %d", m.n);
        CHECK(r.n > 20, "white (ring) pixels: %d", r.n);
        CHECK(hypot(m.cx - ax, m.cy - ay) < 7, "magenta at (%.0f,%.0f), A at (%.0f,%.0f)", m.cx, m.cy, ax, ay);
        CHECK(hypot(r.cx - ax, r.cy - ay) < 2, "ring at (%.1f,%.1f), A at (%.1f,%.1f)", r.cx, r.cy, ax, ay);
    }

    GROUP("the range read-out is top left, level with the clock");
    {
        blob_t km = find(THEME_TEXT_LABEL, 0, 0, 160, 60);
        blob_t clk = find(THEME_TEXT_LABEL, 320, 0, W - 1, 60);
        CHECK(km.n > 20, "no range read-out top left: %d px", km.n);
        CHECK(clk.n > 20, "no clock top right: %d px", clk.n);
        CHECK(fabs(km.cy - clk.cy) < 3, "range at y=%.1f, clock at y=%.1f — not level", km.cy, clk.cy);
    }

    GROUP("no amber anywhere while the data is live (D76: amber means stale)");
    {
        blob_t am = find(THEME_AMBER, 0, 0, W - 1, H - 1);
        CHECK(am.n == 0, "amber pixels on a live screen: %d", am.n);
    }

    GROUP("the route-less Cessna is drawn cyan now, not amber");
    {
        CHECK(tinted_near(bx, by, 12, false) > 10, "cyan-tinted pixels at B: %d", tinted_near(bx, by, 12, false));
    }

    GROUP("altitude is size: a low jet is drawn bigger than a cruising one");
    {
        /* F (2 000 ft) and C (36 000 ft) are both routed, both filled cyan. */
        int low  = find(THEME_CYAN, (int)fx - 18, (int)fy - 18, (int)fx + 18, (int)fy + 18).n;
        int high = find(THEME_CYAN, (int)cx_ - 18, (int)cy_ - 18, (int)cx_ + 18, (int)cy_ + 18).n;
        CHECK(high > 10, "cruise mark has pixels: %d", high);
        CHECK(low > 2 * high, "low mark %d px vs cruise mark %d px — want a clear step", low, high);
    }

    GROUP("the ring is sized to the mark it circles");
    {
        double r_low = ring_radius(ring());   /* on A, 3 000 ft: large */
        tap((int)cx_, (int)cy_);              /* C, 36 000 ft: small  */
        render();
        double r_high = ring_radius(ring());
        CHECK(r_low > r_high + 3, "ring radius low %.1f vs high %.1f", r_low, r_high);
        CHECK(r_high >= 12, "cruise ring still clears its mark: r=%.1f", r_high);
        tap(EMPTY_X, EMPTY_Y);                /* let go again (tested below) */
        render();
    }

    /* ------------------------------------------------------------------ */
    GROUP("finger DOWN on a mark: the ring is already there before it lifts");
    {
        touch_down((int)bx, (int)by);
        render();
        png_write("02_pressing_B");
        blob_t r = ring();
        CHECK(r.n > 20 && hypot(r.cx - bx, r.cy - by) < 2,
              "ring at (%.1f,%.1f) while pressing B at (%.1f,%.1f)", r.cx, r.cy, bx, by);
        touch_up();
        render();
        png_write("03_tapped_B");
        r = ring();
        CHECK(hypot(r.cx - bx, r.cy - by) < 2, "ring stays on B after the tap: (%.1f,%.1f)", r.cx, r.cy);
        blob_t m = magenta();
        CHECK(hypot(m.cx - ax, m.cy - ay) < 7, "magenta did NOT follow the tap: (%.0f,%.0f)", m.cx, m.cy);
    }

    GROUP("the caption answers for the ringed aircraft, not the nearest");
    {
        s_selected[0] = '\0';
        tap(240, 440);
        CHECK(strcmp(s_selected, "b00002") == 0, "caption tap opened \"%s\", want b00002", s_selected);
    }

    GROUP("a swipe that starts on a mark does not move the selection");
    {
        touch_down((int)dx, (int)dy);
        render();
        blob_t r = ring();
        CHECK(hypot(r.cx - dx, r.cy - dy) < 2, "preview on D while pressing: (%.1f,%.1f)", r.cx, r.cy);
        for (int k = 1; k <= 12; k++) { s_px = (int32_t)dx - k * 15; run_ms(20); }
        touch_up();
        run_ms(600);                           /* let the tileview settle */
        lv_obj_scroll_to_x(tv, 0, LV_ANIM_OFF);  /* and put the radar back */
        render();
        r = ring();
        CHECK(hypot(r.cx - bx, r.cy - by) < 2, "ring back on B after a swipe: (%.1f,%.1f)", r.cx, r.cy);
        s_selected[0] = '\0';
        tap(240, 440);
        CHECK(strcmp(s_selected, "b00002") == 0, "caption still B after the swipe: \"%s\"", s_selected);
    }

    GROUP("tap on the empty scope lets go: caption and ring back to the nearest");
    {
        tap(EMPTY_X, EMPTY_Y);                  /* inside the rings, nothing there */
        render();
        png_write("04_let_go");
        blob_t r = ring();
        CHECK(hypot(r.cx - ax, r.cy - ay) < 2, "ring back on A: (%.1f,%.1f)", r.cx, r.cy);
        s_selected[0] = '\0';
        tap(240, 440);
        CHECK(strcmp(s_selected, "a00001") == 0, "caption back to A: \"%s\"", s_selected);
    }

    GROUP("long press on the empty scope still reaches the deck (Einstellungen)");
    {
        int before = s_tile_longpress;
        touch_down(240, 330);                   /* ON the middle ring's outline */
        run_ms(1400);
        touch_up();
        CHECK(s_tile_longpress > before, "tile saw %d long presses", s_tile_longpress - before);

        before = s_tile_longpress;
        touch_down(60, 100);                    /* the corner, outside the scope */
        run_ms(1400);
        touch_up();
        CHECK(s_tile_longpress > before, "corner: tile saw %d long presses", s_tile_longpress - before);
    }

    GROUP("...but a long press on a mark does not (the mark owns its touches)");
    {
        int before = s_tile_longpress;
        touch_down((int)cx_, (int)cy_);
        run_ms(1400);
        touch_up();
        CHECK(s_tile_longpress == before, "long press on a mark leaked to the tile");
        tap(EMPTY_X, EMPTY_Y);                  /* tidy up */
    }

    /* ------------------------------------------------------------------ */
    GROUP("caption press: the pill lights while the finger is down, only then");
    {
        uint16_t pill = to565(THEME_SURFACE_SEL);
        (void)pill;
        render();
        int idle = find(THEME_SURFACE_SEL, 0, 400, W - 1, 463).n;
        touch_down(240, 440);
        render();
        png_write("05_caption_pressed");
        int held = find(THEME_SURFACE_SEL, 0, 400, W - 1, 463).n;
        touch_up();
        render();
        int after = find(THEME_SURFACE_SEL, 0, 400, W - 1, 463).n;
        CHECK(idle == 0, "pill visible at rest: %d px", idle);
        CHECK(held > 500, "pill while pressed: %d px", held);
        CHECK(after == 0, "pill left behind after release: %d px", after);
    }

    GROUP("the pill clears the S cardinal above it");
    {
        render();
        uint16_t g = to565(THEME_GROUND); int s_bottom = 0;
        for (int y = 380; y < 440; y++) for (int x = 225; x <= 255; x++)
            if (s_fb[y * W + x] != g && y > s_bottom && y < 416) s_bottom = y;
        touch_down(240, 440);
        render();
        int pill_top = H;
        uint16_t p1 = to565(THEME_SURFACE_SEL), p2 = to565(THEME_BORDER_IDLE);
        for (int y = 380; y < 470 && pill_top == H; y++) for (int x = 0; x < W; x++)
            if (s_fb[y * W + x] == p1 || s_fb[y * W + x] == p2) { pill_top = y; break; }
        touch_up();
        CHECK(s_bottom > 0 && pill_top < H, "measurement failed: S bottom %d, pill top %d", s_bottom, pill_top);
        CHECK(pill_top > s_bottom + 1, "pill top y=%d touches the S (bottom y=%d)", pill_top, s_bottom);
    }

    GROUP("the pill never reaches the page dots (y >= 464)");
    {
        touch_down(240, 440);
        render();
        int below = find(THEME_SURFACE_SEL, 0, 464, W - 1, H - 1).n;
        touch_up();
        CHECK(below == 0, "pill pixels at or below the dots: %d", below);
    }

    GROUP("the caption carries its arrow");
    {
        /* The arrow is THEME_TEXT_LABEL grey, right of the cyan distance. The
         * distance ends somewhere; there must be label-grey ink after it. */
        render();
        blob_t dist = find(THEME_CYAN, 0, 405, W - 1, 463);
        int    maxx = 0;
        uint16_t cy565 = to565(THEME_CYAN);
        for (int y = 405; y <= 463; y++)
            for (int x = 0; x < W; x++)
                if (s_fb[y * W + x] == cy565 && x > maxx) maxx = x;
        blob_t arrow = find(THEME_TEXT_LABEL, maxx + 1, 405, W - 1, 463);
        CHECK(dist.n > 0, "no cyan distance in the caption band");
        CHECK(arrow.n > 5, "label-grey pixels right of the distance: %d", arrow.n);
    }

    /* ------------------------------------------------------------------ */
    GROUP("a selection nobody touches for 30 s goes back to the nearest");
    {
        /* update() straight after the tap, as the device's 2 s ui_task
         * cadence would: without it the radar looks as though it had been
         * off screen (RADAR_OFFSCREEN_GAP_MS) and the idle clock restarts. */
        update();
        tap((int)bx, (int)by);
        update();
        for (int k = 0; k < 14; k++) { run_ms(2000); update(); }   /* 28 s */
        blob_t r = ring();
        CHECK(hypot(r.cx - bx, r.cy - by) < 2, "still on B at 28 s: (%.1f,%.1f)", r.cx, r.cy);
        for (int k = 0; k < 2; k++) { run_ms(2000); update(); }    /* 32 s */
        r = ring();
        CHECK(hypot(r.cx - ax, r.cy - ay) < 2, "back on A after 30 s: (%.1f,%.1f)", r.cx, r.cy);
    }

    GROUP("time spent on the detail layer does not count against the selection");
    {
        /* Tap the caption, read the card for 40 s, come back: main.c promises
         * the same aircraft is still ringed. While the detail layer is up,
         * ui_task never calls screen_radar_update(), and no press lands on
         * this screen — so a naive 30 s clock expires behind his back. The
         * sim models "the layer is open" exactly that way: no updates. */
        tap((int)bx, (int)by);
        update();
        run_ms(40000);                          /* reading the card */
        update();                               /* back on the radar */
        blob_t r = ring();
        CHECK(hypot(r.cx - bx, r.cy - by) < 2, "selection lost while he read the card: ring at (%.1f,%.1f)", r.cx, r.cy);
        /* ...and the 30 s rule still holds once he is back and idle. */
        for (int k = 0; k < 16; k++) { run_ms(2000); update(); }
        r = ring();
        CHECK(hypot(r.cx - ax, r.cy - ay) < 2, "idle on the radar for 32 s, still on B: (%.1f,%.1f)", r.cx, r.cy);
    }

    GROUP("a long press on the empty scope opens Einstellungen WITHOUT letting go");
    {
        /* LVGL sends CLICKED on release even after a long press; only
         * SHORT_CLICKED is skipped. Holding to open Einstellungen must not
         * also cost him the aircraft he had tapped. */
        tap((int)bx, (int)by);
        update();
        int before = s_tile_longpress;
        touch_down(EMPTY_X, EMPTY_Y);
        run_ms(1400);
        touch_up();
        update();
        blob_t r = ring();
        CHECK(s_tile_longpress > before, "precondition: the long press reached the deck");
        CHECK(hypot(r.cx - bx, r.cy - by) < 2, "long press let go of B: ring at (%.1f,%.1f)", r.cx, r.cy);
        /* A hold shorter than Einstellungen's 1.2 s but past LVGL's 400 ms
         * is not a tap either. */
        touch_down(EMPTY_X, EMPTY_Y);
        run_ms(700);
        touch_up();
        update();
        r = ring();
        CHECK(hypot(r.cx - bx, r.cy - by) < 2, "a 0.7 s hold let go of B: ring at (%.1f,%.1f)", r.cx, r.cy);
        tap(EMPTY_X, EMPTY_Y);                  /* a real tap still does */
        update();
        r = ring();
        CHECK(hypot(r.cx - ax, r.cy - ay) < 2, "a plain tap no longer lets go: (%.1f,%.1f)", r.cx, r.cy);
    }

    /* ------------------------------------------------------------------ */
    GROUP("nearest is sticky through a near-tie, and yields to a clear winner");
    {
        s_ac[E].dst_nm = 5.8f; s_ac[E].dir_deg = 250;   /* 3 % closer than A */
        update();
        double ex, ey; where(E, &ex, &ey);
        blob_t m = magenta();
        CHECK(hypot(m.cx - ax, m.cy - ay) < 7, "magenta jumped on a 3%% difference: (%.0f,%.0f)", m.cx, m.cy);
        s_ac[E].dst_nm = 4.0f;                           /* now clearly nearer */
        update();
        where(E, &ex, &ey);
        m = magenta();
        CHECK(hypot(m.cx - ex, m.cy - ey) < 7, "magenta did not move to the clear winner: (%.0f,%.0f) want (%.0f,%.0f)", m.cx, m.cy, ex, ey);
        /* E is index 4, not 0. The caption must open the aircraft it NAMES
         * — the radar's own nearest — and not whatever sits first in the
         * caller's array. On the device those differ exactly when the
         * hysteresis above is holding on, so this is not hypothetical. */
        s_selected[0] = '\0';
        tap(240, 440);
        CHECK(strcmp(s_selected, "e00005") == 0, "caption opened \"%s\" while naming e00005", s_selected);
        s_ac[E].dst_nm = 18.0f;
        update();
    }

    /* ------------------------------------------------------------------ */
    GROUP("trails: a moving aircraft leaves dots where it has been");
    double c_dir0 = s_ac[C].dir_deg;
    {
        /* C sits at r = 93 px. 1.5 deg every 2 s is 70 px of arc in a
         * minute — fixes 15-16 s apart land ~17 px apart, well clear of the
         * mark itself, which the previous version of this test was not. */
        for (int k = 0; k < 30; k++) {
            s_ac[C].dir_deg += 1.5f;
            run_ms(2000);
            update();
        }
        png_write("06_trails");
        CHECK(arc_tint(c_dir0, s_ac[C].dir_deg - 8.0f, false) > 10,
              "cyan trail pixels along C's past arc: %d", arc_tint(c_dir0, s_ac[C].dir_deg - 8.0f, false));
        /* ...and none AHEAD of it, where it has not been. */
        CHECK(arc_tint(s_ac[C].dir_deg + 8.0f, s_ac[C].dir_deg + 60.0f, false) == 0,
              "trail pixels ahead of C: %d", arc_tint(s_ac[C].dir_deg + 8.0f, s_ac[C].dir_deg + 60.0f, false));
    }

    GROUP("trails: an aircraft that leaves and comes back starts a fresh one");
    {
        float was = s_ac[C].dir_deg;
        s_ac[C].dst_nm = DST_UNKNOWN;           /* gone for one poll */
        run_ms(2000); update();
        s_ac[C].dst_nm  = 20.0f;
        s_ac[C].dir_deg = 60.0f;                /* back, somewhere else entirely */
        run_ms(2000); update();
        CHECK(arc_tint(c_dir0, was, false) == 0,
              "old trail drawn after C came back elsewhere: %d px", arc_tint(c_dir0, was, false));
        s_ac[C].dir_deg = (float)c_dir0;
        run_ms(2000); update();
        /* Two seconds at 60 deg, then back at 300: a jump no aircraft makes.
         * The fix taken at 60 deg must not be left behind as a dot 190 px
         * from the aircraft — radar_logic's jump guard. */
        CHECK(arc_tint(50.0, 70.0, false) == 0,
              "ghost trail dot left at 60 deg: %d px", arc_tint(50.0, 70.0, false));
    }

    /* ------------------------------------------------------------------ */
    GROUP("stale data: amber tag top centre, marks dimmed, nothing else amber");
    {
        for (int k = 0; k < 20; k++) {          /* a fresh tail to watch fade */
            s_ac[C].dir_deg += 1.5f;
            run_ms(2000);
            update();
        }
        CHECK(arc_tint(c_dir0, s_ac[C].dir_deg - 8.0f, false) > 10, "precondition: a trail exists");

        screen_radar_set_net(NET_NO_DATA);
        update();
        png_write("07_stale");
        blob_t tag = find(THEME_AMBER, 120, 0, 360, 60);
        blob_t all = find(THEME_AMBER, 0, 0, W - 1, H - 1);
        CHECK(tag.n > 20, "amber tag pixels top centre: %d", tag.n);
        CHECK(fabs(tag.cx - 240) < 6, "tag not centred: x=%.1f", tag.cx);
        CHECK(all.n == tag.n, "amber outside the tag: %d px", all.n - tag.n);
        CHECK(find(THEME_TEXT_TERTIARY, 120, 0, 360, 60).n == 0, "identity still shown under the tag");
        CHECK(find(THEME_TEXT_LABEL, 0, 0, 160, 60).n > 20, "range read-out lost when stale");
        CHECK(magenta().n == 0, "an undimmed magenta mark on a stale screen: %d px", magenta().n);
        CHECK(find(THEME_CYAN, SCOPE_X0, SCOPE_Y0, SCOPE_X1, SCOPE_Y1).n == 0,
              "an undimmed cyan mark on a stale screen");
        double mx, my; where(A, &mx, &my);
        CHECK(ink_near(mx, my, 10) > 30, "the dimmed mark is still there");
    }

    GROUP("stale: the trails fade out instead of piling up under the marks");
    {
        double from = c_dir0, to = s_ac[C].dir_deg - 8.0f;
        for (int k = 0; k < 40; k++) { run_ms(2000); update(); }    /* 80 s */
        int left = 0;
        for (double b = from; b <= to; b += 1.0) {
            double r = R_OUTER * (s_ac[C].dst_nm / RADIUS_NM), rad = b * M_PI / 180.0;
            left += ink_near(240 + r * sin(rad), 240 - r * cos(rad), 3)
                  - ring_ink(240 + r * sin(rad), 240 - r * cos(rad), 3);
        }
        CHECK(left == 0, "trail still drawn after 80 s of stale data: %d px", left);
    }

    GROUP("stale: when the data comes back, no dot claims the frozen minute");
    {
        /* While stale the positions do not move, so a fix recorded then says
         * "it was here 15 s ago" about a position that is really minutes
         * old. Back on NET_OK with the aircraft moved on, such a fix would be
         * drawn as the newest trail dot, where the aircraft was NOT. */
        /* Built to be the case that matters: a SLOW aircraft. 2.5 nm on
         * (7.2 deg at 20 nm, ~12 px) after the outage, with the last poll 10 s
         * back — inside the jump guard's reach (800 kt x 10 s + 1 nm = 3.2
         * nm), so the guard does not reset the trail and hide the problem.
         * A fast one moves far enough to trip the guard either way. */
        double frozen = s_ac[C].dir_deg;
        run_ms(10000);
        screen_radar_set_net(NET_OK);
        s_ac[C].dir_deg += 7.2f;
        update();
        CHECK(arc_tint(frozen - 1.5, frozen + 1.5, false) == 0,
              "a trail dot at the frozen position: %d px", arc_tint(frozen - 1.5, frozen + 1.5, false));
        screen_radar_set_net(NET_NO_DATA);    /* restore for the next group */
        update();
    }

    GROUP("stale without WiFi says KEIN NETZ — a different, longer tag");
    {
        int no_data = find(THEME_AMBER, 120, 0, 360, 60).n;
        screen_radar_set_net(NET_NO_WIFI);
        update();
        int no_net = find(THEME_AMBER, 120, 0, 360, 60).n;
        CHECK(no_net > 20 && no_net != no_data, "tag pixels %d vs %d — expected a different word", no_net, no_data);
        screen_radar_set_net(NET_OK);
        update();
        CHECK(find(THEME_AMBER, 0, 0, W - 1, H - 1).n == 0, "amber stayed after the data came back");
    }

    /* ------------------------------------------------------------------ */
    GROUP("an empty sky: no ring, no caption, no pill, no arrow");
    {
        s_n = 0;
        update();
        png_write("08_empty");
        CHECK(ring().n == 0, "a ring on an empty sky: %d px", ring().n);
        CHECK(find(THEME_TEXT_LABEL, 0, 420, W - 1, 463).n == 0, "caption-band grey on an empty sky");
        CHECK(find(THEME_CYAN, 0, 405, W - 1, 463).n == 0, "a distance on an empty sky");
        s_n = N_AC;
        update();
    }

    GROUP("caption ladder, step 2: the arrow gives way before the name does");
    {
        /* "Unbekanntes Flugzeug" beside "11,1 km NO": fits without the arrow,
         * not with it. Measured here with the real faces, so this test
         * cannot pass by accident on a pair that would have fitted anyway. */
        put(A, "a00001", "", "", "", 6.0f, 45, true, 90, 3000, 120);
        memset(&s_rt[A], 0, sizeof s_rt[A]);
        update();
        png_write("09_arrow_yields");
        const char *nm = "Unbekanntes Flugzeug";
        int nw = text_w(nm, &plex_sans_cond_25);
        char dist[32]; size_t u = fmt_distance_km(6.0f, dist, sizeof dist);
        snprintf(dist + u, sizeof dist - u, " %s", compass_de_abbr(45));
        int dw = text_w(dist, &plex_mono_32);
        int aw = text_w("\xE2\x86\x92", &plex_sans_cond_25);
        CHECK(nw + 12 + dw <= 440 && nw + 12 + dw + 8 + aw > 440,
              "precondition: name %d + dist %d + arrow %d is not the step-2 case", nw, dw, aw);
        CHECK(find(THEME_TEXT_PRIMARY, 0, 405, W - 1, 463).n > 50, "the name was dropped for the arrow");
        blob_t d = find(THEME_CYAN, 0, 405, W - 1, 463);
        CHECK(d.n > 0, "distance missing");
        s_selected[0] = '\0';
        tap(240, 440);
        CHECK(s_selected[0] != '\0', "caption not tappable without its arrow");
    }

    GROUP("caption ladder, step 3: a name too long even alone gives way — distance and arrow stay");
    {
        put(A, "a00001", "IBE7", "A320", "EC-ABC", 12.0f, 200, true, 90, 3000, 250);
        routed(A, "LOWW", "LEST");                       /* Santiago de Compostela */
        update();
        png_write("10_name_yields");
        const char *nm = "Santiago de Compostela";
        int nw = text_w(nm, &plex_sans_cond_25);
        char dist[32]; size_t u = fmt_distance_km(12.0f, dist, sizeof dist);
        snprintf(dist + u, sizeof dist - u, " %s", compass_de_abbr(200));
        int dw = text_w(dist, &plex_mono_32);
        CHECK(nw + 12 + dw > 440, "precondition: \"%s\" + \"%s\" = %d px fits after all", nm, dist, nw + 12 + dw);
        CHECK(find(THEME_TEXT_PRIMARY, 0, 405, W - 1, 463).n == 0, "a name that cannot fit is shown");
        uint16_t cy565 = to565(THEME_CYAN); int maxx = 0;
        for (int y = 405; y <= 463; y++) for (int x = 0; x < W; x++)
            if (s_fb[y * W + x] == cy565 && x > maxx) maxx = x;
        CHECK(maxx > 0, "distance missing");
        CHECK(find(THEME_TEXT_LABEL, maxx + 1, 405, W - 1, 463).n > 5, "arrow missing beside the distance");
    }

    /* ------------------------------------------------------------------ */
    GROUP("stress: 600 random steps — taps, swipes, long presses, skies, outages");
    {
        /* AGENTS.md section 11, third pattern: stress finds what careful
         * tapping never will. Deterministic (fixed LCG seed), and built with
         * AddressSanitizer + UBSan (see Makefile), so an out-of-bounds index
         * into the per-mark caches or a stale pointer aborts the run rather
         * than drawing something odd. After every step, the invariants:
         *   - live data, aircraft up  -> exactly one ring, on an aircraft
         *   - live data               -> no amber anywhere
         *   - no aircraft             -> no ring */
        sky();
        s_seed = 0x5eed1234u;
        if (getenv("SIM_SEED")) s_seed = (uint32_t)strtoul(getenv("SIM_SEED"), NULL, 0);
        int bad_ring = 0, bad_amber = 0, bad_empty = 0, rings_seen = 0;
        for (int step = 0; step < 600; step++) {
            switch (rnd(9)) {
            case 0: case 1: case 2: {                 /* tap an aircraft */
                double x, y; where(rnd(N_AC), &x, &y);
                tap((int)x, (int)y); break; }
            case 3: tap(EMPTY_X, EMPTY_Y); break;      /* let go */
            case 4: tap(240, 440); break;              /* caption */
            case 5: {                                  /* swipe from anywhere */
                touch_down(60 + rnd(360), 100 + rnd(300));
                for (int k = 1; k <= 8; k++) { s_px -= 20; run_ms(20); }
                touch_up(); run_ms(400);
                lv_obj_scroll_to_x(tv, 0, LV_ANIM_OFF); break; }
            case 6:                                    /* long press */
                touch_down(60 + rnd(360), 100 + rnd(300)); run_ms(1300); touch_up(); break;
            case 7:                                    /* the sky changes */
                s_n = rnd(N_AC + 1);
                for (int i = 0; i < s_n; i++) {
                    s_ac[i].dir_deg = (float)rnd(360);
                    s_ac[i].dst_nm  = (rnd(10) == 0) ? DST_UNKNOWN : 0.5f + (float)rnd(40);
                    s_ac[i].alt_ft  = (rnd(8) == 0) ? ALT_GROUND : rnd(40000);
                }
                break;
            default:                                   /* time, and the net */
                screen_radar_set_net(rnd(5) == 0 ? (rnd(2) ? NET_NO_DATA : NET_NO_WIFI) : NET_OK);
                run_ms(500 + (uint32_t)rnd(20000)); break;
            }
            update();
            int any = 0;
            for (int i = 0; i < s_n; i++) any |= s_ac[i].dst_nm >= 0.0f;
            blob_t r = ring();
            if (!any) {
                if (r.n != 0) bad_empty++;
                continue;
            }
            /* amber only ever with a stale tag present — check the tag's
             * absence implies no amber at all */
            int amber_all = find(THEME_AMBER, 0, 0, W - 1, H - 1).n;
            int amber_tag = find(THEME_AMBER, 120, 0, 360, 60).n;
            bool live = (amber_tag == 0);
            if (live && amber_all != 0) bad_amber++;
            if (r.n == 0) { bad_ring++; continue; }
            rings_seen++;
            bool on_one = false;
            for (int i = 0; i < s_n; i++) {
                if (s_ac[i].dst_nm < 0.0f) continue;
                double x, y; where(i, &x, &y);
                if (hypot(r.cx - x, r.cy - y) < 2.5) on_one = true;
            }
            if (!on_one) {
                bad_ring++;
                if (getenv("SIM_DEBUG") && bad_ring <= 6) {
                    double best = 1e9; int bi = -1;
                    for (int i = 0; i < s_n; i++) { if (s_ac[i].dst_nm < 0) continue;
                        double x, y; where(i, &x, &y); double d = hypot(r.cx - x, r.cy - y);
                        if (d < best) { best = d; bi = i; } }
                    double x, y; where(bi, &x, &y);
                    printf("      step %d: ring n=%d at (%.1f,%.1f) r=%.1f; nearest ac %d at (%.1f,%.1f) dst %.1f nm, off by %.1f\n",
                           step, r.n, r.cx, r.cy, ring_radius(r), bi, x, y, s_ac[bi].dst_nm, best);
                }
            }
        }
        CHECK(rings_seen > 300, "stress barely exercised the ring: %d", rings_seen);
        CHECK(bad_ring == 0,  "%d steps with no ring, or a ring on no aircraft", bad_ring);
        CHECK(bad_amber == 0, "%d live steps with amber on screen", bad_amber);
        CHECK(bad_empty == 0, "%d empty-sky steps with a ring", bad_empty);
        png_write("11_after_stress");
    }

    printf("\n  %d checks, %d failed\n", t_run, t_fail);
    return t_fail ? 1 : 0;
}
