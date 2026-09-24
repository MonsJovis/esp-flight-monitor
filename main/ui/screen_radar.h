/* screen_radar.h — DESIGN.md §5.5 "Radar": the PPI scope, page 3 of the
 * three-page swipe deck (DESIGN.md §6) — his "show me the whole sky" view.
 *
 * DESIGN.md §1/§3 is explicit that this screen is the fun one to build and
 * the less useful one, so it ships last and must not trade readability for
 * spectacle. Two rules from DESIGN.md drive every choice in screen_radar.c:
 *
 *   - §3's warning: the original mockups put radar city labels at 10-11 px,
 *     *below* even this screen's own near-tier floor (~40 cm viewing
 *     distance, 17 px cap height / ~24 px font). See screen_radar.c's top
 *     comment for exactly how this file resolves that — the short version
 *     is fewer, bigger labels, not smaller type.
 *   - §2's six-colour ceiling and "never encode by colour alone" rule
 *     (RTCA DO-257A §2.1.6) — every coloured mark on this screen also
 *     carries a distinct shape.
 *
 * Bearing convention: north is screen-up, and bearing increases clockwise,
 * exactly like a compass rose (0 deg/N -> up, 90 deg/O -> right, 180 deg/S
 * -> down, 270 deg/W -> left). German uses "O" for Ost, never "E"
 * (AGENTS.md §1, §10) — the classic bug this file is careful not to repeat.
 *
 * The aircraft glyph (a small triangle/arrow rotated by track_deg) is
 * implemented from scratch with plain LVGL draw primitives, not copied from
 * the MIT-licensed radar/plane-spotter projects surveyed in docs/RESEARCH.md
 * — the simplest way to keep this file's licence position unambiguous
 * (AGENTS.md §9).
 *
 * Like screen_overhead and screen_settings, this is a single-instance
 * screen: file-scope statics, MAX_AIRCRAFT (flight_types.h) mark objects
 * built once by screen_radar_create() and only shown/hidden/recoloured by
 * screen_radar_update() — never created or destroyed per update. M1
 * measured a 28.5 FPS ceiling and the poll driving this screen runs every
 * 12 s (AGENTS.md §5), so object churn on every update would buy nothing
 * and cost PSRAM bandwidth the framebuffer also needs.
 */
#pragma once
#include "lvgl.h"
#include "flight_types.h"
#include "view_model.h"   /* net_state_t */

/* ---- Geometry, px. Public so test/sim measures against these numbers rather
 * than a copy of them. ----
 *
 * The scope sits 22 px above the panel's centre and is a little smaller than
 * it was (outer ring 140 -> 136) since D78, to make room for the caption's
 * second line: the "S" cardinal's ink ends at y ~387, the caption runs from
 * RADAR_CAPTION_Y to ~462, and the page dots start at 464. The "N" still
 * clears the top chrome row (y 24-41). The last 4 px of that lift are there
 * so the grey S does not sit on the grey first caption line and read as part
 * of it — seen in the first render, not predicted. The whole scope stays inside
 * DESIGN.md §4's ~340 px round-panel circle. */
#define RADAR_CX          240
#define RADAR_CY          218
#define RADAR_R_OUTER     136   /* full radius_nm */
#define RADAR_CARDINAL_R  160   /* N/O/S/W, just outside the outer ring */

/* Caption: line 1 (who · model) at RADAR_CAPTION_Y, line 2 (destination,
 * distance, arrow) RADAR_CAPTION_LINE2_DY below it. The two label boxes
 * overlap by 4 px — Plex Sans' 31 px line box carries 6 px below its
 * baseline — which still leaves ~11 px of clear space between the ink of
 * the two lines. */
#define RADAR_CAPTION_Y        394
#define RADAR_CAPTION_LINE2_DY 27

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the widget tree ONCE, as a full-bleed (480x480) child of `parent`:
 * the three range rings, the four cardinal marks (N/O/S/W), the home
 * marker, MAX_AIRCRAFT aircraft mark objects (hidden until the first
 * update) and the two nearest-aircraft label slots (see screen_radar.c for
 * why exactly two). Call exactly once per process lifetime — it does not
 * check for or clean up a previous tree, matching every other
 * single-instance screen in this codebase (see screen_overhead.h).
 *
 * The tree starts in a valid but empty-looking state (no aircraft, no range
 * readout); call screen_radar_update() at least once before the first frame
 * is flushed.
 *
 * Caller holds display_lock() (display.h) for the duration of this call.
 */
void screen_radar_create(lv_obj_t *parent);

/* Cheap per-poll update: repositions and recolours the existing mark
 * objects for the `n` aircraft in `ac` (defensively re-capped at
 * MAX_AIRCRAFT — flight_types.h — even though the caller should already
 * respect it), scaled so `radius_nm` maps to the outer ring; anything
 * farther is clipped to the outer ring rather than drawn outside it.
 * `routes`/`n_routes` are looked up per aircraft via route_find()
 * (main/net/route_parse.h) to decide the fill: filled with a route, hollow
 * without one. Colour is magenta for the single nearest aircraft and cyan
 * for every other, route or not — no-route is no longer amber (D76). Rings
 * whichever aircraft the caption names (D75) — see screen_radar.c's top
 * comment for the full colour/shape table. Never creates or destroys a
 * widget, so it is safe to call on every poll (AGENTS.md §5: every 10-15 s)
 * without growing the object tree or touching the heap.
 *
 * Caller MUST hold display_lock() (display.h) for the entire call — this
 * function makes LVGL calls directly and takes no lock of its own.
 */
void screen_radar_update(const aircraft_t *ac, int n,
                         const route_t *routes, int n_routes, int radius_nm);

/* --- Touch ---------------------------------------------------------------
 *
 * Two taps, deliberately different:
 *
 *   Tapping a MARK re-points the caption at that aircraft, and moves the white
 *   selection ring onto it, so the answer at the bottom edge and the mark he
 *   actually touched are visibly the same aircraft (D75). It stays there —
 *   through polls, and through the aircraft moving — until he taps another
 *   mark, or the aircraft leaves the ring, at which point the caption falls
 *   back to the nearest. Nothing leaves the screen: he is reading the scope,
 *   and pulling him off it to answer "which one is that" would be the wrong
 *   trade.
 *
 *   Tapping the CAPTION is the one that commits: it asks for the full view of
 *   whatever the caption is currently naming. The caption is the only thing on
 *   this screen with words on it, so it is the only thing that reads as "press
 *   me for more".
 *
 * The callback is handed the aircraft's ICAO hex rather than an aircraft_t,
 * because by the time it runs the caller's array has usually been re-sorted
 * (main.c carries every fix forward between polls, which can change who is
 * nearest). The hex is the only identifier that survives that. */
typedef void (*radar_select_cb)(const char *hex);

/* Registered once at startup; NULL disables the caption tap. */
void screen_radar_set_select_cb(radar_select_cb cb);

/* Drops any mark selection, so the caption goes back to the nearest aircraft.
 * Call when the deck leaves this page: a selection he made five minutes ago
 * (safe to call without display_lock() — it writes one string and no LVGL)
 * is not what he means by a glance. */
void screen_radar_clear_selection(void);

/* Whether what the scope shows is live. Anything but NET_OK dims every mark,
 * stops the trails, and puts the detail layer's own amber tag (KEIN NETZ /
 * KEINE DATEN) top centre, in the identity line's slot (the top left is the
 * range read-out) — the radar is the default screen, and
 * until D76 it was the one screen that could not say its picture was old.
 * Takes effect on the next screen_radar_update(); call under display_lock()
 * like the rest of this API (it only stores a value, but it is read by code
 * that is not). */
void screen_radar_set_net(net_state_t net);

/* The time, top right.
 *
 * It lives here because the Radar is now the default view, and the clock used
 * to sit on the hero screen — which is a layer down since the deck was
 * reordered. Losing it silently in a navigation change would be taking a
 * feature away by accident. Chrome tier, same 13 px mono as the range
 * read-out it balances. Pass "" or NULL to hide it (the clock is not yet set).
 */
void screen_radar_set_clock(const char *hhmm);

#ifdef __cplusplus
}
#endif
