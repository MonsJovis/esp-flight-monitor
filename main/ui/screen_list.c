/* screen_list.c — see screen_list.h for the contract.
 *
 * DESIGN.md §3's warning, restated because it drove every number below:
 * "as drawn, §5.4 is below even the near floor — list secondary lines at
 * 12 px ... the room exists, since the list shows five rows in 300 px and
 * could show four." So this file shows FOUR rows, not five, and both lines
 * of every row use plex_sans_cond_25 (25 px) — the same near-tier body font
 * screen_wifi.c and screen_settings.c already use for their own row labels,
 * comfortably clearing the 24 px near-view floor (DESIGN.md §3, "Near ~40 cm
 * ... 24 px font size ... List rows, settings labels"). No line of body text
 * on this screen goes below that, including the distance/direction line —
 * that was the exact violation DESIGN.md flagged, so it gets no exception
 * here despite screen_overhead.c's glance-tier "direction_word" and
 * screen_settings.c's own unit labels using a smaller 22 px tertiary font
 * beside a big value. This screen is Near tier, not Glance, and the type
 * pass is the point of the exercise.
 *
 * Like screen_wifi.c's network pool, the four rows are a fixed-size POOL,
 * built once and only ever shown/hidden/re-texted — never created or
 * destroyed after screen_list_create() returns. Unlike screen_wifi.c's
 * list, this one does not scroll: DESIGN.md's own fix for the readability
 * problem is exactly four rows, so a fifth-and-beyond aircraft becomes a
 * single "+N weitere" line instead of a scrollable fifth row (task brief).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "screen_list.h"
#include "theme.h"
#include "fonts/fonts.h"
#include "data/fmt_de.h"
#include "data/tables.h"
#include "net/route_parse.h"
#include "strings_de.h"

/* Every German literal this file shows lives in main/strings_de.h, together
 * with the reasoning for each one; tools/check_strings.py fails the build if
 * one reappears here. Everything else that reaches a row is produced by
 * main/data/fmt_de.h / main/data/tables.h (AGENTS.md §10) — this file formats
 * nothing itself.
 */

/* ============================================================================
 * Layout constants — px, on the 8 px base unit (THEME_BASE_UNIT), matching
 * screen_overhead.c / screen_wifi.c's convention.
 * ============================================================================
 */
#define PAD        THEME_SIDE_PADDING                            /* 20 */
#define CONTENT_W  (THEME_SCREEN_WIDTH - 2 * THEME_SIDE_PADDING) /* 440 */
#define GAP_SM     8   /* between two lines/rows that belong to the same idea */
#define GAP_MD     16  /* between two different bands (header -> list) */
#define GAP_INNER  4   /* between a row's own two stacked lines (matches
                         * screen_settings.c's GAP_INNER) */

#define ROW_INSET  16  /* left/right inset for a row's own label(s), matches
                        * screen_wifi.c's ROW_INSET */
#define ROW_PAD_V  8   /* top/bottom inset around a row's two-line stack */
/* Task brief: "row height at least 72 px so a row is tappable by an elderly
 * user". The real height used is measured from the actual font metrics
 * (screen_settings.c's convention — see screen_list_create()) and only
 * falls back to this floor if that measurement would produce something
 * smaller, which it does not for plex_sans_cond_25 (see this file's report
 * note: 2*8 + 2*31 + 4 = 82 px, already above the floor). */
#define ROW_MIN_H  72

/* Task brief: "four rows minimum height 24 px body text, not five cramped
 * ones" — DESIGN.md §3's own fix for its §5.4 warning. */
#define LIST_VISIBLE_ROWS 4

/* Per-row text buffers. 48 matches view_model.h's VIEW_HERO_LEN — the
 * longest strings landing here are the same city/type names that field
 * holds (DESIGN.md §3's measured longest, "Thessaloniki", is 12 chars). */
#define ROW_PRIMARY_LEN   48
#define ROW_SECONDARY_LEN 24  /* "12,4 km NNO" and friends; fmt_de.c already
                                * bounds fmt_distance_km() to well under this */
#define HEADER_BUF_LEN    40
#define OVERFLOW_BUF_LEN  24

/* ============================================================================
 * Widget tree — built once by screen_list_create(), single instance (this
 * device shows exactly one list screen), so plain file-scope statics rather
 * than a heap-allocated context. Matches screen_overhead.c, screen_wifi.c.
 * ============================================================================
 */
static lv_obj_t *s_cont;
static lv_obj_t *s_lbl_header;   /* chrome: count only, plex_mono_13 */
static lv_obj_t *s_lbl_overflow; /* "+N weitere", shown when n > LIST_VISIBLE_ROWS */
static lv_obj_t *s_lbl_empty;    /* STR_EMPTY_SKY, the only content when n == 0 */

/* Row 0 is structurally the only row that can ever be "the nearest aircraft"
 * — the caller hands aircraft in already-sorted (nearest-first) order
 * (screen_list.h), so this is a fixed identity, not something recomputed
 * per update. Its tag label therefore lives outside the generic per-row
 * struct below rather than as a mostly-unused field on every row. */
static lv_obj_t *s_lbl_nearest_tag;

typedef struct {
    lv_obj_t *row;
    lv_obj_t *lbl_primary;   /* destination (German) or plain-language type */
    lv_obj_t *lbl_secondary; /* "12,4 km NO" */
} list_row_t;

static list_row_t s_rows[LIST_VISIBLE_ROWS];

/* Per-row backing store for the tap callback (screen_list.h's list_select_cb
 * contract: the pointer handed to the callback is this screen's own copy,
 * valid until the next screen_list_update()). aircraft_t is a plain,
 * pointer-free struct (flight_types.h), so a value copy is safe and matches
 * AGENTS.md §10's "fixed-size arrays, no heap" rule — no allocation, no
 * ownership question. */
static aircraft_t s_row_ac[LIST_VISIBLE_ROWS];
static bool       s_row_has_ac[LIST_VISIBLE_ROWS];

static list_select_cb s_select_cb;

/* ============================================================================
 * Small helpers — deliberately re-declared per file rather than shared,
 * matching screen_overhead.c and screen_wifi.c, which each keep their own
 * copy of the same two-line helper rather than a shared ui_util module.
 * ============================================================================
 */
static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}

static inline void set_hidden(lv_obj_t *obj, bool hidden)
{
    lv_obj_set_hidden(obj, hidden);
}

static void safe_copy(char *dst, size_t dst_sz, const char *src)
{
    if (dst_sz == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    size_t n = (len < dst_sz - 1) ? len : dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ============================================================================
 * Per-row text resolution. No per-row view_model_t exists (screen_list.h,
 * task brief) — this is the "no reimplemented formatting" bridge for a
 * screen main/data/view_build.c never anticipated. Every number/name still
 * comes from fmt_de.h / tables.h; this only decides WHICH of them to show.
 * ============================================================================
 */

/* Destination city in German when the route resolved and is plausible (the
 * headline he actually wants — task brief); otherwise the plain-language
 * aircraft type, which DESIGN.md §5 is explicit is not an edge case (routes
 * resolve for airline traffic only, ~92% of it — private/GA traffic, the
 * loud low aircraft he actually hears, never has one and never will). */
static void resolve_primary_text(const aircraft_t *ac, const route_t *routes, int n_routes,
                                 char *out, size_t outsz)
{
    const route_t *route = route_find(routes, n_routes, ac->flight);
    if (route != NULL && route->resolved && route->plausible) {
        const char *de = airport_de(route->dest_icao);
        if (de != NULL && de[0] != '\0') {
            safe_copy(out, outsz, de);
            return;
        }
        if (route->dest_city[0] != '\0') {
            /* No German table entry: the API's own (English) name beats a
             * bare ICAO code (AGENTS.md §1) — same fallback order as
             * view_build.c's resolve_city(), which this file cannot call
             * directly (file-static there) but mirrors deliberately. */
            safe_copy(out, outsz, route->dest_city);
            return;
        }
        /* A route that claims to be resolved and plausible but carries no
         * city name at all is not one adsb.im/routeset is documented to
         * produce (route_parse.c only sets resolved=true alongside a city),
         * but showing the aircraft's own type here is more useful to him
         * than the bare word "unbekannt" would be, so fall through rather
         * than stop at that word — this screen has the full aircraft_t on
         * hand and view_build.c's single hero field does not. */
    }
    /* One helper decides what an aircraft is CALLED, for the hero and for this
     * list alike. Going through actype_full_or_code() here put raw ICAO codes
     * on the panel — "DIMO", "PA18" — because that function's last resort is
     * the code itself, so the category fallback below was never reached.
     * Plain language over codes (AGENTS.md §1). */
    const char *name = actype_display_name(ac->type, ac->category);
    safe_copy(out, outsz, (name != NULL) ? name : STR_UNKNOWN_AIRCRAFT);
}

/* "12,4 km NO" — distance converted and rendered by fmt_distance_km(), the
 * direction abbreviated by compass_de_abbr(); this function only decides
 * whether a distance exists at all and joins the two already-German strings
 * with a space, never formatting a number itself (task brief). */
static void resolve_secondary_text(const aircraft_t *ac, char *out, size_t outsz)
{
    if (ac->dst_nm == DST_UNKNOWN) {
        /* Same rule fmt_altitude_m()/fill_aircraft_common() already use:
         * never feed the sentinel to the formatter, and a bearing without a
         * distance is not meaningful to show either. */
        safe_copy(out, outsz, STR_EM_DASH);
        return;
    }
    char dist[24];
    fmt_distance_km(ac->dst_nm, dist, sizeof dist);
    snprintf(out, outsz, "%s %s", dist, compass_de_abbr(ac->dir_deg));
}

/* ============================================================================
 * Row pool
 * ============================================================================
 */

static void row_event_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= LIST_VISIBLE_ROWS || !s_row_has_ac[idx]) {
        return; /* defensive: a hidden row does not receive input in LVGL,
                 * this only guards a stale index if that ever changes */
    }
    if (s_select_cb) {
        s_select_cb(&s_row_ac[idx]);
    }
}

/* Builds pool slot `idx` once, at fixed position/height `y`/`row_h`. Content
 * (text/visibility) is set later by screen_list_update(); this only creates
 * widgets and fixes geometry and styling that never changes again — which
 * row is "the nearest" is a structural fact (row 0, always), not something
 * recomputed per update, so its distinguishing style is applied here, not
 * in screen_list_update().
 *
 * Plain lv_obj_create(), not lv_button_create() — matching screen_settings.c's
 * make_row(), not screen_wifi.c's create_row(). LVGL 9's base lv_obj is
 * clickable by default (lv_obj_class_create_obj() sets obj->clickable = 1
 * for every object, not just buttons — screen_settings.c's cards rely on
 * exactly this), so a button adds nothing here except the default theme's
 * grey fill and PAD_DEF horizontal padding (lv_theme_default.c) — padding
 * this file's ROW_INSET/CONTENT_W arithmetic does not know about and must
 * not silently compound with. lv_obj_remove_style_all() strips it so every
 * child position below is exactly what the constants say. */
static void create_row(lv_obj_t *parent, int idx, int32_t y, int32_t row_h, int32_t body_lh)
{
    list_row_t *r = &s_rows[idx];

    r->row = lv_obj_create(parent);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_size(r->row, CONTENT_W, row_h);
    lv_obj_set_pos(r->row, PAD, y);
    lv_obj_set_style_pad_all(r->row, 0, 0);
    lv_obj_set_scrollable(r->row, false);
    lv_obj_set_hidden(r->row, true); /* pool starts empty; screen_list_update() reveals what's in range */
    lv_obj_add_event_cb(r->row, row_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    if (idx == 0) {
        /* Nearest row: THEME_SURFACE_SEL fill + the "ÜBER DIR" word below —
         * never colour alone (DO-257A §2.1.6, task brief). A filled card
         * does not also need a divider. */
        lv_obj_set_style_radius(r->row, THEME_BASE_UNIT, 0);
        lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(r->row, THEME_SURFACE_SEL, 0);
        lv_obj_set_style_border_width(r->row, 0, 0);
    } else {
        /* Plain divided row, matching screen_wifi.c's own comment on this
         * exact point: "matching how §5.4's list uses dividers between rows
         * rather than a card per row." */
        lv_obj_set_style_radius(r->row, 0, 0);
        lv_obj_set_style_bg_opa(r->row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(r->row, 1, 0);
        lv_obj_set_style_border_side(r->row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(r->row, THEME_DIVIDER, 0);
        /* Immediate press feedback — screen_settings.c's make_row() applies
         * the same reasoning to its own rows: "he is elderly and this screen
         * has no other confirmation until the state visibly changes." Reuses
         * THEME_SURFACE_SEL rather than inventing a colour: momentarily
         * "selected" is exactly what a press is. The nearest row already
         * rests on that fill, so it needs no separate press state. */
        lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(r->row, THEME_SURFACE_SEL, LV_PART_MAIN | LV_STATE_PRESSED);
    }

    r->lbl_primary = make_label(r->row, &plex_sans_cond_25, THEME_TEXT_PRIMARY);
    lv_label_set_long_mode(r->lbl_primary, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(r->lbl_primary, ROW_INSET, ROW_PAD_V);

    r->lbl_secondary = make_label(r->row, &plex_sans_cond_25, THEME_TEXT_LABEL);
    lv_label_set_long_mode(r->lbl_secondary, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(r->lbl_secondary, CONTENT_W - 2 * ROW_INSET);
    lv_obj_set_pos(r->lbl_secondary, ROW_INSET, ROW_PAD_V + body_lh + GAP_INNER);

    if (idx == 0) {
        s_lbl_nearest_tag = make_label(r->row, &plex_sans_cond_25, THEME_GREEN);
        lv_label_set_text(s_lbl_nearest_tag, STR_TAG_NEAREST);
        lv_obj_update_layout(s_lbl_nearest_tag);
        int32_t tag_w = lv_obj_get_width(s_lbl_nearest_tag);
        lv_obj_set_pos(s_lbl_nearest_tag, CONTENT_W - ROW_INSET - tag_w, ROW_PAD_V);
        /* Primary line shares its row with the tag, so it must not run
         * under it — the secondary line below has the full width instead,
         * since only the tag's own line needs to make room for it. */
        lv_obj_set_width(r->lbl_primary, CONTENT_W - 2 * ROW_INSET - tag_w - GAP_SM);
    } else {
        lv_obj_set_width(r->lbl_primary, CONTENT_W - 2 * ROW_INSET);
    }
}

/* ============================================================================
 * Public API
 * ============================================================================
 */

void screen_list_create(lv_obj_t *parent)
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

    /* Line heights, measured once — every position below is derived from
     * these rather than guessed, matching screen_settings.c's convention
     * ("Line heights, measured once"). */
    int32_t header_lh = lv_font_get_line_height(&plex_mono_13);
    int32_t body_lh   = lv_font_get_line_height(&plex_sans_cond_25);

    /* --- Chrome: count line (task brief: "a header line is fine ... keep
     * it at plex_mono_13 and put no information he needs there"). --- */
    s_lbl_header = make_label(s_cont, &plex_mono_13, THEME_TEXT_LABEL);
    lv_obj_set_pos(s_lbl_header, PAD, PAD);
    lv_obj_set_hidden(s_lbl_header, true); /* shown only when the sky is not empty */

    /* --- The four-row pool. Row height is measured from the real font
     * (plex_sans_cond_25's line height), not assumed — for the record
     * (this file's build report): line_height 31 px per row line, so
     * 2*ROW_PAD_V + GAP_INNER + 2*31 = 16 + 4 + 62 = 82 px, already above
     * the 72 px ROW_MIN_H floor; LV_MAX below is the safety net if a future
     * font regeneration ever changes that. --- */
    int32_t row_h    = LV_MAX(ROW_MIN_H, 2 * ROW_PAD_V + GAP_INNER + 2 * body_lh);
    int32_t list_top = PAD + header_lh + GAP_MD;

    for (int i = 0; i < LIST_VISIBLE_ROWS; i++) {
        int32_t y = list_top + i * (row_h + GAP_SM);
        create_row(s_cont, i, y, row_h, body_lh);
    }

    /* --- Overflow line: "+N weitere" instead of a cramped fifth row. --- */
    int32_t overflow_y = list_top + LIST_VISIBLE_ROWS * row_h
                        + (LIST_VISIBLE_ROWS - 1) * GAP_SM + GAP_SM;
    s_lbl_overflow = make_label(s_cont, &plex_sans_cond_25, THEME_TEXT_LABEL);
    lv_obj_set_pos(s_lbl_overflow, PAD, overflow_y);
    lv_obj_set_hidden(s_lbl_overflow, true);

    /* --- Empty sky: the only content on screen in that state, and the
     * screen's DEFAULT appearance right after create() — AGENTS.md §1 never
     * a blank panel, even for the one call between screen_list_create() and
     * the first screen_list_update(). --- */
    s_lbl_empty = make_label(s_cont, &plex_sans_cond_34, THEME_TEXT_PRIMARY);
    lv_obj_set_width(s_lbl_empty, CONTENT_W);
    lv_label_set_long_mode(s_lbl_empty, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_lbl_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_lbl_empty, STR_EMPTY_SKY);
    lv_obj_update_layout(s_lbl_empty);
    lv_obj_align(s_lbl_empty, LV_ALIGN_CENTER, 0, 0);
}

void screen_list_update(const aircraft_t *ac, int n, const route_t *routes, int n_routes)
{
    if (ac == NULL || n < 0) {
        n = 0;
    }
    if (routes == NULL || n_routes < 0) {
        n_routes = 0;
    }
    bool empty = (n == 0);

    set_hidden(s_lbl_empty, !empty);
    set_hidden(s_lbl_header, empty);

    if (empty) {
        for (int i = 0; i < LIST_VISIBLE_ROWS; i++) {
            set_hidden(s_rows[i].row, true);
            s_row_has_ac[i] = false;
        }
        set_hidden(s_lbl_overflow, true);
        return;
    }

    /* --- Chrome count --- */
    char header_buf[HEADER_BUF_LEN];
    if (n == 1) {
        safe_copy(header_buf, sizeof header_buf, STR_HEADER_ONE);
    } else {
        snprintf(header_buf, sizeof header_buf, FMT_HEADER_MANY, n);
    }
    lv_label_set_text(s_lbl_header, header_buf);

    /* --- Rows: at most LIST_VISIBLE_ROWS, nearest first (already sorted by
     * the caller — screen_list.h, task brief: "render them in that
     * order"). --- */
    int visible = (n < LIST_VISIBLE_ROWS) ? n : LIST_VISIBLE_ROWS;
    for (int i = 0; i < visible; i++) {
        char primary[ROW_PRIMARY_LEN];
        char secondary[ROW_SECONDARY_LEN];
        resolve_primary_text(&ac[i], routes, n_routes, primary, sizeof primary);
        resolve_secondary_text(&ac[i], secondary, sizeof secondary);

        lv_label_set_text(s_rows[i].lbl_primary, primary);
        lv_label_set_text(s_rows[i].lbl_secondary, secondary);
        set_hidden(s_rows[i].row, false);

        s_row_ac[i] = ac[i]; /* value copy — see s_row_ac's own comment */
        s_row_has_ac[i] = true;
    }
    for (int i = visible; i < LIST_VISIBLE_ROWS; i++) {
        set_hidden(s_rows[i].row, true);
        s_row_has_ac[i] = false;
    }

    /* --- Overflow --- */
    int overflow_n = n - visible;
    bool show_overflow = overflow_n > 0;
    set_hidden(s_lbl_overflow, !show_overflow);
    if (show_overflow) {
        char overflow_buf[OVERFLOW_BUF_LEN];
        snprintf(overflow_buf, sizeof overflow_buf, FMT_OVERFLOW, overflow_n);
        lv_label_set_text(s_lbl_overflow, overflow_buf);
    }
}

void screen_list_set_select_cb(list_select_cb cb)
{
    s_select_cb = cb;
}
