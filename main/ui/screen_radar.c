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
 *   - ONE caption, in a band UNDER the scope, not beside any mark (D35).
 *     It names the aircraft with the white selection ring round it — the
 *     nearest until he taps another (D75). This comment said "exactly TWO
 *     labels, beside the nearest and second-nearest" until D76; that was
 *     true before D35 and the code stopped doing it then. The second label
 *     slot (s_labels[1]) is still built, and always hidden.
 *   - Every aircraft is drawn — coloured, shaped, sized and trailed, per the
 *     table below — without words. A radar screen with fewer captions than
 *     mockuped is still a radar screen; one with thirteen overlapping 10 px
 *     labels is not readable, and is the failure this project was warned
 *     about.
 *
 * Colour + shape table (RTCA DO-257A §2.1.6 — never colour alone):
 *
 *   nearest aircraft     -> THEME_MAGENTA  ("the thing you're heading toward")
 *   every other aircraft -> THEME_CYAN     (secondary data), route or not
 *
 *   captioned aircraft   -> a white RING around it, whatever colour it is
 *   altitude             -> mark SIZE: large below ~1 500 m, small at cruise
 *   where it has been    -> a fading trail of up to four dots behind it
 *   data gone stale      -> every mark dimmed, AND an amber tag top-left
 *
 * No-route aircraft USED to be amber (D76). Amber is AC 25-11A's caution
 * colour — TCAS spends it on one thing, a traffic advisory — and here it was
 * being spent on the most ordinary aircraft in the sky, every private
 * aircraft there is, which are also the ones he actually hears. It was also
 * redundant: filled-vs-hollow already carries route-known losslessly. Amber
 * on this screen now means exactly one thing, the one thing it should: what
 * you are looking at is not live.
 *
 * The ring is a separate channel from the colour on purpose, and D75 is the
 * argument for it. Colour answers "which one is nearest", which the device
 * decides and which moves on its own between polls; the ring answers "which
 * one is the caption talking about", which HE decides by tapping. Those are
 * two different questions and they had been sharing one answer: tapping a
 * mark re-pointed the caption and left the scope looking exactly as it had,
 * so the only evidence a tap had landed was 190 px away at the bottom edge.
 * They coincide until he taps — the caption defaults to the nearest, so the
 * ring starts out on the magenta mark — and they can only come apart as the
 * direct result of something he just did, which is the moment he needs to
 * see it.
 *
 *   route known -> FILLED mark   ·   no route  -> HOLLOW/outline mark
 *   has_track   -> triangle, rotated to point at track_deg
 *   no track    -> plain dot (rotating an arrow would claim a heading that
 *                  is not known, which is its own kind of misleading mark)
 *
 * So "no route" is shape-alone now — hollow — and that is fine for the same
 * reason "no known heading" always was (dot vs triangle): it carries no
 * colour meaning of its own, so there is no colour it could be confused by.
 * DO-257A's rule is that colour must never be the ONLY carrier, not that
 * every fact needs a colour.
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
#include <string.h>

#include "screen_radar.h"
#include "theme.h"
#include "fonts/fonts.h"
#include <stdio.h>
#include "fmt_de.h"
#include "tables.h"
#include "route_parse.h"
#include "strings_de.h"
#include "identity.h"
#include "widget_signal.h"
#include "widget_busy.h"
#include "radar_logic.h"

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
/* RADAR_CX, RADAR_CY, RADAR_R_OUTER, RADAR_CARDINAL_R and the caption's
 * position live in screen_radar.h, so test/sim measures against the numbers
 * the screen uses instead of a copy. */
#define RADAR_R_MID   (RADAR_R_OUTER * 2 / 3) /* 2/3 radius_nm */
#define RADAR_R_INNER (RADAR_R_OUTER / 3)     /* 1/3 radius_nm */

#define RADAR_RING_W_OUTER 2 /* THEME_HAIRLINE   — DESIGN.md §2 */
#define RADAR_RING_W_INNER 1 /* THEME_HAIRLINE_DIM — DESIGN.md §2, §5.5 */



#define RADAR_HOME_R_OUTER  12
#define RADAR_HOME_R_INNER  6
#define RADAR_HOME_BORDER_W 2

/* Aircraft mark geometry, at the MIDDLE altitude band. The other two bands
 * scale every one of these (RADAR_SCALE_*), so the largest mark has a nose
 * reach of 11 x 1.35 = 14.9 px plus 1 px of stroke — inside the 17 px half-box
 * at any rotation. The box grew from 28 to 34 for that; the touch pad shrank
 * by the same 3 px a side, so the finger target is still 56 px across. */
#define RADAR_MARK_BOX       34
#define RADAR_MARK_NOSE      11
#define RADAR_MARK_BASE_HALF 7
#define RADAR_MARK_TAIL      7
#define RADAR_MARK_DOT_R     7

/* Size by altitude band (radar_logic.h). Three steps, not a continuous scale:
 * a continuous size cannot be read, only compared, and he is not comparing,
 * he is looking for the big one. */
#define RADAR_SCALE_LOW  1.35f
#define RADAR_SCALE_MID  1.00f
#define RADAR_SCALE_HIGH 0.75f

/* Stale data: every mark and trail drawn at this opacity. The amber tag says
 * it in words; this makes the scope itself look like what it is, a picture of
 * a moment ago. */
#define RADAR_STALE_OPA  LV_OPA_50

/* Trail dots, newest to oldest. */
#define RADAR_TRAIL_DOT_R 3
static const lv_opa_t k_trail_opa[RADAR_TRAIL_LEN] = { LV_OPA_70, LV_OPA_50, LV_OPA_30, LV_OPA_20 };

/* A tapped aircraft stays the caption's subject for this long without a
 * touch on this screen, then the caption goes back to the nearest. Same 30 s
 * as DESIGN.md §6's auto-return, for the same reason: the device's resting
 * job is answering "what is up there now", and a selection nobody is looking
 * at any more is just the panel refusing to do that job. */
#define RADAR_SELECTION_TIMEOUT_MS 30000

/* The 30 s is time he spends LOOKING AT THIS SCREEN without touching it, not
 * wall-clock time. ui_task calls screen_radar_update() every 2 s only while
 * the radar is what is on screen — never while the detail layer, the Liste or
 * an overlay covers it — so a gap between two updates longer than this means
 * the radar was not visible, and the idle clock restarts. Without it, reading
 * the detail card for 40 s cost him the very aircraft he had opened it for,
 * breaking the promise in main.c (found in review, reproduced in test/sim). */
#define RADAR_OFFSCREEN_GAP_MS 5000

/* Selection ring. Its outer radius is the mark's nose reach at its altitude
 * band plus RADAR_SEL_RING_CLEAR (sel_ring_r()), so with a 2 px border its
 * inner edge clears the nose by 3 px at every rotation and every band: r=14,
 * 16 and 20 for the small, middle and large marks — close enough to read as
 * "this one", far enough not to touch the glyph it is pointing at. It is its
 * own object rather than part of mark_draw_cb because the largest ring (40 px)
 * does not fit inside the 34 px mark box, and LVGL clips a draw callback to
 * the object that owns it. */
#define RADAR_SEL_RING_R 16       /* at the middle band; see sel_ring_r() */
#define RADAR_SEL_RING_W 2
#define RADAR_SEL_RING_CLEAR 5    /* nose-to-ring-outer, px, at every band */


/* Level with the range read-out in the opposite corner. */
/* The clock's top edge, and the top chrome band for the whole device: nav.c's
 * signal meter is placed to land its feet on this text's baseline (SIG_TOP
 * there). Moving this moves that. */
#define RADAR_CLOCK_Y     24
#define RADAR_CAPTION_GAP 12

/* Invisible touch margin around each mark. A fingertip is ~10 mm; the mark is
 * a 16 px triangle. */
#define RADAR_MARK_TOUCH_PAD 11   /* + the 34 px box = the same 56 px as before */
#define RADAR_CAPTION_TOUCH_PAD 16

#define RADAR_NEAR_LABEL_COUNT 2
#define RADAR_LABEL_NAME_W     132
#define RADAR_LABEL_GAP_MARK   6
#define RADAR_LABEL_LINE_GAP   4
#define RADAR_CAPTION_ARROW_GAP 8
#define RADAR_CAPTION_PILL_PAD_H 10
/* The pill hugs the caption box: 1 px above and below it, not an even pad.
 * Above it is the S cardinal, whose ink ends at y ~387 (RADAR_CY +
 * RADAR_CARDINAL_R + 9) against a caption that starts at 394; below it are
 * the page dots at 464 against a caption box that ends at 462. Both were
 * measured in test/sim — the first version of the pill, evenly padded, went
 * through the bottom of the S. The text's own ink sits well inside its box,
 * so the pill still reads as padded. */
#define RADAR_CAPTION_PILL_PAD_TOP 1
#define RADAR_CAPTION_PILL_PAD_BOT 1

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
static lv_obj_t *s_lbl_clock;
static lv_obj_t *s_lbl_ident;      /* caption line 1: "AUA1Y · Airbus A321" */
static lv_obj_t *s_lbl_cardinal[4];
static lv_obj_t *s_home_outer, *s_home_inner;
static lv_obj_t *s_sel_ring;       /* rings the mark the caption is about */
static lv_obj_t *s_trail_layer;    /* one object, draws every trail */
static lv_obj_t *s_lbl_stale;      /* amber KEIN NETZ / KEINE DATEN */
static lv_obj_t *s_lbl_wait;       /* "Suche Flugzeuge...", no answer here yet */
static lv_obj_t *s_busy_wait;      /* its bar */
static lv_obj_t *s_cap_arrow;      /* the caption's "this goes somewhere" */
static lv_obj_t *s_cap_pill;       /* the caption's pressed state */

static int         s_sel_idx     = -1;   /* where the ring is committed */
static int         s_nearest_idx = -1;
static char        s_nearest_hex[sizeof ((aircraft_t *)0)->hex];
static int         s_rng_nm      = 1;
static net_state_t s_net         = NET_OK;
static bool        s_has_data    = false;  /* nothing polled yet (D88) */
static uint32_t    s_last_touch_ms;
static uint32_t    s_last_update_ms;
static radar_trails_t s_trails;

/* Per-mark drawing state, read by mark_draw_cb() out of the user_data
 * pointer each object was given at creation time. screen_radar_update()
 * writes this; it never touches the lv_obj_t's own style. */
typedef struct {
    lv_color_t color;
    float      heading_deg;
    float      scale;        /* altitude band, RADAR_SCALE_* */
    bool       has_track;
    bool       filled;
    bool       dim;          /* data is stale */
} radar_mark_state_t;

static lv_obj_t          *s_marks[MAX_AIRCRAFT];
static radar_mark_state_t s_mark_state[MAX_AIRCRAFT];

/* Which aircraft the caption is pointed at, by ICAO hex. Empty means "the
 * nearest", which is the default and what it falls back to when the chosen
 * aircraft leaves the ring. Held as a hex rather than an index because the
 * caller re-sorts the array between polls (main.c carries every fix forward,
 * which can change who is nearest) — an index would silently start naming a
 * different aircraft. */
static char s_caption_hex[sizeof ((aircraft_t *)0)->hex];

/* The caption text for every mark on screen, rendered during the update that
 * drew them. A tap can then repaint the caption IMMEDIATELY instead of
 * waiting up to two seconds for the next redraw — at which point he would
 * have tapped again, assuming he had missed. Costs ~1.4 KB of BSS and only
 * ever runs while this page is the visible one. */
static char s_cap_name[MAX_AIRCRAFT][40];
static char s_cap_dist[MAX_AIRCRAFT][24];
static char s_cap_hex[MAX_AIRCRAFT][sizeof ((aircraft_t *)0)->hex];
static char s_cap_id[MAX_AIRCRAFT][52];     /* line 1: who · model */
static char s_cap_who[MAX_AIRCRAFT][12];    /* callsign, or registration */
static int  s_cap_count;

static radar_select_cb s_select_cb;

/* Defined below place_caption(), which they all need. */
static void mark_clicked_cb(lv_event_t *e);
static void mark_pressed_cb(lv_event_t *e);
static void mark_released_cb(lv_event_t *e);
static void caption_clicked_cb(lv_event_t *e);
static void cont_pressed_cb(lv_event_t *e);
static void cont_clicked_cb(lv_event_t *e);
static void wire_caption_press(lv_obj_t *o);

void screen_radar_set_select_cb(radar_select_cb cb) { s_select_cb = cb; }

/* Deliberately touches nothing but this string. Its caller in main.c runs
 * OUTSIDE display_lock() (ui_task takes the lock further down), and the rule
 * that all LVGL calls happen on the display task behind the mutex is not one
 * this screen gets to bend for a tidier-looking ring. The ring catches up on
 * the next screen_radar_update(), which is under the lock — and the page is
 * not on screen at the moment this runs anyway. */
void screen_radar_clear_selection(void) { s_caption_hex[0] = '\0'; }

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
    float    k   = (st->scale > 0.0f) ? st->scale : 1.0f;
    lv_opa_t opa = st->dim ? RADAR_STALE_OPA : LV_OPA_COVER;

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
            { 0.0f,                              -(float)RADAR_MARK_NOSE * k },
            { -(float)RADAR_MARK_BASE_HALF * k,   (float)RADAR_MARK_TAIL * k },
            {  (float)RADAR_MARK_BASE_HALF * k,   (float)RADAR_MARK_TAIL * k },
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
            dsc.opa   = opa;
            lv_draw_triangle(layer, &dsc);
        } else {
            /* No route: same triangle, drawn hollow. Same colour as a routed
             * one since D76 — the outline IS the message. */
            lv_draw_line_dsc_t ldsc;
            lv_draw_line_dsc_init(&ldsc);
            ldsc.color       = st->color;
            ldsc.width       = 2;
            ldsc.opa         = opa;
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
            rdsc.bg_opa       = opa;
            rdsc.border_width = 0;
        } else {
            rdsc.bg_opa        = LV_OPA_TRANSP;
            rdsc.border_color = st->color;
            rdsc.border_opa   = opa;
            rdsc.border_width = 2;
        }
        float r = (float)RADAR_MARK_DOT_R * k;
        lv_area_t dot_area = {
            .x1 = (int32_t)(cx - r),
            .y1 = (int32_t)(cy - r),
            .x2 = (int32_t)(cx + r),
            .y2 = (int32_t)(cy + r),
        };
        lv_draw_rect(layer, &rdsc, &dot_area);
    }
}

/* Every trail on the scope, drawn by ONE object sized to the scope rather
 * than one per mark: a trail reaches up to a minute behind its aircraft, far
 * outside any per-mark box, and a draw callback is clipped to its own object.
 * Drawn BELOW the marks (created first), so a trail never covers the aircraft
 * it belongs to — or a neighbour.
 *
 * Positions come out of radar_logic as (distance, bearing) and go through
 * bearing_to_xy() like everything else on this screen, so there is still one
 * mapping and one place east and west could swap. */
static void trail_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t   cont;
    lv_obj_get_coords(s_cont, &cont);    /* s_cont-relative -> absolute */

    for (int i = 0; i < s_cap_count; i++) {
        if (!s_calc[i].valid) {
            continue;
        }
        const radar_trail_t *t = radar_trails_find(&s_trails, s_cap_hex[i]);
        if (t == NULL) {
            continue;
        }
        const radar_mark_state_t *st = &s_mark_state[i];
        for (int k = 0; k < t->count && k < RADAR_TRAIL_LEN; k++) {
            float r_px = (float)RADAR_R_OUTER * (t->fix[k].dst_nm / (float)s_rng_nm);
            if (r_px > (float)RADAR_R_OUTER) {
                r_px = (float)RADAR_R_OUTER;
            }
            float x, y;
            bearing_to_xy(t->fix[k].dir_deg, r_px, &x, &y);
            /* A fix taken moments ago sits under the mark itself. Drawing it
             * there only thickens the glyph. */
            float dx = x - s_calc[i].x, dy = y - s_calc[i].y;
            if (dx * dx + dy * dy < 36.0f) {
                continue;
            }
            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.radius       = LV_RADIUS_CIRCLE;
            d.bg_color     = st->color;
            d.bg_opa       = st->dim ? (lv_opa_t)(k_trail_opa[k] / 2) : k_trail_opa[k];
            d.border_width = 0;
            int32_t ax = cont.x1 + (int32_t)x, ay = cont.y1 + (int32_t)y;
            lv_area_t a = { ax - RADAR_TRAIL_DOT_R, ay - RADAR_TRAIL_DOT_R,
                            ax + RADAR_TRAIL_DOT_R, ay + RADAR_TRAIL_DOT_R };
            lv_draw_rect(layer, &d, &a);
        }
    }
}

/* Scenery must not eat a press. With the container now owning a click of its
 * own (tap the empty scope to let go of a selection), nav.c's
 * bubble_decorative() no longer walks in here — it stops at anything with a
 * callback — so every object on this screen that is only looked at is made
 * untouchable HERE, and the press falls through to the container. */
static void make_scenery(lv_obj_t *o)
{
    lv_obj_set_clickable(o, false);
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
    /* The empty scope is a target now: a tap on it lets go of the selection.
     * BUBBLE, so the press and the long press still reach nav.c's handlers
     * on the tile — Einstellungen is "long-press anywhere", and anywhere
     * includes here. CLICKED bubbles too, harmlessly: the tile has no click
     * handler. */
    lv_obj_set_clickable(s_cont, true);
    lv_obj_set_event_bubble(s_cont, true);
    lv_obj_add_event_cb(s_cont, cont_pressed_cb, LV_EVENT_PRESSED, NULL);
    /* SHORT_CLICKED, not CLICKED: LVGL 9 sends CLICKED on release even after
     * a long press, so holding the scope to open Einstellungen — or holding
     * past LVGL's 400 ms and thinking better of it — also let go of the
     * aircraft he had tapped. A hold is not a tap. */
    lv_obj_add_event_cb(s_cont, cont_clicked_cb, LV_EVENT_SHORT_CLICKED, NULL);

    /* --- Range rings — DESIGN.md §2 assigns these two tokens specifically:
     * THEME_HAIRLINE for the outer ring, THEME_HAIRLINE_DIM for the two
     * inner ones. --- */
    s_ring_inner = make_ring(s_cont, RADAR_R_INNER, RADAR_RING_W_INNER, THEME_HAIRLINE_DIM);
    s_ring_mid   = make_ring(s_cont, RADAR_R_MID, RADAR_RING_W_INNER, THEME_HAIRLINE_DIM);
    s_ring_outer = make_ring(s_cont, RADAR_R_OUTER, RADAR_RING_W_OUTER, THEME_HAIRLINE);
    make_scenery(s_ring_inner);
    make_scenery(s_ring_mid);
    make_scenery(s_ring_outer);

    /* --- Trails, one layer under every mark. Sized to the scope plus a dot,
     * not the whole panel, because it is invalidated on every update and the
     * panel is not the scope. --- */
    s_trail_layer = lv_obj_create(s_cont);
    lv_obj_remove_style_all(s_trail_layer);
    lv_obj_set_size(s_trail_layer, 2 * (RADAR_R_OUTER + RADAR_TRAIL_DOT_R + 1),
                    2 * (RADAR_R_OUTER + RADAR_TRAIL_DOT_R + 1));
    lv_obj_set_pos(s_trail_layer, RADAR_CX - RADAR_R_OUTER - RADAR_TRAIL_DOT_R - 1,
                   RADAR_CY - RADAR_R_OUTER - RADAR_TRAIL_DOT_R - 1);
    lv_obj_set_scrollable(s_trail_layer, false);
    make_scenery(s_trail_layer);
    lv_obj_add_event_cb(s_trail_layer, trail_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    radar_trails_reset(&s_trails);

    /* --- Outer-ring range readout, in km. Chrome tier — a scale reference,
     * never the thing he has to read up close — so it stays at the same
     * 13 px mono the compass tape already uses for its own tick labels
     * (widget_compass.c), which this screen has no compass tape of its
     * own to inherit the convention from otherwise. --- */
    s_lbl_km = make_label(s_cont, &plex_mono_13, THEME_TEXT_LABEL);


    /* Stale-data tag, top CENTRE, the one slot of the top row that is free
     * since the identity moved into the caption (D78). The two corners are
     * the range read-out and the clock, and a first version put this in the
     * left one, on top of the range: found by rendering it (test/sim), which
     * is the only reason it did not ship. Same face, same
     * colour and the same two words as the detail layer's, so "KEINE DATEN"
     * means one thing wherever it appears. Before D76 the radar — the default
     * screen — had no way to say it at all. */
    s_lbl_stale = make_label(s_cont, &plex_mono_13, THEME_AMBER);
    lv_obj_set_hidden(s_lbl_stale, true);

    /* The same slot, for the one other thing the top row may have to say:
     * there is no answer for this place yet (D82). An empty scope with no
     * words reads exactly like an empty sky, and after a move it was one
     * poll of the OLD place's sky and then that. Label grey, not amber —
     * nothing is wrong — with the device's one moving thing under it
     * (widget_busy.h), sized to the words like §5.2's route tag. Never
     * shown with the amber tag: that one is only up when the network is not
     * NET_OK, and then there is no request to wait for. */
    s_lbl_wait = make_label(s_cont, &plex_mono_13, THEME_TEXT_LABEL);
    lv_label_set_text(s_lbl_wait, STR_AIRCRAFT_SEARCHING);
    lv_obj_update_layout(s_lbl_wait);
    {
        int32_t w = lv_obj_get_width(s_lbl_wait);
        lv_obj_set_pos(s_lbl_wait, (THEME_SCREEN_WIDTH - w) / 2, RADAR_CLOCK_Y);
        s_busy_wait = widget_busy_create(s_cont, w);
        lv_obj_set_pos(s_busy_wait, (THEME_SCREEN_WIDTH - w) / 2,
                       RADAR_CLOCK_Y + lv_font_get_line_height(&plex_mono_13) + 2);
    }
    lv_obj_set_hidden(s_lbl_wait, true);

    /* The clock, balancing the range read-out across the top. */
    s_lbl_clock = make_label(s_cont, &plex_mono_13, THEME_TEXT_LABEL);
    lv_obj_set_hidden(s_lbl_clock, true);

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
    make_scenery(s_home_outer);
    make_scenery(s_home_inner);

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
        /* Touchable, with a generous invisible margin: the mark itself is a
         * ~16 px triangle and a fingertip is not. The extra area overlaps
         * between neighbouring marks, and LVGL hands the tap to the topmost —
         * acceptable, because tapping the wrong one of two aircraft sitting on
         * top of each other costs him one more tap, while a mark he cannot hit
         * at all costs him the feature. */
        lv_obj_set_clickable(m, true);
        lv_obj_set_ext_click_area(m, RADAR_MARK_TOUCH_PAD);
        lv_obj_add_event_cb(m, mark_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        /* Touch-down feedback: the ring jumps to the mark under the finger
         * the moment it lands, and goes back if the finger slides off into a
         * swipe. Every list row on this device answers a press with a fill
         * (THEME_SURFACE_SEL); a 16 px glyph has nothing to fill, so the ring
         * that is about to move there anyway is the fill. */
        lv_obj_add_event_cb(m, mark_pressed_cb, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(m, mark_released_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(m, mark_released_cb, LV_EVENT_PRESS_LOST, NULL);

        s_marks[i] = m;
    }

    /* --- Selection ring, created AFTER the marks so it draws over them: a
     * ring hidden behind a neighbouring mark is not a selection indicator.
     * THEME_WHITE, which is already one of DESIGN.md §2's six and so costs
     * the DO-257A ceiling nothing, and an enclosure rather than a seventh
     * colour precisely because the ceiling is a hard one.
     *
     * Explicitly NOT clickable. lv_obj_create() hands out
     * LV_OBJ_FLAG_CLICKABLE by default and this object sits directly on top
     * of a mark, so leaving the default would make the selected aircraft the
     * one aircraft on the scope he can no longer tap. --- */
    s_sel_ring = lv_obj_create(s_cont);
    lv_obj_remove_style_all(s_sel_ring);
    lv_obj_set_size(s_sel_ring, RADAR_SEL_RING_R * 2, RADAR_SEL_RING_R * 2);
    lv_obj_set_style_radius(s_sel_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_sel_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_sel_ring, RADAR_SEL_RING_W, 0);
    lv_obj_set_style_border_color(s_sel_ring, THEME_WHITE, 0);
    lv_obj_set_scrollable(s_sel_ring, false);
    lv_obj_set_clickable(s_sel_ring, false);
    lv_obj_set_hidden(s_sel_ring, true);

    /* The caption's pressed state: a pill behind the whole caption, shown
     * only while a finger is on it. THEME_SURFACE_SEL with a THEME_BORDER_IDLE
     * edge, which is the device's existing pressed/selected card and not a
     * new look. Created BEFORE the caption labels so it draws under them. */
    s_cap_pill = lv_obj_create(s_cont);
    lv_obj_remove_style_all(s_cap_pill);
    lv_obj_set_style_radius(s_cap_pill, 10, 0);
    lv_obj_set_style_bg_color(s_cap_pill, THEME_SURFACE_SEL, 0);
    lv_obj_set_style_bg_opa(s_cap_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_cap_pill, THEME_BORDER_IDLE, 0);
    lv_obj_set_style_border_width(s_cap_pill, 1, 0);
    lv_obj_set_scrollable(s_cap_pill, false);
    make_scenery(s_cap_pill);
    lv_obj_set_hidden(s_cap_pill, true);

    /* Caption line 1: who and what — the flight number (or registration) and
     * the model, "AUA1Y · Airbus A321" (D78). It lived in the top row at
     * 13 px tertiary until the owner asked for it here, above the distance,
     * which also settles AGENTS.md §8's open question 4 (was 13 px findable
     * from the chair?) by making it not 13 px. plex_sans_cond_25, the
     * smallest face that clears this screen's near floor (the type pass at
     * the top of this file), in THEME_TEXT_LABEL so line 2 — the answer —
     * stays the brighter line. Part of the caption's button. */
    s_lbl_ident = make_label(s_cont, &plex_sans_cond_25, THEME_TEXT_LABEL);
    lv_obj_set_clickable(s_lbl_ident, true);
    lv_obj_set_ext_click_area(s_lbl_ident, RADAR_CAPTION_TOUCH_PAD);
    lv_obj_add_event_cb(s_lbl_ident, caption_clicked_cb, LV_EVENT_CLICKED, NULL);
    wire_caption_press(s_lbl_ident);
    lv_obj_set_hidden(s_lbl_ident, true);

    /* --- The two nearest-aircraft labels — see this file's top comment for
     * why exactly two. Built once, hidden until the first update. --- */
    for (int i = 0; i < RADAR_NEAR_LABEL_COUNT; i++) {
        s_labels[i].name = make_wrapped_label(s_cont, &plex_sans_cond_25, THEME_TEXT_PRIMARY,
                                              RADAR_LABEL_NAME_W);
        s_labels[i].dist = make_label(s_cont, &plex_mono_32, THEME_CYAN);
        if (i == 0) {
            /* Both halves of the caption are the same button. The band is
             * 44 px tall, so the ext area only has to make it comfortably
             * wide, not taller. */
            lv_obj_set_clickable(s_labels[i].name, true);
            lv_obj_set_clickable(s_labels[i].dist, true);
            lv_obj_set_ext_click_area(s_labels[i].name, RADAR_CAPTION_TOUCH_PAD);
            lv_obj_set_ext_click_area(s_labels[i].dist, RADAR_CAPTION_TOUCH_PAD);
            lv_obj_add_event_cb(s_labels[i].name, caption_clicked_cb, LV_EVENT_CLICKED, NULL);
            lv_obj_add_event_cb(s_labels[i].dist, caption_clicked_cb, LV_EVENT_CLICKED, NULL);
            wire_caption_press(s_labels[i].name);
            wire_caption_press(s_labels[i].dist);
        }
        lv_obj_set_hidden(s_labels[i].name, true);
        lv_obj_set_hidden(s_labels[i].dist, true);
    }

    /* The caption's arrow — STR_ROW_ARROW, the same glyph, face and grey the
     * Einstellungen rows use for "this takes you somewhere". The caption does
     * take him somewhere (the detail layer) and until D76 said so only by
     * being the one thing on the screen made of words, which is not a signal
     * anywhere else on this device. Part of the same button. */
    s_cap_arrow = make_label(s_cont, &plex_sans_cond_25, THEME_TEXT_LABEL);
    lv_label_set_text(s_cap_arrow, STR_ROW_ARROW);
    lv_obj_set_clickable(s_cap_arrow, true);
    lv_obj_set_ext_click_area(s_cap_arrow, RADAR_CAPTION_TOUCH_PAD);
    lv_obj_add_event_cb(s_cap_arrow, caption_clicked_cb, LV_EVENT_CLICKED, NULL);
    wire_caption_press(s_cap_arrow);
    lv_obj_set_hidden(s_cap_arrow, true);
}

/* What the caption says about one aircraft, as two lines (D78):
 *
 *   line 1   who and what         "AUA1Y · Airbus A321"
 *   line 2   where, how far, way   "Frankfurt  16,0 km SO  ->"
 *
 * Line 2's name is the DESTINATION, and only when the route is known. A
 * route-less aircraft used to put its model there instead, where it competed
 * with the distance for width and lost (D50); its model is on line 1 now, so
 * line 2 is just the distance. One implementation, used both for the per-mark
 * cache and for the visible caption, so a tapped aircraft can never be
 * described differently from the nearest one.
 *
 * Line 1 follows identity.c's one rule: callsign first, registration where
 * there is no flight number, never both, never a raw ICAO designator. The
 * model falls back to "Unbekanntes Flugzeug" rather than to nothing, because
 * "we do not know what this is" is itself an answer (D46). */
static void build_caption(const aircraft_t *a, const route_t *routes, int n_routes,
                          char *id_out, size_t isz, char *who_out, size_t wsz,
                          char *name_out, size_t nsz, char *dist_out, size_t dsz)
{
    const route_t *rt   = route_find(routes, n_routes, a->flight);
    const char    *name = "";
    if (rt != NULL && rt->resolved) {
        name = airport_de(rt->dest_icao);
        if (name == NULL || name[0] == '\0') {
            name = rt->dest_city;         /* the API's own English name */
        }
    }
    snprintf(name_out, nsz, "%s", name);

    const char *who   = (a->flight[0] != '\0') ? a->flight : a->reg;
    const char *model = actype_display_name(a->type, a->category);
    if (model == NULL) {
        model = STR_UNKNOWN_AIRCRAFT;
    }
    snprintf(who_out, wsz, "%s", who);
    identity_compose(who, model, id_out, isz);

    size_t used = fmt_distance_km(a->dst_nm, dist_out, dsz);
    const char *dir = compass_de_abbr(a->dir_deg);
    if (dir != NULL && used + 1 < dsz) {
        snprintf(dist_out + used, dsz - used, " %s", dir);
    }
}

/* Sizes the pressed-state pill to whatever the caption turned out to be.
 * Positioned on every placement, shown only while pressed. */
static void place_caption_pill(int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_set_pos(s_cap_pill, x - RADAR_CAPTION_PILL_PAD_H, y - RADAR_CAPTION_PILL_PAD_TOP);
    lv_obj_set_size(s_cap_pill, w + 2 * RADAR_CAPTION_PILL_PAD_H,
                    h + RADAR_CAPTION_PILL_PAD_TOP + RADAR_CAPTION_PILL_PAD_BOT);
}

static int32_t text_w(const char *txt, const lv_font_t *font)
{
    lv_point_t p;
    lv_text_get_size(&p, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return p.x;
}

/* Paints and positions both lines of the caption. Split out of
 * screen_radar_update() so a tap on a mark repaints through exactly this path
 * — a second copy of the fit rules would be a second chance to get them
 * wrong. */
static void place_caption(const char *id, const char *who, const char *name, const char *dist)
{
    const int32_t avail = THEME_SCREEN_WIDTH - 2 * THEME_SIDE_PADDING;

    /* ---- line 1: who and what. If "callsign · model" is wider than the
     * panel — it takes a long model name to get there — the callsign alone
     * stays: it is what distinguishes this aircraft from the one next to it,
     * and the model is one tap away on the card. Never truncated, never
     * wrapped. ---- */
    const char *l1 = id;
    if (text_w(l1, &plex_sans_cond_25) > avail) {
        l1 = who;
    }
    bool show_l1 = (l1 != NULL && l1[0] != '\0');
    int32_t x1 = THEME_SCREEN_WIDTH / 2, w1 = 0;
    if (show_l1) {
        lv_label_set_text(s_lbl_ident, l1);
        lv_obj_update_layout(s_lbl_ident);
        w1 = lv_obj_get_width(s_lbl_ident);
        x1 = (THEME_SCREEN_WIDTH - w1) / 2;
        lv_obj_set_pos(s_lbl_ident, x1, RADAR_CAPTION_Y);
    }
    lv_obj_set_hidden(s_lbl_ident, !show_l1);

    /* ---- line 2: [destination]  distance direction  [->], and a ladder for
     * what gives way when it will not fit on one line:
     *
     *   1. name  distance  arrow      everything fits
     *   2. name  distance             the ARROW gives way first
     *   3.       distance  arrow      then the name (D50)
     *
     * The distance never yields (D48): the ring already says WHICH aircraft,
     * so how far and which way is the irreducible part. The arrow yields
     * before the name because the name is information and the arrow is a hint
     * he learns once (D76). Measured unwrapped, because a wrapped label
     * reports the width it was given rather than the width it wants. ---- */
    lv_label_set_text(s_labels[0].name, name);
    lv_label_set_text(s_labels[0].dist, dist);
    lv_obj_update_layout(s_labels[0].dist);
    lv_obj_update_layout(s_cap_arrow);
    const bool    has_name   = (name != NULL && name[0] != '\0');
    const int32_t nw         = has_name ? text_w(name, &plex_sans_cond_25) : 0;
    const int32_t dw         = lv_obj_get_width(s_labels[0].dist);
    const int32_t aw         = lv_obj_get_width(s_cap_arrow);
    const int32_t name_part  = has_name ? nw + RADAR_CAPTION_GAP : 0;
    const int32_t arrow_part = RADAR_CAPTION_ARROW_GAP + aw;

    bool show_name, show_arrow;
    if (name_part + dw + arrow_part <= avail) {
        show_name = has_name; show_arrow = true;
    } else if (name_part + dw <= avail) {
        show_name = has_name; show_arrow = false;
    } else {
        show_name = false;    show_arrow = true;
    }
    lv_obj_set_hidden(s_labels[0].name, !show_name);
    lv_obj_set_hidden(s_labels[0].dist, false);
    lv_obj_set_hidden(s_cap_arrow, !show_arrow);

    /* Give the label the width the text actually wants. It was created with a
     * fixed 132 px and LV_LABEL_LONG_MODE_WRAP, which is about eleven
     * characters at 25 px; now that the fit is measured, the measurement is
     * the cap. */
    if (show_name) {
        lv_obj_set_width(s_labels[0].name, nw);
        lv_obj_update_layout(s_labels[0].name);
    }

    int32_t nh = show_name ? lv_obj_get_height(s_labels[0].name) : 0;
    int32_t dh = lv_obj_get_height(s_labels[0].dist);
    int32_t ah = show_arrow ? lv_obj_get_height(s_cap_arrow) : 0;
    int32_t h  = dh;
    if (nh > h) h = nh;
    if (ah > h) h = ah;
    int32_t total = (show_name ? name_part : 0) + dw + (show_arrow ? arrow_part : 0);
    int32_t x = (THEME_SCREEN_WIDTH - total) / 2;
    if (x < THEME_SIDE_PADDING) x = THEME_SIDE_PADDING;
    const int32_t base = RADAR_CAPTION_Y + RADAR_CAPTION_LINE2_DY;

    int32_t cx = x;
    if (show_name) {
        lv_obj_set_pos(s_labels[0].name, cx, base + (h - nh) / 2);
        cx += name_part;
    }
    lv_obj_set_pos(s_labels[0].dist, cx, base + (h - dh) / 2);
    cx += dw;
    if (show_arrow) {
        lv_obj_set_pos(s_cap_arrow, cx + RADAR_CAPTION_ARROW_GAP, base + (h - ah) / 2);
    }

    /* The pill spans both lines: they are one button. */
    int32_t left  = show_l1 && x1 < x ? x1 : x;
    int32_t right = show_l1 && x1 + w1 > x + total ? x1 + w1 : x + total;
    place_caption_pill(left, RADAR_CAPTION_Y, right - left, base + h - RADAR_CAPTION_Y);
}


/* Puts the ring on mark `idx`, or takes it off the scope for idx < 0.
 *
 * Centred from s_calc[] rather than from the object's own coordinates, so it
 * uses the very same two numbers screen_radar_update() used to place the mark
 * — a ring computed a second way is a ring that can sit a pixel off the thing
 * it is circling, and on a glyph this small that reads as a rendering fault.
 *
 * s_calc[idx].valid is exactly the condition under which the mark itself was
 * unhidden, so this cannot ring an aircraft that is not on screen. */
/* The ring's radius for mark `idx`: the mark's own nose reach at its altitude
 * band, plus a fixed clearance. So a small cruise-altitude mark gets a tight
 * ring and a large low one gets a ring it fits inside — a single fixed ring
 * either swamps the small mark or cuts through the large one. Comes out at
 * exactly RADAR_SEL_RING_R for the middle band. */
static int32_t sel_ring_r(int idx)
{
    float k = s_mark_state[idx].scale > 0.0f ? s_mark_state[idx].scale : 1.0f;
    return (int32_t)ceilf((float)RADAR_MARK_NOSE * k) + RADAR_SEL_RING_CLEAR;
}

/* Draws the ring at `idx` without making it the selection. Used both for the
 * committed placement below and for the press preview, which must be able to
 * put it back where it was. */
static void ring_at(int idx)
{
    if (s_sel_ring == NULL) {
        return;
    }
    if (idx < 0 || idx >= s_cap_count || !s_calc[idx].valid) {
        lv_obj_set_hidden(s_sel_ring, true);
        return;
    }
    /* Draw order: the aircraft he is looking at goes above every other mark,
     * and the ring above it. Seen on the device — a neighbour later in the
     * feed drawn over the mark in question — so it is set here, where the
     * ring is placed, for taps, press previews and updates alike. Nothing
     * else on this screen overlaps the scope, so bringing these two to the
     * front changes nothing but the marks. Hit-testing follows, which is
     * right: the mark on top is the one a finger there should get. */
    lv_obj_move_foreground(s_marks[idx]);
    lv_obj_move_foreground(s_sel_ring);

    int32_t r = sel_ring_r(idx);
    lv_obj_set_size(s_sel_ring, 2 * r, 2 * r);
    lv_obj_set_pos(s_sel_ring, (int32_t)(s_calc[idx].x - (float)r),
                   (int32_t)(s_calc[idx].y - (float)r));
    lv_obj_set_hidden(s_sel_ring, false);
}

static void place_sel_ring(int idx)
{
    s_sel_idx = idx;
    ring_at(idx);
}

/* Any press on this screen counts as him still being here, for
 * RADAR_SELECTION_TIMEOUT_MS. */
static void note_touch(void)
{
    s_last_touch_ms = lv_tick_get();
}

/* Finger down on a mark: the ring goes there now, before he lifts. */
static void mark_pressed_cb(lv_event_t *e)
{
    note_touch();
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < s_cap_count && s_cap_hex[i][0] != '\0') {
        ring_at(i);
    }
}

/* Finger up, or slid off into a swipe: put the ring back on the committed
 * selection. If it was a tap, CLICKED arrives right after this (LVGL sends
 * RELEASED first) and commits the new position before the next frame is
 * rendered, so there is no visible hop back and forth. */
static void mark_released_cb(lv_event_t *e)
{
    (void)e;
    ring_at(s_sel_idx);
}

/* Tap on the empty scope: let go of whatever he had tapped, caption and ring
 * back to the nearest. The gesture had no meaning before and this is the one
 * it has everywhere else — tap outside to deselect. */
static void cont_pressed_cb(lv_event_t *e)
{
    (void)e;
    note_touch();
}

static void clear_to_nearest(void)
{
    s_caption_hex[0] = '\0';
    int n = s_nearest_idx;
    if (n < 0 || n >= s_cap_count || !s_calc[n].valid) {
        return;          /* nothing on the scope; the next update settles it */
    }
    place_caption(s_cap_id[n], s_cap_who[n], s_cap_name[n], s_cap_dist[n]);
    place_sel_ring(n);
}

static void cont_clicked_cb(lv_event_t *e)
{
    /* Only a click that landed on the container itself. Children that own
     * their taps do not bubble, so this is belt and braces — but a bubbled
     * click from some future child would otherwise undo the very selection
     * that child just made. */
    if (lv_event_get_target_obj(e) != s_cont) {
        return;
    }
    if (s_caption_hex[0] != '\0') {
        clear_to_nearest();
    }
}

static void caption_pressed_cb(lv_event_t *e)
{
    (void)e;
    note_touch();
    lv_obj_set_hidden(s_cap_pill, false);
}

static void caption_released_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_set_hidden(s_cap_pill, true);
}

/* Name, distance and arrow are three labels and one button: a press on any of
 * them lights the pill behind all three. */
static void wire_caption_press(lv_obj_t *o)
{
    lv_obj_add_event_cb(o, caption_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(o, caption_released_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(o, caption_released_cb, LV_EVENT_PRESS_LOST, NULL);
}

/* A tap on a mark re-points the caption. It does NOT leave the screen: he is
 * looking at the scope, and answering "which one is that" by throwing him onto
 * another page would be the wrong trade. Repaints immediately from the cache
 * rather than waiting for the next redraw — up to two seconds of nothing
 * happening reads as a missed tap, and he taps again. */
static void mark_clicked_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= s_cap_count || s_cap_hex[i][0] == '\0') {
        return;
    }
    note_touch();
    memcpy(s_caption_hex, s_cap_hex[i], sizeof s_caption_hex);
    place_caption(s_cap_id[i], s_cap_who[i], s_cap_name[i], s_cap_dist[i]);
    /* Same tick as the caption, for the same reason the caption does not wait
     * for the next poll: feedback that arrives up to twelve seconds after the
     * finger lifts is feedback he has already given up on. */
    place_sel_ring(i);
}

/* A tap on the caption commits: it asks for the full view of whatever the
 * caption currently names. The caption is the only thing on this screen with
 * words on it, which is what makes it the only thing that reads as "press me
 * for more". */
static void caption_clicked_cb(lv_event_t *e)
{
    (void)e;
    if (s_select_cb == NULL) {
        return;
    }
    /* Whatever the caption is showing: the tapped aircraft if there is one,
     * otherwise the nearest. NOT index 0: that is the caller's nearest, and
     * since D76 the radar's nearest is sticky and can be a different
     * aircraft for a poll or two — the caption names s_nearest_idx, so the
     * tap must open s_nearest_idx, or he reads one name and gets another. */
    const char *hex = (s_caption_hex[0] != '\0') ? s_caption_hex
                    : ((s_nearest_idx >= 0 && s_nearest_idx < s_cap_count)
                           ? s_cap_hex[s_nearest_idx] : NULL);
    if (hex != NULL && hex[0] != '\0') {
        s_select_cb(hex);
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
    s_rng_nm   = rng_nm;
    bool stale = (s_net != NET_OK);
    uint32_t now_ms = lv_tick_get();

    /* Back on screen after something covered it: time away was not idle time
     * on this screen (RADAR_OFFSCREEN_GAP_MS). */
    if (lv_tick_elaps(s_last_update_ms) > RADAR_OFFSCREEN_GAP_MS) {
        s_last_touch_ms = now_ms;
    }
    s_last_update_ms = now_ms;

    /* --- Stale data, said in words: the same two tags, in the same amber,
     * as the detail layer. --- */
    if (stale) {
        lv_label_set_text(s_lbl_stale, (s_net == NET_NO_WIFI) ? STR_NO_NETWORK_TAG
                                                               : STR_NO_DATA_TAG);
        lv_obj_update_layout(s_lbl_stale);
        lv_obj_set_pos(s_lbl_stale, (THEME_SCREEN_WIDTH - lv_obj_get_width(s_lbl_stale)) / 2,
                       RADAR_CLOCK_Y);
    }
    lv_obj_set_hidden(s_lbl_stale, !stale);

    bool waiting = !stale && !s_has_data && n == 0;
    lv_obj_set_hidden(s_lbl_wait, !waiting);
    widget_busy_set_active(s_busy_wait, waiting);

    /* --- Trails. Recorded only while the data is live. While stale the
     * positions are frozen, and a fix taken then is stamped "now" for a
     * position that is really minutes old — harmless while it sits under the
     * mark, but when the data comes back and a SLOW aircraft turns out to
     * have moved a few pixels, that fix is drawn as the newest trail dot at a
     * spot the aircraft left long ago (a fast one trips radar_logic's jump
     * guard instead). So while stale the trail is only aged: it fades out,
     * which is itself a sign the scope has stopped moving, and starts clean
     * when the data returns. test/sim checks exactly this case. --- */
    if (stale) {
        radar_trails_age(&s_trails, now_ms);
    } else {
        radar_trails_observe(&s_trails, ac, n, now_ms);
    }
    lv_obj_invalidate(s_trail_layer);

    /* --- Outer-ring range readout, in km. The panel speaks km everywhere
     * (AGENTS.md §1); fmt_distance_km() owns both the unit conversion and
     * the German number formatting — this file only asks for it. --- */
    char km_buf[24];
    fmt_distance_km((float)radius_nm, km_buf, sizeof km_buf);
    lv_label_set_text(s_lbl_km, km_buf);
    /* Top left, level with the clock. Stated outright since D76, because it
     * used to happen by accident: the code asked for bearing 135 (SO, down by
     * the scope) and then "clamped it into the panel" with lv_obj_get_x/y —
     * which read the coordinates of the LAST LAYOUT PASS, not the position
     * just set. A label never laid out reads (0,0), the clamp pushed that to
     * (20,20), and there it stayed on every update after. Everything else in
     * the top row — the clock's "opposite corner", the identity's neighbour
     * check, nav.c's signal meter — had long since been built around the
     * accident, so the accident is what is now written down. */
    lv_obj_set_pos(s_lbl_km, THEME_SIDE_PADDING, RADAR_CLOCK_Y);

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

    /* --- Nearest, by real distance (not screen geometry), with hysteresis:
     * last poll's nearest keeps the magenta mark through a near-tie, so two
     * aircraft at the same range do not swap it back and forth on their own
     * (radar_logic.h, D76). The same `valid` rule as s_calc above — both are
     * dst_nm >= 0 — so the index is always one that has a mark. --- */
    int nearest_idx = radar_pick_nearest(ac, n, s_nearest_hex);
    s_nearest_idx   = nearest_idx;
    if (nearest_idx >= 0) {
        memcpy(s_nearest_hex, ac[nearest_idx].hex, sizeof s_nearest_hex);
    } else {
        s_nearest_hex[0] = '\0';
    }

    /* --- Marks: colour + shape per aircraft. See this file's top comment
     * for the full table and the never-colour-alone reasoning. --- */
    for (int i = 0; i < MAX_AIRCRAFT; i++) {
        if (i >= n || !s_calc[i].valid) {
            lv_obj_set_hidden(s_marks[i], true);
            s_cap_hex[i][0] = '\0';      /* hidden marks are not tappable */
            s_cap_id[i][0]  = '\0';
            s_cap_who[i][0] = '\0';
            continue;
        }
        radar_calc_t *c = &s_calc[i];
        /* Magenta for the nearest, cyan for everyone else. Route or no route
         * is the fill, not the colour — see this file's top comment, D76. */
        lv_color_t color = (i == nearest_idx) ? THEME_MAGENTA : THEME_CYAN;

        static const float k_band_scale[] = {
            [RADAR_ALT_LOW]  = RADAR_SCALE_LOW,
            [RADAR_ALT_MID]  = RADAR_SCALE_MID,
            [RADAR_ALT_HIGH] = RADAR_SCALE_HIGH,
        };
        s_mark_state[i].color       = color;
        s_mark_state[i].has_track   = c->has_track;
        s_mark_state[i].heading_deg = c->track_deg;
        s_mark_state[i].filled      = c->has_route;
        s_mark_state[i].scale       = k_band_scale[radar_alt_band(ac[i].alt_ft)];
        s_mark_state[i].dim         = stale;

        /* Cache what this mark's caption would say, so a tap can answer at
         * once. Only for aircraft actually on screen: an index that is hidden
         * must never be tappable. */
        build_caption(&ac[i], routes, n_routes,
                      s_cap_id[i], sizeof s_cap_id[i],
                      s_cap_who[i], sizeof s_cap_who[i],
                      s_cap_name[i], sizeof s_cap_name[i],
                      s_cap_dist[i], sizeof s_cap_dist[i]);
        snprintf(s_cap_hex[i], sizeof s_cap_hex[i], "%s", ac[i].hex);

        lv_obj_set_pos(s_marks[i], (int32_t)(c->x - (float)RADAR_MARK_BOX / 2.0f),
                       (int32_t)(c->y - (float)RADAR_MARK_BOX / 2.0f));
        lv_obj_set_hidden(s_marks[i], false);
        /* Position may be unchanged from the previous poll while colour or
         * shape changed (e.g. a route resolved between polls) — force the
         * redraw rather than relying on lv_obj_set_pos() alone to catch it. */
        lv_obj_invalidate(s_marks[i]);
    }

    s_cap_count = (n < MAX_AIRCRAFT) ? n : MAX_AIRCRAFT;

    /* The nearest draws above every other mark. Marks used to draw in feed
     * order, and on the panel (2026-09-24 23:31) the magenta nearest sat
     * half-hidden under a filled cyan dot that came later in the array —
     * the one aircraft this scope singles out, covered by one it does not.
     * If he has tapped another aircraft, place_sel_ring() below brings that
     * one (and the ring) in front of this. */
    if (nearest_idx >= 0) {
        lv_obj_move_foreground(s_marks[nearest_idx]);
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
        lv_obj_set_hidden(s_cap_arrow, true);
        lv_obj_set_hidden(s_cap_pill, true);
        lv_obj_set_hidden(s_lbl_ident, true);   /* nothing captioned, nothing to name */
        place_sel_ring(-1);            /* ...and nothing to ring */
        return;
    }

    /* A selection nobody has touched the screen about for 30 s is let go.
     * Unsigned elapsed time, so the tick wrapping after 49 days is not a
     * selection that expires instantly or never. */
    if (s_caption_hex[0] != '\0' &&
        lv_tick_elaps(s_last_touch_ms) > RADAR_SELECTION_TIMEOUT_MS) {
        s_caption_hex[0] = '\0';
    }

    /* Which aircraft gets the caption: the one he tapped, if it is still up
     * there, otherwise the nearest. */
    int cap_idx = nearest_idx;
    if (s_caption_hex[0] != '\0') {
        cap_idx = -1;
        for (int i = 0; i < n; i++) {
            if (s_calc[i].valid && strcmp(ac[i].hex, s_caption_hex) == 0) {
                cap_idx = i;
                break;
            }
        }
        if (cap_idx < 0) {
            /* It left the ring. Fall back rather than captioning nothing, and
             * forget the selection so it cannot come back if the same hex
             * reappears an hour later. */
            s_caption_hex[0] = '\0';
            cap_idx = nearest_idx;
        }
    }

    /* The cache filled above already holds exactly this aircraft's text, from
     * the same build_caption() call — no second build. */
    place_caption(s_cap_id[cap_idx], s_cap_who[cap_idx], s_cap_name[cap_idx],
                  s_cap_dist[cap_idx]);
    /* The ring follows the caption, including when the caption fell back to
     * the nearest because the tapped aircraft left the ring. One subject, one
     * ring, decided in one place. */
    place_sel_ring(cap_idx);
}

void screen_radar_set_net(net_state_t net)
{
    s_net = net;
}

void screen_radar_set_has_data(bool has_data)
{
    s_has_data = has_data;
}

void screen_radar_forget_place(void)
{
    radar_trails_reset(&s_trails);
    s_caption_hex[0] = '\0';
    s_nearest_hex[0] = '\0';
}

void screen_radar_set_clock(const char *hhmm)
{
    if (s_lbl_clock == NULL) {
        return;
    }
    if (hhmm == NULL || hhmm[0] == '\0') {
        lv_obj_set_hidden(s_lbl_clock, true);
        return;
    }
    lv_label_set_text(s_lbl_clock, hhmm);
    lv_obj_update_layout(s_lbl_clock);
    /* Right-aligned to the panel edge MINUS the chrome slot: nav.c draws the
     * device's signal meter in that corner, on the root, above this page
     * (widget_signal.h). Without the subtraction the clock is drawn straight
     * underneath it — both are chrome, both are right-aligned, and the one on
     * top wins. */
    lv_obj_set_pos(s_lbl_clock,
                   THEME_SCREEN_WIDTH - THEME_SIDE_PADDING - WIDGET_SIGNAL_CHROME_SLOT -
                       lv_obj_get_width(s_lbl_clock),
                   RADAR_CLOCK_Y);
    lv_obj_set_hidden(s_lbl_clock, false);
}
