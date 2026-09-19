/* The German bridge between the parsed API data and the panel (docs/PLAN.md
 * M2.5/M3): turns an `aircraft_t` + `route_t` into the exact `view_model_t`
 * DESIGN.md §5.1–§5.3 describes. No hardware, no network — pure data and
 * string work, testable on the host in milliseconds (test/host/test_view.c).
 *
 * This module OWNS the decision of which of the three states applies and
 * WHY a route is missing; it is the only place that reads `ac_type_t`'s
 * `category` field for that purpose. It reads main/data/fmt_de.h and
 * main/data/tables.h for every number, unit and name — it does not
 * reimplement any of that formatting itself.
 */
#pragma once
#include <stdbool.h>
#include <time.h>

#include "flight_types.h"
#include "view_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the view for the nearest-aircraft screen (DESIGN.md §5.1/§5.2/§5.3).
 *
 * `ac`    - the nearest aircraft, or NULL when the sky is empty (§5.3, with
 *           no history — prefer view_build_empty() when a last-seen aircraft
 *           is available).
 * `route` - the resolved route for `ac->flight`, or NULL when none was
 *           looked up / found. May be non-NULL with `resolved == false`
 *           (queried, no flight plan) or `plausible == false` (resolved but
 *           the API flagged it as implausible) — both count as "no usable
 *           route" and produce VIEW_NO_ROUTE, not VIEW_OVERHEAD.
 * `now`   - wall-clock time for the chrome (clock, date_line).
 * `traffic_count`, `online` - passed straight through to the chrome.
 * `out`   - fully overwritten; every field NUL-terminated, nothing left
 *           uninitialised. When `ac->dst_nm == DST_UNKNOWN` (flight_types.h),
 *           `distance` becomes the em dash "\xE2\x80\x94" (never a negative
 *           number) and `direction_word`/`direction_abbr` are left blank,
 *           since a bearing without a distance is not meaningful to show.
 *
 * State chosen:
 *   ac == NULL                                  -> VIEW_EMPTY_SKY
 *   ac != NULL, route resolved AND plausible     -> VIEW_OVERHEAD
 *   ac != NULL, otherwise                        -> VIEW_NO_ROUTE
 */
/* As view_build(), but says whether the route lookup is still outstanding.
 * Pass route_searching = true only when the callsign has been sent to the
 * routeset API and no answer has come back yet — NOT when the answer was
 * "this aircraft has no flight plan". */
void view_build_ex(const aircraft_t *ac, const route_t *route, bool route_searching,
                   const struct tm *now, int traffic_count, net_state_t net,
                   view_model_t *out);

void view_build(const aircraft_t *ac, const route_t *route, const struct tm *now,
                 int traffic_count, net_state_t net, view_model_t *out);

/* Builds the view for DESIGN.md §5.3 "Himmel frei" — no aircraft currently
 * in range. Always VIEW_EMPTY_SKY. `last_seen` is optional (NULL when
 * nothing has ever been seen this session); when present its plain-language
 * type, altitude, distance and direction are shown so the screen still says
 * something rather than only showing the clock. `traffic_count` is always 0
 * by construction (an empty sky has none in range).
 */
void view_build_empty(const struct tm *now, const aircraft_t *last_seen, net_state_t net,
                       view_model_t *out);

#ifdef __cplusplus
}
#endif
