/* timetable.h — the ticket's flight number for a callsign, from pairs CI has
 * seen (D94, D95).
 *
 * "AUA25L" is what the transponder says. "OS 254" is what the ticket and the
 * departure board say. ADS-B carries only the first, and every route database
 * stores the callsign suffix as the "number" ("25L"). The research behind this
 * (2026-10-19) found one free source that knows both: AeroDataBox's airport
 * board, which lists the ATC callsign next to the flight number for every
 * flight its ADS-B receivers saw.
 *
 * So CI reads Vienna's board twice a day (tools/build_timetable.py) and keeps
 * the pairs it saw in the last six days: AUA25L flew HAM -> VIE as OS 254.
 * Airlines give a flight the same callsign every day of a season, so
 * yesterday's pair is today's answer. The file is encrypted (net/
 * timetable_crypt.c), the panel fetches and decrypts it (net/
 * timetable_fetch.c), and this module looks callsigns up in it.
 *
 * The rule is D89's: a number is shown only when it is certain. The callsign
 * must match exactly, the route must be the same two airports, and exactly
 * one flight number may be left. Otherwise the callsign stays.
 *
 * Pure: no ESP-IDF, no LVGL. Host-tested in test/host/test_timetable.c.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flight_types.h"

/* About 1,000 flights a day touch VIE, and a callsign repeats daily, so six
 * days hold a few thousand distinct pairs at most. PSRAM (timetable_fetch.c),
 * 32 bytes each. */
#define TT_MAX_PAIRS    4000

/* The first line of a file this parser accepts. A new format is a new string,
 * so an old panel refuses what it cannot read instead of misreading it. */
#define TT_HEADER       "# esp-flight-monitor timetable 2"

/* AeroDataBox's terms let cached data be kept for seven days at most (§5.5).
 * CI drops a pair at six; the panel ignores one older than seven, so a file
 * left behind by a workflow that stopped running ages out by itself. */
#define TT_MAX_AGE_S    (7 * 24 * 3600LL)

typedef struct {
    char     callsign[9];  /* "AUA25L", as the transponder sends it          */
    char     carrier[3];   /* "OS": the IATA designator of the flight number */
    char     from[5];      /* ICAO, as the routeset gives it: "EDDH"         */
    char     to[5];        /* "LOWW"                                         */
    uint16_t number;       /* 254                                            */
    int64_t  seen;         /* when CI last saw the pair, UTC seconds         */
} tt_pair_t;

/* Parses the plaintext CI writes:
 *
 *   # esp-flight-monitor timetable 2
 *   # generated 1792411200
 *   AUA25L,OS,254,EDDH,LOWW,1792411200
 *
 * Other comment lines are ignored, and so is a data line that does not
 * validate (field count, a code of the wrong shape, a number out of range).
 * Returns the pairs written into `out`, at most `max`; -1 when the first line
 * is not TT_HEADER. `*generated` (may be NULL) gets the generated line, or 0. */
int timetable_parse(const char *text, size_t len, tt_pair_t *out, int max,
                    int64_t *generated);

/* "OS 254" for `callsign` on route `rt`, when exactly one flight number is
 * left after these:
 *
 *   - the callsign matches exactly, numeric ones included: WZZ839 flies as
 *     W6 2274, which D89's digit swap could never know (D93)
 *   - the route is resolved, plausible, and the pair's two airports
 *   - the pair is no older than TT_MAX_AGE_S, when `now` is known (> 0)
 *   - for Austrian only: the number starts with the callsign's digits. In 11
 *     of 11 pairs checked, Austrian kept the leading digits and replaced the
 *     rest with letters (AUA25L = OS 254, AUA52TV = OS 527). A pair that
 *     breaks that is a bad pair, not a new rule.
 *
 * Writes "" and returns 0 otherwise. Returns the bytes written, excluding
 * the NUL, on a match. */
size_t timetable_lookup(const tt_pair_t *tt, int n, const char *callsign,
                        const route_t *rt, int64_t now, char *out, size_t outsz);
