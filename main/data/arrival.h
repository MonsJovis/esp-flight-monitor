/* arrival.h — "Landung in etwa 45 Min.", estimated rather than known (D79).
 *
 * Nothing this device can reach knows when an aircraft will land: adsb.lol
 * has positions, adsb.im's routeset has the two airports and where they are,
 * and neither has a schedule. What CAN be done honestly is arithmetic on what
 * is known — the distance still to fly and how fast the aircraft is going —
 * provided the answer is only shown when it is worth believing. Pure, no LVGL,
 * host-tested (test_arrival.c).
 */
#pragma once

#include <stdbool.h>

#include "flight_types.h"

/* Minutes until landing, or -1 when no estimate should be shown. Inside the
 * last couple of miles this comes out at 0-2, which fmt_arrival_de() says as
 * "in wenigen Minuten" — no special case needed, and a mutation run showed
 * the one that used to be here changed nothing.
 *
 * The model, and why each piece is there:
 *
 * - Great-circle distance from the aircraft to the destination airport.
 * - The last ARRIVAL_APPROACH_NM are flown at no more than
 *   ARRIVAL_APPROACH_KT, the rest at the current ground speed. Straight
 *   distance over cruise speed alone ran 25 % early against Flightradar24 on
 *   the owner's own example (483 km out: 34 min vs 45); with the approach
 *   allowance it is 41.
 * - NOT SHOWN while climbing out: below ARRIVAL_CRUISE_FT and still nearer
 *   the origin than the destination. A departure's ground speed is far below
 *   its cruise speed, and the estimate runs 30 % late or worse — too wrong for
 *   "etwa" to cover. The Vienna preset sees a great many departures; better
 *   to say nothing for fifteen minutes than something wrong.
 * - NOT SHOWN when, more than ARRIVAL_HEADING_NM out, the aircraft is flying
 *   more than 90 deg away from its destination: the route match is probably
 *   wrong (a reused callsign, a diversion). Closer in it is allowed, because
 *   a downwind leg points away from the runway by design.
 * - NOT SHOWN without a resolved, plausible route with coordinates, an
 *   airborne altitude, a position, or at least ARRIVAL_MIN_KT; or when the
 *   answer exceeds 18 hours.
 */
#define ARRIVAL_APPROACH_NM  40.0f
#define ARRIVAL_APPROACH_KT  200.0f
#define ARRIVAL_CRUISE_FT    20000
#define ARRIVAL_HEADING_NM   60.0f
#define ARRIVAL_MIN_KT       60
#define ARRIVAL_MAX_MIN      (18 * 60)

int arrival_minutes(const aircraft_t *ac, const route_t *route);

/* How far the aircraft is from the airport it took off from, in whole km, or
 * -1 when it cannot be said (no route, no coordinates, no position) or is not
 * worth saying (under ARRIVAL_FROM_MIN_KM: still at the airport).
 *
 * The owner asked for "departed x hours ago" and nothing this device can
 * reach knows when an aircraft took off. This is the honest half of that
 * question: the STRAIGHT-LINE distance, which is exactly what the panel's
 * wording claims ("… km von Wien entfernt") — not the distance flown, which
 * is always longer and which nothing here measures (D79). */
#define ARRIVAL_FROM_MIN_KM 10

int departed_km(const aircraft_t *ac, const route_t *route);

/* Great-circle distance in nautical miles. Exposed for the tests. */
float arrival_gc_nm(float lat1, float lon1, float lat2, float lon2);
