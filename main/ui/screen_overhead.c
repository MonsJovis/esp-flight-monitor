/* screen_overhead.c — see screen_overhead.h for the contract.
 *
 * Layout (DESIGN.md §4 vertical band structure, 20 px side padding, 8 px
 * base unit): chrome -> compass tape -> hero -> supporting -> data. The
 * bands below the hero are positioned dynamically, using the hero's actual
 * rendered height, because the hero's height is the one thing that changes
 * with the auto-shrink ladder (DESIGN.md §3) — everything else uses a fixed
 * font, so its height is constant and the stacking below never collides.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "screen_overhead.h"
#include "theme.h"
#include "fonts/fonts.h"
#include "widget_compass.h"
#include "widget_busy.h"
#include "strings_de.h"
#include "data/fmt_de.h"

/* Every German literal this screen shows lives in main/strings_de.h, together
 * with the reasoning for each one; tools/check_strings.py fails the build if
 * one reappears here. Everything else that reaches the screen is a string
 * already sitting in view_model_t, put there in German, with correct units,
 * by main/data/view_build.c (view_model.h, AGENTS.md §10).
 *
 * The compass tape's eight cardinal marks are NOT a literal here either. They
 * come from compass_de_abbr() at the eight 45-degree bearings, so the "O for
 * Ost, never E" rule (AGENTS.md §1, §10) has exactly one implementation on
 * this device instead of one per screen that draws a compass. That is the
 * DECISIONS.md D36 lesson applied before it can bite a second time.
 */

/* ============================================================================
 * Layout constants — px, on the 8 px base unit (THEME_BASE_UNIT).
 * ============================================================================
 */
#define PAD       THEME_SIDE_PADDING                         /* 20 */
#define CONTENT_W (THEME_SCREEN_WIDTH - 2 * THEME_SIDE_PADDING) /* 440 */

#define GAP_SM 8   /* between two lines that belong to the same idea */
#define GAP_MD 16  /* between two different ideas (bands) */

#define Y_CHROME  16
#define Y_COMPASS 40
/* Top row (origin+arrow, or the "KEIN FLUGPLAN" tag) starts right after the
 * compass tape's fixed height. */
#define Y_TOPROW (Y_COMPASS + WIDGET_COMPASS_HEIGHT + GAP_MD)

/* The hero ladder — DESIGN.md §3's auto-shrink, largest first. */
static const struct {
    const lv_font_t *font;
    int32_t          px;
} HERO_LADDER[] = {
    { &plex_sans_cond_100, 100 },
    { &plex_sans_cond_76,  76  },
    { &plex_sans_cond_56,  56  },
};
#define HERO_LADDER_LEN (sizeof HERO_LADDER / sizeof HERO_LADDER[0])

/* ============================================================================
 * Widget tree — built once by screen_overhead_create(), single instance
 * (this device shows exactly one overhead screen), so plain file-scope
 * statics rather than a heap-allocated context. Matches the rest of this
 * codebase (main/main.c, main/debug/dbg_screen.c).
 * ============================================================================
 */
static lv_obj_t *s_cont;

/* True only while this screen's widgets exist — the same guard screen_wifi.c
 * and screen_geo.c carry, arriving here late because nothing used to write
 * into this screen from outside the UI task.
 *
 * Something does now: the fixture commands on the serial console. This screen
 * is only ever built as the detail layer (main.c), so lv_obj_clean() on the
 * active screen destroys it and leaves every pointer in this file dangling —
 * and dbg_fixture_show() then calls lv_label_set_text() on a freed label.
 * Measured, from the M11 stress run: a LoadProhibited inside
 * lv_label_mark_need_refr_text(). */
static bool s_alive;

/* Chrome band */
static lv_obj_t *s_lbl_clock;

/* Set while this screen is the detail layer: see screen_overhead.h. */
static void (*s_back_cb)(void);

static void back_tapped_cb(lv_event_t *e)
{
    (void)e;
    if (s_back_cb != NULL) {
        s_back_cb();
    }
}
static lv_obj_t *s_lbl_offline;
static lv_obj_t *s_lbl_identity;   /* "AUA453 · Airbus A320neo", chrome row, centred */ /* the network caution; hidden when vm->net == NET_OK */

/* Compass tape band */
static lv_obj_t *s_compass;

/* Top row: either origin+arrow (the route), or the no-route tag. */
static lv_obj_t *s_lbl_origin;
static lv_obj_t *s_lbl_arrow;
static lv_obj_t *s_lbl_no_route_tag;
/* The sweep under the no-route tag while the route lookup is on the wire.
 * Only ever visible with that tag, and only in its "still looking" reading —
 * see the update path. */
static lv_obj_t *s_busy_route;

/* Hero band */
static lv_obj_t *s_lbl_hero;

/* The route lookup's skeleton (D84): ghosts where the origin and the
 * destination will land, in the shape the answer will have. */
static lv_obj_t *s_ghost_origin;
static lv_obj_t *s_ghost_hero;
static int32_t   s_ghost_hero_lh;   /* the hero face's line box the ghost stands in */

/* The rest of the card's skeleton, up only between create() and the first
 * update (D85): the two supporting lines and the data band's two rows. */
static lv_obj_t *s_ghost_line[2];
static lv_obj_t *s_ghost_band[2];
static bool      s_skeleton;
#define GHOST_ORIGIN_W  (CONTENT_W * 30 / 100)
#define GHOST_HERO_W    (CONTENT_W * 62 / 100)
/* An x-height of the hero face. A constant, not lv_obj_get_height(): right
 * after create() nothing has been laid out and that reads 0. */
#define GHOST_HERO_H    (s_ghost_hero_lh * 2 / 5)

/* Supporting band */
static lv_obj_t *s_lbl_reason;             /* §5.2 only */
/* §5.1 only, under the destination: vm->route_line — "Landung in etwa 45
 * Min.", or while climbing out "16 km von Wien entfernt" (D79). */
static lv_obj_t *s_lbl_route_line;
static lv_obj_t *s_lbl_date;               /* §5.3 only */
static lv_obj_t *s_lbl_last_seen_caption;  /* §5.3 only, STR_LAST_SEEN */
static lv_obj_t *s_lbl_airline;            /* §5.1/§5.2, reused as "last seen" airline in §5.3 */
static lv_obj_t *s_lbl_type_full;          /* §5.1/§5.2, reused as "last seen" type in §5.3 */

/* Data band — §5.1/§5.2 only */
static lv_obj_t *s_lbl_altitude;
static lv_obj_t *s_lbl_speed;              /* beside the altitude: "780 km/h" (D79) */
static lv_obj_t *s_lbl_distance;
static lv_obj_t *s_lbl_direction_word;

/* Exposed for the integrator's M3 log line (screen_overhead.h). Starts at
 * 100 so an unread value never looks like a false "it shrank" report. */
static int32_t s_last_hero_px = 100;

static inline void set_hidden(lv_obj_t *obj, bool hidden)
{
    lv_obj_set_hidden(obj, hidden);
}

/* DESIGN.md §3 hero auto-shrink: measure the RENDERED, unwrapped width of
 * `text` against each face in the ladder and step 100 -> 76 -> 56 until one
 * fits `max_width`. Falls through to the smallest face rather than ever
 * truncating — DESIGN.md is explicit that a half-name is worse than a
 * smaller one. */
static const lv_font_t *pick_hero_font(const char *text, int32_t max_width, int32_t *out_px)
{
    for (size_t i = 0; i < HERO_LADDER_LEN; i++) {
        lv_point_t sz;
        lv_text_get_size(&sz, text, HERO_LADDER[i].font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        bool last = (i == HERO_LADDER_LEN - 1);
        if (sz.x <= max_width || last) {
            if (out_px) {
                *out_px = HERO_LADDER[i].px;
            }
            return HERO_LADDER[i].font;
        }
    }
    if (out_px) {
        *out_px = HERO_LADDER[0].px; /* unreachable: HERO_LADDER_LEN > 0 */
    }
    return HERO_LADDER[0].font;
}

/* Creates a label with fixed styling in one call — every label on this
 * screen wants a font and a colour token, nothing else varies at creation
 * time (position and text are set per-update). */
static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}

/* Same, but wrapped to CONTENT_W — for anything that carries a name or a
 * full sentence and must never run past 480 px (task brief: "make sure a
 * long German city name and a long airline name do not overflow"). */
static lv_obj_t *make_wrapped_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = make_label(parent, font, color);
    lv_obj_set_width(l, CONTENT_W);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    return l;
}

/* The card as it looks before it knows anything (D85). It used to be the
 * compass tape pointing north and nothing else — no aircraft, not even the
 * way back — for up to the two seconds until ui_task's next tick. Now it is
 * the answer's shape: every line a ghost, the bar under the origin's, and
 * "Zurück" where it always is, so the card can be left before it has loaded.
 * The first screen_overhead_update() takes it all down. */
static void show_skeleton(void)
{
    s_skeleton = true;
    for (uint32_t i = 0; i < lv_obj_get_child_count(s_cont); i++) {
        lv_obj_set_hidden(lv_obj_get_child(s_cont, (int32_t)i), true);
    }
    lv_obj_set_hidden(s_ghost_origin, false);
    lv_obj_set_hidden(s_ghost_hero, false);
    lv_obj_set_pos(s_ghost_hero, PAD,
                   Y_TOPROW + lv_font_get_line_height(&plex_sans_cond_34) + GAP_SM
                   + (s_ghost_hero_lh - GHOST_HERO_H) / 2
                   + s_ghost_hero_lh / 10);
    for (int i = 0; i < 2; i++) {
        lv_obj_set_hidden(s_ghost_line[i], false);
        lv_obj_set_hidden(s_ghost_band[i], false);
    }
    lv_obj_set_width(s_busy_route, GHOST_ORIGIN_W);
    lv_obj_set_hidden(s_busy_route, false);
    widget_busy_set_active(s_busy_route, true);
}

static void hide_skeleton_extras(void)
{
    s_skeleton = false;
    for (int i = 0; i < 2; i++) {
        lv_obj_set_hidden(s_ghost_line[i], true);
        lv_obj_set_hidden(s_ghost_band[i], true);
    }
}

static void on_cont_deleted(lv_event_t *e)
{
    (void)e;
    s_alive = false;
    s_cont  = NULL;
}

void screen_overhead_create(lv_obj_t *parent)
{
    s_cont = lv_obj_create(parent);
    lv_obj_add_event_cb(s_cont, on_cont_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_remove_style_all(s_cont);
    lv_obj_set_size(s_cont, THEME_SCREEN_WIDTH, THEME_SCREEN_HEIGHT);
    lv_obj_set_pos(s_cont, 0, 0);
    lv_obj_set_style_bg_color(s_cont, THEME_GROUND, 0);
    lv_obj_set_style_bg_opa(s_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_cont, 0, 0);
    lv_obj_set_style_border_width(s_cont, 0, 0);
    lv_obj_set_scrollable(s_cont, false);

    /* --- Chrome band -- present in every state (DESIGN.md §4: "consistent
     * across all seven screens so the eye learns one map"). --- */
    s_lbl_clock = make_label(s_cont, &plex_mono_13, THEME_TEXT_LABEL);
    lv_obj_set_pos(s_lbl_clock, PAD, Y_CHROME);

    /* Which aircraft this is, in the empty middle of the chrome row.
     *
     * It goes here rather than into the body because the body is a ladder of
     * priorities that already yields under pressure (D48) — and this line must
     * not. It is the one he needs to look the aircraft up afterwards, it is
     * the only place the flight number appears at all, and the hero shrinking
     * from 100 px to 56 px must not take it away.
     *
     * Tertiary tier, centred between "Zurück" on the left and the network
     * caution on the right: findable, never competing with the destination,
     * and in the same place on every aircraft.
     *
     * Carries the AIRLINE since the swap; the identity it used to hold is now
     * in the body, where he asked for it. (The Radar's identity left its top
     * row too, for the caption's first line — D78.) */
    s_lbl_identity = make_label(s_cont, &plex_mono_13, THEME_TEXT_TERTIARY);
    lv_obj_set_hidden(s_lbl_identity, true);

    s_lbl_offline = make_label(s_cont, &plex_mono_13, THEME_AMBER);
    lv_label_set_text(s_lbl_offline, STR_NO_NETWORK_TAG);   /* text set per update */
    lv_obj_update_layout(s_lbl_offline);
    lv_obj_set_pos(s_lbl_offline, PAD + CONTENT_W - lv_obj_get_width(s_lbl_offline), Y_CHROME);
    lv_obj_set_hidden(s_lbl_offline, true); /* shown only when vm->net != NET_OK */

    /* --- Compass tape band --- */
    /* Built here, not stored as a file-scope table, because compass_de_abbr()
     * is a function call and this is the one place the eight marks are
     * needed. widget_compass_create() copies the text into its own labels. */
    const char *cardinals[WIDGET_COMPASS_NUM_CARDINALS];
    for (int i = 0; i < WIDGET_COMPASS_NUM_CARDINALS; i++) {
        cardinals[i] = compass_de_abbr((float)i * (360.0f / WIDGET_COMPASS_NUM_CARDINALS));
    }
    /* The cast adds const at the second level, which C will not do
     * implicitly even though it is safe (C11 6.5.16.1); the callee only
     * reads. */
    s_compass = widget_compass_create(s_cont, CONTENT_W,
                                      (const char *const *)cardinals);
    lv_obj_set_pos(s_compass, PAD, Y_COMPASS);

    /* --- Top row: origin + route arrow, or the no-route tag --- */
    s_lbl_origin = make_label(s_cont, &plex_sans_cond_34, THEME_TEXT_PRIMARY);
    s_lbl_arrow  = make_label(s_cont, &plex_sans_cond_34, THEME_MAGENTA);
    lv_label_set_text(s_lbl_arrow, STR_ROUTE_ARROW);
    s_lbl_no_route_tag = make_label(s_cont, &plex_sans_cond_34, THEME_AMBER);
    lv_label_set_text(s_lbl_no_route_tag, STR_NO_FLIGHT_PLAN); /* replaced per-update */
    lv_obj_set_hidden(s_lbl_origin, true);
    lv_obj_set_hidden(s_lbl_arrow, true);
    lv_obj_set_hidden(s_lbl_no_route_tag, true);

    /* The bar lives in the GAP_SM between the top row and the hero, not in
     * space of its own: this screen's whole layout rule is that the hero
     * starts at the same Y whatever the top row is saying, so that the panel
     * does not jump when traffic appears. Four pixels of the eight-pixel gap,
     * two above and two below, and the hero does not move.
     *
     * Width is the tag's, set per update — a bar the full 440 px under a
     * 300 px tag would read as a progress bar for the whole screen rather
     * than as this one sentence still working. */
    s_busy_route = widget_busy_create(s_cont, CONTENT_W);
    lv_obj_set_pos(s_busy_route, PAD,
                   Y_TOPROW + lv_font_get_line_height(&plex_sans_cond_34) + 2);

    /* --- Hero band -- the single most important thing on the panel --- */
    s_lbl_hero = make_wrapped_label(s_cont, &plex_sans_cond_100, THEME_WHITE);

    /* --- The route lookup's skeleton (D84) ---
     *
     * While the route is being looked up this card used to put the model
     * name in the destination's place under an amber "ROUTE WIRD GESUCHT" —
     * so the headline changed from "Airbus A321" to "Frankfurt" when the
     * answer landed, and the one colour that means "caution" was standing in
     * for "one moment". Now the card takes the ANSWER's shape: a ghost where
     * "Wien →" goes, a ghost where "Frankfurt" goes, the bar between them,
     * and the sentence under it saying what is happening (DESIGN.md §4). The
     * model is not lost: the identity line carries it (view_build.c).
     *
     * Ghost heights are an x-height, not the line box: a bar as tall as the
     * line reads as a redaction. Widths are a short city and a longer one,
     * uneven so they read as text that has not arrived. The hero ghost's y is
     * set per update, because the hero's y is. */
    {
        int32_t lh34 = lv_font_get_line_height(&plex_sans_cond_34);
        int32_t gh34 = lh34 / 2;
        s_ghost_origin = widget_busy_ghost(s_cont, PAD, Y_TOPROW + (lh34 - gh34) / 2,
                                           GHOST_ORIGIN_W, gh34, false);
        lv_obj_set_hidden(s_ghost_origin, true);

        s_ghost_hero_lh = lv_font_get_line_height(&plex_sans_cond_100);
        s_ghost_hero = widget_busy_ghost(s_cont, PAD, 0, GHOST_HERO_W,
                                         GHOST_HERO_H, false);
        lv_obj_set_hidden(s_ghost_hero, true);

        /* The whole card, for the moment between the tap and the first
         * update (D85). Where each line of a routed card lands: the route
         * line and the identity under the hero, the altitude and distance
         * rows anchored to the bottom edge exactly as the real band is. */
        int32_t y_hero = Y_TOPROW + lh34 + GAP_SM;
        int32_t lh25   = lv_font_get_line_height(&plex_sans_cond_25);
        int32_t y_line = y_hero + s_ghost_hero_lh + GAP_SM;
        s_ghost_line[0] = widget_busy_ghost(s_cont, PAD, y_line + lh25 / 4,
                                            CONTENT_W * 56 / 100, lh25 / 2, false);
        s_ghost_line[1] = widget_busy_ghost(s_cont, PAD, y_line + lh25 + GAP_SM + lh25 / 4,
                                            CONTENT_W * 42 / 100, lh25 / 2, true);
        int32_t lh32   = lv_font_get_line_height(&plex_mono_32);
        int32_t y_band = THEME_SCREEN_HEIGHT - PAD - (2 * lh32 + GAP_SM);
        s_ghost_band[0] = widget_busy_ghost(s_cont, PAD, y_band + lh32 / 4,
                                            CONTENT_W * 48 / 100, lh32 / 2, false);
        s_ghost_band[1] = widget_busy_ghost(s_cont, PAD, y_band + lh32 + GAP_SM + lh32 / 4,
                                            CONTENT_W * 36 / 100, lh32 / 2, false);
    }

    /* --- Supporting band --- */
    s_lbl_reason = make_wrapped_label(s_cont, &plex_sans_cond_22, THEME_TEXT_PRIMARY);
    s_lbl_date   = make_wrapped_label(s_cont, &plex_sans_cond_25, THEME_TEXT_PRIMARY);
    s_lbl_last_seen_caption = make_label(s_cont, &plex_mono_13, THEME_TEXT_LABEL);
    lv_label_set_text(s_lbl_last_seen_caption, STR_LAST_SEEN);
    s_lbl_airline   = make_wrapped_label(s_cont, &plex_sans_cond_25, THEME_TEXT_PRIMARY);
    s_lbl_type_full = make_wrapped_label(s_cont, &plex_sans_cond_22, THEME_TEXT_PRIMARY);
    /* The route line, directly under the destination it belongs to —
     * "Frankfurt / Landung in etwa 45 Min." reads as one statement.
     * Same face and colour as the identity line under it: supporting text,
     * not a data value, because it is a sentence and often an estimate. */
    s_lbl_route_line = make_wrapped_label(s_cont, &plex_sans_cond_25, THEME_TEXT_PRIMARY);
    lv_obj_set_hidden(s_lbl_route_line, true);
    lv_obj_set_hidden(s_lbl_reason, true);
    lv_obj_set_hidden(s_lbl_date, true);
    lv_obj_set_hidden(s_lbl_last_seen_caption, true);
    lv_obj_set_hidden(s_lbl_airline, true);
    lv_obj_set_hidden(s_lbl_type_full, true);

    /* --- Data band -- cyan values, tertiary word beside distance --- */
    s_lbl_altitude = make_label(s_cont, &plex_mono_32, THEME_CYAN);
    /* Ground speed shares the altitude's row: height and speed are the two
     * numbers that say what the aircraft is doing, the way an ALT/GS pair
     * does on any traffic display. Its own row would have cost the body
     * another 41 px it does not have. */
    s_lbl_speed    = make_label(s_cont, &plex_mono_32, THEME_CYAN);
    lv_obj_set_hidden(s_lbl_speed, true);
    s_lbl_distance = make_label(s_cont, &plex_mono_32, THEME_CYAN);
    s_lbl_direction_word = make_label(s_cont, &plex_sans_cond_22, THEME_TEXT_TERTIARY);
    lv_obj_set_hidden(s_lbl_altitude, true);
    lv_obj_set_hidden(s_lbl_distance, true);
    lv_obj_set_hidden(s_lbl_direction_word, true);

    show_skeleton();

    /* Last, deliberately: until every widget exists there is nothing safe for
     * an update to write into. */
    s_alive = true;
}

void screen_overhead_update(const view_model_t *vm)
{
    /* The tree may have been torn down since the caller last looked. */
    if (!s_alive) {
        return;
    }
    bool empty_sky = (vm->state == VIEW_EMPTY_SKY);
    bool no_route  = (vm->state == VIEW_NO_ROUTE);
    bool overhead  = (vm->state == VIEW_OVERHEAD);

    /* The first answer replaces the opening skeleton (D85). Every label
     * below sets its own visibility, and the two ghosts the route lookup
     * shares are decided by `searching`, so only the extras go here. */
    if (s_skeleton) {
        hide_skeleton_extras();
    }

    /* --- Chrome --- */
    /* In §5.3 the hero IS the clock, so the chrome copy is the same four
     * characters twice on one screen — it reads as a rendering fault rather
     * than as chrome. Hide it there. */
    /* "Zurück" in the clock's slot while this is the detail layer — the time
     * is not what he opened it for, and an invisible way out is not one. */
    lv_label_set_text(s_lbl_clock, (s_back_cb != NULL) ? STR_BACK : vm->clock);
    set_hidden(s_lbl_clock, vm->state == VIEW_EMPTY_SKY);
    /* Right-aligned, so the text has to be set BEFORE the position is
     * recomputed — "KEINE DATEN" is three glyphs wider than "KEIN NETZ" and
     * would otherwise hang off the edge it is aligned to. */
    set_hidden(s_lbl_offline, vm->net == NET_OK);
    if (vm->net != NET_OK) {
        lv_label_set_text(s_lbl_offline,
                          (vm->net == NET_NO_WIFI) ? STR_NO_NETWORK_TAG
                                                   : STR_NO_DATA_TAG);
        lv_obj_update_layout(s_lbl_offline);
        lv_obj_set_pos(s_lbl_offline,
                       PAD + CONTENT_W - lv_obj_get_width(s_lbl_offline), Y_CHROME);
    }

    /* The identity line, centred in what the chrome row has left over. The
     * two neighbours are fixed: "Zurück"/clock at PAD on the left, and the
     * caution right-aligned. Placed AFTER the caution has been sized and
     * shown or hidden for this frame, because it has to measure it: reading
     * that state a line earlier would use last frame's answer, and be wrong
     * for exactly one frame every time the network changes. If the gap will
     * not hold the line, it hides rather than overlapping either — an identifier printed across a
     * warning is worse than no identifier. */
    {
        bool show_id = vm->airline[0] != '\0';
        if (show_id) {
            lv_label_set_text(s_lbl_identity, vm->airline);
            lv_obj_update_layout(s_lbl_identity);
            lv_obj_update_layout(s_lbl_clock);
            int32_t left  = PAD + lv_obj_get_width(s_lbl_clock) + GAP_MD;
            int32_t right = PAD + CONTENT_W
                          - (lv_obj_is_hidden(s_lbl_offline) ? 0
                             : lv_obj_get_width(s_lbl_offline) + GAP_MD);
            int32_t w = lv_obj_get_width(s_lbl_identity);
            /* Centred on the SCREEN, not in the gap between its neighbours.
             * Centring in the gap would move the line sideways every time the
             * network caution appeared or went away — and a line that shifts
             * when nothing about the aircraft changed reads as a glitch. It
             * only has to not COLLIDE with them. */
            int32_t x = (THEME_SCREEN_WIDTH - w) / 2;
            if (x >= left && x + w <= right) {
                lv_obj_set_pos(s_lbl_identity, x, Y_CHROME);
            } else {
                show_id = false;
            }
        }
        set_hidden(s_lbl_identity, !show_id);
    }

    /* --- Compass tape -- nothing to point at when the sky is empty --- */
    set_hidden(s_compass, empty_sky);
    if (!empty_sky) {
        widget_compass_set_bearing(s_compass, vm->bearing_deg, vm->direction_abbr);
    }

    /* --- Top row --- */
    bool show_origin  = overhead && vm->has_origin;
    /* Still looking: the skeleton instead of the tag and the hero (D84). */
    bool searching    = no_route && vm->route_searching;
    bool show_no_route = no_route && !searching;
    set_hidden(s_lbl_origin, !show_origin);
    set_hidden(s_lbl_arrow, !show_origin);
    set_hidden(s_lbl_no_route_tag, !show_no_route);
    set_hidden(s_ghost_origin, !searching);

    /* The top row's font is fixed (34 px), so its height never changes --
     * the hero always starts at the same Y regardless of which state is
     * showing, so the screen does not visibly jump when traffic appears
     * or disappears. */
    int32_t toprow_h = lv_font_get_line_height(&plex_sans_cond_34);
    int32_t y_hero   = Y_TOPROW + toprow_h + GAP_SM;

    /* §5.3 shows neither the compass tape nor the top row, so holding their
     * space open leaves ~124 px of dead panel above the clock while the data
     * band falls off the bottom. The no-jump rule above is about §5.1 <-> §5.2,
     * which are the same screen in two states; the empty sky is a different
     * mode and is allowed to lay itself out. */
    if (empty_sky) {
        y_hero = Y_COMPASS;
    }

    if (show_origin) {
        lv_label_set_text(s_lbl_origin, vm->origin);
        lv_obj_set_pos(s_lbl_origin, PAD, Y_TOPROW);
        lv_obj_update_layout(s_lbl_origin);
        int32_t arrow_x = PAD + lv_obj_get_width(s_lbl_origin) + GAP_SM;
        lv_obj_set_pos(s_lbl_arrow, arrow_x, Y_TOPROW);
    }
    if (show_no_route) {
        lv_label_set_text(s_lbl_no_route_tag, STR_NO_FLIGHT_PLAN);
        lv_obj_set_pos(s_lbl_no_route_tag, PAD, Y_TOPROW);
    }
    /* Under the origin's ghost and as wide as it: the bar belongs to the
     * thing that is still coming, not to the whole screen. */
    if (searching) {
        lv_obj_set_width(s_busy_route, GHOST_ORIGIN_W);
    }
    /* THE DISTINCTION THIS DRAWS IS THE POINT. Both sentences are amber, both
     * sit in the same place, and until now the only difference between "this
     * aircraft filed no flight plan" and "I am still asking about this one"
     * was eighteen characters he has to read at 70 cm. One of them is final
     * and one of them is not; now one of them moves. */
    widget_busy_set_active(s_busy_route, searching);

    /* --- Hero --- */
    const char      *hero_text;
    const lv_font_t *hero_font;
    if (empty_sky) {
        /* No mono face exists above 32 px (fonts.h), so the one numeral
         * this screen shows at hero size borrows the Sans Condensed hero
         * face. See this file's report note: DESIGN.md §3 does not cover
         * this case, and Plex Sans Condensed's figures are not guaranteed
         * tabular the way Plex Mono's are (DESIGN.md §3) -- acceptable
         * here because the clock repaints once a minute, not every frame. */
        /* Normally the clock is the hero here. Before SNTP has answered there
         * is no clock worth showing, so the model puts a sentence in the hero
         * instead and we render that — at a size that fits, since it is words
         * rather than four digits. */
        if (vm->clock_valid) {
            hero_text = vm->clock;
            hero_font = &plex_sans_cond_100;
        } else {
            int32_t hero_px;
            hero_text = vm->hero;
            hero_font = pick_hero_font(hero_text, CONTENT_W, &hero_px);
            s_last_hero_px = hero_px;
        }
    } else {
        int32_t hero_px;
        hero_text = vm->hero;
        hero_font = pick_hero_font(hero_text, CONTENT_W, &hero_px);
        s_last_hero_px = hero_px;
    }
    lv_obj_set_style_text_font(s_lbl_hero, hero_font, 0);
    lv_label_set_text(s_lbl_hero, hero_text);
    lv_obj_set_pos(s_lbl_hero, PAD, y_hero);
    lv_obj_update_layout(s_lbl_hero);
    set_hidden(s_lbl_hero, searching);
    set_hidden(s_ghost_hero, !searching);
    /* GAP_SM under the hero, not GAP_MD (D79). The hero's line box already
     * carries ~20 px of descender space below its baseline, so the visible
     * gap stays generous — and the 8 px it gives back are exactly what makes
     * room for a SECOND supporting line (the route line under a one-line
     * destination, with the identity under that). Measured, not guessed:
     * with GAP_MD the two lines needed 370 px against a 362 px limit. */
    int32_t y_next = y_hero + lv_obj_get_height(s_lbl_hero) + GAP_SM;
    if (searching) {
        /* One line of the full-size face, which is what a city name usually
         * takes; the ghost sits where its x-height would. */
        lv_obj_set_pos(s_ghost_hero, PAD,
                       y_hero + (s_ghost_hero_lh - GHOST_HERO_H) / 2
                              + s_ghost_hero_lh / 10);
        y_next = y_hero + s_ghost_hero_lh + GAP_SM;
    }

    /* --- Supporting + data bands -- content differs by state ---
     *
     * `show_data` is true whenever view_build.c actually had a real
     * aircraft_t behind this view and ran fill_aircraft_common() on it:
     * always true for VIEW_OVERHEAD/VIEW_NO_ROUTE, and true for
     * VIEW_EMPTY_SKY only when a last-seen aircraft exists (view_build.c's
     * `view_build_empty()` leaves altitude/distance/direction_word blank
     * when `last_seen == NULL`). Gating on the field's own emptiness --
     * rather than on `state` -- means the same "is there aircraft data"
     * question covers all three states with one check, including the
     * no-last-seen edge case where §5.3 must show only clock + date and
     * nothing else, which is still "not blank" (AGENTS.md §1). */
    bool show_data = vm->altitude[0] != '\0';
    bool show_last_seen_block = empty_sky && show_data;

    set_hidden(s_lbl_reason, !no_route);
    set_hidden(s_lbl_date, !empty_sky);
    set_hidden(s_lbl_last_seen_caption, !show_last_seen_block);

    if (no_route) {
        lv_label_set_text(s_lbl_reason, vm->reason);
        lv_obj_set_pos(s_lbl_reason, PAD, y_next);
        lv_obj_update_layout(s_lbl_reason);
        y_next += lv_obj_get_height(s_lbl_reason) + GAP_SM;
    }

    if (empty_sky) {
        lv_label_set_text(s_lbl_date, vm->date_line);
        lv_obj_set_pos(s_lbl_date, PAD, y_next);
        lv_obj_update_layout(s_lbl_date);
        y_next += lv_obj_get_height(s_lbl_date) + GAP_MD;

        if (show_last_seen_block) {
            lv_obj_set_pos(s_lbl_last_seen_caption, PAD, y_next);
            lv_obj_update_layout(s_lbl_last_seen_caption);
            y_next += lv_obj_get_height(s_lbl_last_seen_caption) + GAP_SM;
        }
    }

    /* The "airline" slot: vm->airline in §5.1/§5.2. In §5.3 it is
     * relabelled by STR_LAST_SEEN above and reused for vm->hero instead
     * -- view_build_empty() runs the last-seen aircraft's type through the
     * same hero_from_type() helper VIEW_NO_ROUTE's true hero uses, which is
     * a better, never-"?" string than type_full alone, so it is the right
     * thing to show as the headline of "last aircraft seen". Same fields,
     * same objects, different meaning by position; the model is one struct
     * for all three states (view_model.h), so this is the natural reuse. */
    /* When the clock is not yet set, vm->hero already holds the "Kein Netz"
     * sentence and is rendered as the hero, so this slot must not repeat it. */
    /* The body slot carries the IDENTITY — flight number and model — and the
     * airline has moved up to the chrome row. Swapped on request, and the
     * reasoning holds up: "ASL12H · Airbus A320" is what he wants when he has
     * deliberately opened one aircraft, and the operator is the part he can
     * usually infer from the callsign anyway. §5.3 is unchanged; there the
     * slot still carries the hero, because there is no aircraft to identify. */
    bool show_route_line = !no_route && !empty_sky && vm->route_line[0] != '\0';
    set_hidden(s_lbl_route_line, !show_route_line);
    if (show_route_line) {
        lv_label_set_text(s_lbl_route_line, vm->route_line);
        lv_obj_set_pos(s_lbl_route_line, PAD, y_next);
        lv_obj_update_layout(s_lbl_route_line);
        y_next += lv_obj_get_height(s_lbl_route_line) + GAP_SM;
    }

    const char *airline_slot_text =
        empty_sky ? (vm->clock_valid ? vm->hero : "") : vm->identity;
    bool show_airline_slot = airline_slot_text[0] != '\0';
    set_hidden(s_lbl_airline, !show_airline_slot);
    if (show_airline_slot) {
        lv_label_set_text(s_lbl_airline, airline_slot_text);
        lv_obj_set_pos(s_lbl_airline, PAD, y_next);
        lv_obj_update_layout(s_lbl_airline);
        y_next += lv_obj_get_height(s_lbl_airline) + GAP_SM;
    }

    /* view_build() already blanks type_full when it is identical to the
     * hero (avoids "Leichtflugzeug" over "Leichtflugzeug"); view_build_empty()
     * has no such dedup against the airline-slot text above, so this file
     * does it for that one case rather than showing the same name twice. */
    /* strstr, not strcmp: the slot above now holds "AUA453 · Airbus A320neo",
     * so the model is a SUBSTRING of it rather than equal to it, and an
     * equality test would let "Airbus A320neo" print again directly
     * underneath itself. */
    bool show_type = vm->type_full[0] != '\0' &&
                     strstr(airline_slot_text, vm->type_full) == NULL;
    set_hidden(s_lbl_type_full, !show_type);
    if (show_type) {
        lv_label_set_text(s_lbl_type_full, vm->type_full);
        lv_obj_set_pos(s_lbl_type_full, PAD, y_next);
        lv_obj_update_layout(s_lbl_type_full);
        y_next += lv_obj_get_height(s_lbl_type_full) + GAP_MD;
    }

    /* --- Data band --- */
    set_hidden(s_lbl_altitude, !show_data);
    set_hidden(s_lbl_distance, !show_data);
    if (show_data) {
        /* The data band is ANCHORED TO THE BOTTOM, not flowed after the
         * supporting text. Flowing it fell off the panel in two of the three
         * real states: a two-line reason sentence in §5.2, and the date plus
         * "ZULETZT GESEHEN" plus the last-seen type in §5.3, both pushed
         * "16,8 km Nordosten" past y=480 where it was simply cut in half.
         * DESIGN.md §4's band order already implies this — data is the bottom
         * band, so it belongs to the bottom edge. Anchoring also means the
         * distance sits in the same place on every screen, which is what makes
         * it readable at a glance instead of something you have to find. */
        lv_label_set_text(s_lbl_altitude, vm->altitude);
        lv_label_set_text(s_lbl_distance, vm->distance);
        lv_obj_update_layout(s_lbl_altitude);
        lv_obj_update_layout(s_lbl_distance);

        int32_t alt_h  = lv_obj_get_height(s_lbl_altitude);
        int32_t band_h = alt_h + GAP_SM + lv_obj_get_height(s_lbl_distance);
        int32_t y_band = THEME_SCREEN_HEIGHT - PAD - band_h;

        /* The band is anchored, full stop. What used to be here let it flow
         * down when the supporting text reached it, on the reasoning that an
         * overlap is worse than smaller type — but flowing does not avoid the
         * collision, it converts it into a distance cut in half at y=480.
         * A two-line hero ("Unbekanntes Flugzeug" at the ladder's smallest
         * face) does exactly that, and it is now a common state rather than a
         * rare one, because an unnameable aircraft no longer renders as its
         * ICAO code (D46).
         *
         * So the supporting text gives way instead, bottom-up. The order is
         * the product's own priority: the hero is the answer, the distance is
         * the second question he asks, and the type line is the first thing
         * he can do without — especially here, where the hero is already
         * saying everything that is known about the aircraft. */
        /* Half a GAP_SM between the last supporting line's BOX and the band's
         * BOX. Both boxes carry their own air — ~6 px under the 25 px sans
         * baseline, ~8 px above the 32 px mono digits — so the ink stays
         * ~18 px apart. A full GAP_SM cost exactly the route line (D79):
         * route line + identity measured 363 px against a 362 px limit,
         * because a wrapped 25 px label is 35 px tall here, not the font's
         * nominal 31. */
        int32_t y_limit = y_band - GAP_SM / 2;

        /* REFLOW, not just drop (D79). The lines between the hero and the
         * band are laid out top-down above; when they do not all fit, the
         * least important goes, and the rest CLOSE UP. This used to hide
         * every line whose already-computed position crossed the limit, and
         * nothing moved into the space a hidden line had freed. A private
         * aircraft with a two-line type name — "Diamond DV20 Katana" — lost
         * its reason sentence (right) and then its registration as well,
         * which would have fitted exactly where the sentence had been; the
         * card showed ~70 px of nothing and no identifier at all.
         *
         * `keep` is the order of importance, 1 kept longest:
         *   1 identity   "OE-AHM" / "AUA1Y · Airbus A321": the one line that
         *                says WHICH aircraft; the owner asked for it twice
         *   2 reason     why there is no route (§5.2)
         *   3 route line "Landung in etwa 45 Min." / "16 km von Wien
         *                entfernt" (only ever with a route, so it never
         *                competes with the reason)
         *   4 type line  the hero or the identity usually says it already
         * In the empty sky the identity slot carries the last-seen aircraft,
         * which is that screen's own headline, so it keeps rank 1 there too.
         * The lines above them (date, "last seen") are not in the reflow and
         * never move. */
        struct { lv_obj_t *obj; int32_t gap; int keep; } sup[] = {
            { s_lbl_reason,     GAP_SM, 2 },
            { s_lbl_route_line, GAP_SM, 3 },
            { s_lbl_airline,    GAP_SM, 1 },
            { s_lbl_type_full,  GAP_MD, 4 },
        };
        const int n_sup = (int)(sizeof sup / sizeof sup[0]);
        int32_t y0 = -1;
        for (int i = 0; i < n_sup; i++) {
            if (!lv_obj_is_hidden(sup[i].obj) &&
                (y0 < 0 || lv_obj_get_y(sup[i].obj) < y0)) {
                y0 = lv_obj_get_y(sup[i].obj);
            }
        }
        if (y0 >= 0) {
            for (;;) {
                int32_t need = 0, last_gap = 0;
                int worst = -1;
                for (int i = 0; i < n_sup; i++) {
                    if (lv_obj_is_hidden(sup[i].obj)) continue;
                    need += lv_obj_get_height(sup[i].obj) + sup[i].gap;
                    last_gap = sup[i].gap;
                    if (worst < 0 || sup[i].keep > sup[worst].keep) worst = i;
                }
                if (worst < 0 || y0 + need - last_gap <= y_limit) break;
                lv_obj_set_hidden(sup[worst].obj, true);
            }
            int32_t y = y0;
            for (int i = 0; i < n_sup; i++) {
                if (lv_obj_is_hidden(sup[i].obj)) continue;
                lv_obj_set_pos(sup[i].obj, PAD, y);
                y += lv_obj_get_height(sup[i].obj) + sup[i].gap;
            }
        }

        lv_obj_set_pos(s_lbl_altitude, PAD, y_band);

        /* Speed only for an aircraft that is up there now. The empty sky's
         * "last seen" block has an altitude too, but a speed from minutes ago
         * presented beside it would be a stale number dressed as a live one. */
        bool show_speed = !empty_sky && vm->speed[0] != '\0';
        set_hidden(s_lbl_speed, !show_speed);
        if (show_speed) {
            lv_label_set_text(s_lbl_speed, vm->speed);
            lv_obj_update_layout(s_lbl_speed);
            lv_obj_set_pos(s_lbl_speed, PAD + lv_obj_get_width(s_lbl_altitude) + GAP_MD * 2,
                           y_band);
        }
        y_next = y_band + alt_h + GAP_SM;

        lv_obj_set_pos(s_lbl_distance, PAD, y_next);
        int32_t dist_w = lv_obj_get_width(s_lbl_distance);
        int32_t dist_h = lv_obj_get_height(s_lbl_distance);

        /* A bearing without a distance is not meaningful, so view_build.c
         * leaves direction_word blank in that one case (DST_UNKNOWN) --
         * show the distance value alone rather than an empty word beside it. */
        bool show_dir = vm->direction_word[0] != '\0';
        set_hidden(s_lbl_direction_word, !show_dir);
        if (show_dir) {
            lv_label_set_text(s_lbl_direction_word, vm->direction_word);
            lv_obj_update_layout(s_lbl_direction_word);
            int32_t dir_h = lv_obj_get_height(s_lbl_direction_word);
            /* Vertically centre the (shorter) sans-serif word on the taller
             * mono figure it sits beside, rather than top-aligning the two. */
            lv_obj_set_pos(s_lbl_direction_word, PAD + dist_w + GAP_MD, y_next + (dist_h - dir_h) / 2);
        }
    } else {
        set_hidden(s_lbl_direction_word, true);
        set_hidden(s_lbl_speed, true);
    }
}

int32_t screen_overhead_hero_size_px(void)
{
    return s_last_hero_px;
}

void screen_overhead_set_back_cb(void (*cb)(void))
{
    s_back_cb = cb;
    if (s_cont == NULL) {
        return;
    }
    /* A card still showing its skeleton can already be left (D85). */
    if (s_skeleton && cb != NULL) {
        lv_label_set_text(s_lbl_clock, STR_BACK);
        lv_obj_set_hidden(s_lbl_clock, false);
    }
    /* The container itself is the button. LVGL only delivers clicks to
     * objects that ask for them, and this one has never asked before. */
    lv_obj_set_clickable(s_cont, cb != NULL);
    if (cb != NULL) {
        lv_obj_add_event_cb(s_cont, back_tapped_cb, LV_EVENT_CLICKED, NULL);
    } else {
        lv_obj_remove_event_cb(s_cont, back_tapped_cb);
    }
}
