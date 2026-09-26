/* screen_list.c — see screen_list.h for the contract.
 *
 * DESIGN.md §3's warning, restated because it drove every number below:
 * "as drawn, §5.4 is below even the near floor — list secondary lines at
 * 12 px ... the room exists, since the list shows five rows in 300 px and
 * could show four." So a row on this screen is as tall as it needs to be
 * for both its lines to use plex_sans_cond_25 (25 px) — the same near-tier
 * body font screen_wifi.c and screen_settings.c already use for their own
 * row labels, comfortably clearing the 24 px near-view floor (DESIGN.md §3,
 * "Near ~40 cm ... 24 px font size ... List rows, settings labels"). No line
 * of body text on this screen goes below that, including the
 * distance/direction line — that was the exact violation DESIGN.md flagged,
 * so it gets no exception here despite screen_overhead.c's glance-tier
 * "direction_word" and screen_settings.c's own unit labels using a smaller
 * 22 px tertiary font beside a big value. This screen is Near tier, not
 * Glance, and the type pass is the point of the exercise.
 *
 * WHAT CHANGED, AND WHY THE "+N weitere" LINE IS GONE. The screen used to
 * show exactly four rows and fold everything else into a single
 * "+N weitere" count line, because four full-size rows are all that fit on
 * a 480 px panel and the alternative on the table was five cramped ones.
 * That was the wrong pair of options: the third one is to keep the row
 * height and let him SCROLL, which is what he asked for ("auf der liste:
 * ich würd gern vertikal scrollen können"). So the row design is unchanged
 * to the pixel and the list now scrolls through every aircraft in range, up
 * to MAX_AIRCRAFT. A count line standing in for aircraft he cannot reach
 * has no purpose once he can reach all of them, so it is deleted rather
 * than kept at the foot — FMT_OVERFLOW in main/strings_de.h is now unused.
 *
 * The header ("%d Flugzeuge in Reichweite") is NOT in the scrolling
 * column. It sits on the non-scrolling root above it, so the one number
 * that says how much there is to scroll through cannot itself scroll away.
 *
 * WHERE THE FLIGHT NUMBER WENT. He asked for the flight number and the
 * aircraft model to be visible in every view. A row is still two lines and
 * still 82 px: the identity goes in the half of line 2 the distance was
 * never using, right-aligned and in chrome-tier type, and it yields — model
 * first, then the whole line — rather than push the distance around or make
 * the row taller. resolve_identity_text() carries the full reasoning; the
 * string itself is main/data/identity.c's, shared with the Radar and the
 * hero so that three screens cannot name the same aircraft three ways.
 *
 * ============================================================================
 * WHY THE ROW POOL IS A SLIDING WINDOW AND NOT ONE ROW PER AIRCRAFT
 * ============================================================================
 * The obvious shape for this — and the one the task asked for — is
 * MAX_AIRCRAFT rows built once in screen_list_create() with the unused ones
 * hidden, exactly as screen_radar.c does with its 24 marks. It does not fit,
 * and the margin is not close.
 *
 * LVGL's heap on this build is a FIXED 64 KiB pool: CONFIG_LV_USE_STDLIB_MALLOC
 * is off and CONFIG_LV_MEM_SIZE is 65536, so lv_mem_core_builtin.c carves
 * `work_mem_int` out of .bss once and every widget in the product — all three
 * deck pages plus whichever overlay is open — lives inside it. It is not the
 * ESP-IDF heap and it does not grow.
 *
 * Measured (host build of this exact tree against the device's own sdkconfig,
 * so the object layout is the real one):
 *
 *   one row = row container + 2 labels           ~1 220 B
 *   three deck pages, old 4-row list                40 304 B   (62 % of pool)
 *   three deck pages, 24-row list                   63 896 B   (97 % of pool)
 *   ... and then opening Einstellungen needs another 13 320 B and fails.
 *
 * A pre-built row per aircraft costs +23 680 B of a pool that has about
 * 11 000 B of usable slack once the settings overlay is accounted for. It
 * does not run out on a test bench with an empty sky either — the rows are
 * built at boot whether or not an aircraft is ever seen, so the device would
 * simply stop being able to open Einstellungen.
 *
 * So the pool is sized to the VIEWPORT rather than to the data: POOL_ROWS
 * slots, enough to cover the visible area at any scroll offset, recycled as
 * he scrolls. Everything the task actually asked for survives — full-size
 * rows, every aircraft reachable, no allocation per update — and the screen
 * costs about 3.5 KiB more than the four-row version it replaces instead of
 * 23.7 KiB more. The ceiling on what is reachable is now MAX_AIRCRAFT
 * (flight_types.h), the data model's own limit, rather than a widget budget.
 *
 * Like screen_wifi.c's network pool, the slots are built ONCE and only ever
 * moved, re-texted and shown/hidden — no widget is created or destroyed after
 * screen_list_create() returns, so neither a poll nor a scroll grows the
 * object tree.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "screen_list.h"
#include "theme.h"
#include "fonts/fonts.h"
#include "data/fmt_de.h"
#include "data/identity.h"
#include "data/tables.h"
#include "net/route_parse.h"
#include "strings_de.h"
#include "widget_busy.h"

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

/* The narrowest clear channel allowed between the distance and the identity
 * that shares line 2 with it. Three base units, wider than the 12 px
 * screen_radar.c leaves between its two captions, because these two are not a
 * pair: one is 25 px body text and the other is 13 px chrome, and at that size
 * difference a tight gap reads as the small text hanging off the big one. */
#define IDENT_GAP  (3 * THEME_BASE_UNIT)

/* nav.c paints the three page-indicator dots on the SCREEN ROOT, i.e. on top
 * of this page, at y = THEME_SCREEN_HEIGHT - 16 and 8 px tall. The scrolling
 * viewport therefore stops short of that band instead of running to the
 * panel edge, so a row scrolled to the very bottom is never half-covered by
 * a dot it has nothing to do with. */
#define DOTS_CLEARANCE 24

/* Slots in the recycled row window. Enough to cover the viewport at ANY
 * scroll offset, which is what the arithmetic below has to guarantee, not
 * approximate — a slot short and a strip of bare ground walks up the screen
 * as he drags.
 *
 * With viewport height V, row pitch P and scroll offset s, the rows touching
 * the viewport are the indices floor(s/P) .. floor((s+V)/P), i.e. at most
 * ceil(V/P) + 1 of them. Both V and P are worst-cased at compile time from
 * the constants above rather than from the fonts, so this holds whatever a
 * future font regeneration does to the measured line height:
 *
 *   P >= ROW_MIN_H + GAP_SM                    = 80 px  (row height floor)
 *   V  = 480 - (PAD + header_lh + GAP_MD) - DOTS_CLEARANCE
 *      <= 480 - (20 + 0 + 16) - 24            = 420 px  (header_lh >= 0)
 *   ceil(420/80) + 1                          = 7 slots
 *
 * The real numbers are P = 90 and V = 402, needing 6; the seventh is the
 * slack that makes the bound true rather than lucky. */
#define POOL_ROWS 7

/* A tap is only a tap if the list was standing still when he put his finger
 * down. See row_event_cb() for the LVGL 9.6 behaviour this closes over. Two
 * or three frames at the measured 28 FPS — far shorter than the time it
 * takes a person to lift, aim and tap, far longer than one coasting frame. */
#define TAP_AFTER_SCROLL_MS 120

/* How often the "has he left this page?" check below runs. Short enough that
 * any real swipe away is caught (a tileview page change takes far longer),
 * long enough to be free — the check is one rectangle intersection. */
#define VISIBILITY_POLL_MS 200

/* Per-row text buffers. 48 matches view_model.h's VIEW_HERO_LEN — the
 * longest strings landing here are the same city/type names that field
 * holds (DESIGN.md §3's measured longest, "Thessaloniki", is 12 chars). */
#define ROW_PRIMARY_LEN   48
#define ROW_SECONDARY_LEN 24  /* "12,4 km NNO" and friends; fmt_de.c already
                                * bounds fmt_distance_km() to well under this */
/* "AUA1234" + " \xC2\xB7 " + the longest name in main/data/tbl_actype.c
 * ("General Dynamics F-16 Fighting Falcon", 37) = 49 bytes and a NUL. */
#define ROW_IDENT_LEN     56
#define HEADER_BUF_LEN    40

/* ============================================================================
 * Widget tree — built once by screen_list_create(), single instance (this
 * device shows exactly one list screen), so plain file-scope statics rather
 * than a heap-allocated context. Matches screen_overhead.c, screen_wifi.c.
 * ============================================================================
 */
/* The visibility poller's handle, and whether this screen's widgets still
 * exist. Both exist because of one crash, and it is a crash with two halves.
 *
 * lv_timer_create() below starts a timer that reads s_cont thirty times a
 * minute for the life of the process. Every debug view in main/debug/ calls
 * lv_obj_clean(lv_screen_active()), which frees s_cont and tells this file
 * nothing — so pressing 'f' or 'b' with the deck up was a LoadProhibited in
 * lv_obj_get_parent(), from an LVGL timer, about a quarter of a second later.
 * Reachable since the timer landed and never once stepped on, because the
 * stress runs that churn overlays do not press the two keys that clean the
 * screen.
 *
 * The other half is quieter: screen_list_create() runs again on every
 * ui_resume(), so each debug view left ANOTHER timer behind, all of them
 * looking at the same freed pointer.
 *
 * The handle fixes the second and the delete callback fixes the first, and
 * the callback is what makes it stay fixed — LVGL tells us, so nothing has to
 * remember to call anything (screen_wifi.c's on_main_deleted, D58). */
static lv_timer_t *s_vis_timer;
static bool        s_alive;

static lv_obj_t *s_cont;       /* full-bleed root, never scrolls */
static lv_obj_t *s_lbl_header; /* chrome: count only, plex_mono_13, PINNED */
static lv_obj_t *s_list;       /* the scrolling column */
static lv_obj_t *s_spacer;     /* see its comment in screen_list_create() */
static lv_obj_t *s_lbl_empty;  /* STR_EMPTY_SKY, the only content when n == 0 */

/* The wait for the first answer at this place (D82): the bar under the header
 * line and ghost rows where the first rows will land — DESIGN.md §4, the same
 * two halves screen_wifi.c and screen_geo.c show, never one without the other.
 * Only while the list is EMPTY: a list he can read keeps its rows. */
#define LIST_SKEL_ROWS 3
static lv_obj_t   *s_busy;
static lv_obj_t   *s_skel[LIST_SKEL_ROWS];
/* Starts as "no answer yet" (D88): a list nobody has fed yet knows nothing,
 * and saying "Der Himmel ist frei." then was exactly what he saw at every boot. */
static bool        s_has_data = false;
static net_state_t s_src_net  = NET_OK;

/* Slot 0 is the only slot that can ever hold the NEAREST aircraft, and that
 * is a property of the window arithmetic rather than a coincidence: slot k
 * always shows data index (s_window_first + k), so index 0 is on screen only
 * when s_window_first is 0, and then it is in slot 0. The "ÜBER DIR" tag can
 * therefore live on slot 0 alone instead of on all seven — which is the same
 * reasoning the four-row version used, and still saves a label. What slot 0
 * does NOT get to assume any more is that it always IS the nearest: scrolled
 * down, it holds an ordinary row and has to look like one (style_slot0()). */
static lv_obj_t *s_lbl_nearest_tag;
static int32_t   s_tag_w;

typedef struct {
    lv_obj_t *row;
    lv_obj_t *lbl_primary;   /* destination (German) or plain-language type */
    lv_obj_t *lbl_secondary; /* "12,4 km NO" */
    lv_obj_t *lbl_ident;     /* "BAW123 · Boeing 777-300ER", right-aligned on
                              * line 2 beside the distance; hidden when the
                              * feed named the aircraft neither way, and when
                              * even the bare identifier will not fit */
} list_row_t;

static list_row_t s_rows[POOL_ROWS];
static int        s_slot_idx[POOL_ROWS]; /* data index in each slot, -1 = hidden */

/* Measured once in screen_list_create(), used by the window arithmetic. */
static int32_t s_row_h;
static int32_t s_pitch;

/* The list itself, in full. The rows are a window onto this, so unlike the
 * four-row version the screen has to keep every aircraft and not just the
 * ones currently on screen.
 *
 * aircraft_t is a plain, pointer-free struct (flight_types.h), so a value
 * copy is safe and matches AGENTS.md §10's "fixed-size arrays, no heap" rule
 * — no allocation, no ownership question. The two text lines are resolved
 * once per update and kept, rather than re-resolved every time a row is
 * recycled: a route lookup plus a table lookup per row per scroll frame is
 * work this device does not need to do, and it also means a row that
 * scrolls back into view says exactly what it said before. ~3.6 KiB of .bss
 * against 110 KiB of free internal RAM. */
static aircraft_t s_ac[MAX_AIRCRAFT];
static char       s_primary[MAX_AIRCRAFT][ROW_PRIMARY_LEN];
static char       s_secondary[MAX_AIRCRAFT][ROW_SECONDARY_LEN];
/* The identity line and the x it was measured to sit at, both resolved once
 * per update rather than per recycled slot. The x is not decoration: the fit
 * rule below has to measure the string anyway to decide whether the model
 * survives, and throwing that measurement away would mean re-running it on
 * every scroll frame that slides the window by a row. An empty string is the
 * hidden state, and then the x means nothing. ~1.4 KiB of .bss. */
static char       s_ident[MAX_AIRCRAFT][ROW_IDENT_LEN];
static int32_t    s_ident_x[MAX_AIRCRAFT];
static int        s_n;            /* aircraft currently listed */
static int        s_window_first; /* data index in slot 0; -1 when nothing is shown */

static list_select_cb s_select_cb;

/* Tracks whether this page was on screen at the last visibility poll — see
 * visibility_timer_cb(). */
static bool s_was_visible;

/* Tick of the last pixel the column moved by, from any cause — finger,
 * momentum, or a rewind. Read by row_event_cb(). */
static uint32_t s_last_scroll_ms;

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
 * loud low aircraft he actually hears, never has one and never will).
 *
 * Returns TRUE when the title it wrote is the destination city, i.e. when the
 * route resolved — which is also exactly the question "may the identity line
 * below this title repeat the aircraft model?". A row already headed
 * "Cessna 208 Caravan" must not then say "· Cessna 208 Caravan" underneath
 * itself (identity.h), and every path here that falls through to the type name
 * returns false, including the resolved-but-city-less one. */
static bool resolve_primary_text(const aircraft_t *ac, const route_t *routes, int n_routes,
                                 char *out, size_t outsz)
{
    const route_t *route = route_find(routes, n_routes, ac->flight);
    if (route != NULL && route->resolved && route->plausible) {
        const char *de = airport_de(route->dest_icao);
        if (de != NULL && de[0] != '\0') {
            safe_copy(out, outsz, de);
            return true;
        }
        if (route->dest_city[0] != '\0') {
            /* No German table entry: the API's own (English) name beats a
             * bare ICAO code (AGENTS.md §1) — same fallback order as
             * view_build.c's resolve_city(), which this file cannot call
             * directly (file-static there) but mirrors deliberately. */
            safe_copy(out, outsz, route->dest_city);
            return true;
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
    return false;
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

/* Width of a string as it would be drawn on one line. Unwrapped, because a
 * wrapped label reports the width it was GIVEN and not the width it wants —
 * the same call and the same reason as screen_radar.c and screen_overhead.c.
 */
static int32_t text_w(const char *text, const lv_font_t *font)
{
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

/* The identity line: "BAW123 · Boeing 777-300ER", right-aligned on line 2.
 *
 * WHERE IT GOES AND WHY IT IS NOT A THIRD LINE. The flight number is what he
 * needs if he wants to look the aircraft up afterwards, and until now it was
 * on no screen at all. It could have had a line of its own — at the cost of a
 * taller row, and a taller row means fewer aircraft visible at a glance, which
 * is the one thing the Liste is for. Line 2 was already paying for its full
 * width and using less than half of it ("16,9 km NNO"), so the line goes in
 * the space that was there rather than in space taken from the count.
 *
 * WHY IT IS 13 px CHROME NEXT TO 25 px BODY. DESIGN.md §2 reserves
 * text-tertiary for "values that sit beside a brighter one", which is this
 * exactly: it has to be findable when he goes looking and must never compete
 * with the distance, the only thing on line 2 he reads at a glance. Mono
 * because it is at least half a code, and every code on this device is set in
 * mono. It is deliberately BELOW §3's 24 px near floor, which is the one place
 * this screen departs from that floor — the floor governs text that answers a
 * question, and this line answers none: it is the enrichment, and the rule
 * immediately below is what keeps it honest about that.
 *
 * THE YIELD RULE, AND WHAT IT YIELDS TO. Measured against the room left over
 * once the distance and a clear channel have taken theirs. Too wide and the
 * model is dropped and it tries again as the bare identifier; too wide even
 * then and it is hidden outright. The distance never moves and is never
 * measured against the identity, because the answer outranks the enrichment —
 * the same priority D48 and D50 already settled for the hero and the radar
 * caption. Nothing is ellipsised: half a registration is not a registration,
 * and nothing shrinks below 13 px, which is the floor for chrome.
 *
 * Measured over the whole of main/data/tbl_actype.c at the default 30 nm
 * radius, the model survives on 88 % of airliner types and the identifier on
 * 100 % of everything: the third rung needs a distance string half the row
 * wide and no formatter here emits one. */
static void resolve_identity_text(const aircraft_t *ac, bool with_model,
                                  const char *secondary,
                                  char *out, size_t outsz, int32_t *out_x)
{
    *out_x = 0;

    /* What line 2 has left once the distance beside it has taken its share.
     * The distance is measured from its TEXT and not from lv_obj_get_width():
     * the secondary label is a fixed-width, dots-mode label, so its widget
     * width is the whole row and says nothing about how much ink is in it. */
    int32_t room = CONTENT_W - 2 * ROW_INSET - text_w(secondary, &plex_sans_cond_25) - IDENT_GAP;

    aircraft_identity(ac, with_model, out, outsz);
    if (out[0] == '\0') {
        return; /* neither a callsign nor a registration: nothing to say */
    }
    int32_t want = text_w(out, &plex_mono_13);

    if (want > room && with_model) {
        /* First thing overboard is the model — it is the part of this line he
         * can also read off the row's own title, off the Radar and off the
         * hero. The identifier is the part that exists nowhere else. */
        aircraft_identity(ac, false, out, outsz);
        want = text_w(out, &plex_mono_13);
    }
    if (want > room) {
        out[0] = '\0'; /* hidden, not squeezed */
        return;
    }
    *out_x = CONTENT_W - ROW_INSET - want;
}

/* ============================================================================
 * Row styling
 * ============================================================================
 */

/* The plain, divided look every row except the nearest one wears. Split out
 * of create_row() because slot 0 has to be able to put it on and take it off
 * again as the window slides past index 0. */
static void style_plain(lv_obj_t *row)
{
    /* Matching screen_wifi.c's own comment on this exact point: "matching how
     * §5.4's list uses dividers between rows rather than a card per row." */
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, THEME_DIVIDER, 0);
}

/* The nearest aircraft: THEME_SURFACE_SEL fill plus the "ÜBER DIR" word —
 * never colour alone (DO-257A §2.1.6, task brief). A filled card does not
 * also need a divider. */
static void style_nearest(lv_obj_t *row)
{
    lv_obj_set_style_radius(row, THEME_BASE_UNIT, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, THEME_SURFACE_SEL, 0);
    lv_obj_set_style_border_width(row, 0, 0);
}

/* Dresses slot 0 for whichever aircraft the window has put in it. Every
 * style property either form needs was already written once in create_row(),
 * so both branches here only overwrite values in a local style that already
 * has the slots for them — lv_style_set_prop() reallocs when it meets a
 * property for the FIRST time, and never again. That matters more than it
 * looks: this runs on a scroll, inside a 64 KiB pool that is two thirds
 * full, and a screen that reallocated a style every time he dragged past the
 * top row would fragment it for no reason at all. */
static void style_slot0(bool nearest)
{
    list_row_t *r = &s_rows[0];
    if (nearest) {
        style_nearest(r->row);
    } else {
        style_plain(r->row);
    }
    set_hidden(s_lbl_nearest_tag, !nearest);
    /* The primary line shares its row with the tag when the tag is there, so
     * it must not run under it; without the tag it has the full width. The
     * secondary line always has the full width — the tag is on line 1, and
     * the only other thing on line 2 is the identity, which is measured
     * against the distance and moves out of ITS way rather than the other way
     * round (resolve_identity_text()). Nothing on line 2 has to be reserved
     * for here, on slot 0 or anywhere else. */
    lv_obj_set_width(r->lbl_primary,
                     nearest ? CONTENT_W - 2 * ROW_INSET - s_tag_w - GAP_SM
                             : CONTENT_W - 2 * ROW_INSET);
}

/* ============================================================================
 * The row window
 *
 * Slot k shows data index (s_window_first + k), positioned at its true place
 * in the column — y = index * pitch, in the column's own content coordinates.
 * LVGL subtracts the parent's scroll offset when it refreshes a child's
 * position (lv_obj_move_to() in lv_obj_pos.c), so a slot set to index 17's
 * y lands where index 17 belongs no matter where the column is scrolled to,
 * and nothing here has to know or track the scroll offset except to decide
 * WHICH indices are wanted.
 *
 * The column's scrollable height comes from s_spacer rather than from the
 * rows, which only ever cover the visible seventh of it.
 * ============================================================================
 */

static void hide_slot(int k)
{
    set_hidden(s_rows[k].row, true);
    s_slot_idx[k] = -1;
}

static void apply_slot(int k, int idx)
{
    list_row_t *r = &s_rows[k];
    lv_obj_set_y(r->row, idx * s_pitch);
    /* lv_label_set_text() copies, which is one small lv_realloc per line —
     * the same cost the four-row version already paid on every poll, now
     * also paid when the window slides by a whole row. Not once per scrolled
     * pixel: refresh_window() returns early unless the first index actually
     * changed, so a full-screen fling costs a handful of these. The
     * allocation-free alternative, lv_label_set_text_static() pointing into
     * s_primary[], is not usable here: LVGL 9.6's lv_label_set_dots() bails
     * out with "Long mode \"dots\" is not supported with static text", and
     * losing the ellipsis on a long city name is a visible regression. */
    lv_label_set_text(r->lbl_primary, s_primary[idx]);
    lv_label_set_text(r->lbl_secondary, s_secondary[idx]);
    /* Hidden rather than set to "": an empty label is still a laid-out,
     * invalidated child, and this one is decided per aircraft, so on a quiet
     * sky most of them would be that. Both the text and the x were settled in
     * screen_list_update() — a slot only carries them across. */
    bool has_ident = (s_ident[idx][0] != '\0');
    if (has_ident) {
        lv_label_set_text(r->lbl_ident, s_ident[idx]);
        lv_obj_set_x(r->lbl_ident, s_ident_x[idx]);
    }
    set_hidden(r->lbl_ident, !has_ident);
    if (k == 0) {
        style_slot0(idx == 0);
    }
    set_hidden(r->row, false);
    s_slot_idx[k] = idx;
}

/* Re-points the window at wherever the column is scrolled to. `force` skips
 * the "nothing moved" shortcut, for when the DATA changed under a window
 * that happens to start at the same index. */
static void refresh_window(bool force)
{
    if (s_n <= 0) {
        for (int k = 0; k < POOL_ROWS; k++) {
            hide_slot(k);
        }
        s_window_first = -1;
        return;
    }

    int32_t sy = lv_obj_get_scroll_y(s_list);
    if (sy < 0) {
        sy = 0; /* elastic over-scroll at the top still shows index 0 first */
    }
    int first = (int)(sy / s_pitch);
    int max_first = s_n - POOL_ROWS;
    if (max_first < 0) {
        max_first = 0;
    }
    if (first > max_first) {
        first = max_first;
    }
    if (!force && first == s_window_first) {
        return;
    }
    s_window_first = first;

    for (int k = 0; k < POOL_ROWS; k++) {
        int idx = first + k;
        if (idx >= s_n) {
            hide_slot(k);
        } else {
            apply_slot(k, idx);
        }
    }
}

/* ============================================================================
 * Scroll position
 *
 * WHEN THE LIST REWINDS TO THE TOP, AND WHY THOSE TWO MOMENTS ONLY.
 *
 * A glance at Liste has to start at the nearest aircraft — he must never
 * swipe over and find himself halfway down a list he left ten minutes ago.
 * But the mirror-image failure is just as bad and much easier to cause: a
 * list that jerks back to the top under his finger while he is reading it.
 * screen_list_update() runs every couple of seconds, so anything that
 * rewinds on ordinary change would do exactly that. Hence two triggers, and
 * deliberately no others:
 *
 * 1. HE LEFT THE PAGE. Detected by polling lv_obj_is_visible() rather than
 *    by hooking nav.c's tileview: this file is handed a parent and told
 *    nothing about what kind of object it is, and a geometric "is any part
 *    of me on screen" test holds whatever the integrator wraps this screen
 *    in. The rewind happens while the page is off screen, so it is never
 *    something he watches happen. (It does not fire when a full-screen
 *    OVERLAY covers the page — Einstellungen — because the page is still
 *    geometrically visible underneath. Coming back from settings to the
 *    same scroll position is the harmless half of this.)
 *
 * 2. THE LIST HE WAS READING IS GONE. "Substantially" is defined here as:
 *    more than half of the aircraft that were listed at the last update are
 *    no longer in range at all, matched by ICAO hex. Ordinary churn
 *    deliberately does NOT qualify — one aircraft leaving, another arriving,
 *    or the whole order shifting as distances update, all leave him where he
 *    was, because the rows he is looking at are still rows about aircraft
 *    that are still up there. A majority turnover means the content under
 *    his scroll position has been replaced wholesale, and then "where he
 *    was" no longer refers to anything.
 * ============================================================================
 */
static void scroll_to_top(void)
{
    if (s_list != NULL) {
        lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
    }
}

static void visibility_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_alive || s_cont == NULL) {
        return;   /* the tree is gone; see s_vis_timer's comment */
    }
    /* s_cont, not s_list: s_list is hidden outright in the empty-sky state,
     * and a hidden object is never "visible", which would read as him
     * leaving the page every time the sky cleared. */
    bool visible = lv_obj_is_visible(s_cont);
    if (s_was_visible && !visible) {
        scroll_to_top();
        refresh_window(true);
    }
    s_was_visible = visible;
}

/* True when more than half of what is currently listed is no longer in `ac`.
 * Call BEFORE s_ac is overwritten. O(24*24) strcmp worst case, every couple
 * of seconds — immeasurable beside a single row repaint. */
static bool set_turned_over(const aircraft_t *ac, int n)
{
    if (s_n <= 0) {
        return false; /* nothing was on screen, so nothing was lost */
    }
    int kept = 0;
    for (int i = 0; i < s_n; i++) {
        for (int j = 0; j < n; j++) {
            if (strcmp(s_ac[i].hex, ac[j].hex) == 0) {
                kept++;
                break;
            }
        }
    }
    return kept * 2 < s_n;
}

/* ============================================================================
 * Events
 * ============================================================================
 */

static void list_scroll_event_cb(lv_event_t *e)
{
    (void)e;
    s_last_scroll_ms = lv_tick_get();
    refresh_window(false);
}

/* LV_EVENT_CLICKED, NOT pressed/released — and that choice is what keeps
 * scrolling from selecting an aircraft by accident. Verified in
 * managed_components/lvgl__lvgl/src/indev/lv_indev.c (9.6): indev_proc_release()
 * only reaches `send_event(LV_EVENT_CLICKED, ...)` inside
 * `if(scroll_obj == NULL)`; once lv_indev_scroll_handler() has picked a
 * scroll object the release sends LV_EVENT_SCROLL_THROW_BEGIN to that object
 * instead and the pressed row gets nothing. The same handler also does
 * `lv_obj_remove_state(indev->pointer.act_obj, LV_STATE_PRESSED)` the moment
 * a scroll starts (lv_indev_scroll.c), so the row he began the drag on also
 * drops its press highlight rather than staying lit through the fling.
 * LV_EVENT_RELEASED, by contrast, fires either way — which is exactly the
 * bug this avoids.
 *
 * ONE CASE LV_EVENT_CLICKED DOES NOT COVER, AND WHY THE TICK CHECK IS HERE.
 * Reading the same file further: a press that lands while the column is
 * still COASTING is a fresh press, so indev_proc_press() takes the
 * `indev_obj_act != indev->pointer.act_obj` branch, and that branch does
 * `indev->pointer.scroll_obj = NULL` before the drag has become a drag. The
 * release afterwards therefore finds scroll_obj == NULL and does fire
 * LV_EVENT_CLICKED. So in stock LVGL 9.6, stabbing a finger at a flying list
 * to stop it opens whichever row happened to be under the finger — which is
 * the same accident, arriving by a different road, and it would be a
 * bewildering one for him. lv_obj_scroll_by_raw() sends LV_EVENT_SCROLL for
 * every pixel of every throw frame, so "was the list moving a moment ago" is
 * a fact this file can simply have; a tap that arrives inside that window is
 * a brake, not a choice, and is dropped.
 *
 * The user data is the SLOT, not the aircraft: which aircraft a slot is
 * showing is whatever the window last put there. */
static void row_event_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    if (k < 0 || k >= POOL_ROWS) {
        return;
    }
    int idx = s_slot_idx[k];
    if (idx < 0 || idx >= s_n) {
        return; /* defensive: a hidden row does not receive input in LVGL,
                 * this only guards a stale slot if that ever changes */
    }
    if (lv_tick_elaps(s_last_scroll_ms) < TAP_AFTER_SCROLL_MS) {
        return;
    }
    if (s_select_cb) {
        s_select_cb(&s_ac[idx]);
    }
}

/* ============================================================================
 * Row pool
 * ============================================================================
 */

/* Builds pool slot `idx` once. Position and content are the window's job
 * afterwards; this only creates widgets and fixes the geometry and styling
 * that never changes again.
 *
 * Plain lv_obj_create(), not lv_button_create() — matching screen_settings.c's
 * make_row(), not screen_wifi.c's create_row(). LVGL 9's base lv_obj is
 * clickable by default (lv_obj_constructor() sets obj->clickable = 1 for
 * every object, not just buttons — screen_settings.c's cards rely on
 * exactly this), so a button adds nothing here except the default theme's
 * grey fill and PAD_DEF horizontal padding (lv_theme_default.c) — padding
 * this file's ROW_INSET/CONTENT_W arithmetic does not know about and must
 * not silently compound with. lv_obj_remove_style_all() strips it so every
 * child position below is exactly what the constants say.
 *
 * The row is NOT scrollable itself, which is also what lets the column
 * underneath it scroll: lv_indev_find_scroll_obj() walks up from the pressed
 * object through every non-scrollable ancestor that has scroll_chain set,
 * and lv_obj_constructor() sets scroll_chain_hor/ver on every object with a
 * parent, so a drag started on a row is handed to s_list. */
static void create_row(lv_obj_t *parent, int idx, int32_t body_lh, int32_t ident_y)
{
    list_row_t *r = &s_rows[idx];

    r->row = lv_obj_create(parent);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_size(r->row, CONTENT_W, s_row_h);
    lv_obj_set_pos(r->row, PAD, 0); /* real y comes from the window */
    lv_obj_set_style_pad_all(r->row, 0, 0);
    lv_obj_set_scrollable(r->row, false);
    lv_obj_set_hidden(r->row, true); /* pool starts empty; the window reveals what is in range */
    lv_obj_add_event_cb(r->row, row_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    /* Immediate press feedback — screen_settings.c's make_row() applies the
     * same reasoning to its own rows: "he is elderly and this screen has no
     * other confirmation until the state visibly changes." Reuses
     * THEME_SURFACE_SEL rather than inventing a colour: momentarily
     * "selected" is exactly what a press is. On the nearest row, which
     * already rests on that fill, it is by construction invisible — which is
     * what the four-row version achieved by leaving the press style off that
     * row entirely, and is the same thing to look at. */
    lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(r->row, THEME_SURFACE_SEL, LV_PART_MAIN | LV_STATE_PRESSED);

    /* Slot 0 has to be able to switch between the two looks without ever
     * meeting a style property for the first time again (style_slot0()), so
     * both are written here, plain last because that is what an unfilled
     * pool looks like. Every other slot is plain forever. */
    if (idx == 0) {
        style_nearest(r->row);
    }
    style_plain(r->row);

    r->lbl_primary = make_label(r->row, &plex_sans_cond_25, THEME_TEXT_PRIMARY);
    lv_label_set_long_mode(r->lbl_primary, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(r->lbl_primary, ROW_INSET, ROW_PAD_V);
    lv_obj_set_width(r->lbl_primary, CONTENT_W - 2 * ROW_INSET);

    r->lbl_secondary = make_label(r->row, &plex_sans_cond_25, THEME_TEXT_LABEL);
    lv_label_set_long_mode(r->lbl_secondary, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(r->lbl_secondary, CONTENT_W - 2 * ROW_INSET);
    lv_obj_set_pos(r->lbl_secondary, ROW_INSET, ROW_PAD_V + body_lh + GAP_INNER);

    /* The identity, sharing line 2 with the distance. No width is set, so the
     * label is exactly as wide as its text and the x written per aircraft is
     * its LEFT edge, computed from the measured width — which is what makes it
     * right-aligned on the same edge as slot 0's "ÜBER DIR" tag a line above,
     * rather than four pixels off it. CLIP, not DOTS: nothing here is ever
     * allowed to be ellipsised, so the mode that would do it is not fitted.
     * The y is baseline alignment with the distance, not top alignment — two
     * faces this far apart in size share a line only if they sit on the same
     * line (screen_list_create() does the arithmetic). */
    r->lbl_ident = make_label(r->row, &plex_mono_13, THEME_TEXT_TERTIARY);
    lv_label_set_long_mode(r->lbl_ident, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_pos(r->lbl_ident, ROW_INSET, ident_y);
    set_hidden(r->lbl_ident, true);

    if (idx == 0) {
        s_lbl_nearest_tag = make_label(r->row, &plex_sans_cond_25, THEME_GREEN);
        lv_label_set_text(s_lbl_nearest_tag, STR_TAG_NEAREST);
        lv_obj_update_layout(s_lbl_nearest_tag);
        s_tag_w = lv_obj_get_width(s_lbl_nearest_tag);
        lv_obj_set_pos(s_lbl_nearest_tag, CONTENT_W - ROW_INSET - s_tag_w, ROW_PAD_V);
        set_hidden(s_lbl_nearest_tag, true);
    }

    s_slot_idx[idx] = -1;
}

/* ============================================================================
 * Public API
 * ============================================================================
 */

/* Cleared by LVGL itself when the deck is torn down, whether that is
 * ui_resume() rebuilding it or a debug view taking the panel over. */
static void on_cont_deleted(lv_event_t *e)
{
    (void)e;
    s_alive = false;
    if (s_vis_timer != NULL) {
        lv_timer_delete(s_vis_timer);
        s_vis_timer = NULL;
    }
    s_cont            = NULL;
    s_list            = NULL;
    s_lbl_header      = NULL;
    s_spacer          = NULL;
    s_lbl_empty       = NULL;
    s_lbl_nearest_tag = NULL;
    s_busy            = NULL;
    for (int i = 0; i < LIST_SKEL_ROWS; i++) {
        s_skel[i] = NULL;
    }
}

void screen_list_create(lv_obj_t *parent)
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

    /* Line heights, measured once — every position below is derived from
     * these rather than guessed, matching screen_settings.c's convention
     * ("Line heights, measured once"). */
    int32_t header_lh = lv_font_get_line_height(&plex_mono_13);
    int32_t body_lh   = lv_font_get_line_height(&plex_sans_cond_25);

    /* --- Chrome: count line (task brief: "a header line is fine ... keep
     * it at plex_mono_13 and put no information he needs there"). It is a
     * child of the NON-scrolling root, above the column, so the one number
     * that says how much there is to scroll through stays on screen the
     * whole way down. --- */
    s_lbl_header = make_label(s_cont, &plex_mono_13, THEME_TEXT_LABEL);
    lv_obj_set_pos(s_lbl_header, PAD, PAD);
    lv_obj_set_hidden(s_lbl_header, true); /* shown only when the sky is not empty */

    /* --- The scrolling column. Vertical only and with momentum, set up the
     * way screen_settings.c sets up its own column, for the same reason it
     * gives: the content is taller than the viewport and he may not realise
     * it scrolls at all, so the first rows must be fully visible before any
     * scroll happens — which the top-down layout gives for free, and the
     * part-visible row at the bottom edge advertises the rest.
     *
     * Vertical only matters for a second reason here that it does not on
     * the settings overlay: this screen is page 2 of nav.c's horizontal
     * swipe deck. Because s_list refuses horizontal scrolling,
     * lv_indev_find_scroll_obj() keeps walking up on a sideways drag and
     * lands on the tileview, so the swipe between Über dir / Liste / Radar
     * still works with a finger anywhere on the list.
     *
     * No scrollbar: the page-indicator dots are this deck's only positional
     * chrome, and a second scroll gauge on the same 480 px panel is noise
     * for a man who is being asked to read, not to navigate. --- */
    int32_t list_top = PAD + header_lh + GAP_MD;
    s_list = lv_obj_create(s_cont);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, THEME_SCREEN_WIDTH, THEME_SCREEN_HEIGHT - list_top - DOTS_CLEARANCE);
    lv_obj_set_pos(s_list, 0, list_top);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    /* Bottom padding only, and for the same reason screen_settings.c gives:
     * the scroll extent is measured from the last child's edge, so without
     * it the final row ends flush against the viewport edge and reads as a
     * row that got cut off rather than a list that ended. */
    lv_obj_set_style_pad_bottom(s_list, GAP_MD, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_scrollable(s_list, true);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scroll_momentum(s_list, true);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_list, list_scroll_event_cb, LV_EVENT_SCROLL, NULL);

    /* --- What makes the column as tall as the traffic rather than as tall
     * as the seven rows in it. LVGL measures a scroll extent from the bottom
     * edge of the lowest visible child (lv_obj_get_scroll_bottom()), and the
     * rows are a window that never reaches the bottom of a long list, so
     * without this the column would refuse to scroll past slot 6. One 1x1
     * transparent object parked at the foot of the real content is the whole
     * mechanism. Not clickable, so it can never swallow a tap. --- */
    s_spacer = lv_obj_create(s_list);
    lv_obj_remove_style_all(s_spacer);
    lv_obj_set_size(s_spacer, 1, 1);
    lv_obj_set_pos(s_spacer, 0, 0);
    lv_obj_set_clickable(s_spacer, false);
    lv_obj_set_scrollable(s_spacer, false);
    lv_obj_set_hidden(s_spacer, true);

    /* --- The row window. Row height is measured from the real font
     * (plex_sans_cond_25's line height), not assumed — for the record
     * (this file's build report): line_height 31 px per row line, so
     * 2*ROW_PAD_V + GAP_INNER + 2*31 = 16 + 4 + 62 = 82 px, already above
     * the 72 px ROW_MIN_H floor; LV_MAX below is the safety net if a future
     * font regeneration ever changes that. At that height four rows and a
     * slice of the fifth fill the viewport, which is both the readability
     * trade DESIGN.md §3 asked for and the hint that there is more. --- */
    s_row_h = LV_MAX(ROW_MIN_H, 2 * ROW_PAD_V + GAP_INNER + 2 * body_lh);
    s_pitch = s_row_h + GAP_SM;

    /* Where the identity line sits so that it shares a BASELINE with the
     * distance rather than a top edge. An LVGL label's y is the top of its
     * line box, and the baseline sits (line_height - base_line) below that, so
     * a 13 px face aligned top-to-top with a 25 px one floats visibly high.
     * Measured, not tabulated: 43 + (31-6) - (18-4) = 54 px with the fonts as
     * generated today, and the row is 82 px, so the 18 px line box clears the
     * bottom inset with room to spare. */
    int32_t ident_y = ROW_PAD_V + body_lh + GAP_INNER
                      + (body_lh - plex_sans_cond_25.base_line)
                      - (lv_font_get_line_height(&plex_mono_13) - plex_mono_13.base_line);

    for (int i = 0; i < POOL_ROWS; i++) {
        create_row(s_list, i, body_lh, ident_y);
    }
    s_window_first = -1;

    /* --- The wait (D82). The bar sits in the GAP_MD under the header line,
     * which is what the header says while it lasts; the ghosts stand exactly
     * where rows 0..2 will, on the non-scrolling root, so nothing about them
     * scrolls or can be tapped. Uneven widths, and nothing on them moves:
     * widget_busy.h. --- */
    s_busy = widget_busy_create(s_cont, CONTENT_W);
    lv_obj_set_pos(s_busy, PAD, PAD + header_lh + (GAP_MD - WIDGET_BUSY_H) / 2);
    {
        static const int32_t prim_pct[LIST_SKEL_ROWS] = { 54, 40, 47 };
        static const int32_t sec_pct[LIST_SKEL_ROWS]  = { 24, 30, 21 };
        int32_t inner = CONTENT_W - 2 * ROW_INSET;
        int32_t gh    = body_lh / 2;   /* an x-height, not a redaction bar */
        for (int i = 0; i < LIST_SKEL_ROWS; i++) {
            lv_obj_t *row = lv_obj_create(s_cont);
            lv_obj_remove_style_all(row);
            lv_obj_set_size(row, CONTENT_W, s_row_h);
            lv_obj_set_pos(row, PAD, list_top + i * s_pitch);
            lv_obj_set_scrollable(row, false);
            lv_obj_set_clickable(row, false);
            style_plain(row);   /* the real rows' hairline: no change of construction */
            widget_busy_ghost(row, ROW_INSET, ROW_PAD_V + (body_lh - gh) / 2,
                              inner * prim_pct[i] / 100, gh, false);
            widget_busy_ghost(row, ROW_INSET,
                              ROW_PAD_V + body_lh + GAP_INNER + (body_lh - gh) / 2,
                              inner * sec_pct[i] / 100, gh, true);
            lv_obj_set_hidden(row, true);
            s_skel[i] = row;
        }
    }

    /* --- Empty sky: the only content on screen in that state, and the
     * screen's DEFAULT appearance right after create() — AGENTS.md §1 never
     * a blank panel, even for the one call between screen_list_create() and
     * the first screen_list_update(). It hangs off the non-scrolling root so
     * it is centred on the panel rather than on a column that is empty
     * anyway; screen_list_update() hides s_list outright in this state, so
     * there is nothing to scroll and no way to drag the sentence around. --- */
    s_lbl_empty = make_label(s_cont, &plex_sans_cond_34, THEME_TEXT_PRIMARY);
    lv_obj_set_width(s_lbl_empty, CONTENT_W);
    lv_label_set_long_mode(s_lbl_empty, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_lbl_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_lbl_empty, STR_EMPTY_SKY);
    lv_obj_update_layout(s_lbl_empty);
    lv_obj_align(s_lbl_empty, LV_ALIGN_CENTER, 0, 0);
    set_hidden(s_list, true);

    /* One timer, created once, never per update — see the scroll-position
     * block above for what it is for, and s_vis_timer for why the handle is
     * kept. Belt and braces: if a previous tree somehow left one behind, it
     * goes now rather than joining this one. */
    s_was_visible = false;
    if (s_vis_timer != NULL) {
        lv_timer_delete(s_vis_timer);
    }
    s_vis_timer = lv_timer_create(visibility_timer_cb, VISIBILITY_POLL_MS, NULL);

    /* Last, like every other screen here: until every widget exists there is
     * nothing safe for the timer to read. */
    s_alive = true;

    /* Drawn in its waiting state straight away (D88). The list is only fed
     * while it is the visible page, so until the first update it used to show
     * whatever create() left — "Der Himmel ist frei." — at every boot, and
     * for up to a tick after every swipe while the first poll was still out.
     * The first real update replaces this with whatever is true. */
    s_has_data = false;
    s_src_net  = NET_OK;
    screen_list_update(NULL, 0, NULL, 0);
}

void screen_list_set_source(bool has_data, net_state_t net)
{
    s_has_data = has_data;
    s_src_net  = net;
}

void screen_list_update(const aircraft_t *ac, int n, const route_t *routes, int n_routes)
{
    /* The same guard every other screen in this directory carries. ui_task
     * re-checks the suspend flag inside the display lock now, so this should
     * be unreachable — "should be" is exactly what screen_overhead.c was
     * before the M11 stress run found otherwise. */
    if (!s_alive) {
        return;
    }
    if (ac == NULL || n < 0) {
        n = 0;
    }
    if (n > MAX_AIRCRAFT) {
        n = MAX_AIRCRAFT; /* defensive re-cap: the backing arrays are exactly
                           * this deep and the caller should already be
                           * bounded by the same constant */
    }
    if (routes == NULL || n_routes < 0) {
        n_routes = 0;
    }
    bool empty = (n == 0);

    /* Decided before s_ac is overwritten — it is a comparison against what
     * he is looking at right now. */
    bool rewind = empty || set_turned_over(ac, n);

    /* --- Take the list. Nearest first, already sorted by the caller
     * (screen_list.h, task brief: "render them in that order"). --- */
    for (int i = 0; i < n; i++) {
        s_ac[i] = ac[i]; /* value copy — see s_ac's own comment */
        bool titled_by_route = resolve_primary_text(&ac[i], routes, n_routes,
                                                    s_primary[i], ROW_PRIMARY_LEN);
        resolve_secondary_text(&ac[i], s_secondary[i], ROW_SECONDARY_LEN);
        /* Last, because what fits depends on what the distance beside it took,
         * and whether the model may appear at all depends on whether the title
         * above it is already the model. */
        resolve_identity_text(&ac[i], titled_by_route, s_secondary[i],
                              s_ident[i], ROW_IDENT_LEN, &s_ident_x[i]);
    }
    s_n = n;

    /* Empty with no answer for this place yet is a wait, not an empty sky
     * (D82). With the network up: the header line says so, the bar sweeps
     * under it, the ghosts stand where the rows will. With it down there is
     * no request to wait for, so one sentence and nothing moving. */
    bool waiting  = empty && !s_has_data && s_src_net == NET_OK;
    bool no_answer = empty && !s_has_data && s_src_net != NET_OK;

    widget_busy_set_active(s_busy, waiting);
    for (int i = 0; i < LIST_SKEL_ROWS; i++) {
        set_hidden(s_skel[i], !waiting);
    }

    set_hidden(s_lbl_empty, !empty || waiting);
    set_hidden(s_lbl_header, empty && !waiting);
    set_hidden(s_list, empty);
    set_hidden(s_spacer, empty);

    if (empty) {
        if (waiting) {
            lv_label_set_text(s_lbl_header, STR_AIRCRAFT_SEARCHING);
        } else {
            /* Set every time, not once at create: the same label says both. */
            lv_label_set_text(s_lbl_empty, no_answer ? STR_AIRCRAFT_NO_ANSWER : STR_EMPTY_SKY);
            lv_obj_align(s_lbl_empty, LV_ALIGN_CENTER, 0, 0);
        }
        refresh_window(true); /* hides every slot */
        scroll_to_top();
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

    /* --- How far the column can scroll: to the bottom edge of the last row,
     * with no trailing inter-row gap. The spacer is 1 px tall, so its own y
     * IS its bottom edge. --- */
    lv_obj_set_y(s_spacer, n * s_pitch - GAP_SM - 1);

    if (rewind) {
        scroll_to_top();
    } else {
        /* A list that got shorter while he was near its foot would otherwise
         * leave him parked below the last row looking at nothing: the extent
         * shrinks but the view does not move. This is LVGL's own fix for
         * exactly that, and a no-op whenever the view is still inside the
         * content. */
        lv_obj_readjust_scroll(s_list, LV_ANIM_OFF);
    }
    refresh_window(true);
}

void screen_list_set_select_cb(list_select_cb cb)
{
    s_select_cb = cb;
}
