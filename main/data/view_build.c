/* See view_build.h. This is the entire bridge between the English/ICAO/
 * nautical world the APIs speak and the German/km/metric text the panel
 * shows an elderly, non-technical reader (AGENTS.md, DESIGN.md §5).
 *
 * Every number and name goes through fmt_de.h / tables.h — nothing here
 * reimplements a conversion, a lookup or a German string table. This file's
 * only job is deciding WHICH strings apply and wiring them into the fixed
 * buffers of view_model_t without ever overflowing or leaving one blank.
 */
#include <stdbool.h>
#include "view_build.h"

#include <string.h>

#include "fmt_de.h"
#include "tables.h"
#include "strings_de.h"
#include "compat.h"

/* ---- small local helper ------------------------------------------------
 *
 * fmt_de.c's safe_copy() is file-static, so this module has its own. Same
 * contract: truncate safely, always NUL-terminate when dst_sz > 0, NULL src
 * becomes "". */
static void copy_trunc(char *dst, size_t dst_sz, const char *src)
{
    if (dst_sz == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    size_t n = (len < dst_sz - 1) ? len : dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ---- chrome: clock, date, traffic count, online flag -------------------- */

/* Before SNTP has ever answered, the clock reads 1970 — and "Donnerstag,
 * 1. Jänner 1970" in 100 px type is precisely what a device looks like when it
 * is broken. It is also the FIRST screen he sees, on a bench or on arrival in
 * Thailand with no network yet. tm_year is years since 1900, so anything below
 * 2020 means the clock has never been set. */
static bool clock_is_set(const struct tm *now)
{
    return now != NULL && now->tm_year >= 120;
}

static void fill_chrome(view_model_t *out, const struct tm *now, int traffic_count, bool online)
{
    out->clock_valid = clock_is_set(now);
    if (out->clock_valid) {
        fmt_time_de(now, out->clock, sizeof out->clock);
        fmt_date_de(now, out->date_line, sizeof out->date_line);
    } else {
        copy_trunc(out->clock, sizeof out->clock, STR_CLOCK_UNSET);
        copy_trunc(out->date_line, sizeof out->date_line, "");
    }
    out->traffic_count = traffic_count;
    out->online = online;
}

/* ---- city name resolution (rule 1: never a bare ICAO code) --------------- */

static void resolve_city(const char *icao, const char *api_city, char *out, size_t outsz)
{
    const char *de = airport_de(icao);
    if (de != NULL && de[0] != '\0') {
        copy_trunc(out, outsz, de);
        return;
    }
    if (api_city != NULL && api_city[0] != '\0') {
        /* No table entry: fall back to the API's own city name rather than
         * ever showing the raw ICAO code.
         *
         * This path is LOUD on purpose. The API's `location` field is
         * English at best and data-entry noise at worst — the panel once put
         * "Rodes Island" in 76 px type as the answer to "where is that plane
         * going", because LGRP was not in the table and nobody knew. A miss
         * is a gap in tbl_airport.c, and the only way anyone finds out is if
         * the device says so: leave the board on a serial console for an
         * afternoon and it tells you exactly which rows to add. */
        ESP_LOGW("view", "airport %s not in tbl_airport.c; showing API text \"%s\"",
                 (icao != NULL) ? icao : "????", api_city);
        copy_trunc(out, outsz, api_city);
        return;
    }
    /* Both the table and the API came up empty — still never a bare code. */
    copy_trunc(out, outsz, STR_UNKNOWN_VALUE);
}

/* ---- VIEW_NO_ROUTE hero: plain-language aircraft type -------------------- */

static void hero_from_type(const ac_type_t *t, const char *icao_type,
                           const char *icao_category, char *out, size_t outsz)
{
    /* Plain language over codes (AGENTS.md §1): the hero is the FULL name —
     * "Diamond DV20 Katana", not "DV20" — and the hero shrink ladder deals with
     * the length. Using the bare model here meant the panel showed a code as its
     * largest text, with the full name repeated underneath as a second line the
     * 480 px panel could not afford.
     *
     * A few entries are placeholders for targets nobody could identify (G2CA
     * before it was corrected: manufacturer "unbekannt"), and their name is just
     * the raw ICAO code. The emitter category says more with fewer letters.
     * Note the near-miss that makes the MANUFACTURER the right signal rather
     * than the model: Diamond's aircraft really is called "DV20". */
    /* Shared with screen_list.c — see tables.h. Written twice, these two drifted
     * apart and the list ended up printing raw ICAO codes. */
    const char *name = actype_display_name(icao_type, icao_category);
    if (name != NULL) {
        copy_trunc(out, outsz, name);
        return;
    }
    /* No usable type designator. Two of the thirteen aircraft in the real
     * Gloggnitz capture were exactly this — genuine aircraft doing 160 kt with
     * no `t` and no `r` — and the hero used to render as a literal "?", which
     * is the largest text on the panel telling him the device is broken.
     * The ICAO emitter category still says WHAT is up there. */
    const char *fallback = actype_full_or_code(icao_type);
    /* actype_full_or_code() is documented to never return NULL or "", but its
     * last resort is "?" — never acceptable as a hero. */
    if (fallback == NULL || fallback[0] == '\0' || strcmp(fallback, ACTYPE_NO_TYPE) == 0) {
        copy_trunc(out, outsz, STR_UNKNOWN_AIRCRAFT);
        return;
    }
    copy_trunc(out, outsz, fallback);
}

/* ---- VIEW_NO_ROUTE reason: WHY, chosen by category (rule 3) -------------- */

static void fill_reason(const ac_type_t *t, char *out, size_t outsz)
{
    ac_category_t cat = (t != NULL) ? t->category : AC_CAT_UNKNOWN;
    const char *s;

    switch (cat) {
    case AC_CAT_PRIVATE:
        s = STR_REASON_GA;
        break;
    case AC_CAT_HELICOPTER:
        s = STR_REASON_HELI;
        break;
    case AC_CAT_MILITARY:
        s = STR_REASON_MIL;
        break;
    case AC_CAT_AIRLINER:
        /* Still a scheduled flight — the plan exists, it just is not
         * available to us right now. Saying it "does not exist" would be
         * wrong and would teach him to distrust the panel (rule 3). */
        s = STR_REASON_UNAVAILABLE;
        break;
    case AC_CAT_UNKNOWN:
    default:
        s = STR_REASON_NONE;
        break;
    }
    copy_trunc(out, outsz, s);
}

/* ---- fields shared by VIEW_OVERHEAD and VIEW_NO_ROUTE -------------------- */

static void fill_aircraft_common(const aircraft_t *ac, const ac_type_t *t, view_model_t *out)
{
    copy_trunc(out->callsign, sizeof out->callsign, ac->flight);
    copy_trunc(out->registration, sizeof out->registration, ac->reg);

    /* Same rule as the hero: "?" is not a thing to show a non-technical user.
     * An empty supporting line simply disappears; a question mark looks broken. */
    {
        const char *tf = actype_full_or_code(ac->type);
        if (tf == NULL || strcmp(tf, ACTYPE_NO_TYPE) == 0) {
            const char *cls = ac_category_de(ac->category);
            tf = (cls != NULL) ? cls : "";
        }
        copy_trunc(out->type_full, sizeof out->type_full, tf);
    }
    copy_trunc(out->size_class, sizeof out->size_class,
               (t != NULL && t->size_class != NULL) ? t->size_class : "");

    fmt_altitude_m(ac->alt_ft, out->altitude, sizeof out->altitude);

    if (ac->dst_nm == DST_UNKNOWN) {
        /* Never feed the sentinel to fmt_distance_km() — it would render as
         * a nonsense negative distance ("-1,9 km"). Same em-dash convention
         * fmt_altitude_m() already uses for ALT_UNKNOWN. A bearing without a
         * distance is not meaningful either, so leave it blank too. */
        copy_trunc(out->distance, sizeof out->distance, STR_EM_DASH);
        out->direction_word[0] = '\0';
        out->direction_abbr[0] = '\0';
        out->bearing_deg = ac->dir_deg;
    } else {
        fmt_distance_km(ac->dst_nm, out->distance, sizeof out->distance);
        copy_trunc(out->direction_word, sizeof out->direction_word, compass_de_adv(ac->dir_deg));
        copy_trunc(out->direction_abbr, sizeof out->direction_abbr, compass_de_abbr(ac->dir_deg));
        out->bearing_deg = ac->dir_deg;
    }
}

static void fill_airline(const route_t *route, view_model_t *out)
{
    if (route != NULL && route->airline_code[0] != '\0') {
        const char *name = airline_de(route->airline_code);
        /* rule 5: never print the raw 3-letter code when the name is
         * unknown — leave it blank instead. */
        copy_trunc(out->airline, sizeof out->airline, name != NULL ? name : "");
    } else {
        out->airline[0] = '\0';
    }
}

/* ---- public API ----------------------------------------------------------- */

void view_build(const aircraft_t *ac, const route_t *route, const struct tm *now,
                int traffic_count, bool online, view_model_t *out)
{
    view_build_ex(ac, route, false, now, traffic_count, online, out);
}

void view_build_ex(const aircraft_t *ac, const route_t *route, bool route_searching,
                   const struct tm *now, int traffic_count, bool online,
                   view_model_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    fill_chrome(out, now, traffic_count, online);

    if (ac == NULL) {
        out->state = VIEW_EMPTY_SKY;
        return;
    }

    bool usable_route = (route != NULL && route->resolved && route->plausible);
    const ac_type_t *t = actype(ac->type);

    fill_aircraft_common(ac, t, out);
    fill_airline(route, out);

    if (usable_route) {
        out->state = VIEW_OVERHEAD;
        resolve_city(route->dest_icao, route->dest_city, out->hero, sizeof out->hero);
        resolve_city(route->orig_icao, route->orig_city, out->origin, sizeof out->origin);
        out->has_origin = true;
        out->reason[0] = '\0';
    } else {
        out->state = VIEW_NO_ROUTE;
        hero_from_type(t, ac->type, ac->category, out->hero, sizeof out->hero);
        out->origin[0] = '\0';
        out->has_origin = false;
        out->route_searching = route_searching;
        if (route_searching) {
            /* Still looking. Do NOT assert there is no flight plan — say what is
             * actually happening, and let the settled answer replace it. */
            copy_trunc(out->reason, sizeof out->reason,
                       STR_REASON_SEARCHING);
        } else {
            fill_reason(t, out->reason, sizeof out->reason);
        }
    }

    /* A supporting line that merely restates the hero is noise, and on a 480 px
     * panel it is noise that costs a whole row: hero "H135" above "Airbus H135"
     * pushed the distance off the bottom of §5.2 and §5.3 entirely. Substring,
     * not equality — "Airbus H135" is not equal to "H135" but tells him nothing
     * new. A city hero never matches an aircraft type, so §5.1 keeps both. */
    if (out->hero[0] != '\0' && strstr(out->type_full, out->hero) != NULL) {
        out->type_full[0] = '\0';
    }
}

void view_build_empty(const struct tm *now, const aircraft_t *last_seen, bool online,
                       view_model_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    fill_chrome(out, now, 0, online);
    out->state = VIEW_EMPTY_SKY;

    /* No clock yet means the device has never reached the network. Say that,
     * in a sentence, instead of showing a 1970 date it cannot justify. The
     * hero carries it because on this screen the hero IS the clock slot, and
     * an honest "Kein Netz" is worth more to him than a wrong time. */
    if (!clock_is_set(now)) {
        copy_trunc(out->hero, sizeof out->hero, STR_NO_NETWORK_HERO);
        copy_trunc(out->date_line, sizeof out->date_line,
                   STR_NO_NETWORK_SUB);
        out->has_origin = false;
        return;
    }

    if (last_seen == NULL) {
        return;
    }

    const ac_type_t *t = actype(last_seen->type);
    fill_aircraft_common(last_seen, t, out);
    hero_from_type(t, last_seen->type, last_seen->category, out->hero, sizeof out->hero);
    /* Same dedupe as view_build(): §5.3 shows the last-seen aircraft with the
     * same hero/supporting pair, so it inherits the same redundancy. */
    if (out->hero[0] != '\0' && strstr(out->type_full, out->hero) != NULL) {
        out->type_full[0] = '\0';
    }
    /* origin/airline/reason intentionally left blank: there is no route
     * context for a historical sighting, and `reason` is reserved for
     * VIEW_NO_ROUTE (view_model.h). */
    out->has_origin = false;
}
