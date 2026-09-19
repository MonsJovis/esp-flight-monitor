/* screen_list.h — DESIGN.md §5.4 "Liste", page 2 of the three-page swipe deck
 * (§6: "Über dir jetzt" · Liste · Radar). His "just browsing" mode: he taps
 * over to see what else is in the sky, sorted by distance, nearest first.
 *
 * ⚠️ DESIGN.md §3's warning is why this file exists in this shape: "as drawn,
 * §5.4 is below even the near floor — list secondary lines at 12 px ... the
 * room exists, since the list shows five rows in 300 px and could show four."
 * This implementation makes that trade explicitly: FOUR fixed rows, no
 * scrolling, every line of body text >=24 px (the near-tier floor, ~40 cm
 * viewing distance). A fifth-and-beyond aircraft is summarised as a single
 * "+N weitere" count line instead of a fifth cramped row.
 *
 * Like screen_overhead.c and screen_wifi.c, the widget tree (chrome line,
 * the 4-row pool, the overflow line, the empty-sky sentence) is built ONCE
 * and only ever shown/hidden/re-texted afterwards — no widget is created or
 * destroyed after screen_list_create() returns, so calling
 * screen_list_update() on every poll never grows the object tree.
 */
#pragma once
#include "lvgl.h"
#include "flight_types.h"

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
 *                 Only the first 4 entries are ever shown as rows; anything
 *                 beyond that is folded into the "+N weitere" count line
 *                 instead of a fifth row. `n <= 0` renders the empty-sky
 *                 sentence instead of an empty list (AGENTS.md §1: never a
 *                 blank panel).
 * `routes`/`n_routes` - resolved routes to look up each visible aircraft's
 *                 destination in (via route_find(), main/net/route_parse.h).
 *                 A row whose callsign has no usable route (route_find()
 *                 returns NULL, or `resolved`/`plausible` is false — true for
 *                 roughly half of real traffic, DESIGN.md §5, not an edge
 *                 case) falls back to the aircraft's plain-language type.
 *
 * Caller MUST hold display_lock() (display.h) for the entire call — this
 * function makes LVGL calls directly and takes no lock of its own.
 */
void screen_list_update(const aircraft_t *ac, int n, const route_t *routes, int n_routes);

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
