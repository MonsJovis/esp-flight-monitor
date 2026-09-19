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
 * (main/net/route_parse.h) to decide route-known (cyan, or magenta for the
 * single nearest aircraft) vs no-route (amber) — see screen_radar.c's top
 * comment for the full colour/shape table. Never creates or destroys a
 * widget, so it is safe to call on every poll (AGENTS.md §5: every 10-15 s)
 * without growing the object tree or touching the heap.
 *
 * Caller MUST hold display_lock() (display.h) for the entire call — this
 * function makes LVGL calls directly and takes no lock of its own.
 */
void screen_radar_update(const aircraft_t *ac, int n,
                         const route_t *routes, int n_routes, int radius_nm);

#ifdef __cplusplus
}
#endif
