/* The HTTP half of the place search (§5.8). geo_parse.h is the other half and
 * holds everything that can be tested without a network.
 *
 * BLOCKING, like wifi_scan(). It opens a connection, waits on a free
 * community service and parses what comes back, so it must not run on the
 * display task — main.c gives it a task of its own, exactly as it does for
 * the WiFi scan, and hands the result back under display_lock().
 */
#pragma once
#include "geo_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the search screen shows at once. Six rows fill the list without
 * scrolling on a 480 px panel, and the two or three extra are there for
 * "Wien", which really does return four different places (test_geo.c). */
#define GEOCODE_MAX_RESULTS 8

/* Negative returns, distinct from any result count. The screen turns these
 * into one sentence — "Die Suche hat nicht geantwortet" — but they are kept
 * apart here because they are different things in a log line. */
#define GEOCODE_ERR_INVAL  (-1)   /* query too short, or bad arguments      */
#define GEOCODE_ERR_NET    (-2)   /* DNS, connect, timeout, or an HTTP error */
#define GEOCODE_ERR_PARSE  (-3)   /* answered, but not with JSON we can read */

/* Looks `query` up and writes at most `max` hits into `out`.
 *
 * Returns the number of hits (0 is a NORMAL answer — the place does not
 * exist, or he mistyped it), or one of the negatives above.
 *
 * Blocks for as long as the request takes, up to GEOCODE_TIMEOUT_MS. Never
 * call from the display task.
 */
int geocode_lookup(const char *query, geo_place_t *out, int max);

#ifdef __cplusplus
}
#endif
