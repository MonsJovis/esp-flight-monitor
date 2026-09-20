/* Pure parser for Open-Meteo's geocoding search, plus the URL builder and the
 * IANA -> POSIX timezone conversion that go with it.
 *
 * No networking, no ESP-IDF headers (AGENTS.md §10: anything host-testable is
 * host-tested) — the HTTP GET itself is main/net/geocode.c. This file is
 * string and number work, and it runs in test/host/test_geo.c in
 * milliseconds.
 *
 * WHY OPEN-METEO AND NOT NOMINATIM. Measured, 2026-09-20, with the project's
 * own User-Agent:
 *
 *   http://geocoding-api.open-meteo.com/v1/search   200, no redirect
 *   http://nominatim.openstreetmap.org/search       301 -> https
 *   http://photon.komoot.io/api/                    301 -> https
 *
 * AGENTS.md §4 is plain HTTP end to end and §5's whole memory argument rests
 * on it: a TLS handshake wants ~40 KB of internal heap on a board that has
 * ~24 KB free. Open-Meteo is the only one of the three that does not ask for
 * it. It also answers a question Nominatim does not — it returns the IANA
 * timezone of every hit, which is exactly what AGENTS.md §6 needs to keep its
 * promise that he never sets a clock.
 *
 * What it costs: this is a PLACE search, not an address search. "Gloggnitz"
 * and "Pattaya" resolve; "Semmeringstraße 11" does not. That is the right
 * trade at this radius — the default poll is 30 nm (55 km), so moving the
 * query point by the 600 m between a town centre and a house on its edge
 * changes which aircraft come back not at all.
 *
 * Licence: Open-Meteo's geocoding data is GeoNames under CC BY 4.0, so the
 * attribution line on the settings screen names it (AGENTS.md §9).
 */
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Longest place name seen in the wild is well under this; the field is sized
 * for a German compound with room to spare rather than for the theoretical
 * maximum, and geo_parse() truncates safely rather than dropping a hit. */
#define GEO_NAME_LEN   48

/* The two FINISHED display lines. Both are assembled here rather than by the
 * screen, because AGENTS.md §10 puts display text in the data layer and
 * leaves the UI to position it — which is also what lets both be checked by
 * a host test instead of by looking at a panel. Two German administrative
 * names plus a separator go past 48 bytes in UTF-8 more often than you would
 * think: "Politischer Bezirk Neunkirchen" alone is 30. */
#define GEO_REGION_LEN 72
#define GEO_LABEL_LEN  72

/* POSIX TZ strings top out well short of this: the longest in the generated
 * table (main/net/tz_table.h) is "ACST-9:30ACDT,M10.1.0,M4.1.0/3" at 30. */
#define GEO_TZ_LEN     40

typedef struct {
    char   name[GEO_NAME_LEN];     /* "Innsbruck" — the result row's headline */
    char   region[GEO_REGION_LEN]; /* "Tirol · Österreich" — the row's second line,
                                    * which is what tells three Wiens apart   */
    char   label[GEO_LABEL_LEN];   /* "Innsbruck · Tirol" — what the settings
                                    * card keeps afterwards, and therefore what
                                    * goes into NVS (main/data/settings.h)     */
    char   tz[GEO_TZ_LEN];         /* POSIX, already converted                 */
    double lat, lon;
} geo_place_t;

/* Builds the search URL into `out`, percent-encoding `query`.
 *
 * Returns the number of bytes written (excluding the NUL), or -1 if the URL
 * would not fit, if `query` is NULL, or if it is shorter than two characters
 * — the endpoint answers a one-character query with an empty result set, so
 * there is no point spending a request on it.
 */
int geo_build_url(const char *query, int count, char *out, size_t out_sz);

/* Parses the response into `out`, converting each hit's IANA timezone to a
 * POSIX TZ string and assembling its German region line.
 *
 * A response with NO `results` key is the endpoint's way of saying "nothing
 * found" — it is a normal outcome and returns 0, not an error. -1 is reserved
 * for a body that does not parse as JSON at all.
 *
 * Writes at most `max` entries. Returns the number written, or -1.
 */
int geo_parse(const char *json, size_t len, geo_place_t *out, int max);

/* IANA zone name -> POSIX TZ string, written into `buf`.
 *
 * ESP-IDF's newlib carries no zoneinfo database, so "Europe/Vienna" means
 * nothing to setenv("TZ", ...) — only "CET-1CEST,M3.5.0,M10.5.0/3" does.
 * main/net/tz_table.h holds that mapping for 114 zones, generated from real
 * tzdata by tools/build_tz_table.py.
 *
 * A zone that is not in the table falls back to a WHOLE-HOUR FIXED OFFSET
 * derived from `lon` ("<+07>-7"). That is deliberately a dumb answer: it can
 * be an hour out in a country observing DST, and it never pretends to know a
 * DST rule it does not have. The alternative — guessing at a rule — puts the
 * clock an hour out on exactly the days it changes, which is worse, because
 * then it is wrong only sometimes and nobody can tell why.
 *
 * Returns `buf`. Never fails; `iana` may be NULL.
 */
const char *geo_tz_posix(const char *iana, double lon, char *buf, size_t buf_sz);

#ifdef __cplusplus
}
#endif
