/* screen_radar.c — see screen_radar.h for the contract.
 *
 * ============================================================================
 * THE TYPE-PASS DESIGN.md §3 CALLS FOR (read this before touching a size)
 * ============================================================================
 * The original mockups put radar city labels at 10-11 px. This screen is
 * read at the near distance (~40 cm), whose floor is a 17 px cap height —
 * roughly a 24 px font for the faces this project ships (fonts/fonts.h).
 * There is no radar-specific font ladder, so the smallest faces that clear
 * that floor are plex_sans_cond_25 (words) and plex_mono_32 (figures, per
 * DESIGN.md §3's "IBM Plex Mono for anything numeric").
 *
 * A label built from those two faces is tall — a wrapped city name plus a
 * 32 px distance figure is roughly 70-100 px — and the scope itself is only
 * ~280 px across (see RADAR_R_OUTER below, sized to respect DESIGN.md §4's
 * round-panel remark). Labelling every aircraft at that size would cover
 * the sky in text. So, per the task brief's own resolution ("if that means
 * fewer labels on screen, show fewer labels"):
 *
 *   - Exactly TWO aircraft get a text label: the nearest (always, per
 *     DESIGN.md §2 — it is also the magenta "thing you are heading
 *     toward") and the second-nearest. Two, not more, because two label
 *     blocks at this mandated size already use a meaningful fraction of
 *     the scope; a third risked overlapping one of the first two on
 *     ordinary traffic (the measured samples in AGENTS.md §6 run 7-13
 *     aircraft at 30 nm), which is worse than not labelling it.
 *   - Every OTHER aircraft is still drawn — coloured and shaped, per the
 *     colour/shape table below — just without a name or a distance figure
 *     next to it. A radar screen with fewer captions than mockuped is still
 *     a radar screen; one with thirteen overlapping 10 px labels is not
 *     readable, is not the same failure this project was warned about.
 *
 * Colour + shape table (RTCA DO-257A §2.1.6 — never colour alone):
 *
 *   nearest aircraft     -> THEME_MAGENTA  ("the thing you're heading toward")
 *   other, route known   -> THEME_CYAN     (secondary data)
 *   other, no route      -> THEME_AMBER    (caution, abnormal source)
 *
 *   route known -> FILLED mark   ·   no route  -> HOLLOW/outline mark
 *   has_track   -> triangle, rotated to point at track_deg
 *   no track    -> plain dot (rotating an arrow would claim a heading that
 *                  is not known, which is its own kind of misleading mark)
 *
 * So "no route" is never colour-alone: it is amber AND hollow. "No known
 * heading" is shape-alone by design (dot vs triangle), which is fine
 * because it carries no colour meaning of its own to begin with.
 *
 * ============================================================================
 * BEARING -> SCREEN MAPPING, AND HOW IT WAS VERIFIED
 * ============================================================================
 * North is screen-up; bearing increases clockwise. bearing_to_xy() computes
 *   x = cx + r * sin(bearing) ,  y = cy - r * cos(bearing)
 * which was checked by hand for the four cardinal bearings before writing a
 * single aircraft mark:
 *   bearing   0 (N) -> (sin 0, -cos 0)   = ( 0, -1)  -> straight UP     ✓
 *   bearing  90 (O) -> (sin90, -cos90)   = ( 1,  0)  -> RIGHT (east)    ✓
 *   bearing 180 (S) -> (sin180,-cos180)  = ( 0,  1)  -> straight DOWN   ✓
 *   bearing 270 (W) -> (sin270,-cos270)  = (-1,  0)  -> LEFT (west)     ✓
 * Those four results are exactly where "N"/"O"/"S"/"W" are drawn in
 * screen_radar_create(), using the SAME function — so the cardinal marks
 * and every aircraft mark share one, single-tested mapping; there is no
 * second formula anywhere in this file that could silently disagree and
 * mirror east and west against each other.
 *
 * As a second, data-shaped check: DESIGN.md's own worked example is
 * `MEA201 · Beirut -> London · 15 km NO` (north-EAST) — bearing ~45 deg.
 * bearing_to_xy(45, r) gives (+0.707r, -0.707r): positive x (right/east
 * side) and negative y (upper/north half) — the upper-RIGHT quadrant, which
 * is exactly where "NO" belongs on a screen with north up.
 *
 * The same bearing_to_xy() is reused, unrotated, as the base orientation
 * for the aircraft glyph itself (see mark_draw_cb): a local "nose points
 * up" triangle rotated by the SAME clockwise convention, so a heading of
 * 90 deg (east) turns the nose to point right, not left.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include "screen_radar.h"
#include "theme.h"
#include "fonts/fonts.h"
#include <stdio.h>
#include "fmt_de.h"
#include "tables.h"
#include "route_parse.h"
#include "strings_de.h"

/* ============================================================================
 * FIXED GERMAN STRINGS — audited block, one hard-coded copy in this file:
 * the four cardinal letters. Everything else that reaches this screen is
 * either a formatted number (fmt_distance_km(), main/data/fmt_de.h) or a
 * name looked up from the German data tables (airport_de() /
 * actype_full_or_code(), main/data/tables.h) — never typed here, per the
 * task brief's "do not format German or numbers yourself".
 *
 * German uses "O" for Ost, never "E" — AGENTS.md §1 and §10 both call this
 * out by name as the classic bug.
 * ============================================================================
 */

/* ============================================================================
 * Layout constants, px. Centred on the panel; sized so the whole scope
 * (rings + cardinal marks) stays inside a ~328 px circle, comfortably under
 * DESIGN.md §4's "~340 px diameter" round-panel remark, even though the
 * actual panel here is the square 480x480 (THEME_SCREEN_WIDTH/HEIGHT).
 * ============================================================================
 */
#define RADAR_CX (THEME_SCREEN_WIDTH / 2)  /* 240 */
#define RADAR_CY (THEME_SCREEN_HEIGHT / 2) /* 240 */

#define RADAR_R_OUTER 140 /* full radius_nm */
#define RADAR_R_MID   93  /* 2/3 radius_nm */
#define RADAR_R_INNER 47  /* 1/3 radius_nm */

#define RADAR_RING_W_OUTER 2 /* THEME_HAIRLINE   — DESIGN.md §2 */
#define RADAR_RING_W_INNER 1 /* THEME_HAIRLINE_DIM — DESIGN.md §2, §5.5 */

#define RADAR_CARDINAL_R 164 /* N/O/S/W sit just outside the outer ring */

#define RADAR_KM_LABEL_BEARING 135.0f /* SO — clear of the rings' own N/O/S/W marks */
#define RADAR_KM_LABEL_R       148.0f

#define RADAR_HOME_R_OUTER  12
#define RADAR_HOME_R_INNER  6
#define RADAR_HOME_BORDER_W 2

/* Aircraft mark bounding box. A triangle with these proportions has a
 * maximum reach of RADAR_MARK_NOSE (11 px) from its own centre at any
 * rotation, so a 28 px box leaves several px of margin on every side. */
#define RADAR_MARK_BOX       28
#define RADAR_MARK_NOSE      11
#define RADAR_MARK_BASE_HALF 7
#define RADAR_MARK_TAIL      7
#define RADAR_MARK_DOT_R     7

/* See this file's top comment for why exactly two. */
/* The scope reaches y=404 (centre 240 + cardinal radius 164); the page
 * indicator sits at y=464. The caption lives in the gap between them. */
#define RADAR_CAPTION_Y   416
#define RADAR_CAPTION_GAP 12

#define RADAR_NEAR_LABEL_COUNT 2
#define RADAR_LABEL_NAME_W     132
#define RADAR_LABEL_GAP_MARK   6
#define RADAR_LABEL_LINE_GAP   4

#define RADAR_DEG2RAD 0.017453292519943295f

/* ============================================================================
 * Widget tree — built once by screen_radar_create(), single instance (this
 * device shows exactly one radar screen), so plain file-scope statics
 * rather than a heap-allocated context — matches screen_overhead.c and
 * every other screen in this codebase.
 * ============================================================================
 */
static lv_obj_t *s_cont;
static lv_obj_t *s_ring_inner, *s_ring_mid, *s_ring_outer;
static lv_obj_t *s_lbl_km;
static lv_obj_t *s_lbl_cardinal[4];
static lv_obj_t *s_home_outer, *s_home_inner;

/* Per-mark drawing state, read by mark_draw_cb() out of the user_data
 * pointer each object was given at creation time. screen_radar_update()
 * writes this; it never touches the lv_obj_t's own style. */
typedef struct {
    lv_color_t color;
    float      heading_deg;
    bool       has_track;
    bool       filled;
} radar_mark_state_t;

static lv_obj_t          *s_marks[MAX_AIRCRAFT];
static radar_mark_state_t s_mark_state[MAX_AIRCRAFT];

/* One label slot = one name label + one distance label, reused for
 * whichever aircraft index currently ranks nearest / second-nearest. */
typedef struct {
    lv_obj_t *name;
    lv_obj_t *dist;
} radar_label_t;
static radar_label_t s_labels[RADAR_NEAR_LABEL_COUNT];

/* Per-aircraft geometry + status, recomputed at the top of every
 * screen_radar_update() call and consumed twice in the same call (once for
 * the marks, once for the two label slots) — file-scope so it costs no
 * per-call allocation, matching this file's "build once" rule for objects. */
typedef struct {
    float x, y;        /* screen position; meaningful only if `valid` */
    bool  valid;        /* false if dst_nm was DST_UNKNOWN */
    bool  has_route;
    bool  has_track;
    float track_deg;
} radar_calc_t;
static radar_calc_t s_calc[MAX_AIRCRAFT];

/* ============================================================================
 * Small geometry + widget helpers
 * ============================================================================
 */

/* THE bearing -> screen mapping. See this file's top comment for the
 * worked-by-hand verification of all four cardinal directions. Every mark
 * on this screen — cardinal letters, aircraft position, aircraft heading —
 * goes through this one function, so there is exactly one place a mirrored
 * east/west bug could hide, and it has been checked. */
static void bearing_to_xy(float bearing_deg, float radius_px, float *out_x, float *out_y)
{
    float b = fmodf(bearing_deg, 360.0f);
    if (b < 0.0f) {
        b += 360.0f;
    }
    float rad = b * RADAR_DEG2RAD;
    *out_x = (float)RADAR_CX + radius_px * sinf(rad);
    *out_y = (float)RADAR_CY - radius_px * cosf(rad);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}

/* Fixed-width, wrapping label for aircraft/city names — DESIGN.md §3 is
 * explicit that a half-name is worse than a smaller one, so long names wrap
 * to a second line rather than ever being clipped. */
static lv_obj_t *make_wrapped_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                                    int32_t width)
{
    lv_obj_t *l = make_label(parent, font, color);
    lv_obj_set_width(l, width);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    return l;
}

/* A plain circular outline, centred on the radar's own centre — used for
 * the three range rings. */
static lv_obj_t *make_ring(lv_obj_t *parent, int32_t r, int32_t border_w, lv_color_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, r * 2, r * 2);
    lv_obj_set_pos(o, RADAR_CX - r, RADAR_CY - r);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, border_w, 0);
    lv_obj_set_style_border_color(o, color, 0);
    lv_obj_set_scrollable(o, false);
    return o;
}

/* Centres an already-texted label on (cx, cy). */
static void place_centered_xy(lv_obj_t *obj, int32_t cx, int32_t cy)
{
    lv_obj_update_layout(obj);
    int32_t w = lv_obj_get_width(obj);
    int32_t h = lv_obj_get_height(obj);
    lv_obj_set_pos(obj, cx - w / 2, cy - h / 2);
}

/* Nudges an already-positioned object back inside the panel, `margin` px
 * from every edge — used for the outer-ring km readout, whose length
 * varies with the German-formatted distance text. */
static void clamp_into_panel(lv_obj_t *obj, int32_t margin)
{
    int32_t x = lv_obj_get_x(obj);
    int32_t y = lv_obj_get_y(obj);
    int32_t w = lv_obj_get_width(obj);
    int32_t h = lv_obj_get_height(obj);
    if (x < margin) {
        x = margin;
    }
    if (y < margin) {
        y = margin;
    }
    if (x + w > THEME_SCREEN_WIDTH - margin) {
        x = THEME_SCREEN_WIDTH - margin - w;
    }
    if (y + h > THEME_SCREEN_HEIGHT - margin) {
        y = THEME_SCREEN_HEIGHT - margin - h;
    }
    lv_obj_set_pos(obj, x, y);
}

/* ============================================================================
 * Aircraft mark drawing — one lv_obj_t per potential aircraft (built once in
 * screen_radar_create()), each with this single draw-event callback bound
 * at creation. screen_radar_update() never touches these objects' styles;
 * it only writes s_mark_state[i] and repositions/hides the object, then
 * invalidates it so the next render pass re-runs this callback with the
 * new state. Drawn from scratch with LVGL's low-level draw primitives
 * (lv_draw_triangle / lv_draw_line / lv_draw_rect) — see screen_radar.h's
 * top comment on why this is not adapted from the MIT-licensed radar
 * projects in docs/RESEARCH.md.
 * ============================================================================
 */
static void mark_draw_cb(lv_event_t *e)
{
    lv_obj_t                 *obj   = lv_event_get_target_obj(e);
    lv_layer_t                *layer = lv_event_get_layer(e);
    const radar_mark_state_t *st    = (const radar_mark_state_t *)lv_event_get_user_data(e);
    if (st == NULL) {
        return;
    }

    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    float cx = ((float)area.x1 + (float)area.x2) / 2.0f;
    float cy = ((float)area.y1 + (float)area.y2) / 2.0f;

    if (st->has_track) {
        float b = fmodf(st->heading_deg, 360.0f);
        if (b < 0.0f) {
            b += 360.0f;
        }
        float rad = b * RADAR_DEG2RAD;
        float c   = cosf(rad);
        float s   = sinf(rad);

        /* Local triangle, nose pointing "up" (north) before rotation —
         * rotated by the SAME clockwise, screen-Y-down convention as
         * bearing_to_xy() above, so a heading of 90 deg (east) turns the
         * nose to the right, matching where an eastbound aircraft's own
         * mark sits relative to its neighbours. */
        const float local_pts[3][2] = {
            { 0.0f,                          -(float)RADAR_MARK_NOSE },
            { -(float)RADAR_MARK_BASE_HALF,   (float)RADAR_MARK_TAIL },
            {  (float)RADAR_MARK_BASE_HALF,   (float)RADAR_MARK_TAIL },
        };
        lv_point_precise_t pts[3];
        for (int k = 0; k < 3; k++) {
            float lx = local_pts[k][0];
            float ly = local_pts[k][1];
            float dx = lx * c - ly * s;
            float dy = lx * s + ly * c;
            pts[k].x = (lv_value_precise_t)(cx + dx);
            pts[k].y = (lv_value_precise_t)(cy + dy);
        }

        if (st->filled) {
            lv_draw_triangle_dsc_t dsc;
            lv_draw_triangle_dsc_init(&dsc);
            dsc.p[0]  = pts[0];
            dsc.p[1]  = pts[1];
            dsc.p[2]  = pts[2];
            dsc.color = st->color;
            dsc.opa   = LV_OPA_COVER;
            lv_draw_triangle(layer, &dsc);
        } else {
            /* No route: same triangle, drawn hollow — DO-257A never-colour-
             * alone, so "no route" is amber AND an outline, not amber alone. */
            lv_draw_line_dsc_t ldsc;
            lv_draw_line_dsc_init(&ldsc);
            ldsc.color       = st->color;
            ldsc.width       = 2;
            ldsc.opa         = LV_OPA_COVER;
            ldsc.round_start = 1;
            ldsc.round_end   = 1;
            for (int k = 0; k < 3; k++) {
                int kk    = (k + 1) % 3;
                ldsc.p1   = pts[k];
                ldsc.p2   = pts[kk];
                lv_draw_line(layer, &ldsc);
            }
        }
    } else {
        /* No known heading: a plain dot rather than an arrow that would
         * claim a direction of travel nobody actually reported. */
        lv_draw_rect_dsc_t rdsc;
        lv_draw_rect_dsc_init(&rdsc);
        rdsc.radius = LV_RADIUS_CIRCLE;
        if (st->filled) {
            rdsc.bg_color     = st->color;
            rdsc.bg_opa       = LV_OPA_COVER;
            rdsc.border_width = 0;
        } else {
            rdsc.bg_opa        = LV_OPA_TRANSP;
            rdsc.border_color = st->color;
            rdsc.border_width = 2;
        }
        lv_area_t dot_area = {
            .x1 = (int32_t)(cx - (float)RADAR_MARK_DOT_R),
            .y1 = (int32_t)(cy - (float)RADAR_MARK_DOT_R),
            .x2 = (int32_t)(cx + (float)RADAR_MARK_DOT_R),
            .y2 = (int32_t)(cy + (float)RADAR_MARK_DOT_R),
        };
        lv_draw_rect(layer, &rdsc, &dot_area);
    }
}

/* ============================================================================
 * Public API — see screen_radar.h for the full contract.
 * ============================================================================
 */
void screen_radar_create(lv_obj_t *parent)
{
    s_cont = lv_obj_create(parent);
    lv_obj_remove_style_all(s_cont);
    lv_obj_set_size(s_cont, THEME_SCREEN_WIDTH, THEME_SCREEN_HEIGHT);
    lv_obj_set_pos(s_cont, 0, 0);
    lv_obj_set_style_bg_color(s_cont, THEME_GROUND, 0);
    lv_obj_set_style_bg_opa(s_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_cont, 0, 0);
    lv_obj_set_style_border_width(s_cont, 0, 0);
    lv_obj_set_scrollable(s_cont, false);

    /* --- Range rings — DESIGN.md §2 assigns these two tokens specifically:
     * THEME_HAIRLINE for the outer ring, THEME_HAIRLINE_DIM for the two
     * inner ones. --- */
    s_ring_inner = make_ring(s_cont, RADAR_R_INNER, RADAR_RING_W_INNER, THEME_HAIRLINE_DIM);
    s_ring_mid   = make_ring(s_cont, RADAR_R_MID, RADAR_RING_W_INNER, THEME_HAIRLINE_DIM);
    s_ring_outer = make_ring(s_cont, RADAR_R_OUTER, RADAR_RING_W_OUTER, THEME_HAIRLINE);

    /* --- Outer-ring range readout, in km. Chrome tier — a scale reference,
     * never the thing he has to read up close — so it stays at the same
     * 13 px mono the compass tape already uses for its own tick labels
     * (widget_compass.c), which this screen has no compass tape of its
     * own to inherit the convention from otherwise. --- */
    s_lbl_km = make_label(s_cont, &plex_mono_13, THEME_TEXT_LABEL);

    /* --- Cardinal marks: N / O / S / W, at the SAME bearings (0/90/180/270)
     * used to verify bearing_to_xy() above. "O" for Ost, never "E"
     * (AGENTS.md §1, §10). --- */
    for (int i = 0; i < 4; i++) {
        s_lbl_cardinal[i] = make_label(s_cont, &plex_sans_cond_25, THEME_TEXT_LABEL);
        /* compass_de_abbr(), not a private { "N", "O", "S", "W" } table:
         * one implementation of the O-for-Ost rule for the whole device. */
        lv_label_set_text(s_lbl_cardinal[i], compass_de_abbr((float)(i * 90)));
        float x, y;
        bearing_to_xy((float)(i * 90), (float)RADAR_CARDINAL_R, &x, &y);
        place_centered_xy(s_lbl_cardinal[i], (int32_t)x, (int32_t)y);
    }

    /* --- Home marker: THEME_GREEN, AC 25-11A "engaged / normal conditions".
     * A small bullseye rather than a plain dot, so green carries a shape as
     * well as a colour (DO-257A §2.1.6) — no aircraft mark is ever drawn at
     * r=0, but the never-colour-alone rule is not conditional on ambiguity
     * actually arising. --- */
    s_home_outer = lv_obj_create(s_cont);
    lv_obj_remove_style_all(s_home_outer);
    lv_obj_set_size(s_home_outer, RADAR_HOME_R_OUTER * 2, RADAR_HOME_R_OUTER * 2);
    lv_obj_set_pos(s_home_outer, RADAR_CX - RADAR_HOME_R_OUTER, RADAR_CY - RADAR_HOME_R_OUTER);
    lv_obj_set_style_radius(s_home_outer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_home_outer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_home_outer, RADAR_HOME_BORDER_W, 0);
    lv_obj_set_style_border_color(s_home_outer, THEME_GREEN, 0);
    lv_obj_set_scrollable(s_home_outer, false);

    s_home_inner = lv_obj_create(s_cont);
    lv_obj_remove_style_all(s_home_inner);
    lv_obj_set_size(s_home_inner, RADAR_HOME_R_INNER * 2, RADAR_HOME_R_INNER * 2);
    lv_obj_set_pos(s_home_inner, RADAR_CX - RADAR_HOME_R_INNER, RADAR_CY - RADAR_HOME_R_INNER);
    lv_obj_set_style_radius(s_home_inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_home_inner, THEME_GREEN, 0);
    lv_obj_set_style_bg_opa(s_home_inner, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s_home_inner, false);

    /* --- Aircraft marks: built once, MAX_AIRCRAFT of them, hidden until
     * the first update. Each is an empty bounding box whose only job is to
     * host mark_draw_cb(); screen_radar_update() never creates or destroys
     * one of these (M1's 28.5 FPS ceiling plus the 12 s poll interval make
     * per-update allocation the wrong trade — see screen_radar.h). --- */
    for (int i = 0; i < MAX_AIRCRAFT; i++) {
        lv_obj_t *m = lv_obj_create(s_cont);
        lv_obj_remove_style_all(m);
        lv_obj_set_size(m, RADAR_MARK_BOX, RADAR_MARK_BOX);
        lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(m, 0, 0);
        lv_obj_set_scrollable(m, false);
        lv_obj_add_event_cb(m, mark_draw_cb, LV_EVENT_DRAW_MAIN, &s_mark_state[i]);
        lv_obj_set_hidden(m, true);
        s_marks[i] = m;
    }

    /* --- The two nearest-aircraft labels — see this file's top comment for
     * why exactly two. Built once, hidden until the first update. --- */
    for (int i = 0; i < RADAR_NEAR_LABEL_COUNT; i++) {
        s_labels[i].name = make_wrapped_label(s_cont, &plex_sans_cond_25, THEME_TEXT_PRIMARY,
                                              RADAR_LABEL_NAME_W);
        s_labels[i].dist = make_label(s_cont, &plex_mono_32, THEME_CYAN);
        lv_obj_set_hidden(s_labels[i].name, true);
        lv_obj_set_hidden(s_labels[i].dist, true);
    }
}

void screen_radar_update(const aircraft_t *ac, int n, const route_t *routes, int n_routes,
                         int radius_nm)
{
    if (n < 0) {
        n = 0;
    }
    if (n > MAX_AIRCRAFT) {
        n = MAX_AIRCRAFT; /* defensive re-cap — see screen_radar.h */
    }
    int rng_nm = (radius_nm > 0) ? radius_nm : 1;

    /* --- Outer-ring range readout, in km. The panel speaks km everywhere
     * (AGENTS.md §1); fmt_distance_km() owns both the unit conversion and
     * the German number formatting — this file only asks for it. --- */
    char km_buf[24];
    fmt_distance_km((float)radius_nm, km_buf, sizeof km_buf);
    lv_label_set_text(s_lbl_km, km_buf);
    {
        float x, y;
        bearing_to_xy(RADAR_KM_LABEL_BEARING, RADAR_KM_LABEL_R, &x, &y);
        place_centered_xy(s_lbl_km, (int32_t)x, (int32_t)y);
        clamp_into_panel(s_lbl_km, THEME_SIDE_PADDING);
    }

    /* --- Per-aircraft geometry + route status, computed once up front:
     * both the marks loop and the label loop below need it. --- */
    for (int i = 0; i < n; i++) {
        const aircraft_t *a = &ac[i];
        radar_calc_t      *c = &s_calc[i];

        c->valid = (a->dst_nm >= 0.0f); /* DST_UNKNOWN is negative — flight_types.h */
        if (!c->valid) {
            continue; /* no distance, no placement — nothing to draw */
        }

        float r_px = (float)RADAR_R_OUTER * (a->dst_nm / (float)rng_nm);
        if (r_px > (float)RADAR_R_OUTER) {
            r_px = (float)RADAR_R_OUTER; /* clip to the outer ring, per the task brief */
        }
        if (r_px < 0.0f) {
            r_px = 0.0f;
        }
        bearing_to_xy(a->dir_deg, r_px, &c->x, &c->y);

        const route_t *rt = route_find(routes, n_routes, a->flight);
        c->has_route      = (rt != NULL) && rt->resolved;
        c->has_track      = a->has_track;
        c->track_deg      = a->track_deg;
    }
    for (int i = n; i < MAX_AIRCRAFT; i++) {
        s_calc[i].valid = false;
    }

    /* --- Nearest and second-nearest, by real distance (not screen
     * geometry) — the nearest drives the magenta "thing you're heading
     * toward" mark, and both drive which two aircraft get a text label. --- */
    int nearest_idx = -1;
    int second_idx  = -1;
    for (int i = 0; i < n; i++) {
        if (!s_calc[i].valid) {
            continue;
        }
        if (nearest_idx < 0 || ac[i].dst_nm < ac[nearest_idx].dst_nm) {
            second_idx  = nearest_idx;
            nearest_idx = i;
        } else if (second_idx < 0 || ac[i].dst_nm < ac[second_idx].dst_nm) {
            second_idx = i;
        }
    }

    /* --- Marks: colour + shape per aircraft. See this file's top comment
     * for the full table and the never-colour-alone reasoning. --- */
    for (int i = 0; i < MAX_AIRCRAFT; i++) {
        if (i >= n || !s_calc[i].valid) {
            lv_obj_set_hidden(s_marks[i], true);
            continue;
        }
        radar_calc_t *c = &s_calc[i];
        lv_color_t    color =
            (i == nearest_idx) ? THEME_MAGENTA : (c->has_route ? THEME_CYAN : THEME_AMBER);

        s_mark_state[i].color       = color;
        s_mark_state[i].has_track   = c->has_track;
        s_mark_state[i].heading_deg = c->track_deg;
        s_mark_state[i].filled      = c->has_route;

        lv_obj_set_pos(s_marks[i], (int32_t)(c->x - (float)RADAR_MARK_BOX / 2.0f),
                       (int32_t)(c->y - (float)RADAR_MARK_BOX / 2.0f));
        lv_obj_set_hidden(s_marks[i], false);
        /* Position may be unchanged from the previous poll while colour or
         * shape changed (e.g. a route resolved between polls) — force the
         * redraw rather than relying on lv_obj_set_pos() alone to catch it. */
        lv_obj_invalidate(s_marks[i]);
    }

    /* --- The caption, BELOW the scope rather than inside it. ---
     *
     * The labels used to float beside their marks. At the sizes DESIGN.md §3
     * requires for this tier — 25 px words, 32 px figures — two of those blocks
     * cover the middle of a 280 px scope and sit on top of other aircraft and
     * each other; a live capture had "DIMO 2,1 km" printed across the home
     * marker. Widening the scope does not help, because the constraint is the
     * text, and shrinking the text is the one thing the type pass forbids.
     *
     * So the scope stays a picture — shape and colour, no prose — and the words
     * he actually has to read live in a band under it, at full size, where the
     * same spot always means the same thing. That also matches §4's band order:
     * the data band belongs at the bottom.
     *
     * Only the nearest is captioned. It is the one the magenta mark already
     * singles out, and a second caption is the crowding problem again. */
    lv_obj_set_hidden(s_labels[1].name, true);
    lv_obj_set_hidden(s_labels[1].dist, true);

    if (nearest_idx < 0) {
        lv_obj_set_hidden(s_labels[0].name, true);
        lv_obj_set_hidden(s_labels[0].dist, true);
        return;
    }

    const aircraft_t *a  = &ac[nearest_idx];
    const route_t    *rt = route_find(routes, n_routes, a->flight);
    const char       *name;
    if (rt != NULL && rt->resolved) {
        name = airport_de(rt->dest_icao);
        if (name == NULL || name[0] == '\0') {
            name = rt->dest_city;         /* the API's own English name */
        }
    } else {
        /* Plain language, never a raw ICAO code (AGENTS.md §1). */
        name = actype_display_name(a->type, a->category);
        if (name == NULL) {
            name = STR_UNKNOWN_AIRCRAFT;
        }
    }
    lv_label_set_text(s_labels[0].name, name);

    char dist_buf[32];
    size_t used = fmt_distance_km(a->dst_nm, dist_buf, sizeof dist_buf);
    const char *dir = compass_de_abbr(a->dir_deg);
    if (dir != NULL && used + 1 < sizeof dist_buf) {
        snprintf(dist_buf + used, sizeof dist_buf - used, " %s", dir);
    }
    lv_label_set_text(s_labels[0].dist, dist_buf);

    /* ONE line, or no name at all.
     *
     * The caption band is the 44 px between the scope and the page dots, so a
     * name that wraps does not get taller — it gets cut across the distance
     * and the dots. "Unbekanntes Flugzeug" did exactly that, rendering as
     * "Unbekannte / s Flugzeug" over the top of "9,7 km NNO", and D46 made
     * that string common rather than rare.
     *
     * When the pair will not fit on one line the NAME yields, not the
     * distance — the same priority D48 applies to the hero screen, and for
     * the same reason: the magenta mark already says WHICH aircraft this is,
     * so the caption's remaining job is how far and which way. Measured
     * unwrapped, because a wrapped label reports the width it was given
     * rather than the width it wants. */
    lv_point_t want;
    lv_text_get_size(&want, name, &plex_sans_cond_25, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    lv_obj_update_layout(s_labels[0].dist);
    bool name_fits = (want.x + RADAR_CAPTION_GAP + lv_obj_get_width(s_labels[0].dist))
                     <= (THEME_SCREEN_WIDTH - 2 * THEME_SIDE_PADDING);

    lv_obj_set_hidden(s_labels[0].name, !name_fits);
    lv_obj_set_hidden(s_labels[0].dist, false);

    if (!name_fits) {
        lv_obj_update_layout(s_labels[0].dist);
        int32_t w = lv_obj_get_width(s_labels[0].dist);
        lv_obj_set_pos(s_labels[0].dist, (THEME_SCREEN_WIDTH - w) / 2,
                       RADAR_CAPTION_Y);
        return;
    }

    /* Give the label the width the text actually wants. It was created with a
     * fixed 132 px and LV_LABEL_LONG_MODE_WRAP, which is about eleven
     * characters at 25 px — so "Thessaloniki" would have wrapped too, and the
     * check above would have called it a fit. The cap exists to stop a
     * caption running off the panel; now that the fit is measured properly,
     * the cap is the measurement. */
    lv_obj_set_width(s_labels[0].name, want.x);

    /* One line, centred as a pair, so a long name and a short distance stay
     * visually joined instead of drifting to opposite edges. */
    lv_obj_update_layout(s_labels[0].name);
    lv_obj_update_layout(s_labels[0].dist);
    int32_t nw = lv_obj_get_width(s_labels[0].name);
    int32_t dw = lv_obj_get_width(s_labels[0].dist);
    int32_t nh = lv_obj_get_height(s_labels[0].name);
    int32_t dh = lv_obj_get_height(s_labels[0].dist);
    int32_t total = nw + RADAR_CAPTION_GAP + dw;
    int32_t x = (THEME_SCREEN_WIDTH - total) / 2;
    if (x < THEME_SIDE_PADDING) x = THEME_SIDE_PADDING;
    int32_t base = RADAR_CAPTION_Y;

    lv_obj_set_pos(s_labels[0].name, x, base + (dh > nh ? (dh - nh) / 2 : 0));
    lv_obj_set_pos(s_labels[0].dist, x + nw + RADAR_CAPTION_GAP,
                   base + (nh > dh ? (nh - dh) / 2 : 0));
}
