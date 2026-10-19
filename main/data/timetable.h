/* timetable.h — the ticket's flight number for an alphanumeric callsign (D94).
 *
 * "AUA25L" is what the transponder says. "OS 254" is what the ticket and the
 * departure board say, and nothing the panel polls knows the second one: the
 * route databases store the callsign suffix as the "number" ("25L"), because
 * that is all ADS-B carries. The research behind this (reports/, 2026-10-19)
 * found exactly one free way to get the real number: match the aircraft
 * against a timetable on airline, route and time.
 *
 * The timetable comes from CI, not from the device. A GitHub Action holds the
 * Lufthansa Open API key, asks it for every flight at Vienna, and writes the
 * answer as a small CSV (tools/build_timetable.py). The panel fetches that
 * file (net/timetable_fetch.c) and this module does the matching. So the key
 * never reaches a device or the public repo.
 *
 * The rule is the one D89 set: a number is shown only when it is certain.
 * Exactly one flight must survive every filter, or the callsign stays.
 *
 * Pure: no ESP-IDF, no LVGL. Host-tested in test/host/test_timetable.c.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flight_types.h"

/* What the file may hold. About 600 flights touch VIE in the 32 hours the CI
 * asks for (Austrian alone flew 524 movements in such a window on 2026-10-19),
 * so this is three times that. It lives in PSRAM (timetable_fetch.c). */
#define TT_MAX_FLIGHTS  2000

/* The first line of a file this parser accepts. A version bump is a new
 * string, so an old panel refuses a format it cannot read instead of
 * misreading it. */
#define TT_HEADER       "# esp-flight-monitor timetable 1"

/* How far outside its departure and arrival times a flight still counts as
 * the one in the air. Before departure: the table may be two hours old, and a
 * flight leaves a little early now and then. After arrival: a delay the table
 * has not heard of yet, and an aircraft that has just landed (D83). */
#define TT_SLACK_BEFORE_S   (30 * 60)
#define TT_SLACK_AFTER_S    (60 * 60)

/* When the arrival estimate (arrival.h, D79) has an answer, the flight's own
 * arrival time must agree with it this closely. It is what tells a delayed
 * flight from the next one on the same route. */
#define TT_ETA_TOLERANCE_S  (45 * 60)

typedef struct {
    char     carrier[3];   /* operating carrier, IATA: "OS"                  */
    char     from[5];      /* ICAO, as the routeset gives it: "EDDH"         */
    char     to[5];        /* "LOWW"                                          */
    uint16_t number;       /* 254                                             */
    int64_t  dep;          /* best known departure, UTC seconds: actual,
                              else estimated, else scheduled (CI chooses)    */
    int64_t  arr;          /* best known arrival, the same way               */
} tt_flight_t;

/* Parses the CSV the CI writes:
 *
 *   # esp-flight-monitor timetable 1
 *   # generated 1760896800
 *   OS,254,EDDH,LOWW,1760879100,1760884200
 *
 * Other comment lines are ignored, and so is a data line that does not
 * validate (wrong field count, a code of the wrong shape, an arrival not
 * after the departure). Returns the number of flights written into `out`, at
 * most `max`; -1 when the first line is not TT_HEADER, which is what a 404
 * page or a future format looks like. `*generated` (may be NULL) gets the
 * generated line's value, 0 when there is none. */
int timetable_parse(const char *text, size_t len, tt_flight_t *out, int max,
                    int64_t *generated);

/* "OS 254" for this aircraft, when exactly one flight in `tt` survives:
 *
 *   - the callsign is an airline's alphanumeric one ("AUA25L"); a numeric
 *     callsign is already its own flight number (identity.c, D89)
 *   - the operating carrier is the callsign's airline (tables.h airline_iata)
 *   - the route is resolved, plausible, and the same two airports
 *   - `now` lies between departure and arrival, give or take the slack above
 *   - the arrival time agrees with the arrival estimate, when there is one
 *   - for Austrian only: the number starts with the callsign's digits. In 11
 *     of 11 pairs checked, Austrian kept the leading digits and replaced the
 *     rest with letters (AUA25L = OS 254, AUA52TV = OS 527). No other airline
 *     at Vienna does.
 *
 * The same flight number on two days counts once. Writes "" and returns 0
 * when nothing or more than one flight survives, or `now` is 0 (no clock).
 * Returns the bytes written otherwise, excluding the NUL. */
size_t timetable_match(const tt_flight_t *tt, int n, const aircraft_t *ac,
                       const route_t *rt, int64_t now, char *out, size_t outsz);
