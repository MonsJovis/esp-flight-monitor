/* screen_geo.c — see screen_geo.h for the contract.
 *
 * DESIGN.md §5.8 / §3 NEAR tier (~40 cm, leaned into): every label he acts on
 * is >=25 px and every tappable row is >=64 px, the same floors screen_wifi.c
 * works to. Two full-bleed sub-screens share `parent` and only one is visible
 * at a time; both are built once (screen_overhead.c's create-once/update-many
 * pattern), and the hit list is a fixed POOL of rows so that no widget is
 * ever created or destroyed on the display task after start-up.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "screen_geo.h"
#include "theme.h"
#include "fonts/fonts.h"
#include "strings_de.h"
#include "geocode.h"
#include "widget_input.h"
#include "widget_busy.h"

/* ============================================================================
 * FIXED UI CHROME STRINGS — there are none in this file. Every word it shows
 * comes from main/strings_de.h, and tools/check_strings.py fails the build if
 * one reappears here (screen_settings.c's convention).
 *
 * NOTE on "..." in STR_GEO_SEARCHING: three ASCII periods, not U+2026. The
 * font subset has no ellipsis glyph and LVGL draws a missing glyph as nothing
 * at all (AGENTS.md §7). The reasoning lives beside the string.
 * ============================================================================
 */

/* ============================================================================
 * Layout — px, on the 8 px base unit (THEME_BASE_UNIT).
 * ============================================================================
 */
#define PAD       THEME_SIDE_PADDING                            /* 20  */
#define CONTENT_W (THEME_SCREEN_WIDTH - 2 * THEME_SIDE_PADDING) /* 440 */
#define GAP_SM    8
#define GAP_MD    16

#define BTN_H     64    /* comfortably past the 56 px floor, as in WLAN */
#define FIELD_H   64
#define ROW_INSET 16
#define ROW_PAD_V 8

/* One row per hit. GEOCODE_MAX_RESULTS is what the integrator asks the
 * endpoint for, so sizing the pool from it means the two can never disagree
 * about how many can come back. */
#define GEO_MAX_ROWS GEOCODE_MAX_RESULTS

/* ============================================================================
 * Widget tree — built once, single instance, file-scope statics (matches
 * screen_wifi.c and screen_overhead.c).
 * ============================================================================
 */

/* --- s_type: title, field, Suchen/Zurück, keyboard --- */
static lv_obj_t *s_type;
static lv_obj_t *s_ta;
static lv_obj_t *s_kb;

/* --- s_res: title, status, the busy bar, hit list, Neu suchen/Zurück --- */
static lv_obj_t *s_res;
static lv_obj_t *s_lbl_status;
static lv_obj_t *s_btn_again;
static lv_obj_t *s_busy;
static lv_obj_t *s_list;

/* Ghost rows, shown only while a search is in flight. Three, because three is
 * what fits above the fold — a fourth would be half-visible and read as a
 * result that failed to draw rather than as a placeholder. */
#define GEO_SKEL_ROWS 3
static lv_obj_t *s_skel[GEO_SKEL_ROWS];

typedef struct {
    lv_obj_t *row;
    lv_obj_t *lbl_name;
    lv_obj_t *lbl_region;
} geo_row_t;

static geo_row_t   s_rows[GEO_MAX_ROWS];
/* The hits themselves, kept because the pick callback hands the integrator a
 * whole geo_place_t — coordinates and timezone included — and a label on
 * screen cannot give those back. */
static geo_place_t s_places[GEO_MAX_ROWS];
static int         s_n_places;

/* True only while this screen's widgets exist.
 *
 * The lookup runs on its own task and calls screen_geo_set_results() when it
 * lands, typically two to ten seconds after he tapped Suchen. If the overlay
 * was closed in the meantime every pointer in this file is dangling and the
 * search task writes through all of them — the D58 panic, which reached this
 * repo twice through the WLAN screen before it was understood.
 *
 * Cleared by LVGL itself on LV_EVENT_DELETE, so nothing outside this file has
 * to remember to call anything, which is the only version of this that stays
 * true. */
static bool s_alive;

/* True between "he asked" and "an answer was drawn" — i.e. exactly while an
 * answer handed to screen_geo_set_results() is still the answer to the
 * question on the glass.
 *
 * It exists because "Neu suchen" does not cancel anything: it puts the typing
 * sub-screen back up while the previous request is still on the wire, and
 * geocode.c waits ten seconds before giving up. Without this, that request
 * landing mid-word called show_results() and took the keyboard out from under
 * his fingers to show hits for a question he stopped asking. The integrator's
 * generation counter (main.c, s_geo_gen) does not cover it — it only moves
 * when a NEW search starts or the screen is re-opened, and tapping Neu suchen
 * is neither. */
static bool s_awaiting;

static geo_search_cb s_search_cb;
static geo_pick_cb   s_pick_cb;
static geo_exit_cb   s_exit_cb;

static void show_typing(void);

static void on_type_deleted(lv_event_t *e)
{
    (void)e;
    s_alive = false;
    s_type  = NULL;
    s_res   = NULL;
    /* The hits die with the tree. Left standing, they outlive the widgets
     * that showed them: a freshly built screen would start with a non-zero
     * s_n_places, so screen_geo_debug_tap() would pass its range check and
     * pick a place from the PREVIOUS session — writing a location to NVS
     * that nothing on the glass ever offered. */
    s_n_places = 0;
    s_awaiting = false;
    /* The waiting furniture dies with the tree too. Left standing, these are
     * the same dangling pointers s_alive exists to stop being written
     * through — and set_waiting() is reached from paths that do not all
     * check it. */
    s_busy = NULL;
    s_btn_again = NULL;
    for (int i = 0; i < GEO_SKEL_ROWS; i++) {
        s_skel[i] = NULL;
    }
}

/* ============================================================================
 * Small helpers — each screen keeps its own copies rather than sharing one
 * set across files (screen_wifi.c, screen_settings.c do the same).
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

static lv_obj_t *make_button(lv_obj_t *parent, int32_t w, int32_t h, const char *text,
                             lv_color_t bg, lv_color_t border, lv_color_t text_color)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    widget_kill_button_chrome(btn);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, border, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_FULL, 0);
    lv_obj_set_style_radius(btn, THEME_BASE_UNIT, 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &plex_sans_cond_25, 0);
    lv_obj_set_style_text_color(lbl, text_color, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

/* A full-bleed sub-screen. Both states are one of these. */
static lv_obj_t *make_sheet(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, THEME_SCREEN_WIDTH, THEME_SCREEN_HEIGHT);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_set_style_bg_color(o, THEME_GROUND, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_scrollable(o, false);
    return o;
}

/* The same title on both states, in the same place, so that flipping between
 * them does not read as having gone somewhere else. Returns the y below it. */
static int32_t make_title(lv_obj_t *parent)
{
    lv_obj_t *t = make_label(parent, &plex_sans_cond_34, THEME_TEXT_PRIMARY);
    lv_label_set_text(t, STR_GEO_TITLE);
    lv_obj_set_pos(t, PAD, PAD);
    lv_obj_update_layout(t);
    return PAD + lv_obj_get_height(t) + GAP_SM;
}

/* ============================================================================
 * State switching
 * ============================================================================
 */

static void apply_status(const char *text, lv_color_t color)
{
    lv_label_set_text(s_lbl_status, text);
    lv_obj_set_style_text_color(s_lbl_status, color, 0);
}

/* The waiting state, as one switch. The bar and the ghost rows are two halves
 * of one idea and there is no state in which one of them belongs without the
 * other, so they are never set separately — see widget_busy.h. */
static void set_waiting(bool waiting)
{
    if (!s_alive) {
        return;
    }
    widget_busy_set_active(s_busy, waiting);
    for (int i = 0; i < GEO_SKEL_ROWS; i++) {
        lv_obj_set_hidden(s_skel[i], !waiting);
    }
}

static void show_typing(void)
{
    /* Leaving the results state ends the wait as far as this screen is
     * concerned, whatever the network is still doing. A bar left sweeping
     * behind a hidden screen is an animation nobody can see, invalidating an
     * area nobody is looking at, until the overlay is torn down.
     *
     * And it ends it for the ANSWER too: he is typing again, so a reply to
     * the previous word must not flip him back here mid-keystroke. */
    set_waiting(false);
    s_awaiting = false;
    lv_obj_set_hidden(s_res, true);
    lv_obj_set_hidden(s_type, false);
    /* Re-attach rather than assume: the keyboard keeps whatever text area it
     * was last given, and this screen only ever has the one. */
    lv_keyboard_set_textarea(s_kb, s_ta);
    lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
}

static void show_results(void)
{
    lv_obj_set_hidden(s_type, true);
    lv_obj_set_hidden(s_res, false);
}

/* ============================================================================
 * The hit list
 * ============================================================================
 */

static void row_event_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_n_places) {
        return;
    }
    if (s_pick_cb) {
        s_pick_cb(&s_places[idx]);
    }
}

/* Builds pool slot `idx` once. Content is set later by update_row(); this
 * only creates widgets and fixes their static geometry.
 *
 * TWO LINES PER ROW, and the second one is not decoration: "Wien" returns
 * four places and "Pattaya" two, so the name alone cannot tell him which one
 * he means. The region line is what makes the list answerable. */
static void create_row(lv_obj_t *parent, int idx, int32_t row_h,
                       int32_t name_lh, int32_t region_lh)
{
    geo_row_t *row = &s_rows[idx];

    row->row = lv_button_create(parent);
    lv_obj_set_size(row->row, CONTENT_W, row_h);
    widget_kill_button_chrome(row->row);
    /* NO PADDING, so ROW_INSET and ROW_PAD_V below mean what they say.
     *
     * LVGL's default theme pads lv_button — about 13 px each side and 8 top
     * and bottom at this DPI — and both lv_obj_set_pos() and lv_obj_align()
     * measure from the CONTENT area, not the object. So every label here was
     * placed 13 px right and 8 px down of where it asked to be, while its
     * width was computed from CONTENT_W as though the row had no padding at
     * all: the right-hand end ran past the row and LV_LABEL_LONG_MODE_DOTS,
     * which measures against the object's own size, ellipsised late or not at
     * all. Found on the WLAN list next door, where the same arithmetic drew a
     * place name's "..." straight through the green tick beside it. */
    lv_obj_set_style_pad_all(row->row, 0, 0);
    /* A DIVIDED LIST, not a stack of cards. Every card on this device is a
     * place he can BE (the location cards in Einstellungen); these are
     * candidates he is choosing between, which is DESIGN.md §5.4's list, and
     * screen_wifi.c draws its unsaved networks the same way for the same
     * reason. Square corners, a hairline under each row, no gap between
     * them — which is also what lets three and a bit rows fit instead of
     * two and a half, so the list visibly continues past the fold. */
    lv_obj_set_style_radius(row->row, 0, 0);
    lv_obj_set_style_bg_opa(row->row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(row->row, THEME_SURFACE_SEL, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row->row, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(row->row, 1, 0);
    lv_obj_set_style_border_side(row->row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row->row, THEME_DIVIDER, 0);
    lv_obj_set_hidden(row->row, true);  /* the pool starts empty */
    lv_obj_add_event_cb(row->row, row_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    row->lbl_name = make_label(row->row, &plex_sans_cond_25, THEME_TEXT_PRIMARY);
    lv_obj_set_width(row->lbl_name, CONTENT_W - 2 * ROW_INSET);
    lv_label_set_long_mode(row->lbl_name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(row->lbl_name, ROW_INSET, ROW_PAD_V);

    row->lbl_region = make_label(row->row, &plex_sans_cond_22, THEME_TEXT_LABEL);
    lv_obj_set_width(row->lbl_region, CONTENT_W - 2 * ROW_INSET);
    lv_label_set_long_mode(row->lbl_region, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(row->lbl_region, ROW_INSET, ROW_PAD_V + name_lh + GAP_SM / 2);
}

/* One ghost row, the same size and shape as a real hit, in the same place.
 *
 * The widths are three different pairs rather than three identical ones: a
 * column of three bars of exactly equal length reads as a graphic, and a
 * graphic is a thing that is finished. Uneven, it reads as text that has not
 * arrived — which is what it is. Nothing here animates; the bar above is the
 * one moving thing on this device (widget_busy.h). */
static void create_skeleton(lv_obj_t *parent, int idx, int32_t row_h,
                            int32_t name_lh, int32_t region_lh)
{
    static const int32_t name_pct[GEO_SKEL_ROWS]   = { 52, 38, 46 };
    static const int32_t region_pct[GEO_SKEL_ROWS] = { 34, 26, 30 };

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, CONTENT_W, row_h);
    lv_obj_set_style_pad_all(row, 0, 0);
    /* The same hairline the real rows carry, so the list does not visibly
     * change construction when the answer lands. */
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, THEME_DIVIDER, 0);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_hidden(row, true);

    int32_t inner = CONTENT_W - 2 * ROW_INSET;
    /* Ghost heights are the x-height of the line they stand in, near enough:
     * a bar as tall as the full line box looks like a redaction. */
    int32_t nh = name_lh / 2;
    int32_t rh = region_lh / 2;
    widget_busy_ghost(row, ROW_INSET, ROW_PAD_V + (name_lh - nh) / 2,
                      inner * name_pct[idx] / 100, nh, false);
    widget_busy_ghost(row, ROW_INSET,
                      ROW_PAD_V + name_lh + GAP_SM / 2 + (region_lh - rh) / 2,
                      inner * region_pct[idx] / 100, rh, true);

    s_skel[idx] = row;
}

void screen_geo_set_results(const geo_place_t *places, int n)
{
    /* He may have tapped Zurück while the lookup was in flight — see s_alive. */
    if (!s_alive) {
        return;
    }
    /* Or tapped "Neu suchen" and started typing the next word. The screen is
     * no longer asking this question, so the answer is not shown — see
     * s_awaiting. Dropping it is the whole point: painting it would call
     * show_results() and take the keyboard away from under his fingers. */
    if (!s_awaiting) {
        return;
    }
    s_awaiting = false;

    /* Whatever came back, the waiting is over — including the two answers
     * that are not successes. A bar still sweeping under "Kein Ort mit diesem
     * Namen" would say the device is still looking. */
    set_waiting(false);

    bool failed = (n < 0);
    if (failed || places == NULL) {
        n = 0;
    }
    if (n > GEO_MAX_ROWS) {
        n = GEO_MAX_ROWS;
    }

    for (int i = 0; i < n; i++) {
        s_places[i] = places[i];
        lv_label_set_text(s_rows[i].lbl_name, s_places[i].name);
        lv_label_set_text(s_rows[i].lbl_region, s_places[i].region);
        /* A hit whose region the parser dropped (no glyphs, or the endpoint
         * knew neither the province nor the country) gets its name centred in
         * the row instead of a blank second line. */
        lv_obj_set_hidden(s_rows[i].lbl_region, s_places[i].region[0] == '\0');
        lv_obj_set_hidden(s_rows[i].row, false);
    }
    for (int i = n; i < GEO_MAX_ROWS; i++) {
        lv_obj_set_hidden(s_rows[i].row, true);
    }
    s_n_places = n;

    /* AGENTS.md §1: never a blank panel, and never a silent one. Each of the
     * three outcomes says what happened in a sentence, and the two that are
     * not successes are told apart on purpose — "nothing exists by that
     * name" is his typo to fix, "the search did not answer" is the device's
     * problem and nothing he types will help. */
    char buf[48];
    if (failed) {
        apply_status(STR_GEO_FAILED, THEME_AMBER);
    } else if (n == 0) {
        apply_status(STR_GEO_NONE, THEME_TEXT_LABEL);
    } else if (n == 1) {
        apply_status(STR_GEO_ONE, THEME_GREEN);
    } else {
        snprintf(buf, sizeof buf, FMT_GEO_MANY, n);
        apply_status(buf, THEME_GREEN);
    }

    /* Always land at the top: after a second search the list must not still
     * be scrolled to where the previous one was left. */
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
    show_results();
}

/* ============================================================================
 * Suchen / Neu suchen / Zurück
 * ============================================================================
 */

/* Copies the field into `out`, trimming ASCII whitespace at both ends. He
 * will leave a trailing space after tapping the space bar by accident, and an
 * un-trimmed query is a percent-encoded "%20" the endpoint matches nothing
 * against — the same trap AGENTS.md §7 documents for the space-padded
 * `flight` field, arriving from the other direction. */
static void trimmed_query(char *out, size_t out_sz)
{
    const char *src = lv_textarea_get_text(s_ta);
    if (src == NULL) {
        out[0] = '\0';
        return;
    }
    size_t start = 0;
    while (src[start] == ' ' || src[start] == '\t') {
        start++;
    }
    size_t end = strlen(src);
    while (end > start && (src[end - 1] == ' ' || src[end - 1] == '\t')) {
        end--;
    }
    size_t n = end - start;
    if (n > out_sz - 1) {
        n = out_sz - 1;
    }
    memcpy(out, src + start, n);
    out[n] = '\0';
}

/* Everything the screen itself does when a search starts: flip to the results
 * state, say so, empty the list, and start the wait.
 *
 * Split out from do_search() so that screen_geo_debug_searching() can put the
 * screen into this state WITHOUT a network round trip, and put it into the
 * real one rather than a hand-made copy of it. The state is worth reaching on
 * its own because it is the hardest one to photograph: the endpoint answers
 * in about 250 ms, so a screenshot of the actual wait loses the race almost
 * every time, and a loading state nobody can photograph is a loading state
 * nobody can check. */
static void begin_search(void)
{
    apply_status(STR_GEO_SEARCHING, THEME_TEXT_LABEL);
    for (int i = 0; i < GEO_MAX_ROWS; i++) {
        lv_obj_set_hidden(s_rows[i].row, true);
    }
    s_n_places = 0;
    s_awaiting = true;
    show_results();
    /* After show_results(), not before: the bar measures itself when it is
     * switched on, and an object inside a hidden parent has no width to
     * measure. */
    set_waiting(true);
}

static void do_search(void)
{
    char query[SCREEN_GEO_QUERY_MAX];
    trimmed_query(query, sizeof query);

    /* Two characters is the endpoint's own floor (geo_build_url()). Below it,
     * flip to the results state and say nothing was found rather than sitting
     * on the typing screen doing nothing visible — a tap that produces no
     * change at all is how he decides the device is broken. */
    if (strlen(query) < 2) {
        begin_search();                 /* flip over, then answer at once */
        screen_geo_set_results(NULL, 0);
        return;
    }

    begin_search();

    if (s_search_cb) {
        s_search_cb(query);
    }
}

static void search_clicked_cb(lv_event_t *e)  { (void)e; do_search(); }
static void again_clicked_cb(lv_event_t *e)   { (void)e; show_typing(); }

static void exit_clicked_cb(lv_event_t *e)
{
    (void)e;
    if (s_exit_cb) {
        s_exit_cb();
    }
}

/* lv_keyboard's OK key sends LV_EVENT_READY to its text area, and — because
 * s_ta is one_line — so does its Enter key. Wiring both to do_search() means
 * the keyboard behaves the way every other on-screen keyboard he has used
 * behaves, on top of the explicit Suchen button. Its close glyph
 * (LV_EVENT_CANCEL) leaves the screen, matching Zurück beside it. */
static void ta_ready_cb(lv_event_t *e)  { (void)e; do_search(); }
static void ta_cancel_cb(lv_event_t *e) { (void)e; exit_clicked_cb(e); }

/* ============================================================================
 * Public API
 * ============================================================================
 */

void screen_geo_create(lv_obj_t *parent)
{
    int32_t name_lh   = lv_font_get_line_height(&plex_sans_cond_25);
    int32_t region_lh = lv_font_get_line_height(&plex_sans_cond_22);
    int32_t btn_w     = (CONTENT_W - GAP_MD) / 2;

    /* ---------- s_type ---------- */
    s_type = make_sheet(parent);
    lv_obj_add_event_cb(s_type, on_type_deleted, LV_EVENT_DELETE, NULL);

    int32_t y = make_title(s_type);
    y += GAP_SM;

    s_ta = lv_textarea_create(s_type);
    lv_obj_set_size(s_ta, CONTENT_W, FIELD_H);
    lv_obj_set_pos(s_ta, PAD, y);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_placeholder_text(s_ta, STR_GEO_PLACEHOLDER);
    lv_textarea_set_max_length(s_ta, SCREEN_GEO_QUERY_MAX - 1);
    lv_obj_set_style_text_font(s_ta, &plex_sans_cond_25, 0);
    widget_style_field(s_ta);
    lv_obj_add_event_cb(s_ta, ta_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_ta, ta_cancel_cb, LV_EVENT_CANCEL, NULL);
    y += FIELD_H + GAP_MD;

    /* Suchen carries the green pairing theme.h already names for "the good
     * path" (the WLAN screen's saved-network tokens); Zurück stays neutral. */
    lv_obj_t *btn_search = make_button(s_type, btn_w, BTN_H, STR_GEO_BTN_SEARCH,
                                       THEME_SURFACE_GREEN, THEME_BORDER_GREEN,
                                       THEME_TEXT_PRIMARY);
    lv_obj_set_pos(btn_search, PAD, y);
    lv_obj_add_event_cb(btn_search, search_clicked_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_back1 = make_button(s_type, btn_w, BTN_H, STR_BACK,
                                      THEME_SURFACE_SEL, THEME_BORDER_IDLE,
                                      THEME_TEXT_PRIMARY);
    lv_obj_set_pos(btn_back1, PAD + btn_w + GAP_MD, y);
    lv_obj_add_event_cb(btn_back1, exit_clicked_cb, LV_EVENT_CLICKED, NULL);
    y += BTN_H + GAP_MD;

    /* Full screen width (0, not PAD) for the largest possible keys, filling
     * everything below the buttons so it never overlaps the field above.
     *
     * lv_obj_align(), NOT lv_obj_set_pos(). lv_keyboard's constructor does
     * `lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, 0, 0)` on itself
     * (lv_keyboard.c), and in LVGL 9 x/y are an OFFSET FROM THE ALIGNMENT
     * once one is set — so lv_obj_set_pos(kb, 0, 240) does not put the
     * keyboard 240 px down the screen, it puts it 240 px BELOW THE BOTTOM
     * EDGE, where nothing can be seen and nothing can be tapped. The screen
     * renders perfectly and simply has no keyboard on it. Measured on the
     * panel with tools/grab_screen.py, which is the only reason it was
     * caught: every other widget on this screen positions itself with
     * set_pos and lands where it is told, because nothing else in this
     * codebase aligns itself in its own constructor. */
    s_kb = lv_keyboard_create(s_type);
    lv_obj_set_size(s_kb, THEME_SCREEN_WIDTH, THEME_SCREEN_HEIGHT - y);
    lv_obj_align(s_kb, LV_ALIGN_TOP_LEFT, 0, y);
    widget_style_keyboard(s_kb);
    lv_keyboard_set_textarea(s_kb, s_ta);
    lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);

    /* ---------- s_res ---------- */
    s_res = make_sheet(parent);
    lv_obj_set_hidden(s_res, true);

    int32_t ry = make_title(s_res);

    s_lbl_status = make_label(s_res, &plex_sans_cond_25, THEME_TEXT_LABEL);
    /* Height as well as width — see screen_wifi.c for what a content-sized
     * height does to a DOT-mode label: it grows a second line instead of
     * ellipsising, and draws it over whatever is below. Nothing here is long
     * enough to trigger it today, which is exactly why it is worth pinning
     * before someone lengthens a sentence. */
    lv_obj_set_size(s_lbl_status, CONTENT_W, name_lh);
    lv_label_set_long_mode(s_lbl_status, LV_LABEL_LONG_MODE_DOTS);
    /* Never blank, even before the first search (AGENTS.md §1). */
    apply_status(STR_GEO_IDLE, THEME_TEXT_LABEL);
    lv_obj_set_pos(s_lbl_status, PAD, ry);

    /* The busy bar goes INSIDE the gap that was already between the status
     * line and the list, not below it. GAP_MD is 16 px and the bar is 4, so
     * it sits with 6 px of air above and below and the list starts exactly
     * where it did — which matters more than it sounds: the row pool is sized
     * so that three rows and a sliver of a fourth are visible, and that
     * sliver is the only thing telling him the list continues past the fold
     * (see create_row). Twelve pixels spent here would have bought a list
     * that ends on a clean row edge and looks complete when it is not. */
    s_busy = widget_busy_create(s_res, CONTENT_W);
    lv_obj_set_pos(s_busy, PAD, ry + name_lh + (GAP_MD - WIDGET_BUSY_H) / 2);

    ry += name_lh + GAP_MD;

    int32_t btn_y = THEME_SCREEN_HEIGHT - PAD - BTN_H;

    s_btn_again = make_button(s_res, btn_w, BTN_H, STR_GEO_BTN_SEARCH,
                              THEME_SURFACE_SEL, THEME_BORDER_IDLE,
                              THEME_TEXT_PRIMARY);
    lv_obj_set_pos(s_btn_again, PAD, btn_y);
    lv_obj_add_event_cb(s_btn_again, again_clicked_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_back2 = make_button(s_res, btn_w, BTN_H, STR_BACK,
                                      THEME_SURFACE_SEL, THEME_BORDER_IDLE,
                                      THEME_TEXT_PRIMARY);
    lv_obj_set_pos(btn_back2, PAD + btn_w + GAP_MD, btn_y);
    lv_obj_add_event_cb(btn_back2, exit_clicked_cb, LV_EVENT_CLICKED, NULL);

    /* The scrollable hit list, filling what is left between the status line
     * and the buttons. Vertical only, with momentum, like every other list on
     * this device. */
    s_list = lv_obj_create(s_res);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_pos(s_list, PAD, ry);
    lv_obj_set_size(s_list, CONTENT_W, btn_y - GAP_MD - ry);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(s_list, 0, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(s_list, true);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scroll_momentum(s_list, true);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    /* A hidden pool row takes no flex layout space, so the first visible hit
     * always lands at the top of the list with no extra handling — and the
     * same property is what lets the ghost rows and the real rows share one
     * flex column without either having to know about the other. The ghosts
     * are created FIRST because flex order is creation order, and a ghost
     * below a result would be a promise of more that is not coming. */
    /* One height, used by the ghost rows and the real ones. It used to be
     * spelled out inside create_row(); a ghost row that was a pixel taller
     * than the row it stands in would make the list twitch at the moment the
     * answer arrives, which is the one moment he is looking at it. */
    int32_t row_h = 2 * ROW_PAD_V + name_lh + GAP_SM / 2 + region_lh;
    for (int i = 0; i < GEO_SKEL_ROWS; i++) {
        create_skeleton(s_list, i, row_h, name_lh, region_lh);
    }
    for (int i = 0; i < GEO_MAX_ROWS; i++) {
        create_row(s_list, i, row_h, name_lh, region_lh);
    }

    /* Last, deliberately: until every widget exists there is nothing safe for
     * the search task to write into. */
    s_alive = true;
}

bool screen_geo_is_up(void)
{
    return s_alive;
}

void screen_geo_debug_searching(void)
{
    if (!s_alive) {
        return;
    }
    begin_search();
}

bool screen_geo_debug_again(void)
{
    if (!s_alive || s_btn_again == NULL || lv_obj_is_hidden(s_res)) {
        return false;
    }
    lv_obj_send_event(s_btn_again, LV_EVENT_CLICKED, NULL);
    return true;
}

bool screen_geo_debug_layer(void)
{
    return s_alive && widget_keyboard_debug_layer(s_kb);
}

bool screen_geo_debug_tap(int idx)
{
    if (!s_alive || idx < 0 || idx >= s_n_places) {
        return false;
    }
    lv_obj_send_event(s_rows[idx].row, LV_EVENT_CLICKED, NULL);
    return true;
}

void screen_geo_set_search_cb(geo_search_cb cb) { s_search_cb = cb; }
void screen_geo_set_pick_cb(geo_pick_cb cb)     { s_pick_cb = cb; }
void screen_geo_set_exit_cb(geo_exit_cb cb)     { s_exit_cb = cb; }
