/* screen_settings.h — DESIGN.md §5.6 "Einstellungen".
 *
 * He is shown this screen once and, ideally, never again — reached by a
 * long-press on the chrome bar (DESIGN.md §6), never by swipe. That framing
 * drives every choice in screen_settings.c:
 *
 *   - Location is four tappable cards (Gloggnitz / Wien / Pattaya / Eigener
 *     Ort), never a coordinate form. AGENTS.md §6: "switching location should
 *     be one tap, not a coordinate entry form." "Eigener Ort" shows the place
 *     the search last found — "Innsbruck · Tirol" — or, until he has searched
 *     once, its stored coordinates. Setting it is one row further down
 *     ("Ort suchen", §5.8, screen_geo.h) rather than an entry field competing
 *     with the one-tap preset switch that is the point of this section.
 *   - Every tappable row is >= 56 px tall, every label he has to read is
 *     >= 24 px (DESIGN.md §3's Near tier, ~40 cm), and the active location
 *     card carries a word ("Aktiv") as well as its magenta fill — DO-257A
 *     §2.1.6, never colour alone.
 *   - The screen scrolls (content is taller than 480 px); the first section
 *     is fully visible without scrolling since he may not realise it does.
 *
 * Like screen_overhead, this is a single-instance screen: file-scope
 * statics, built once by screen_settings_create(), refreshed by
 * screen_settings_update(). Three callbacks hand persistence, WiFi
 * navigation and exit navigation to the integrator — this file knows
 * nothing about NVS, the swipe deck, or the WiFi screen.
 */
#pragma once
#include "lvgl.h"
#include "settings.h"
#include "fmt_de.h"    /* update_state_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the widget tree ONCE, as a full-bleed (480x480) scrollable child of
 * `parent`. Call exactly once per process lifetime — it does not check for
 * or clean up a previous tree, matching every other single-instance screen
 * in this codebase (see screen_overhead.h).
 *
 * The tree starts in a valid but default-looking state (settings_defaults());
 * call screen_settings_update() at least once before the first frame is
 * flushed so the real, persisted settings show instead.
 *
 * Caller holds display_lock() (display.h) for the duration of this call.
 */
void screen_settings_create(lv_obj_t *parent);

/* Cheap per-call update: reflects every field of `s` onto the already-built
 * widget tree — which location card is active (fill + border + the "Aktiv"
 * word), the radius and brightness sliders and their numeric read-outs, the
 * auto-dim switch and its window text, and the second line of the "Eigener
 * Ort" card (the searched place's name, or its coordinates until there is
 * one — the font changes with it, which is why that line is re-styled here
 * and not only re-texted). Never creates or destroys a widget, so it is
 * safe to call whenever the integrator's copy of `settings_t` changes —
 * after loading from NVS, and again after a settings_changed_cb round-trip.
 *
 * Caller MUST hold display_lock() (display.h) for the entire call — this
 * function makes LVGL calls directly and takes no lock of its own.
 */
void screen_settings_update(const settings_t *s);

/* Sets the one line under the "Akku" heading — finished German, built by
 * battery_line_text() (main/power/battery_policy.h). Separate from
 * screen_settings_update() because the battery is not part of settings_t and
 * has no business making a settings round-trip look like a user change.
 *
 * NULL or an empty string leaves whatever is there alone: a transient I2C
 * fault on the PMIC must not blank a line he is reading.
 *
 * SAFE TO CALL WHILE THE SCREEN IS CLOSED, unlike screen_settings_update().
 * The text is kept in this module and painted onto the label the next time
 * one exists — which matters because the poll behind it runs every ten
 * seconds and this screen is an overlay that spends nearly all of its life
 * deleted.
 *
 * Caller MUST hold display_lock().
 */
void screen_settings_set_battery(const char *line);

/* ---- Software: the update row (D74) ------------------------------------
 *
 * The device already updates itself in the night window. This is the half he
 * can reach: a row that checks, and — once something is offered — installs
 * without waiting for 22:00.
 *
 * Both callbacks fire on the display task from a tap. Wire them to
 * ota_request_check() and ota_request_install_now(); this file never touches
 * the network, exactly as screen_geo.h and screen_wifi.h do not.
 */
typedef void (*settings_update_cb)(void);
void screen_settings_set_update_cbs(settings_update_cb check,
                                    settings_update_cb install);

/* Move the row to `state`. `version` is read only for UPD_AVAILABLE.
 *
 * Safe to call when the screen is closed — the state is kept here and
 * rendered the next time it is built, the same arrangement
 * screen_settings_set_battery() uses and for the same reason (D58: a task
 * writing through a label pointer into a deleted tree).
 *
 * UPD_INSTALLING raises a full-screen takeover on lv_layer_top() that
 * swallows touch for the ~26 s the download and flash write take, and
 * survives the settings overlay being torn down underneath it. Any other
 * state takes it down again.
 *
 * Caller holds display_lock(). */
void screen_settings_set_update_state(update_state_t state, const char *version);

/* Fired once per completed user change: a location card tap, the auto-dim
 * switch toggling, or a slider drag ENDING — deliberately not once per pixel
 * dragged. AGENTS.md §7's flash-tearing bug is provoked by NVS commits, and
 * "persist on this callback" is the obvious integration, so this file does
 * not invite a write storm during a slider drag; see the slider event
 * handlers in screen_settings.c.
 *
 * `s` points at this screen's own working copy of the settings and is only
 * valid for the duration of the call — copy it if it needs to outlive the
 * call. The integrator is expected to sanitise (settings_sanitise()),
 * persist (settings_save()) and apply the result, then typically call
 * screen_settings_update() back with the authoritative value.
 */
typedef void (*settings_changed_cb)(const settings_t *s);
void screen_settings_set_cb(settings_changed_cb cb);

/* Fired when the WLAN row is tapped. Wire this to DESIGN.md §5.7's WiFi
 * screen — this file has no knowledge that screen exists. */
typedef void (*settings_wifi_cb)(void);
void screen_settings_set_wifi_cb(settings_wifi_cb cb);

/* Fired when "Zurück" is tapped. This screen is reached by long-press and is
 * not in the swipe deck (DESIGN.md §6), so leaving it is entirely this
 * callback's job — wire it back to whatever screen was showing before. */
/* Called when he taps "Ort suchen". Opens §5.8 (screen_geo.h); this screen
 * knows nothing about it beyond that somebody else will handle it, the same
 * arrangement the WLAN row has. */
typedef void (*settings_geo_cb)(void);
void screen_settings_set_geo_cb(settings_geo_cb cb);

typedef void (*settings_exit_cb)(void);
void screen_settings_set_exit_cb(settings_exit_cb cb);

#ifdef __cplusplus
}
#endif
