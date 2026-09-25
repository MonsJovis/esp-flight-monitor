/* screen_list.h — DESIGN.md §5.4 "Liste", page 2 of the three-page swipe deck
 * (§6: "Über dir jetzt" · Liste · Radar). His "just browsing" mode: he taps
 * over to see what else is in the sky, sorted by distance, nearest first.
 *
 * ⚠️ DESIGN.md §3's warning is why this file exists in this shape: "as drawn,
 * §5.4 is below even the near floor — list secondary lines at 12 px ... the
 * room exists, since the list shows five rows in 300 px and could show four."
 * The row height that follows from it is not negotiable: every line of body
 * text is >=24 px (the near-tier floor, ~40 cm viewing distance), which means
 * roughly four rows fit the panel at a time.
 *
 * It does NOT follow that only four aircraft are reachable. The list SCROLLS
 * vertically through everything in range, up to MAX_AIRCRAFT (flight_types.h)
 * — full-size rows and all of the traffic, rather than a choice between the
 * two. The count header stays pinned above the scrolling column, so the
 * number of aircraft is on screen wherever he has scrolled to.
 *
 * A row is two lines. Line 1 is the destination when the route resolved and
 * the plain-language aircraft type when it did not; line 2 is the distance
 * and bearing on the left, and on the right, in chrome-tier type, the
 * aircraft's identity — its flight number, or its registration when it has no
 * flight number, plus the model where the title above is not already the
 * model. That last element is the only thing on the row that can disappear:
 * it is measured against the room the distance leaves and gives up the model,
 * then the whole line, rather than crowd the distance or grow the row.
 * main/data/identity.h composes the string; screen_list.c has the fit rule.
 *
 * Like screen_overhead.c and screen_wifi.c, the widget tree (chrome line,
 * the row pool, the empty-sky sentence) is built ONCE and only ever moved,
 * shown/hidden and re-texted afterwards — no widget is created or destroyed
 * after screen_list_create() returns, so neither a poll nor a scroll grows
 * the object tree. The pool is sized to the VIEWPORT, not to MAX_AIRCRAFT,
 * and recycled as he scrolls: LVGL's heap here is a fixed 64 KiB pool and a
 * row per aircraft does not fit in it. screen_list.c has the measurements.
 */
#pragma once
#include "lvgl.h"
#include "flight_types.h"
#include "view_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the widget tree ONCE, as a full-bleed (480x480) child of `parent`.
 * Call exactly once per process lifetime — matches every other single-
 * instance screen in this codebase (screen_overhead.c, screen_wifi.c).
 *
 * The tree starts in a valid but empty-looking (empty-sky) state; call
 * screen_list_update() at least once before the first frame is flushed.
 *
 * Caller holds display_lock() (display.h) for the duration of this call.
 */
void screen_list_create(lv_obj_t *parent);

/* Cheap per-poll update: sets text, colours and visibility on the fixed
 * widget pool. Never creates or destroys a widget, so it is safe to call on
 * every poll (AGENTS.md §5: every 10-15 s) without growing the object tree
 * or touching the heap.
 *
 * `ac`/`n`      - the aircraft currently in range, ALREADY SORTED nearest
 *                 first (adsb_parse's own doing — this function does not
 *                 re-sort). `ac[0]`, if it exists, is rendered as the
 *                 nearest row and gets the "ÜBER DIR" marker + THEME_SURFACE_SEL
 *                 fill (never colour alone — DO-257A §2.1.6).
 *                 Every entry gets a row of its own, reachable by scrolling;
 *                 `n` is capped at MAX_AIRCRAFT, which is the pool depth and
 *                 also the most the data model can produce. `n <= 0` renders
 *                 the empty-sky sentence instead of an empty list
 *                 (AGENTS.md §1: never a blank panel), and the column is
 *                 hidden outright so there is nothing to scroll.
 *
 *                 The scroll position is preserved across calls — ordinary
 *                 churn must not jerk the list out from under him mid-read —
 *                 and rewound to the top only when he leaves the page or
 *                 when most of the aircraft he was looking at have left the
 *                 area. screen_list.c documents both rules where they live.
 * `routes`/`n_routes` - resolved routes to look up each visible aircraft's
 *                 destination in (via route_find(), main/net/route_parse.h).
 *                 A row whose callsign has no usable route (route_find()
 *                 returns NULL, or `resolved`/`plausible` is false — true for
 *                 roughly half of real traffic, DESIGN.md §5, not an edge
 *                 case) falls back to the aircraft's plain-language type, and
 *                 its identity line then omits the model, which that title is
 *                 already carrying.
 *
 * Caller MUST hold display_lock() (display.h) for the entire call — this
 * function makes LVGL calls directly and takes no lock of its own.
 */
void screen_list_update(const aircraft_t *ac, int n, const route_t *routes, int n_routes);

/* Whether the list has an answer for the current place yet, and if not, why.
 * Read by the next screen_list_update(). An empty list with no answer is not
 * an empty sky (D82): with the network up it says "Suche Flugzeuge...", with a
 * bar and ghost rows where the rows will land; with the network down it says
 * "Noch keine Flugdaten." and nothing moves. With an answer, `net` is ignored
 * and the list behaves as it always has. */
void screen_list_set_source(bool has_data, net_state_t net);

/* Fired when he taps a row. `ac` points at this screen's own internal copy
 * of the tapped aircraft_t — valid for the duration of the callback, and
 * unchanged until the next screen_list_update() call overwrites the same
 * slot, but NOT beyond that. Copy out whatever fields the receiver needs
 * before returning, rather than holding onto the pointer.
 *
 * The integrator routes this to a detail view; this screen does not
 * implement one itself.
 *
 * Called from inside an LVGL input event, on the display task, so the
 * caller already holds display_lock() implicitly — do not attempt to take
 * it again inside the callback.
 */
typedef void (*list_select_cb)(const aircraft_t *ac);

/* Registers the row-tap callback. Pass NULL to unregister. Only one
 * callback at a time — the same single-subscriber pattern as
 * screen_wifi_set_join_cb() etc. */
void screen_list_set_select_cb(list_select_cb cb);

#ifdef __cplusplus
}
#endif
