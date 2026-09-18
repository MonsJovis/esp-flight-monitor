/* Shared data model. Owned by the integrator — parser, table and UI code all
 * depend on these shapes, so they are defined once, here, and not duplicated.
 *
 * Fixed-size char arrays throughout, no heap strings (AGENTS.md §10).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* adsb.lol reports alt_baro as the STRING "ground" when on the ground, and omits
 * it entirely for some targets. Two sentinels rather than one so the UI can say
 * "am Boden" and "—" differently. (AGENTS.md §7) */
#define ALT_GROUND   (-100000)
#define ALT_UNKNOWN  (-100001)

/* adsb.lol pre-computes `dst` from the query point, so it is normally present —
 * but a source that omits it must NOT default to 0.0, because the list is sorted
 * by distance and 0.0 sorts an unknown aircraft to the front, where it becomes
 * "the plane overhead" and gets the headline. Sorts last instead. */
#define DST_UNKNOWN  (-1.0f)

#define MAX_AIRCRAFT      24   /* ~13 seen at 30 nm over Gloggnitz; 45 at 60 nm */
#define CITY_NAME_LEN     40   /* "Frankfurt am Main" = 17; 40 is slack */
#define AIRLINE_NAME_LEN  40   /* "Middle East Airlines" = 20 */

typedef struct {
    char    hex[8];       /* "3c4b26"                                        */
    char    flight[9];    /* callsign, TRIMMED of the API's trailing spaces  */
    char    type[5];      /* ICAO type "A20N"; "" when absent (mil/blocked)  */
    char    reg[9];       /* registration "OE-LBA"; "" when absent           */
    int32_t alt_ft;       /* feet, or ALT_GROUND / ALT_UNKNOWN               */
    float   dst_nm;       /* pre-computed by adsb.lol from the query point   */
    float   dir_deg;      /* pre-computed bearing TO the aircraft, 0..360    */
    float   track_deg;    /* aircraft's own heading; valid iff has_track     */
    int32_t gs_kt;        /* ground speed, knots; -1 when absent             */
    double  lat, lon;
    bool    has_track;
} aircraft_t;

typedef struct {
    char    callsign[9];                 /* key, trimmed, uppercase          */
    char    airline_code[4];             /* "DLH"; "" when unknown           */
    char    orig_icao[5];                /* "LOWW"; "" when unresolved       */
    char    dest_icao[5];
    char    orig_city[CITY_NAME_LEN];    /* ENGLISH, straight from the API   */
    char    dest_city[CITY_NAME_LEN];    /* German lookup happens downstream */
    bool    plausible;                   /* API's own sanity flag            */
    bool    resolved;                    /* false => "unknown" => §5.2       */
} route_t;

/* Drives §5.2: a Cessna doing circuits has no flight plan and never will, so the
 * screen must say WHY rather than leave a blank slot. (DESIGN.md §5) */
typedef enum {
    AC_CAT_UNKNOWN = 0,
    AC_CAT_AIRLINER,     /* scheduled traffic — a route is expected           */
    AC_CAT_PRIVATE,      /* GA / training — no route is NORMAL, not an error  */
    AC_CAT_HELICOPTER,
    AC_CAT_MILITARY,
} ac_category_t;

typedef struct {
    const char   *manufacturer;  /* "Diamond"                                */
    const char   *model;         /* "DV20"                                   */
    const char   *full_name;     /* "Diamond DV20 Katana"                    */
    const char   *size_class;    /* "Zweisitzer", "Mittelstreckenjet"        */
    ac_category_t category;
} ac_type_t;
