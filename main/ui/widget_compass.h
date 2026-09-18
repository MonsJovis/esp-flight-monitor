/* widget_compass.h — the compass tape (DESIGN.md §4, "compass tape" band).
 *
 * A horizontal strip showing the bearing TO the tracked aircraft, in the
 * avionics idiom: fixed German cardinal marks along the tape, a magenta
 * marker that slides to the live bearing, and a small numeric read-out
 * beside it — colour never carries the meaning alone (DO-257A §2.1.6).
 * There is no LVGL widget for this, so it is built from plain objects and
 * labels: a coloured strip for the baseline, a rounded coloured object for
 * the marker, and lv_label for every piece of text.
 *
 * This widget does no lookup, no translation and no unit conversion of its
 * own — the caller (screen_overhead.c) owns every German string, including
 * the eight cardinal labels, per AGENTS.md §10 ("keep all user-facing
 * strings in one translation unit"). The one exception is the numeric
 * degree read-out ("42°"): view_model_t hands this widget a raw float
 * (view_model_t.bearing_deg) specifically for tape *positioning*, so turning
 * it into a short number with a degree sign is this widget's own
 * presentation logic, not a re-do of the German/units work that lives in
 * main/data — there is no formatted string for it anywhere upstream.
 */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIDGET_COMPASS_HEIGHT        56
#define WIDGET_COMPASS_NUM_CARDINALS 8   /* N, NO, O, SO, S, SW, W, NW — 45° apart */

/* Builds the tape ONCE as a child of `parent`, `width` px wide and
 * WIDGET_COMPASS_HEIGHT px tall, positioned at (0, 0) inside its own
 * bounding box — the caller places that box with lv_obj_set_pos().
 *
 * `cardinal_labels` supplies the WIDGET_COMPASS_NUM_CARDINALS strings for
 * 0°, 45°, 90° ... 315°, in that order, starting at true north. Ownership
 * stays with the caller, but the strings are copied into the tick labels
 * immediately, so the array need not outlive this call.
 *
 * This device has exactly one compass tape, so the widget keeps its child
 * handles in file-scope statics rather than heap-allocated per-instance
 * state — consistent with AGENTS.md §10 (fixed storage, no heap). Calling
 * this a second time would silently rebind those statics to the new
 * instance; the API still takes `parent`/returns the built object so a
 * second, independent tape can be added later without an interface change.
 *
 * Caller holds display_lock() (display.h) for the duration of this call.
 */
lv_obj_t *widget_compass_create(lv_obj_t *parent, int32_t width,
                                 const char *const cardinal_labels[WIDGET_COMPASS_NUM_CARDINALS]);

/* Cheap per-poll update: repositions the bearing marker and rewrites the two
 * small read-out labels beside it. Never touches the fixed cardinal ticks
 * and never allocates.
 *
 *   compass      the object returned by widget_compass_create()
 *   bearing_deg  0..360, degrees TO the aircraft (view_model_t.bearing_deg)
 *   bearing_abbr e.g. "NO" — already German (view_model_t.direction_abbr).
 *                This widget positions it; it does no lookup of its own.
 *
 * Caller holds display_lock() (display.h) for the duration of this call.
 */
void widget_compass_set_bearing(lv_obj_t *compass, float bearing_deg, const char *bearing_abbr);

#ifdef __cplusplus
}
#endif
