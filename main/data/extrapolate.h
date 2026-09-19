/* Where an aircraft is NOW, between polls.
 *
 * The data source is polled every 12 seconds (SRC_POLL_INTERVAL_MS), and
 * between polls the screens redraw from a snapshot that does not change. So
 * the radar marks sit perfectly still for twelve seconds and then jump — and
 * the jump is small enough to miss: on a 55 km scope an airliner at 450 kt
 * moves 7 px per poll, a Cessna at 100 kt moves 1.6 px. The panel looks
 * frozen, which is how it was first reported.
 *
 * Polling faster is not the answer: 12 s is already set by the source's rate
 * limits (AGENTS.md §5), and a faster poll would still be a series of jumps.
 *
 * Every real radar display solves this the same way, and so does this one:
 * carry the aircraft forward along its last known track at its last known
 * groundspeed. The feed already gives both. The result is not a guess in the
 * sense that matters here — it is strictly CLOSER to where the aircraft
 * actually is than a twelve-second-old fix, and it is what makes the picture
 * read as a picture of the sky rather than a screenshot of one.
 *
 * Pure: no ESP-IDF, no clock of its own, no allocation. The caller says how
 * old the fix is.
 */
#pragma once
#include <stdbool.h>

#include "flight_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Never carry a fix further than this. Beyond it the aircraft has probably
 * turned, descended, or landed, and a confidently-drawn mark somewhere it is
 * not is worse than a mark that stopped moving. Two failed polls plus change:
 * the display goes still, which is itself the honest signal that the data has
 * stopped arriving. */
#define EXTRAPOLATE_MAX_AGE_S 30.0f

/* Advances `ac` along its own track by `age_s` seconds, writing the result to
 * `out` (which may alias `ac`). Updates dst_nm and dir_deg only — the polar
 * position the screens actually draw from.
 *
 * Returns false and copies `ac` through unchanged when the aircraft cannot be
 * carried forward honestly:
 *   - age_s is <= 0, not finite, or beyond EXTRAPOLATE_MAX_AGE_S
 *   - the aircraft is not reporting a track (has_track false)
 *   - groundspeed is <= 0 (parked, or simply not reported)
 *   - the distance is DST_UNKNOWN, so there is no position to advance
 * In every one of those cases the mark stays where the feed last put it.
 */
bool aircraft_extrapolate(const aircraft_t *ac, float age_s, aircraft_t *out);

#ifdef __cplusplus
}
#endif
