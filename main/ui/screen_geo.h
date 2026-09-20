/* screen_geo.h — DESIGN.md §5.8 "Ort suchen".
 *
 * The entry path that AGENTS.md §6's "advanced custom option" never had. The
 * "Eigener Ort" card existed from M6 but there was no way to SET it: its
 * coordinates were whatever settings_defaults() had put there, read-only,
 * with a TODO in screen_settings.c saying a coordinate keypad was exactly the
 * "coordinate entry form" §6 exists to avoid. That was right about the
 * keypad and wrong about the conclusion — the answer is not a form, it is a
 * search, which is how everyone has entered a location on a device since
 * about 2008.
 *
 * So: he types a town, taps Suchen, and taps the right one out of a list.
 * Never a coordinate, never a keypad, never a decimal point.
 *
 * TWO STATES, one screen, only one visible at a time — the same shape
 * screen_wifi.c uses for its list and password steps, and for the same
 * reason: an on-screen keyboard and a scrollable list of results cannot both
 * have enough room on a 480 px panel, and hiding one behind the other is
 * less confusing than shrinking both.
 *
 *   TYPING  — title, one text field, Suchen/Zurück, and the keyboard.
 *   RESULTS — title, a status line that is never blank, the scrollable hit
 *             list, and Neu suchen/Zurück.
 *
 * This screen never touches the network. It does not call geocode_lookup()
 * (that blocks for seconds and must not run on the display task) and it does
 * not write settings — the integrator wires the callbacks below to
 * main/net/geocode.h and to settings_save(), exactly as it does for WLAN.
 *
 * ALL entry points below assume the caller already holds display_lock()
 * (display.h) — this file makes LVGL calls directly and takes no lock of its
 * own, matching screen_wifi.h and screen_overhead.h.
 */
#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "geo_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Longest query the field accepts. German place names run long
 * ("Sankt Martin im Sulmtal" is 23) but nothing useful is longer than this,
 * and geo_build_url()'s encoder is sized to match. */
#define SCREEN_GEO_QUERY_MAX 48

/* Builds the widget tree ONCE, as a full-bleed (480x480) child of `parent`.
 * Call exactly once per process lifetime — matches every other
 * single-instance screen in this codebase.
 *
 * The tree starts in the TYPING state with an empty field, which is where he
 * always wants to be on arrival: he opened this screen because he wants to
 * type something.
 *
 * Caller holds display_lock() for the duration of this call.
 */
void screen_geo_create(lv_obj_t *parent);

/* Hands this screen the result of the integrator's geocode_lookup() and
 * switches to the RESULTS state.
 *
 * `n` >= 0 is a hit count — INCLUDING ZERO, which is a normal answer meaning
 * the place does not exist or he mistyped it, and gets its own sentence
 * rather than an error. `n` < 0 is a transport failure and gets a different
 * one; see STR_GEO_NONE and STR_GEO_FAILED in main/strings_de.h.
 *
 * Safe to call after the screen has been closed — it checks, and returns.
 * That is not defensive padding: the search runs on its own task and lands
 * two to ten seconds later, by which time he may well have tapped Zurück,
 * and writing through a deleted widget tree is the D58 panic exactly.
 *
 * Caller holds display_lock() for the duration of this call.
 */
void screen_geo_set_results(const geo_place_t *places, int n);

/* Called when he taps Suchen (or the keyboard's own OK key). `query` is the
 * trimmed field contents and is only valid for the duration of the call.
 *
 * The integrator should start a task, call geocode_lookup() on it, and
 * report back through screen_geo_set_results(). This screen has already put
 * "Suche Orte..." on the status line by the time the callback fires, so a
 * lookup that takes three seconds still looks like something is happening.
 */
typedef void (*geo_search_cb)(const char *query);
void screen_geo_set_search_cb(geo_search_cb cb);

/* Called when he taps one of the hits. Everything the device needs to move
 * itself is in `place` — coordinates, the finished label for the settings
 * card, and the POSIX timezone, so that the clock follows the location the
 * way AGENTS.md §6 requires.
 *
 * The pointer is only valid for the duration of the call: it points into
 * this screen's own result array. Copy what you keep.
 *
 * This screen does not close itself afterwards. Whether picking a place
 * returns to Einstellungen is a navigation decision and navigation is the
 * integrator's (nav.c's) business — see main.c, where it does.
 */
typedef void (*geo_pick_cb)(const geo_place_t *place);
void screen_geo_set_pick_cb(geo_pick_cb cb);

/* Called when he taps Zurück, from either state. Not called when he taps
 * "Neu suchen", which only moves between this screen's own two states. */
typedef void (*geo_exit_cb)(void);
void screen_geo_set_exit_cb(geo_exit_cb cb);

/* Sends LV_EVENT_CLICKED to hit row `idx`, exactly as a fingertip would.
 *
 * A debug entry point, and it earns its place: tapping a hit is the one step
 * of this feature a build host cannot perform, and it is the step that
 * DELETES THIS SCREEN FROM INSIDE ONE OF ITS OWN EVENT CALLBACKS — the pick
 * callback navigates back to Einstellungen, which tears this overlay down
 * while LVGL is still dispatching the click that started it. Three shipped
 * paths already do that (both Zurück buttons and the WLAN screen's exit) and
 * LVGL 9 supports it, but "supported" and "verified on this hardware" are
 * different claims and D62 is what happens when they are confused.
 *
 * Calling on_geo_pick() directly from a task would test the settings write
 * and prove nothing about the teardown, because it would not be inside an
 * event dispatch at all. This is.
 *
 * Returns true if the click was dispatched. FALSE is the interesting answer
 * and the reason this is not void: the screen may have been torn down while
 * the search that produced `idx` was still on the wire, in which case there
 * is nothing to tap and the caller must not log that it tapped something.
 *
 * Out of range, no results, or screen gone: does nothing. Caller holds
 * display_lock().
 */
bool screen_geo_debug_tap(int idx);

#ifdef __cplusplus
}
#endif
