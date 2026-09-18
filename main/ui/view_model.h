/* What the screen renders.
 *
 * Everything here is FINAL DISPLAY TEXT: German, correct units, correctly
 * formatted numbers. The UI layer does no conversion, no lookup and no
 * formatting — it positions strings and picks colours. That split is what lets
 * the whole language-and-units layer be tested on the host in milliseconds,
 * and it keeps German out of widget constructors (AGENTS.md §10).
 *
 * Owned by the integrator. Filled by main/data/view_build.c, consumed by
 * main/ui/screen_overhead.c.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "flight_types.h"

typedef enum {
    /* DESIGN.md §5.1 — the answer: destination as hero. */
    VIEW_OVERHEAD = 0,
    /* DESIGN.md §5.2 — no flight plan. NOT a fallback: 7 of the 13 aircraft in
     * our own live capture were GA with no route, and they are precisely the
     * ones he hears. Keeps the layout, swaps the hero, says WHY. */
    VIEW_NO_ROUTE,
    /* DESIGN.md §5.3 — empty sky. Never a blank panel. */
    VIEW_EMPTY_SKY,
} view_state_t;

#define VIEW_HERO_LEN   48
#define VIEW_LINE_LEN   64
#define VIEW_REASON_LEN 96

typedef struct {
    view_state_t state;

    /* Hero band */
    char hero[VIEW_HERO_LEN];     /* "London", or the type when there is no route */
    char origin[VIEW_HERO_LEN];   /* "Wien"; "" when unknown                      */
    bool has_origin;

    /* Supporting band — all pre-formatted, all German */
    char airline[VIEW_LINE_LEN];  /* "Austrian Airlines"; "" when unknown         */
    char type_full[VIEW_LINE_LEN];/* "Airbus A320neo"                             */
    char size_class[VIEW_LINE_LEN];/* "Mittelstreckenjet"                         */
    char callsign[16];            /* "AUA453"                                     */
    char registration[16];        /* "OE-LBA"; "" when blocked                    */

    /* Data band — values already carry their units */
    char altitude[24];            /* "9.100 m", "am Boden", "—"                   */
    char distance[24];            /* "12,4 km"                                    */
    char direction_word[24];      /* "Nordosten"                                  */
    char direction_abbr[8];       /* "NO" — German uses O for Ost, never E        */
    float bearing_deg;            /* for the compass tape; 0..360                 */

    /* §5.2 only: the sentence that explains the missing route. A blank slot
     * reads as broken; a sentence reads as informative. */
    char reason[VIEW_REASON_LEN];

    /* Chrome */
    char clock[8];                /* "09:47"                                      */
    char date_line[VIEW_LINE_LEN];/* "Freitag, 18. September 2026"                */
    int  traffic_count;           /* aircraft currently in range                  */
    bool online;                  /* false => amber network caution               */
} view_model_t;
