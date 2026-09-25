/* Pure parser for tar1090-shaped position responses (adsb.lol v2, adsb.fi v2/v3).
 *
 * No networking, no ESP-IDF headers here (AGENTS.md §7, §9) — this file takes a
 * JSON buffer and fills plain aircraft_t structs so it can be unit-tested on the
 * host in milliseconds. The HTTP fetch lives elsewhere.
 */
#pragma once
#include <stdbool.h>
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

/* The same, with a say over what is kept (D83). `keep` sees every aircraft
 * the parser would otherwise consider, BEFORE the nearest-`max` cut — so what
 * it drops can never push out something it would have kept. NULL keeps all. */
typedef bool (*adsb_keep_fn)(const aircraft_t *ac, void *ctx);
int adsb_parse_ex(const char *json, size_t len, aircraft_t *out, int max,
                  adsb_keep_fn keep, void *ctx);

/* Re-applies that ordering to a list already in hand.
 *
 * Exposed because the ordering rule — nearest first, and an aircraft whose
 * distance is unknown never wins slot 0 — belongs to this module and is
 * relied on structurally elsewhere (screen_list.c treats row 0 as "the
 * nearest", main.c reads ac[0] as "the plane overhead"). Anything that
 * changes a distance after parsing has to restore it, and must not
 * reimplement the DST_UNKNOWN half by hand: see main/data/extrapolate.h,
 * which moves every aircraft between polls.
 */
void adsb_sort_by_distance(aircraft_t *ac, int n);
