/* screen_wifi.h — DESIGN.md §5.7 WLAN provisioning screen.
 *
 * The one screen that is allowed to interrupt him (DESIGN.md §6): it appears
 * by itself when no known network is in range. It exists instead of a
 * captive portal because a portal needs a phone, a second network join and a
 * browser, in a foreign country, from someone who will not read a manual
 * (AGENTS.md §8). That decision is only affordable if this screen actually
 * works standing in front of the device, so:
 *
 *   - Tapping an already-saved network reconnects with NO keyboard at all.
 *   - Tapping an unknown one opens a password step sized for a first-time,
 *     40 cm-away, elderly reader: 24 px minimum type, a visible/hidden
 *     toggle on the password (a password he cannot check is how he gets
 *     stuck in a loop), and Verbinden/Abbrechen buttons at least 56 px tall.
 *
 * This screen never touches the network itself. It does not call
 * wifi_scan() (that blocks for seconds and must not run on the display
 * task) and it does not call wifi_creds_set() (so a password never has a
 * chance to reach a log line from inside UI code) — see wifi_join_cb below.
 * The integrator wires this screen's callbacks to main/net/wifi.h and
 * drives screen_wifi_set_networks()/screen_wifi_set_status() from there.
 *
 * ALL entry points below (create, both setters, all three *_set_*_cb
 * setters) assume the caller already holds display_lock() (display.h) for
 * the duration of the call — this file makes LVGL calls directly and takes
 * no lock of its own, matching screen_overhead.h.
 */
#pragma once
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Matches net/wifi.h's WIFI_SSID_LEN (32 + NUL, IEEE 802.11 max SSID
 * length). Not #include-ing wifi.h here on purpose — this screen only ever
 * moves SSID/password strings around, it never calls a wifi_* function, and
 * the task brief's own signatures below spell out `33` literally. Kept as a
 * named constant instead of a bare literal so the one place that matters
 * (the array declarations) documents itself. */
#define SCREEN_WIFI_SSID_LEN 33

/* Builds the widget tree ONCE, as a full-bleed (480x480) child of `parent`.
 * Call exactly once per process lifetime — matches every other
 * single-instance screen in this codebase (screen_overhead.c).
 *
 * The tree starts in a valid, non-blank state (status line reads "Nicht
 * verbunden", the list shows "Keine Netzwerke gefunden" — AGENTS.md §1,
 * never a blank panel) even before the first screen_wifi_set_networks()/
 * screen_wifi_set_status() call.
 *
 * Caller holds display_lock() (display.h) for the duration of this call.
 */
void screen_wifi_create(lv_obj_t *parent);

/* Rebuilds the network list from the integrator's most recent wifi_scan()
 * result. `ssids`/`n` is what was found in range; `saved`/`n_saved` is what
 * wifi_creds_list() currently holds (SSIDs only — it deliberately never
 * returns a password, so this screen never sees one for an already-known
 * network either). A saved SSID that also appears in `ssids` gets the green
 * "gespeichert" tag (§1 of the task brief; colour is never the only carrier
 * — see the tag's tick + word in the .c file).
 *
 * This function only sets text/visibility on a pre-built, fixed-size pool
 * of rows (screen_wifi.c's SCREEN_WIFI_MAX_ROWS) — it never creates or
 * destroys a widget, so it is cheap enough to call after every scan. `n`
 * beyond the pool size is silently clamped; the list scrolls for anything
 * that still doesn't fit on screen.
 *
 * Caller holds display_lock() (display.h) for the duration of this call.
 * This screen never calls wifi_scan() itself — scanning blocks for seconds
 * and must stay off the display task; the integrator scans, then calls
 * this.
 */
void screen_wifi_set_networks(const char ssids[][33], int n,
                              const char saved[][33], int n_saved);

/* Sets the status line. Never leaves it blank (AGENTS.md §1):
 *   scanning==true                     -> "Suche Netzwerke..." (grey/label)
 *   scanning==false, connected==true   -> "Verbunden mit <ssid_or_null>" (green)
 *   scanning==false, connected==false,
 *     ssid_or_null != NULL             -> "Verbindung fehlgeschlagen: <ssid>" (amber)
 *   scanning==false, connected==false,
 *     ssid_or_null == NULL             -> "Nicht verbunden" (grey/label) —
 *                                          the one state the task brief didn't
 *                                          name; added so the line is never
 *                                          empty before the first scan result
 *                                          or status update arrives.
 * `scanning` takes priority over `connected`/`ssid_or_null` so a rescan
 * while still technically associated to the old network doesn't show two
 * contradictory messages at once.
 *
 * Caller holds display_lock() (display.h) for the duration of this call.
 */
void screen_wifi_set_status(const char *ssid_or_null, bool connected, bool scanning);

/* Called when he taps Verbinden (typed-password step) or taps an
 * already-saved row (no keyboard shown at all).
 *
 * For a SAVED row this screen calls `cb(ssid, NULL)` — it never had the
 * password to begin with (wifi_creds_list() never returns one, see
 * wifi.h), so NULL is the deliberate signal "(re)connect to `ssid` using
 * whatever is already stored for it in NVS", e.g. by calling
 * wifi_reconnect_now() without touching wifi_creds_set() at all.
 *
 * For an UNSAVED row this screen calls `cb(ssid, password)` with whatever
 * was typed into the password text area. This screen never calls
 * wifi_creds_set() itself and never logs `password` — storing it is the
 * integrator's job (AGENTS.md §10: secrets never in the repo, and never in
 * a log line). The pointer is only valid for the duration of the call; it
 * points at the password text area's own buffer, which this screen clears
 * immediately afterwards.
 */
typedef void (*wifi_join_cb)(const char *ssid, const char *password);
void screen_wifi_set_join_cb(wifi_join_cb cb);

/* Called when he taps "Suchen". This screen does not call wifi_scan()
 * itself (it blocks for seconds and must not run on the display task) —
 * the integrator's callback should kick off a scan on its own task/thread
 * and report back through screen_wifi_set_networks() and
 * screen_wifi_set_status(NULL, false, true) while it runs.
 */
typedef void (*wifi_rescan_cb)(void);
void screen_wifi_set_rescan_cb(wifi_rescan_cb cb);

/* Called when he taps the exit row (leaving this screen, e.g. back to
 * Einstellungen — DESIGN.md §6). Not called when he cancels the password
 * step; Abbrechen there just returns to the network list on this same
 * screen.
 */
typedef void (*wifi_exit_cb)(void);
void screen_wifi_set_exit_cb(wifi_exit_cb cb);

/* Opens the password step from the build host, and says how it got there.
 *
 * Returns:
 *   SCREEN_WIFI_PW_TAPPED — an unsaved network was in range and its row was
 *       CLICKED, exactly as a fingertip would. The whole path.
 *   SCREEN_WIFI_PW_FORCED — networks were in range but every one of them is
 *       already saved, so there was no row a finger could have tapped to get
 *       here. The step was opened directly instead. The keyboard is on the
 *       glass and can be photographed; the row-tap that normally leads to it
 *       was NOT exercised, and a caller that reports this as the same thing
 *       is reporting a check it did not run.
 *   SCREEN_WIFI_PW_NONE — no networks at all. Nothing was opened.
 *
 * It never taps a SAVED row even when that is the only row there: a tap on a
 * saved row does not open anything, it starts a join with stored credentials,
 * and a debug command must not reconnect the device as a side effect.
 *
 * A debug entry point, and it earns its place more than most: the password
 * step is the one screen in this product that cannot be reached from the
 * build host, because reaching it requires tapping a network the device has
 * no credentials for. That is not a footnote. Its keyboard was positioned
 * off the bottom edge of the panel from M6 until 2026-09-20 — four
 * milestones of a password step with no keyboard on it — and it survived
 * precisely because every check of this screen in this repo stopped at the
 * network list. The fix was one line. Finding it took building a second
 * keyboard next door and hitting the same wall.
 *
 * So the hole gets a door. `K` on the serial console opens WLAN, scans, and
 * calls this, and tools/grab_screen.py reads back what is actually on the
 * glass (D4, D41).
 *
 * UNSAVED, deliberately, and never a saved one: tapping a saved row does not
 * open anything, it starts a join with stored credentials. This must not have
 * a side effect on the network the device is using.
 *
 * Nothing is typed and nothing is stored: it opens the step and stops. The
 * password itself is his to type, on the panel, and never travels through a
 * console or a transcript (AGENTS.md §10).
 *
 * Caller holds display_lock().
 */
#define SCREEN_WIFI_PW_NONE   0
#define SCREEN_WIFI_PW_TAPPED 1
#define SCREEN_WIFI_PW_FORCED 2
int screen_wifi_debug_password_step(void);

#ifdef __cplusplus
}
#endif
