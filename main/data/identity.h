/* "Welches Flugzeug ist das?" — the line that names the individual aircraft.
 *
 * Every other string on this device answers a question about the flight: where
 * it is going, how far away it is, what kind of aircraft it is. This one
 * answers "which one", and it is the line he needs if he wants to look the
 * aircraft up afterwards on his phone.
 *
 * Two identifiers, and which one applies depends on what is flying:
 *
 *   The CALLSIGN is the flight number — "AUA453", "RYR4MR". Airliners have
 *   one; it is what is printed on a boarding pass and what Flightradar24
 *   searches on.
 *
 *   The REGISTRATION is the aircraft's own name — "OE-LBA", "D-EABC". A
 *   glider or a Cessna over the house has no flight number at all, so the
 *   registration is the only handle there is, and it is the one painted on
 *   the tail he is looking at.
 *
 * Callsign first, registration as the fallback. Never both: two codes side by
 * side is a database row, not an answer.
 *
 * Pure — no LVGL, no ESP-IDF. Composed here rather than in each screen so the
 * Radar, the Liste and the detail layer cannot end up naming the same
 * aircraft three different ways, which is the mistake D36 and D46 both were.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#include "flight_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Writes "RYR4MR · Boeing 737-800", or "OE-ABC · Cessna 172", or just
 * "RYR4MR" when the model is unknown or unwanted, or "" when the feed gave
 * neither an identifier nor a type.
 *
 * `with_model` is false where the model is already the headline — a list row
 * titled "Cessna 208 Caravan" must not then say "· Cessna 208 Caravan" under
 * itself. Same dedup rule view_build.c applies between the hero and the
 * supporting line.
 *
 * Returns the number of bytes written, excluding the NUL (snprintf's
 * convention, clamped to what fit).
 */
size_t aircraft_identity(const aircraft_t *ac, bool with_model,
                         char *out, size_t outsz);

/* The same joining rule, for callers that already hold the two strings.
 *
 * main/data/view_build.c does: by the time it composes this line it has
 * already decided whether the model duplicates the hero and blanked it if so
 * (a screen must not say "Cessna 172" above "· Cessna 172"). Re-deriving the
 * model from the type designator there would undo that decision, so it passes
 * what it settled on. Either argument may be NULL or "".
 */
size_t identity_compose(const char *who, const char *model, char *out, size_t outsz);

#ifdef __cplusplus
}
#endif
