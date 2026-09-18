/* Lookup tables that make the panel speak German (docs/PLAN.md M2.5).
 *
 * The APIs in this project's data chain return English city names, bare
 * ICAO/IATA codes, and nothing at all for airline display names (AGENTS.md
 * §4, §1). These three tables are what turns that into "Wien -> London",
 * "Austrian Airlines" and "Airbus A320neo" on screen.
 *
 * All three tables are static const arrays of string literals (so they land
 * in flash, not RAM), sorted ascending by key and searched with bsearch. No
 * dynamic allocation anywhere in this module. C11, no ESP-IDF headers, so it
 * builds unchanged on the host test runner (test/host/Makefile).
 */
#pragma once
#include <stddef.h>

#include "flight_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 1. Airport -> German city name ---------------------------------- */

/* Keyed on the 4-letter ICAO airport code — that is what routeset's
 * `airport_codes` gives us ("LOWW-EGLL"), and it is unambiguous, unlike the
 * 3-letter IATA code which occasionally collides across countries.
 *
 * Returns the German city name, or NULL if `icao` is not in the table (the
 * caller falls back to the API's own English `location`). Never returns an
 * empty string.
 */
const char *airport_de(const char *icao);

/* ---- 2. Airline ICAO code -> display name ----------------------------- */

/* Keyed on the 3-letter ICAO airline code returned in routeset's
 * `airline_code` (e.g. "DLH", "AUA"). No API in this project's chain
 * supplies an airline display name, so this table ships in flash.
 *
 * Returns the display name, or NULL if `icao3` is not in the table.
 */
const char *airline_de(const char *icao3);

/* ---- 3. ICAO aircraft type -> structured entry ------------------------ */

/* Keyed on the ICAO type designator from adsb.lol's `t` field (e.g. "A20N",
 * "DV20", "EC35"). Returns a pointer into the static table, or NULL if
 * `icao_type` is not present. `ac_type_t` is defined in flight_types.h; its
 * `category` field drives DESIGN.md §5.2 ("Ohne Route" — why an aircraft has
 * no route: AC_CAT_PRIVATE means no route is NORMAL, not an error).
 */
const ac_type_t *actype(const char *icao_type);

/* Full display name for `icao_type`, or the raw code itself when the type is
 * unknown. Never NULL, never empty — the panel must always show something
 * rather than a blank hero line. */
const char *actype_full_or_code(const char *icao_type);

/* ---- Test-only introspection ------------------------------------------
 *
 * These accessors expose the raw sorted arrays so test/host/test_tables.c
 * can walk them directly to assert sortedness and key-uniqueness (the
 * property that makes bsearch correct in the first place). Production code
 * has no reason to call these — it always goes through the three lookups
 * above.
 */

typedef struct {
    const char *key;
    const char *value;
} str_lookup_t;

typedef struct {
    const char *icao_type;
    ac_type_t   info;
} actype_lookup_t;

const str_lookup_t   *tbl_airport_entries(size_t *count);
const str_lookup_t   *tbl_airline_entries(size_t *count);
const actype_lookup_t *tbl_actype_entries(size_t *count);

/* ICAO emitter category ("A1", "A3", "B1", ...) -> plain German class name.
 * Real aircraft do appear in the feed with no `t` at all — two of the thirteen
 * in our own capture — and the hero cannot be a question mark. Returns NULL for
 * an unknown or empty category. */
const char *ac_category_de(const char *icao_category);

#ifdef __cplusplus
}
#endif
