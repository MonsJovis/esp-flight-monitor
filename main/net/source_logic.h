/* Pure logic for the flight_source polling brain: backoff timing, the
 * position-source table, and the route-cache set-difference helper.
 *
 * No networking, no ESP-IDF headers here (AGENTS.md §7, §9) — this is the
 * part of flight_source that can be unit-tested on the host in milliseconds.
 * The HTTP calls, the FreeRTOS task and the mutex-guarded published state
 * live in flight_source.c.
 *
 * NOT wired into test/host/Makefile yet — its MODULE_SRCS glob only picks up
 * main/data and main/net files ending in _parse.c. Add
 * main/net/source_logic.c to that list to build test/host/test_source.c.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Timing constants (AGENTS.md §5 — measured, do not re-derive) ------
 *
 * adsb.lol throttles at roughly the 7th rapid request, returns 429, and a
 * sustained hit escalates to a 503 lasting several minutes. It may also
 * emit a spurious 308 while throttling — a documented quirk, not a real
 * redirect (source_is_throttle_status() below).
 */
#define SRC_POLL_INTERVAL_MS    12000   /* normal cadence; never < 10000 */
#define SRC_BACKOFF_BASE_MS     12000   /* first failure: just retry at the normal cadence */
#define SRC_BACKOFF_CAP_MS      300000  /* 5 min — matches adsb.lol's documented 503 cooldown */
/* After the device is moved the next poll goes out at once instead of at the
 * cadence (D82) — but never closer than this to the previous request, so a
 * couple of quick moves cannot add up to the burst adsb.lol throttles. */
#define SRC_MOVE_MIN_GAP_MS     3000
/* Two callsign states, two rate limits.
 *
 * A callsign nobody has ever asked about is why the screen currently says
 * "ROUTE WIRD GESUCHT", and the route is the single most important thing on the
 * panel — making him wait two minutes for it, by which time the aircraft may
 * have flown out of range, defeats the product. Ask promptly.
 *
 * A callsign already asked about and still unanswered is just a retry, and
 * retries are what get a free community service annoyed (AGENTS.md §5). Those
 * keep the slow interval.
 *
 * The cache means any given callsign is only ever asked once per flight, so the
 * fast path cannot run away: it fires when genuinely NEW traffic appears. */
#define SRC_ROUTE_POST_MIN_INTERVAL_MS  120000   /* retries of unanswered ones */
#define SRC_ROUTE_POST_NEW_INTERVAL_MS   15000   /* at least one never asked  */ /* routeset batches at most once per 2 min */

/* Delay before the next poll attempt, given how many polls in a row have
 * failed (0 = the previous poll succeeded). Doubles from SRC_BACKOFF_BASE_MS,
 * capped at SRC_BACKOFF_CAP_MS. A tight retry loop is exactly what gets a
 * device IP-banned from a free community API (AGENTS.md §5). */
int64_t source_backoff_delay_ms(int consec_failures);

/* True for an HTTP status that means "back off", not "here is real data":
 * 429 (rate limited), 503 (cooldown), and adsb.lol's spurious 308 while
 * throttling. Deliberately does NOT match other 3xx codes — a 301/302 is a
 * genuine redirect (e.g. adsb.fi bouncing plain HTTP to https://) and must
 * be treated as a hard error, not throttling, and never silently followed
 * (http_get() disables esp_http_client's auto-redirect for exactly this
 * reason). Only call this for a response from a source where 308-as-
 * throttling is a KNOWN quirk (currently: adsb.lol only). */
bool source_is_throttle_status(int http_status);

/* ---- Position source table --------------------------------------------
 *
 * A table, not a single URL, so a second source can be added in M4 without
 * restructuring flight_source.c. Both entries here are the SAME host
 * (api.adsb.lol) — opendata.adsb.fi was measured live on 2026-09-18 and
 * redirects plain HTTP to https:// (301), and this project carries no TLS
 * stack in M2 (AGENTS.md's ~40 KB-per-connection architecture decision).
 * TODO(M4): revisit TLS, then give adsb.fi its own enabled SRC_* entry —
 * it also returns a `desc` field inline that would remove a lookup.
 *
 * Only source_next_enabled() should be used to pick which entry to poll;
 * do not assume SRC_ADSB_LOL_POINT is always the answer once M4 adds a
 * second enabled entry.
 */
typedef enum {
    SRC_ADSB_LOL_POINT = 0,   /* GET /v2/point/{lat}/{lon}/{radius_nm} — the one actually polled */
    SRC_ADSB_LOL_LATLON,      /* GET /v2/lat/{lat}/lon/{lon}/dist/{radius_nm} — same host, alternate
                                 URL shape. Confirmed to return the same payload over plain HTTP.
                                 NOT a failover target: it shares fate with SRC_ADSB_LOL_POINT under
                                 throttling, since it's the same host. Present in the table, disabled,
                                 purely so the shape exists for reference / future use. */
    SRC_COUNT,
} source_id_t;

/* Human-readable name for logging — never used to build a URL. */
const char *source_name(source_id_t src);

/* True if `src` is currently eligible to be polled. Exactly one entry is
 * enabled today (SRC_ADSB_LOL_POINT); AGENTS.md §5's "switch to a fallback
 * on repeated failure" is NOT implemented here on purpose — with no second
 * host to fall back to, flight_source.c degrades instead (keeps polling the
 * same source with backoff, keeps the last good snapshot, and exposes
 * flight_source_is_stale() for the UI). This flag exists so M4 can flip a
 * second entry on without touching the polling loop. */
bool source_is_enabled(source_id_t src);

/* First enabled source at or after `from`, wrapping around the table. With
 * today's table (one enabled entry) this always returns that entry. */
source_id_t source_next_enabled(source_id_t from);

/* Writes the poll URL for `src` at (lat, lon, radius_nm) into buf (size
 * buf_sz). Returns the number of bytes written (excluding the NUL), or -1
 * if it would not fit or `src` is not a known entry. */
int source_build_url(source_id_t src, double lat, double lon, int radius_nm,
                      char *buf, size_t buf_sz);

/* ---- Route cache -------------------------------------------------------
 *
 * The three-way RESOLVING/RESOLVED/NONE distinction flight_source exposes
 * per callsign (route_t itself only has a two-way `resolved` bool —
 * AGENTS.md/PLAN.md M4: showing "no route" while still looking teaches the
 * user to distrust the panel) is declared as route_status_t in
 * flight_source.h, not here — this file only supplies the pure
 * set-difference helper below; the state itself is not pure/host-testable
 * data, it is flight_source's own cache.
 *
 * Set-difference: callsigns present in `onscreen` (n_onscreen of them) but
 * absent from `known` (n_known of them) — i.e. never looked up yet. Each
 * entry is a NUL-terminated string of at most 8 bytes (matching
 * aircraft_t.flight / route_t.callsign). Writes at most max_pending
 * deduplicated results into out_pending. Skips empty callsigns. Returns the
 * count written. */
int source_find_uncached(const char (*onscreen)[9], int n_onscreen,
                          const char (*known)[9], int n_known,
                          char (*out_pending)[9], int max_pending);

/* Whether it is time to fire the batched routeset POST: there is something
 * to ask about, and enough time has passed since the last attempt
 * (AGENTS.md §5 — "one POST every few minutes, not one per poll"). Pass
 * INT64_MAX for ms_since_last_post if a POST has never been made. */
bool source_should_post_routes(int n_pending, int64_t ms_since_last_post);

/* As above, but `n_never_asked` counts callsigns that have not yet been sent to
 * the API even once. Those earn the fast interval. */
bool source_should_post_routes_ex(int n_pending, int n_never_asked,
                                  int64_t ms_since_last_post);

/* ---- Compass -----------------------------------------------------------
 *
 * English 8-point abbreviation for a bearing in degrees (any value; wrapped
 * mod 360). Used for the M2 log line only ("12.4 nm NE") — the German
 * compass words for the UI are a M2.5/M7 concern (AGENTS.md §1, §7) and do
 * NOT belong in this file.
 */
const char *source_compass_abbrev_en(float bearing_deg);

#ifdef __cplusplus
}
#endif
