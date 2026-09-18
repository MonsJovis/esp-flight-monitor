/* screen_overhead.h — DESIGN.md §5.1/§5.2/§5.3, one screen in three states:
 *
 *   VIEW_OVERHEAD   the answer: destination as hero, route the headline.
 *   VIEW_NO_ROUTE   co-equal, not a fallback: hero swaps to the aircraft
 *                   type and says WHY there is no route.
 *   VIEW_EMPTY_SKY  never a blank panel: clock, date, last aircraft seen.
 *
 * The three states share one widget tree, built once and then only shown,
 * hidden and re-texted per poll (DESIGN.md §6: "he never navigates between
 * them", so there is nothing here that looks like page switching).
 */
#pragma once
#include "lvgl.h"
#include "view_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the widget tree ONCE, as a full-bleed (480x480) child of `parent`.
 * Call exactly once per process lifetime — it does not check for or clean
 * up a previous tree, matching every other single-instance screen in this
 * codebase (see main/debug/dbg_screen.c).
 *
 * The tree starts in a valid but empty-looking state; call
 * screen_overhead_update() at least once before the first frame is flushed.
 *
 * Caller holds display_lock() (display.h) for the duration of this call.
 */
void screen_overhead_create(lv_obj_t *parent);

/* Cheap per-poll update: sets text, colours and visibility for the state
 * carried in `vm`. Never creates or destroys a widget — show/hide only —
 * so it is safe to call on every poll (AGENTS.md §5: every 10-15 s) without
 * growing the object tree or touching the heap.
 *
 * Caller MUST hold display_lock() (display.h) for the entire call — this
 * function makes LVGL calls directly and takes no lock of its own.
 */
void screen_overhead_update(const view_model_t *vm);

/* The hero font size actually used by the most recent screen_overhead_update()
 * call that rendered a destination/type name (VIEW_OVERHEAD or
 * VIEW_NO_ROUTE) — 100, 76 or 56. VIEW_EMPTY_SKY's clock does not go through
 * the auto-shrink ladder, so it leaves this value unchanged.
 *
 * For the integrator to log per docs/PLAN.md's M3 measurement, "longest
 * destination name that fits at 100 px".
 */
int32_t screen_overhead_hero_size_px(void);

#ifdef __cplusplus
}
#endif
