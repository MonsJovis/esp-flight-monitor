/* Pure parser for tar1090-shaped position responses (adsb.lol v2, adsb.fi v2/v3).
 *
 * No networking, no ESP-IDF headers here (AGENTS.md §7, §9) — this file takes a
 * JSON buffer and fills plain aircraft_t structs so it can be unit-tested on the
 * host in milliseconds. The HTTP fetch lives elsewhere.
 */
#pragma once
#include <stddef.h>
#include "flight_types.h"

/* Parses either wrapper shape — {"ac":[...]} (adsb.lol v2, adsb.fi v3) or
 * {"aircraft":[...]} (adsb.fi v2) — into `out`, sorted ascending by dst_nm so
 * out[0] is the nearest aircraft ("the plane overhead").
 *
 * Writes at most `max` entries (extras are silently dropped, not overflowed).
 * Returns the number of aircraft written, or -1 if `json` does not parse as a
 * JSON object containing an array under either wrapper key.
 */
int adsb_parse(const char *json, size_t len, aircraft_t *out, int max);
