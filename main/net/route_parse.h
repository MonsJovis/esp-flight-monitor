/* Pure parser for adsb.im's /api/0/routeset response, plus the request-body
 * builder for the same endpoint.
 *
 * No networking, no ESP-IDF headers here (AGENTS.md §7, §9) — pure data and
 * string work, testable on the host in milliseconds. The HTTP POST itself
 * lives elsewhere.
 */
#pragma once
#include <stddef.h>
#include "flight_types.h"

/* Parses the routeset response — a top-level JSON ARRAY, one entry per
 * requested callsign — into `out`. An entry whose `airport_codes` is the
 * literal string "unknown" is a NORMAL outcome (private/GA aircraft with no
 * flight plan), not an error: resolved is set false and the ICAO/city fields
 * are left as "".
 *
 * Writes at most `max` entries. Returns the number written, or -1 if `json`
 * does not parse as a JSON array.
 */
int route_parse(const char *json, size_t len, route_t *out, int max);

/* Case-insensitive exact match on the trimmed callsign. Does NOT depend on
 * `routes` being in any particular order relative to the original request.
 * Returns NULL if `callsign` is absent from `routes`.
 */
const route_t *route_find(const route_t *routes, int n, const char *callsign);

/* Builds the routeset POST body:
 *   {"planes":[{"callsign":"AUA453","lat":47.6,"lng":15.9}, ...]}
 * Note the key is "lng", not "lon" (see test/fixtures/routeset_request.json).
 * Aircraft with an empty callsign are skipped.
 *
 * Returns the number of bytes written (excluding the NUL terminator), or -1
 * if the request would not fit in `out_sz`.
 */
int route_build_request(const aircraft_t *ac, int n, char *out, size_t out_sz);
