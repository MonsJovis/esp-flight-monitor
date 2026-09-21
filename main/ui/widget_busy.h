/* widget_busy.h — the one way this device says "I am working on it".
 *
 * Three screens wait on the network in front of him, and until now all three
 * said so in words and nothing else: "Suche Netzwerke...", "Suche Orte...",
 * "ROUTE WIRD GESUCHT". A sentence is enough to know WHAT is happening. It is
 * not enough to know that anything still IS happening — a line of static text
 * looks exactly the same two seconds in and twenty seconds in, and the man
 * reading it has no way to tell a slow answer from a dead device. He taps
 * again. That is AGENTS.md §1's "never a silent panel" met on the letter and
 * missed on the point.
 *
 * So: one moving thing, the same moving thing everywhere, and nothing else
 * moves on this device. A 4 px line that sweeps left to right under whichever
 * sentence is doing the waiting. It carries no information he has to read —
 * the sentence above it already does that — it only has to be alive.
 *
 * SHARED for the same reason widget_input.c is shared: it must be IDENTICAL
 * on all three screens or it stops being one idea and becomes three
 * decorations. Small per-screen helpers (make_label, make_button) are copied
 * by convention in this codebase; this is not one of those.
 *
 * COLOUR: cyan, on every screen, including under the amber route tag. This is
 * not a semantic colour choice fighting AC 25-11A — cyan is already what this
 * device uses for "live" (the focused field border, a pressed key), and the
 * bar is not colour-coding a state, it is the device's pulse. No new token:
 * DESIGN.md §2's six stand.
 *
 * COST: the bar invalidates its own 440x4 px and nothing else, ~1.7k pixels a
 * frame against the 230k the panel pushes at 28.5 FPS with a full-screen
 * invalidate (PLAN.md M1). It is not free, and it stops the moment it is
 * switched off, which is the part that matters — see widget_busy_set_active().
 *
 * Caller holds display_lock(), like everything else that touches LVGL here.
 */
#pragma once
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Height of the bar, in px. Exported because callers lay out around it. */
#define WIDGET_BUSY_H 4

/* Builds the bar, `w` px wide, and returns it HIDDEN and NOT ANIMATING.
 * Position it like any other widget (lv_obj_set_pos) — unlike lv_keyboard,
 * this one does not align itself behind your back.
 *
 * Build it once with the rest of the screen, then switch it on and off. It is
 * two objects and a style; creating one per wait would put widget churn on
 * the display task, which is the thing every screen in this codebase is
 * written to avoid.
 */
lv_obj_t *widget_busy_create(lv_obj_t *parent, int32_t w);

/* Switches the bar on (visible, sweeping) or off (hidden, stopped).
 *
 * OFF REALLY STOPS IT. Hiding an object does not stop an animation attached
 * to it: lv_anim_timer keeps calling the exec callback, which keeps calling
 * lv_obj_set_x, which keeps invalidating an area nobody can see, thirty times
 * a second, forever. On a device that dims itself at 22:00 to save power and
 * that has a documented tearing risk (AGENTS.md §7), an invisible animation
 * that never ends is not a cosmetic bug. So this deletes the animation rather
 * than only hiding the object.
 *
 * GENUINELY idempotent in both directions, because the screens call it from
 * status handlers that fire more than once per state — screen_overhead.c
 * calls it once every UI tick for as long as a route lookup is outstanding.
 * It said this before and was not: every "on" call restarted the animation,
 * which puts the segment back at x = 0, so a 1400 ms sweep driven by a
 * 2000 ms tick snapped back to the left edge twice a cycle. An "on" call now
 * returns without touching LVGL unless the bar is stopped or its track has
 * been resized.
 */
void widget_busy_set_active(lv_obj_t *busy, bool active);

/* One ghost bar: a rounded rectangle the shape of a line of text that has not
 * arrived yet. The caller composes rows out of these, because only the caller
 * knows what its own rows look like — but the COLOUR lives here, so three
 * screens' skeletons cannot drift into three different greys.
 *
 * `dim` picks the quieter of the two tones, for a secondary line under a
 * primary one.
 *
 * Why a skeleton at all, when there is already a bar sweeping: the bar says
 * the device is working, the skeleton says WHERE the answer will appear. On a
 * screen whose whole content area is empty while it waits, that difference is
 * the difference between "it is thinking" and "it is thinking, and the list
 * is going to fill in there". It is also the reason nothing pulses or
 * shimmers here: the bar is the one moving thing, and a skeleton that
 * breathes on its own turns a calm wait into a busy one.
 */
lv_obj_t *widget_busy_ghost(lv_obj_t *parent, int32_t x, int32_t y,
                            int32_t w, int32_t h, bool dim);

#ifdef __cplusplus
}
#endif
