/* The polling brain (PLAN.md M2). Owns a FreeRTOS task that polls positions,
 * batches route resolution, and publishes both behind a mutex.
 *
 * NO LVGL CALLS WHATSOEVER — this task must never touch the UI (AGENTS.md
 * §10: network work lives on its own task, all LVGL calls happen on the
 * display task behind display_lock()/display_unlock()). The UI task reads
 * the published snapshot via flight_source_snapshot().
 *
 * Failure handling is DEGRADE, not switch: there is exactly one enabled
 * position source right now (see source_logic.h for why — opendata.adsb.fi
 * redirects plain HTTP to https:// and this project carries no TLS stack in
 * M2). On repeated failure this module keeps polling the same source with
 * exponential backoff and keeps publishing the last good snapshot rather
 * than blanking it (AGENTS.md §1: a blank panel reads as broken). Call
 * flight_source_is_stale() to know whether that snapshot is aging.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "flight_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The three states a callsign's route can be in. route_t itself only has a
 * two-way `resolved` bool, which cannot distinguish "still looking" from
 * "definitively has no flight plan" — showing "no route" while still
 * looking teaches the user to distrust the panel (PLAN.md M4). */
typedef enum {
    ROUTE_STATUS_RESOLVING = 0, /* asked (or queued to ask), no answer yet */
    ROUTE_STATUS_RESOLVED,      /* asked, got a route                     */
    ROUTE_STATUS_NONE,          /* asked, definitively no flight plan     */
} route_status_t;

/* Starts the polling task at (lat, lon, radius_nm). Safe to call once; a
 * second call logs a warning and returns ESP_ERR_INVALID_STATE without
 * starting a second task. */
esp_err_t flight_source_start(double lat, double lon, int radius_nm);

/* Changes where the next poll queries. Takes effect on the poll after next;
 * does not restart the task, clear the published snapshot, or clear the
 * route cache (routes are keyed on callsign, not location, so a relocation
 * does not invalidate them — AGENTS.md §5). */
void flight_source_set_location(double lat, double lon, int radius_nm);

/* Copies the current aircraft snapshot (already sorted ascending by
 * dst_nm — out[0] is "the plane overhead") into `out`, and the matching
 * route for each into `routes` (routes[i] corresponds to out[i]), all under
 * mutex. Writes at most min(max, max_routes) route entries; pass
 * max_routes = 0 / routes = NULL to skip routes entirely.
 *
 * A route not yet resolved comes back with routes[i].resolved == false;
 * use flight_source_route_status(out[i].flight) to tell "still looking"
 * (ROUTE_STATUS_RESOLVING) apart from "definitively none"
 * (ROUTE_STATUS_NONE) — route_t alone cannot make that distinction.
 *
 * Returns the number of aircraft written (0 if nothing has been fetched
 * yet, or if the last fetch found an empty sky).
 */
int flight_source_snapshot(aircraft_t *out, int max, route_t *routes, int max_routes);

/* The resolution state of `callsign` (trimmed, case-insensitive). A
 * callsign never seen before reads as ROUTE_STATUS_RESOLVING — it will be
 * queued into the next batch request the moment it appears in a poll. */
route_status_t flight_source_route_status(const char *callsign);

/* ---- Health accessors --------------------------------------------------
 *
 * The coordinator's design (source_logic.h): with one enabled source there
 * is nothing to fail over to, so these expose enough for the UI to render
 * an honest amber "keine Verbindung" caution over the last good snapshot,
 * per AGENTS.md §1's "never show a blank/broken-looking screen" rule.
 */

/* How many polls in a row have failed (0 = the most recent poll succeeded). */
int flight_source_consecutive_failures(void);

/* Name of the source currently being polled (for logging; never used to
 * build a URL). Fixed to "adsb.lol (point)" until M4 enables a second
 * source. */
const char *flight_source_current_source_name(void);

/* esp_timer-based ms timestamp of the last successful poll. 0 if no poll
 * has ever succeeded. */
int64_t flight_source_last_success_ms(void);

/* Milliseconds since the last successful poll. INT64_MAX if no poll has
 * ever succeeded (so simple ">" staleness thresholds behave sensibly). */
int64_t flight_source_last_success_age_ms(void);

/* True once the most recent poll attempt failed (bad HTTP status, timeout,
 * throttling, or a parse failure) — i.e. the published snapshot is aging
 * and the UI should show a caution rather than treat it as fresh. Becomes
 * false again the moment a poll succeeds. */
bool flight_source_is_stale(void);

#ifdef __cplusplus
}
#endif
