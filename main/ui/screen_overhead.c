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

/* ============================================================================
 * FIXED UI CHROME STRINGS — the only German (or non-data) literals in this
 * file. Everything else that reaches the screen is a string already sitting
 * in view_model_t, put there in German, with correct units, by
 * main/data/view_build.c (view_model.h, AGENTS.md §10). Audit THIS block,
 * not the rest of the file, when checking for stray hard-coded text.
 * ============================================================================
 */
#define CHROME_NO_FLIGHT_PLAN "KEIN FLUGPLAN"   /* §5.2 amber caution tag, beside the reason sentence */
#define CHROME_NO_NETWORK     "KEIN NETZ"       /* chrome caution, shown only when vm->online is false */
#define CHROME_LAST_SEEN      "ZULETZT GESEHEN" /* §5.3 caption above the last known aircraft's data */
#define CHROME_ROUTE_ARROW    "\xE2\x86\x92"    /* U+2192 "→" — not a word, the route glyph (magenta) */

/* The compass tape's 8 fixed cardinal marks, 45° apart starting at north.
 * "O" for Ost, never "E" — AGENTS.md §1 and §10. These are structural chrome
 * (they never change, for any aircraft, ever), so they belong in this same
 * audited block even though widget_compass.c is a different file; that
 * widget takes no German literals of its own, see widget_compass.h.
 */
static const char *const CHROME_CARDINALS[WIDGET_COMPASS_NUM_CARDINALS] = {
    "N", "NO", "O", "SO", "S", "SW", "W", "NW",
};

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

/* Chrome band */
static lv_obj_t *s_lbl_clock;
static lv_obj_t *s_lbl_offline; /* CHROME_NO_NETWORK, shown only when !vm->online */

/* Compass tape band */
static lv_obj_t *s_compass;

/* Top row: either origin+arrow (the route), or the no-route tag. */
static lv_obj_t *s_lbl_origin;
static lv_obj_t *s_lbl_arrow;
static lv_obj_t *s_lbl_no_route_tag;

/* Hero band */
static lv_obj_t *s_lbl_hero;

/* Supporting band */
static lv_obj_t *s_lbl_reason;             /* §5.2 only */
static lv_obj_t *s_lbl_date;               /* §5.3 only */
static lv_obj_t *s_lbl_last_seen_caption;  /* §5.3 only, CHROME_LAST_SEEN */
static lv_obj_t *s_lbl_airline;            /* §5.1/§5.2, reused as "last seen" airline in §5.3 */
static lv_obj_t *s_lbl_type_full;          /* §5.1/§5.2, reused as "last seen" type in §5.3 */

/* Data band — §5.1/§5.2 only */
static lv_obj_t *s_lbl_altitude;
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

void screen_overhead_create(lv_obj_t *parent)
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

    /* --- Chrome band -- present in every state (DESIGN.md §4: "consistent
     * across all seven screens so the eye learns one map"). --- */
    s_lbl_clock = make_label(s_cont, &plex_mono_13, THEME_TEXT_LABEL);
    lv_obj_set_pos(s_lbl_clock, PAD, Y_CHROME);

    s_lbl_offline = make_label(s_cont, &plex_mono_13, THEME_AMBER);
    lv_label_set_text(s_lbl_offline, CHROME_NO_NETWORK);
    lv_obj_update_layout(s_lbl_offline);
    lv_obj_set_pos(s_lbl_offline, PAD + CONTENT_W - lv_obj_get_width(s_lbl_offline), Y_CHROME);
    lv_obj_set_hidden(s_lbl_offline, true); /* shown only when !vm->online */

    /* --- Compass tape band --- */
    s_compass = widget_compass_create(s_cont, CONTENT_W, CHROME_CARDINALS);
    lv_obj_set_pos(s_compass, PAD, Y_COMPASS);

    /* --- Top row: origin + route arrow, or the no-route tag --- */
    s_lbl_origin = make_label(s_cont, &plex_sans_cond_34, THEME_TEXT_PRIMARY);
    s_lbl_arrow  = make_label(s_cont, &plex_sans_cond_34, THEME_MAGENTA);
    lv_label_set_text(s_lbl_arrow, CHROME_ROUTE_ARROW);
    s_lbl_no_route_tag = make_label(s_cont, &plex_sans_cond_34, THEME_AMBER);
    lv_label_set_text(s_lbl_no_route_tag, CHROME_NO_FLIGHT_PLAN);
    lv_obj_set_hidden(s_lbl_origin, true);
    lv_obj_set_hidden(s_lbl_arrow, true);
    lv_obj_set_hidden(s_lbl_no_route_tag, true);

    /* --- Hero band -- the single most important thing on the panel --- */
    s_lbl_hero = make_wrapped_label(s_cont, &plex_sans_cond_100, THEME_WHITE);

    /* --- Supporting band --- */
    s_lbl_reason = make_wrapped_label(s_cont, &plex_sans_cond_22, THEME_TEXT_PRIMARY);
    s_lbl_date   = make_wrapped_label(s_cont, &plex_sans_cond_25, THEME_TEXT_PRIMARY);
    s_lbl_last_seen_caption = make_label(s_cont, &plex_mono_13, THEME_TEXT_LABEL);
    lv_label_set_text(s_lbl_last_seen_caption, CHROME_LAST_SEEN);
    s_lbl_airline   = make_wrapped_label(s_cont, &plex_sans_cond_25, THEME_TEXT_PRIMARY);
    s_lbl_type_full = make_wrapped_label(s_cont, &plex_sans_cond_22, THEME_TEXT_PRIMARY);
    lv_obj_set_hidden(s_lbl_reason, true);
    lv_obj_set_hidden(s_lbl_date, true);
    lv_obj_set_hidden(s_lbl_last_seen_caption, true);
    lv_obj_set_hidden(s_lbl_airline, true);
    lv_obj_set_hidden(s_lbl_type_full, true);

    /* --- Data band -- cyan values, tertiary word beside distance --- */
    s_lbl_altitude = make_label(s_cont, &plex_mono_32, THEME_CYAN);
    s_lbl_distance = make_label(s_cont, &plex_mono_32, THEME_CYAN);
    s_lbl_direction_word = make_label(s_cont, &plex_sans_cond_22, THEME_TEXT_TERTIARY);
    lv_obj_set_hidden(s_lbl_altitude, true);
    lv_obj_set_hidden(s_lbl_distance, true);
    lv_obj_set_hidden(s_lbl_direction_word, true);
}

void screen_overhead_update(const view_model_t *vm)
{
    bool empty_sky = (vm->state == VIEW_EMPTY_SKY);
    bool no_route  = (vm->state == VIEW_NO_ROUTE);
    bool overhead  = (vm->state == VIEW_OVERHEAD);

    /* --- Chrome --- */
    /* In §5.3 the hero IS the clock, so the chrome copy is the same four
     * characters twice on one screen — it reads as a rendering fault rather
     * than as chrome. Hide it there. */
    lv_label_set_text(s_lbl_clock, vm->clock);
    set_hidden(s_lbl_clock, vm->state == VIEW_EMPTY_SKY);
    set_hidden(s_lbl_offline, vm->online);

    /* --- Compass tape -- nothing to point at when the sky is empty --- */
    set_hidden(s_compass, empty_sky);
    if (!empty_sky) {
        widget_compass_set_bearing(s_compass, vm->bearing_deg, vm->direction_abbr);
    }

    /* --- Top row --- */
    bool show_origin  = overhead && vm->has_origin;
    bool show_no_route = no_route;
    set_hidden(s_lbl_origin, !show_origin);
    set_hidden(s_lbl_arrow, !show_origin);
    set_hidden(s_lbl_no_route_tag, !show_no_route);

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
        lv_obj_set_pos(s_lbl_no_route_tag, PAD, Y_TOPROW);
    }

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
        hero_text = vm->clock;
        hero_font = &plex_sans_cond_100;
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
    int32_t y_next = y_hero + lv_obj_get_height(s_lbl_hero) + GAP_MD;

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
     * relabelled by CHROME_LAST_SEEN above and reused for vm->hero instead
     * -- view_build_empty() runs the last-seen aircraft's type through the
     * same hero_from_type() helper VIEW_NO_ROUTE's true hero uses, which is
     * a better, never-"?" string than type_full alone, so it is the right
     * thing to show as the headline of "last aircraft seen". Same fields,
     * same objects, different meaning by position; the model is one struct
     * for all three states (view_model.h), so this is the natural reuse. */
    const char *airline_slot_text = empty_sky ? vm->hero : vm->airline;
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
    bool show_type = vm->type_full[0] != '\0' && strcmp(vm->type_full, airline_slot_text) != 0;
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

        /* If the supporting text really is long enough to reach the band, let
         * it flow instead of overlapping — smaller type is recoverable, two
         * strings drawn on top of each other is not. */
        if (y_band < y_next) {
            y_band = y_next;
        }

        lv_obj_set_pos(s_lbl_altitude, PAD, y_band);
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
    }
}

int32_t screen_overhead_hero_size_px(void)
{
    return s_last_hero_px;
}
