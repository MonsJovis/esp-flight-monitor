/* Host tests for the German lookup tables (docs/PLAN.md M2.5).
 *
 * These tables are what makes the panel say "Wien -> London" and "Austrian
 * Airlines" instead of "LOWW -> EGLL" and "AUA". The highest-value check
 * here is sortedness/uniqueness -- that is the property that makes bsearch
 * correct in the first place, and a single out-of-order entry would make
 * lookups silently fail for everything after it.
 */
#include "test_util.h"

#include <ctype.h>

#include "tables.h"
#include "flight_types.h"
#include "strings_de.h"   /* STR_REASON_* -- the sentences `category` chooses  */
#include "view_model.h"   /* VIEW_HERO_LEN -- the buffer every city name lands in */

/* ---- helper: sortedness + no-duplicate-keys, generic over any table with
 * a `const char *key` (or icao_type) as its first member and a known
 * stride. Used for all three tables below. */
static void check_sorted_unique(const char *label, const char *const *keys, size_t n)
{
    for (size_t i = 1; i < n; i++) {
        int cmp = strcmp(keys[i - 1], keys[i]);
        CHECK(cmp < 0);
        if (cmp >= 0) {
            printf("      %s: \"%s\" is not strictly before \"%s\"\n",
                   label, keys[i - 1], keys[i]);
        }
    }
}

/* ---- helper: does this byte sequence decode as UTF-8 by structure?
 *
 * The table is written by hand in a UTF-8 source file, so the failure mode is
 * not "invalid Unicode" but "an editor or a paste re-encoded one row as
 * Latin-1". That produces a lone 0xFC where "Br\xC3\xBCssel" should be, and
 * LVGL draws a lone continuation byte as nothing at all -- "Brssel" on the
 * panel, with no error anywhere. Checked structurally: lead byte says how many
 * continuation bytes follow, each must be 10xxxxxx, and overlong or
 * out-of-range forms are rejected. */
static int utf8_valid(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned char c = *p++;
        int extra;
        unsigned int cp;
        if (c < 0x80)             { continue; }
        else if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1Fu; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0Fu; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07u; }
        else { return 0; }  /* 0x80-0xBF stray continuation, or 0xF8-0xFF */
        for (int i = 0; i < extra; i++) {
            if ((*p & 0xC0) != 0x80) { return 0; }
            cp = (cp << 6) | (unsigned int)(*p++ & 0x3F);
        }
        if (extra == 1 && cp < 0x80)    { return 0; }  /* overlong */
        if (extra == 2 && cp < 0x800)   { return 0; }
        if (extra == 3 && cp < 0x10000) { return 0; }
        if (cp > 0x10FFFF)              { return 0; }
        if (cp >= 0xD800 && cp <= 0xDFFF) { return 0; }  /* surrogate half */
    }
    return 1;
}

/* ---- helper: is this display name just the ICAO code again?
 *
 * The bug class in one predicate. `hero_from_type()` used to fall through to
 * actype_full_or_code(), whose last resort is the raw designator, and the panel
 * put "C177" on the hero in 76 px type. The structural half of that is fixed in
 * view_build.c; this is the half that has to hold for every row of the table,
 * now and after the next 200 rows land -- a name that IS its code, or that
 * merely prefixes its code with something, is the same failure wearing a hat.
 *
 * Deliberately CASE-SENSITIVE. Designators are uppercase and display names are
 * not, and the difference is load-bearing: "Pitts Special" legitimately begins
 * with the four letters of PITT, and "Ilyushin Il-76" with the two of IL76.
 * Case-folding here would reject both and teach the next author to delete the
 * check. What it must catch is the literal designator -- "C177", "C177 Cardinal"
 * -- and for that, exact case is exactly right. */
static int name_is_or_starts_with_code(const char *code, const char *name)
{
    if (code == NULL || name == NULL) {
        return 0;
    }
    return strncmp(name, code, strlen(code)) == 0;
}

/* ---- helper: the sentence view_build.c's fill_reason() picks for a category.
 *
 * A mirror of the switch in main/data/view_build.c, which is static and cannot
 * be called from here. test_view.c owns the end-to-end proof that fill_reason()
 * really emits these; what this file owns is the other half of the contract --
 * that each ROW carries the category which lands on the right sentence. A
 * glider filed as AC_CAT_AIRLINER compiles, sorts and looks fine in review, and
 * then tells him "Eine Route gibt es nur bei Linienflügen." about a sailplane. */
static const char *reason_for_category(ac_category_t cat)
{
    switch (cat) {
    case AC_CAT_PRIVATE:    return STR_REASON_GA;
    case AC_CAT_HELICOPTER: return STR_REASON_HELI;
    case AC_CAT_MILITARY:   return STR_REASON_MIL;
    case AC_CAT_AIRLINER:   return STR_REASON_UNAVAILABLE;
    case AC_CAT_UNKNOWN:
    default:                return STR_REASON_NONE;
    }
}

/* ---- helper: assert one actype row, field for field. The spot checks below
 * come in groups of a dozen, and spelling five CHECKs out per row buried the
 * one thing each row is actually claiming. */
static void check_type(const char *code, const char *full_name,
                       const char *size_class, ac_category_t cat)
{
    const ac_type_t *t = actype(code);
    CHECK(t != NULL);
    if (t == NULL) {
        printf("      actype(\"%s\") returned NULL\n", code);
        return;
    }
    CHECK_STR(t->full_name, full_name);
    CHECK_STR(t->size_class, size_class);
    CHECK_INT(t->category, cat);
    /* Whatever else a row says, it may never hand the panel the code back. */
    CHECK(!name_is_or_starts_with_code(code, t->full_name));
    CHECK_STR(actype_display_name(code, "A1"), full_name);
}

int main(void)
{
    GROUP("airport table: sorted, unique keys");
    {
        size_t n = 0;
        const str_lookup_t *tbl = tbl_airport_entries(&n);
        /* The floor is a coverage statement, not a formality. resolve_city()
         * falls back to the API's own `location` when a code is missing, and
         * that fallback showed him "Rodes Island" for LGRP. Several hundred
         * entries is what makes the fallback rare across both catchments
         * (tbl_airport.c's header comment). */
        CHECK(n >= 500);
        const char **keys = malloc(n * sizeof(char *));
        for (size_t i = 0; i < n; i++) {
            keys[i] = tbl[i].key;
        }
        check_sorted_unique("airport", keys, n);
        free(keys);
    }

    GROUP("airline table: sorted, unique keys");
    {
        size_t n = 0;
        const str_lookup_t *tbl = tbl_airline_entries(&n);
        CHECK(n >= 150);
        const char **keys = malloc(n * sizeof(char *));
        for (size_t i = 0; i < n; i++) {
            keys[i] = tbl[i].key;
        }
        check_sorted_unique("airline", keys, n);
        free(keys);
    }

    GROUP("actype table: sorted, unique keys");
    {
        size_t n = 0;
        const actype_lookup_t *tbl = tbl_actype_entries(&n);
        /* Was 200, raised to 400 when the table grew from 209 rows to 403.
         * The floor is a coverage statement, exactly as it is for the airport
         * table: every designator the feed emits and this table does not carry
         * is one more hero reading "Unbekanntes Flugzeug" at him. The 209-row
         * version was weighted towards airliners and had no Cessna 177 in it,
         * which is how C177 reached the panel as a bare code. Light GA,
         * gliders, helicopters and ultralights are the traffic he actually
         * hears, so halving this number would be undoing the fix. */
        CHECK(n >= 400);
        const char **keys = malloc(n * sizeof(char *));
        for (size_t i = 0; i < n; i++) {
            keys[i] = tbl[i].icao_type;
        }
        check_sorted_unique("actype", keys, n);
        free(keys);
    }

    GROUP("airport table: every ICAO code in routeset_response.json fixture resolves");
    {
        /* airport_codes fields in test/fixtures/routeset_response.json,
         * split on '-' and flattened, excluding the literal "unknown"
         * entries (private aircraft with no route -- not our concern here). */
        static const char *fixture_airports[] = {
            "LRCL", "EDDM", "LGSM", "EHAM", "EGSS", "EDDW",
            "LWSK", "LOWW", "LIRZ", "EPKK", "LROP",
        };
        for (size_t i = 0; i < sizeof(fixture_airports) / sizeof(fixture_airports[0]); i++) {
            const char *city = airport_de(fixture_airports[i]);
            CHECK(city != NULL);
            if (city != NULL) {
                CHECK(city[0] != '\0');
            } else {
                printf("      airport_de(\"%s\") returned NULL\n", fixture_airports[i]);
            }
        }
    }

    GROUP("airline table: every airline_code in routeset_response.json fixture resolves");
    {
        static const char *fixture_airlines[] = { "DLH", "CND", "RYR", "AUA" };
        for (size_t i = 0; i < sizeof(fixture_airlines) / sizeof(fixture_airlines[0]); i++) {
            const char *name = airline_de(fixture_airlines[i]);
            CHECK(name != NULL);
            if (name != NULL) {
                CHECK(name[0] != '\0');
            } else {
                printf("      airline_de(\"%s\") returned NULL\n", fixture_airlines[i]);
            }
        }
    }

    GROUP("actype table: every `t` in adsblol_gloggnitz_30nm.json fixture resolves");
    {
        /* Every distinct "t" value in the captured 30 nm Gloggnitz sample,
         * including the two oddities (TWR, G2CA) -- see tbl_actype.c for
         * the judgement call on each. */
        static const char *fixture_types[] = {
            "A319", "B38M", "B39M", "B738", "BCS3", "DV20", "EC35", "G2CA", "TWR",
        };
        for (size_t i = 0; i < sizeof(fixture_types) / sizeof(fixture_types[0]); i++) {
            const ac_type_t *t = actype(fixture_types[i]);
            CHECK(t != NULL);
            if (t != NULL) {
                CHECK(t->manufacturer != NULL);
                CHECK(t->model != NULL);
                CHECK(t->full_name != NULL && t->full_name[0] != '\0');
                CHECK(t->size_class != NULL && t->size_class[0] != '\0');
            } else {
                printf("      actype(\"%s\") returned NULL\n", fixture_types[i]);
            }
            /* actype_full_or_code must always produce something displayable. */
            const char *full = actype_full_or_code(fixture_types[i]);
            CHECK(full != NULL);
            CHECK(full[0] != '\0');
        }
    }

    GROUP("exact shape of the documented example entries");
    {
        const ac_type_t *dv20 = actype("DV20");
        CHECK(dv20 != NULL);
        if (dv20 != NULL) {
            CHECK_STR(dv20->manufacturer, "Diamond");
            CHECK_STR(dv20->model, "DV20");
            CHECK_STR(dv20->full_name, "Diamond DV20 Katana");
            CHECK_STR(dv20->size_class, "Zweisitzer");
            CHECK_INT(dv20->category, AC_CAT_PRIVATE);
        }

        const ac_type_t *ec35 = actype("EC35");
        CHECK(ec35 != NULL);
        if (ec35 != NULL) {
            CHECK_STR(ec35->manufacturer, "Airbus Helicopters");
            CHECK_STR(ec35->model, "H135");
            CHECK_STR(ec35->full_name, "Airbus H135");
            CHECK_STR(ec35->size_class, "Hubschrauber");
            CHECK_INT(ec35->category, AC_CAT_HELICOPTER);
        }

        const ac_type_t *c172 = actype("C172");
        CHECK(c172 != NULL);
        if (c172 != NULL) {
            CHECK_STR(c172->manufacturer, "Cessna");
            CHECK_STR(c172->model, "172");
            CHECK_STR(c172->full_name, "Cessna 172 Skyhawk");
            CHECK_STR(c172->size_class, "Viersitzer");
            CHECK_INT(c172->category, AC_CAT_PRIVATE);
        }

        const ac_type_t *a20n = actype("A20N");
        CHECK(a20n != NULL);
        if (a20n != NULL) {
            CHECK_STR(a20n->manufacturer, "Airbus");
            CHECK_STR(a20n->model, "A320neo");
            CHECK_STR(a20n->full_name, "Airbus A320neo");
            CHECK_STR(a20n->size_class, "Mittelstreckenjet");
            CHECK_INT(a20n->category, AC_CAT_AIRLINER);
        }
    }

    GROUP("C177 -- the row this expansion is named after");
    {
        /* Seen on the real device an hour before this test was written: the
         * hero band rendered "C177", in 76 px type. That is the raw ICAO type
         * designator for a Cessna 177 Cardinal; AGENTS.md §1 forbids bare codes
         * in as many words, and docs/DECISIONS.md D36 is entirely about having
         * removed the identical failure from the list screen.
         *
         * hero_from_type() no longer falls through to the designator, so the
         * worst case is now "Unbekanntes Flugzeug" -- honest, but not the
         * answer. The answer is this row, and the only reason the device could
         * not say it is that the type was not in AIRCRAFT_TYPES[]. */
        check_type("C177", "Cessna 177 Cardinal", "Viersitzer", AC_CAT_PRIVATE);
        CHECK_STR(actype_full_or_code("C177"), "Cessna 177 Cardinal");
        /* And with no emitter category at all: the name must not depend on the
         * feed also having sent a category, because for GA it often has not. */
        CHECK_STR(actype_display_name("C177", ""), "Cessna 177 Cardinal");
        CHECK_STR(actype_display_name("C177", NULL), "Cessna 177 Cardinal");
        /* Its retractable sibling, which the feed emits just as often. */
        check_type("C77R", "Cessna 177RG Cardinal", "Viersitzer", AC_CAT_PRIVATE);
    }

    GROUP("light GA and trainers -- low and slow over the house");
    {
        /* The Vienna TMA floor over Gloggnitz is high, and what is underneath
         * it is this: club Cessnas, Pipers and Diamonds out of Wiener Neustadt
         * and the strips around it. Every one of these used to be a code. */
        check_type("C162", "Cessna 162 Skycatcher", "Zweisitzer", AC_CAT_PRIVATE);
        check_type("C175", "Cessna 175 Skylark", "Viersitzer", AC_CAT_PRIVATE);
        check_type("C207", "Cessna 207 Stationair", "Sechssitzer", AC_CAT_PRIVATE);
        check_type("C310", "Cessna 310", "Sechssitzer", AC_CAT_PRIVATE);
        check_type("C337", "Cessna 337 Skymaster", "Sechssitzer", AC_CAT_PRIVATE);
        check_type("C72R", "Cessna 172RG Cutlass", "Viersitzer", AC_CAT_PRIVATE);
        check_type("C82R", "Cessna 182RG Skylane", "Viersitzer", AC_CAT_PRIVATE);

        check_type("PA22", "Piper PA-22 Tri-Pacer", "Viersitzer", AC_CAT_PRIVATE);
        check_type("PA25", "Piper PA-25 Pawnee", "Agrarflugzeug", AC_CAT_PRIVATE);
        check_type("PA38", "Piper PA-38 Tomahawk", "Zweisitzer", AC_CAT_PRIVATE);
        check_type("PA44", "Piper PA-44 Seminole", "Viersitzer", AC_CAT_PRIVATE);
        check_type("P28R", "Piper PA-28R Arrow", "Viersitzer", AC_CAT_PRIVATE);
        check_type("P32R", "Piper PA-32R Saratoga", "Sechssitzer", AC_CAT_PRIVATE);
        check_type("P46T", "Piper PA-46 Meridian", "Turboprop", AC_CAT_PRIVATE);

        check_type("BE33", "Beechcraft Debonair", "Viersitzer", AC_CAT_PRIVATE);
        check_type("BE35", "Beechcraft Bonanza 35", "Viersitzer", AC_CAT_PRIVATE);
        check_type("BE55", "Beechcraft Baron 55", "Sechssitzer", AC_CAT_PRIVATE);
        check_type("BE76", "Beechcraft Duchess", "Viersitzer", AC_CAT_PRIVATE);
        check_type("BE30", "Beechcraft King Air 300", "Turboprop", AC_CAT_PRIVATE);

        check_type("S22T", "Cirrus SR22T", "Viersitzer", AC_CAT_PRIVATE);
        check_type("SF50", "Cirrus Vision Jet", "Geschäftsreisejet", AC_CAT_PRIVATE);
        check_type("DA20", "Diamond DA20 Katana", "Zweisitzer", AC_CAT_PRIVATE);
        check_type("DA62", "Diamond DA62", "Sechssitzer", AC_CAT_PRIVATE);

        check_type("AT3",  "Aero AT-3", "Zweisitzer", AC_CAT_PRIVATE);
        check_type("DR40", "Robin DR-400", "Viersitzer", AC_CAT_PRIVATE);
        check_type("TB20", "Socata TB-20 Trinidad", "Viersitzer", AC_CAT_PRIVATE);
        check_type("M20T", "Mooney M20", "Viersitzer", AC_CAT_PRIVATE);
        check_type("P06T", "Tecnam P2006T", "Viersitzer", AC_CAT_PRIVATE);
        check_type("AA5",  "Grumman AA-5 Tiger", "Viersitzer", AC_CAT_PRIVATE);
        check_type("WILG", "PZL-104 Wilga", "Viersitzer", AC_CAT_PRIVATE);
    }

    GROUP("business jets and turboprops");
    {
        check_type("C25M", "Cessna Citation M2", "Geschäftsreisejet", AC_CAT_PRIVATE);
        check_type("C68A", "Cessna Citation Latitude", "Geschäftsreisejet", AC_CAT_PRIVATE);
        check_type("C700", "Cessna Citation Longitude", "Geschäftsreisejet", AC_CAT_PRIVATE);
        check_type("CL30", "Bombardier Challenger 300", "Geschäftsreisejet", AC_CAT_PRIVATE);
        check_type("E50P", "Embraer Phenom 100", "Geschäftsreisejet", AC_CAT_PRIVATE);
        check_type("E35L", "Embraer Legacy 600", "Geschäftsreisejet", AC_CAT_PRIVATE);
        check_type("FA8X", "Dassault Falcon 8X", "Geschäftsreisejet", AC_CAT_PRIVATE);
        check_type("G280", "Gulfstream G280", "Geschäftsreisejet", AC_CAT_PRIVATE);
        check_type("HDJT", "HondaJet HA-420", "Geschäftsreisejet", AC_CAT_PRIVATE);
        check_type("LJ75", "Learjet 75", "Geschäftsreisejet", AC_CAT_PRIVATE);
        check_type("PRM1", "Beechcraft Premier I", "Geschäftsreisejet", AC_CAT_PRIVATE);
        check_type("MU2",  "Mitsubishi MU-2", "Turboprop", AC_CAT_PRIVATE);
        check_type("PAY3", "Piper PA-42 Cheyenne III", "Turboprop", AC_CAT_PRIVATE);
    }

    GROUP("gliders and motorgliders -- this is Austria");
    {
        /* Semmering and the Wechsel are ridge-soaring country and Gloggnitz sits
         * under both. A sailplane filed as anything but AC_CAT_PRIVATE would be
         * told "Der Flugplan ist im Moment nicht verfügbar." about an aircraft
         * that has never had one, so the category matters as much as the name. */
        check_type("AS21", "Schleicher ASK 21", "Segelflugzeug", AC_CAT_PRIVATE);
        check_type("DISC", "Schempp-Hirth Discus", "Segelflugzeug", AC_CAT_PRIVATE);
        check_type("JANU", "Schempp-Hirth Janus", "Segelflugzeug", AC_CAT_PRIVATE);
        check_type("NIMB", "Schempp-Hirth Nimbus", "Segelflugzeug", AC_CAT_PRIVATE);
        check_type("VENT", "Schempp-Hirth Ventus", "Segelflugzeug", AC_CAT_PRIVATE);
        check_type("STDC", "Schempp-Hirth Standard Cirrus", "Segelflugzeug", AC_CAT_PRIVATE);
        check_type("LS6",  "Rolladen-Schneider LS6", "Segelflugzeug", AC_CAT_PRIVATE);
        check_type("DG80", "DG Flugzeugbau DG-800", "Segelflugzeug", AC_CAT_PRIVATE);
        check_type("DG1T", "DG Flugzeugbau DG-1000T", "Segelflugzeug", AC_CAT_PRIVATE);
        check_type("L13",  "LET L-13 Blanik", "Segelflugzeug", AC_CAT_PRIVATE);
        check_type("SF25", "Scheibe SF-25 Falke", "Motorsegler", AC_CAT_PRIVATE);
        check_type("SNUS", "Pipistrel Sinus", "Motorsegler", AC_CAT_PRIVATE);
        /* The rows that were already here keep their meaning. */
        check_type("DUOD", "Schempp-Hirth Duo Discus", "Segelflugzeug", AC_CAT_PRIVATE);
        check_type("LS8",  "Rolladen-Schneider LS8", "Segelflugzeug", AC_CAT_PRIVATE);

        /* GLID is a CLASS, not a model -- Doc 8643's catch-all, and what a
         * glider with no better entry in its transponder actually squawks. The
         * honest German word beats guessing a manufacturer (the task brief's
         * rule, and the reason this row exists at all). */
        check_type("GLID", "Segelflugzeug", "Segelflugzeug", AC_CAT_PRIVATE);
        /* And it must survive actype_display_name() rather than being read as a
         * placeholder row and dropped to the emitter category -- which for a
         * glider squawking A1 would answer "Leichtflugzeug". */
        CHECK_STR(actype_display_name("GLID", "A1"), "Segelflugzeug");
        CHECK_STR(actype_display_name("GLID", ""), "Segelflugzeug");
    }

    GROUP("helicopters");
    {
        check_type("A119", "Leonardo A119 Koala", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("A169", "Leonardo AW169", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("A189", "Leonardo AW189", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("AS32", "Airbus AS332 Super Puma", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("AS65", "Airbus AS365 Dauphin", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("B407", "Bell 407", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("B505", "Bell 505", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("EC25", "Airbus H225", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("EC75", "Airbus H175", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("EH10", "Leonardo AW101", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("EN28", "Enstrom 280", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("EN48", "Enstrom 480", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("GAZL", "Aérospatiale Gazelle", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("H269", "Schweizer 269", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("MD52", "MD Helicopters MD 520N", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("S61",  "Sikorsky S-61", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("S92",  "Sikorsky S-92", "Hubschrauber", AC_CAT_HELICOPTER);

        /* Airbus renamed the Eurocopter fleet, and both spellings reach the
         * feed. EC35 and H135 are the same machine and must read the same --
         * that is the ÖAMTC Christophorus he sees more than anything else. */
        check_type("H135", "Airbus H135", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("H145", "Airbus H145", "Hubschrauber", AC_CAT_HELICOPTER);
        check_type("H125", "Airbus H125", "Hubschrauber", AC_CAT_HELICOPTER);
        CHECK_STR(actype_full_or_code("H135"), actype_full_or_code("EC35"));
        CHECK_STR(actype_full_or_code("H145"), actype_full_or_code("EC45"));

        /* Uniformed rotorcraft are MILITARY, not HELICOPTER: the sentence for
         * "why no route" differs, and a Black Hawk really is in no public plan. */
        check_type("NH90", "NHIndustries NH90", "Hubschrauber", AC_CAT_MILITARY);
        check_type("S70",  "Sikorsky S-70 Black Hawk", "Hubschrauber", AC_CAT_MILITARY);
        check_type("TIGR", "Airbus Tiger", "Hubschrauber", AC_CAT_MILITARY);
        check_type("UH60", "Sikorsky UH-60 Black Hawk", "Hubschrauber", AC_CAT_MILITARY);
    }

    GROUP("warbirds, aerobatics, homebuilts and ultralights");
    {
        /* A Ju 52 or an An-2 over Lower Austria on a summer Sunday is a
         * privately flown historic aircraft, not a military flight: it gets the
         * GA sentence, because that is the true one. */
        check_type("AN2",  "Antonov An-2", "Historisches Flugzeug", AC_CAT_PRIVATE);
        check_type("DC3",  "Douglas DC-3", "Historisches Flugzeug", AC_CAT_PRIVATE);
        check_type("DH82", "De Havilland Tiger Moth", "Historisches Flugzeug", AC_CAT_PRIVATE);
        check_type("JU52", "Junkers Ju 52", "Historisches Flugzeug", AC_CAT_PRIVATE);
        check_type("P51",  "North American P-51 Mustang", "Historisches Flugzeug", AC_CAT_PRIVATE);
        check_type("SPIT", "Supermarine Spitfire", "Historisches Flugzeug", AC_CAT_PRIVATE);
        check_type("T6",   "North American T-6 Texan", "Historisches Flugzeug", AC_CAT_PRIVATE);

        check_type("PITT", "Pitts Special", "Kunstflugzeug", AC_CAT_PRIVATE);
        check_type("SU26", "Sukhoi Su-26", "Kunstflugzeug", AC_CAT_PRIVATE);
        check_type("YK52", "Yakovlev Yak-52", "Kunstflugzeug", AC_CAT_PRIVATE);
        check_type("E300", "Extra EA-300", "Kunstflugzeug", AC_CAT_PRIVATE);

        check_type("RV6",  "Van's RV-6", "Zweisitzer", AC_CAT_PRIVATE);
        check_type("RV7",  "Van's RV-7", "Zweisitzer", AC_CAT_PRIVATE);
        check_type("RV10", "Van's RV-10", "Viersitzer", AC_CAT_PRIVATE);
        check_type("RV14", "Van's RV-14", "Zweisitzer", AC_CAT_PRIVATE);

        check_type("EV97", "Evektor EuroStar", "Ultraleichtflugzeug", AC_CAT_PRIVATE);
        check_type("FK9",  "FK-Lightplanes FK9", "Ultraleichtflugzeug", AC_CAT_PRIVATE);
        check_type("WT9",  "Aerospool WT9 Dynamic", "Ultraleichtflugzeug", AC_CAT_PRIVATE);
        check_type("VL3",  "JMB VL-3", "Ultraleichtflugzeug", AC_CAT_PRIVATE);
        check_type("VIRU", "Pipistrel Virus", "Ultraleichtflugzeug", AC_CAT_PRIVATE);
        check_type("MTOS", "AutoGyro MTOsport", "Tragschrauber", AC_CAT_PRIVATE);
        check_type("CALI", "AutoGyro Calidus", "Tragschrauber", AC_CAT_PRIVATE);

        /* The other four class designators, same argument as GLID above. */
        check_type("ULAC", "Ultraleichtflugzeug", "Ultraleichtflugzeug", AC_CAT_PRIVATE);
        check_type("GYRO", "Tragschrauber", "Tragschrauber", AC_CAT_PRIVATE);
        check_type("BALL", "Ballon", "Ballon", AC_CAT_PRIVATE);
        check_type("SHIP", "Luftschiff", "Luftschiff", AC_CAT_PRIVATE);
    }

    GROUP("military types that transit Austrian airspace");
    {
        check_type("PC9",  "Pilatus PC-9", "Militärflugzeug", AC_CAT_MILITARY);
        check_type("TEX2", "Beechcraft T-6 Texan II", "Militärflugzeug", AC_CAT_MILITARY);
        check_type("RFAL", "Dassault Rafale", "Militärflugzeug", AC_CAT_MILITARY);
        check_type("F15",  "Boeing F-15 Eagle", "Militärflugzeug", AC_CAT_MILITARY);
        check_type("F5",   "Northrop F-5", "Militärflugzeug", AC_CAT_MILITARY);
        check_type("MG29", "Mikoyan MiG-29", "Militärflugzeug", AC_CAT_MILITARY);
        check_type("SU27", "Sukhoi Su-27", "Militärflugzeug", AC_CAT_MILITARY);
        check_type("HAWK", "BAE Systems Hawk", "Militärflugzeug", AC_CAT_MILITARY);
        check_type("B52",  "Boeing B-52 Stratofortress", "Militärflugzeug", AC_CAT_MILITARY);
        check_type("C30J", "Lockheed C-130J Hercules", "Militärflugzeug", AC_CAT_MILITARY);
        check_type("C160", "Transall C-160", "Militärflugzeug", AC_CAT_MILITARY);
        check_type("C27J", "Leonardo C-27J Spartan", "Militärflugzeug", AC_CAT_MILITARY);
        check_type("C295", "Airbus C295", "Militärflugzeug", AC_CAT_MILITARY);
        check_type("CN35", "CASA CN-235", "Militärflugzeug", AC_CAT_MILITARY);
        check_type("P3",   "Lockheed P-3 Orion", "Militärflugzeug", AC_CAT_MILITARY);
        check_type("P8",   "Boeing P-8 Poseidon", "Militärflugzeug", AC_CAT_MILITARY);
    }

    GROUP("regional and Asian types -- the half of the year in Pattaya");
    {
        check_type("DHC6", "De Havilland Twin Otter", "Turboprop", AC_CAT_AIRLINER);
        check_type("BN2P", "Britten-Norman Islander", "Kleinflugzeug", AC_CAT_PRIVATE);
        check_type("E110", "Embraer EMB-110 Bandeirante", "Turboprop", AC_CAT_AIRLINER);
        check_type("SW4",  "Swearingen Metro", "Turboprop", AC_CAT_AIRLINER);
        check_type("AN12", "Antonov An-12", "Frachtflugzeug", AC_CAT_AIRLINER);
        check_type("AN24", "Antonov An-24", "Turboprop", AC_CAT_AIRLINER);
        check_type("AN26", "Antonov An-26", "Turboprop", AC_CAT_AIRLINER);
        check_type("MA60", "Xi'an MA60", "Turboprop", AC_CAT_AIRLINER);
        check_type("ARJ2", "COMAC ARJ21", "Regionaljet", AC_CAT_AIRLINER);
        check_type("C919", "COMAC C919", "Mittelstreckenjet", AC_CAT_AIRLINER);
        check_type("SU95", "Sukhoi Superjet 100", "Regionaljet", AC_CAT_AIRLINER);
        check_type("F27",  "Fokker F27 Friendship", "Turboprop", AC_CAT_AIRLINER);
        check_type("F28",  "Fokker F28 Fellowship", "Regionaljet", AC_CAT_AIRLINER);
        check_type("B732", "Boeing 737-200", "Mittelstreckenjet", AC_CAT_AIRLINER);
        check_type("B733", "Boeing 737-300", "Mittelstreckenjet", AC_CAT_AIRLINER);
        check_type("MD82", "McDonnell Douglas MD-82", "Mittelstreckenjet", AC_CAT_AIRLINER);
        check_type("MD90", "McDonnell Douglas MD-90", "Mittelstreckenjet", AC_CAT_AIRLINER);
        check_type("A30B", "Airbus A300B", "Großraumjet", AC_CAT_AIRLINER);
        check_type("B742", "Boeing 747-200", "Großraumjet", AC_CAT_AIRLINER);
        /* E175 files under three designators depending on the wing. One
         * aircraft, one name -- the same rule the airport table follows for a
         * city with three fields. */
        check_type("E75L", "Embraer E175", "Regionaljet", AC_CAT_AIRLINER);
        check_type("E75S", "Embraer E175", "Regionaljet", AC_CAT_AIRLINER);
        CHECK_STR(actype_full_or_code("E75L"), actype_full_or_code("E175"));
        CHECK_STR(actype_full_or_code("E75S"), actype_full_or_code("E175"));
    }

    GROUP("no aircraft name is, or starts with, its own ICAO designator");
    {
        /* The bug class, closed for the whole table at once rather than row by
         * row. "C177" reached a 76 px hero because one lookup could return the
         * designator; the table itself must never be the source of the same
         * thing, and 194 new rows are 194 new chances to reintroduce it. */
        size_t n = 0;
        const actype_lookup_t *e = tbl_actype_entries(&n);
        CHECK(n > 0);
        for (size_t i = 0; i < n; i++) {
            if (name_is_or_starts_with_code(e[i].icao_type, e[i].info.full_name)) {
                printf("    FAIL  %s: full_name \"%s\" is the designator itself\n",
                       e[i].icao_type, e[i].info.full_name);
                CHECK(0);
            }
        }
        /* An invariant nobody has watched fail is an invariant nobody knows
         * works, so prove the detector fires -- and prove it does NOT fire on
         * the two legitimate near-misses that tempted a case-fold. */
        CHECK(name_is_or_starts_with_code("C177", "C177") == 1);
        CHECK(name_is_or_starts_with_code("C177", "C177 Cardinal") == 1);
        CHECK(name_is_or_starts_with_code("C177", "Cessna 177 Cardinal") == 0);
        CHECK(name_is_or_starts_with_code("PITT", "Pitts Special") == 0);
        CHECK(name_is_or_starts_with_code("IL76", "Ilyushin Il-76") == 0);
    }

    GROUP("every aircraft row is displayable, table-wide");
    {
        /* Same shape as the airport table's sweep above, and for the same
         * reason: adding 194 rows must not be able to smuggle in the one row
         * that blanks the hero or truncates mid-character. */
        size_t n = 0;
        const actype_lookup_t *e = tbl_actype_entries(&n);
        CHECK(n >= 400);
        for (size_t i = 0; i < n; i++) {
            const char *code = e[i].icao_type;
            const ac_type_t *t = &e[i].info;

            if (t->full_name == NULL || t->full_name[0] == '\0') {
                printf("    FAIL  %s has an empty full_name\n", code);
                CHECK(0);
                continue;
            }
            /* It has to survive the copy into view_model_t.hero intact. A name
             * cut at 47 bytes is not a shorter answer, it is a wrong one. */
            if (strlen(t->full_name) >= VIEW_HERO_LEN) {
                printf("    FAIL  %s: \"%s\" is %zu bytes, VIEW_HERO_LEN is %d\n",
                       code, t->full_name, strlen(t->full_name), (int)VIEW_HERO_LEN);
                CHECK(0);
            }
            if (!utf8_valid(t->full_name)) {
                printf("    FAIL  %s: \"%s\" is not valid UTF-8\n", code, t->full_name);
                CHECK(0);
            }
            /* size_class is the supporting line under the hero. Empty is
             * allowed nowhere in this table -- view_build.c already copes with
             * a NULL by writing "", and a row that relies on that is a row that
             * silently loses a line of the §5.2 layout. */
            if (t->size_class == NULL || t->size_class[0] == '\0') {
                printf("    FAIL  %s has an empty size_class\n", code);
                CHECK(0);
            } else if (!utf8_valid(t->size_class)) {
                printf("    FAIL  %s: size_class \"%s\" is not valid UTF-8\n",
                       code, t->size_class);
                CHECK(0);
            }
            /* actype_is_placeholder() dereferences both of these. */
            if (t->manufacturer == NULL || t->model == NULL) {
                printf("    FAIL  %s has a NULL manufacturer or model\n", code);
                CHECK(0);
            }
        }
    }

    GROUP("category picks the sentence fill_reason() shows him");
    {
        /* view_build.c's fill_reason() switches on `category` and nothing else,
         * so the category IS the sentence. A glider carrying AC_CAT_AIRLINER
         * compiles, sorts, and reads fine in review -- and then tells him
         * "Der Flugplan ist im Moment nicht verfügbar." about a sailplane that
         * has never filed one. A sample of each group, spelled out as the
         * sentence rather than as the enum, so the failure message names what
         * he would actually have read. */
        static const struct { const char *code; const char *reason; } rows[] = {
            /* Segelflugzeuge und Motorsegler -> no route is NORMAL */
            { "GLID", STR_REASON_GA },
            { "AS21", STR_REASON_GA },
            { "DISC", STR_REASON_GA },
            { "VENT", STR_REASON_GA },
            { "DG1T", STR_REASON_GA },
            { "SF25", STR_REASON_GA },
            { "DUOD", STR_REASON_GA },   /* already in the table before this pass */
            { "LS4",  STR_REASON_GA },
            /* ... and so is a club Cessna doing circuits */
            { "C177", STR_REASON_GA },
            { "PA38", STR_REASON_GA },
            { "ULAC", STR_REASON_GA },
            { "BALL", STR_REASON_GA },

            /* Hubschrauber -> a different true sentence */
            { "H135", STR_REASON_HELI },
            { "EC35", STR_REASON_HELI },   /* already in the table */
            { "B407", STR_REASON_HELI },
            { "A169", STR_REASON_HELI },
            { "GAZL", STR_REASON_HELI },
            { "S92",  STR_REASON_HELI },

            /* Militär -> not in any public plan, uniformed rotorcraft included */
            { "EUFI", STR_REASON_MIL },    /* already in the table */
            { "PC9",  STR_REASON_MIL },
            { "RFAL", STR_REASON_MIL },
            { "C30J", STR_REASON_MIL },
            { "P8",   STR_REASON_MIL },
            { "NH90", STR_REASON_MIL },
            { "UH60", STR_REASON_MIL },

            /* Linienverkehr -> the plan exists, we just cannot see it */
            { "A320", STR_REASON_UNAVAILABLE },
            { "SU95", STR_REASON_UNAVAILABLE },
            { "DHC6", STR_REASON_UNAVAILABLE },
        };
        for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
            const ac_type_t *t = actype(rows[i].code);
            CHECK(t != NULL);
            if (t == NULL) {
                printf("      actype(\"%s\") returned NULL\n", rows[i].code);
                continue;
            }
            const char *got = reason_for_category(t->category);
            CHECK_STR(got, rows[i].reason);
            if (strcmp(got, rows[i].reason) != 0) {
                printf("      %s would have been told the wrong reason\n", rows[i].code);
            }
        }

        /* And the same rule swept over the whole table, keyed on the size_class
         * he reads: whatever else changes, a row that CALLS itself a glider,
         * a helicopter or a military aircraft must carry the category that
         * makes fill_reason() agree with it. */
        size_t n = 0;
        const actype_lookup_t *e = tbl_actype_entries(&n);
        for (size_t i = 0; i < n; i++) {
            const char *code = e[i].icao_type;
            const char *sc = e[i].info.size_class;
            ac_category_t cat = e[i].info.category;
            if (sc == NULL) { continue; }

            if (strcmp(sc, "Segelflugzeug") == 0 || strcmp(sc, "Motorsegler") == 0 ||
                strcmp(sc, "Ultraleichtflugzeug") == 0 || strcmp(sc, "Tragschrauber") == 0) {
                if (cat != AC_CAT_PRIVATE) {
                    printf("    FAIL  %s is a %s but not AC_CAT_PRIVATE\n", code, sc);
                    CHECK(0);
                }
            } else if (strcmp(sc, "Hubschrauber") == 0) {
                /* Civil or uniformed, but never an airliner and never unknown:
                 * both of those produce a sentence about flight plans that is
                 * simply not true of a helicopter. */
                if (cat != AC_CAT_HELICOPTER && cat != AC_CAT_MILITARY) {
                    printf("    FAIL  %s is a Hubschrauber with category %d\n", code, (int)cat);
                    CHECK(0);
                }
            } else if (strcmp(sc, "Militärflugzeug") == 0) {
                if (cat != AC_CAT_MILITARY) {
                    printf("    FAIL  %s is a Militärflugzeug but not AC_CAT_MILITARY\n", code);
                    CHECK(0);
                }
            }
        }
    }

    GROUP("category split: airliner vs. private/GA");
    {
        const ac_type_t *airliner = actype("A320");
        CHECK(airliner != NULL);
        if (airliner != NULL) {
            CHECK_INT(airliner->category, AC_CAT_AIRLINER);
        }

        const ac_type_t *ga = actype("C172");
        CHECK(ga != NULL);
        if (ga != NULL) {
            CHECK_INT(ga->category, AC_CAT_PRIVATE);
        }

        /* A category mismatch here is worse than a wrong size_class string --
         * it drives DESIGN.md §5.2's explanation of *why* a route is absent. */
        CHECK(airliner->category != ga->category);
    }

    GROUP("German exonyms vs. deliberately-unchanged local names");
    {
        CHECK_STR(airport_de("LOWW"), "Wien");
        CHECK_STR(airport_de("EDDM"), "München");
        CHECK_STR(airport_de("LIMC"), "Mailand");
        CHECK_STR(airport_de("LKPR"), "Prag");
        CHECK_STR(airport_de("EPWA"), "Warschau");
        /* The rule is "the name he would say out loud", not "the name that
         * exists in German". Laibach, Pressburg and a parenthetical
         * "Klausenburg (Cluj-Napoca)" all failed that: the first two are
         * historical rather than current Austrian usage, and a dual name is
         * 25 characters, which cannot be a 100 px hero and must not be
         * truncated (DESIGN.md §3). See docs/DECISIONS.md D17. */
        CHECK_STR(airport_de("LJLJ"), "Ljubljana");
        CHECK_STR(airport_de("LZIB"), "Bratislava");
        CHECK_STR(airport_de("LRCL"), "Klausenburg");
        /* No name may contain a parenthetical alternative -- the hero shows
         * exactly one name. */
        CHECK(strchr(airport_de("LRCL"), '(') == NULL);
        /* No invented German exonym for these -- Austrian usage keeps the
         * local spelling (AGENTS.md §1, the design note in the task brief). */
        CHECK_STR(airport_de("EHAM"), "Amsterdam");
        CHECK_STR(airport_de("LEMD"), "Madrid");
        CHECK_STR(airport_de("LEBL"), "Barcelona");
    }

    GROUP("airport table: a spot check through every catchment");
    {
        /* LGRP is why this table grew. The routeset API answered "Rodes
         * Island" -- not English, not German, not Greek, just data-entry noise
         * -- and resolve_city() put it in the 76 px hero as the answer to
         * "where is that plane going". Every other row here is a sample of a
         * region the fallback used to own. */
        CHECK_STR(airport_de("LGRP"), "Rhodos");
        /* The field he will actually be under in Thailand. "U-Tapao" is how
         * it is written and said locally and in Austrian trip talk; Rayong and
         * Pattaya are both nearby but neither is the field's name. */
        CHECK_STR(airport_de("VTBU"), "U-Tapao");

        /* Vienna TMA and the near neighbours -- the traffic he hears. */
        CHECK_STR(airport_de("LOAN"), "Wiener Neustadt");
        CHECK_STR(airport_de("LOWS"), "Salzburg");
        CHECK_STR(airport_de("LZIB"), "Bratislava");

        /* Western Europe. */
        CHECK_STR(airport_de("LFMN"), "Nizza");
        CHECK_STR(airport_de("LFST"), "Straßburg");
        CHECK_STR(airport_de("EBLG"), "Lüttich");
        CHECK_STR(airport_de("EDDG"), "Münster");
        CHECK_STR(airport_de("LSZR"), "Altenrhein");

        /* Italy and Iberia. */
        CHECK_STR(airport_de("LIPQ"), "Triest");
        CHECK_STR(airport_de("LIPB"), "Bozen");
        CHECK_STR(airport_de("LIME"), "Bergamo");
        CHECK_STR(airport_de("LEMG"), "Málaga");
        CHECK_STR(airport_de("LECO"), "A Coruña");
        CHECK_STR(airport_de("GCTS"), "Teneriffa");

        /* Poland and the near east of the EU: exonyms Austrian German really
         * uses, and none where it does not. */
        CHECK_STR(airport_de("EPGD"), "Danzig");
        CHECK_STR(airport_de("EPWR"), "Breslau");
        CHECK_STR(airport_de("EPPO"), "Posen");
        CHECK_STR(airport_de("EPRZ"), "Rzeszów");
        CHECK_STR(airport_de("LKTB"), "Brünn");
        CHECK_STR(airport_de("LKKV"), "Karlsbad");
        CHECK_STR(airport_de("LRSB"), "Hermannstadt");

        /* Nordics and the Baltic. */
        CHECK_STR(airport_de("ENTC"), "Tromsø");
        CHECK_STR(airport_de("ESGG"), "Göteborg");
        CHECK_STR(airport_de("EYVI"), "Wilna");

        /* Greece, the Adriatic and the eastern Mediterranean -- the corridor
         * Gloggnitz sits on, and the one that produced the bug. */
        CHECK_STR(airport_de("LGKR"), "Korfu");
        CHECK_STR(airport_de("LGIR"), "Heraklion");
        CHECK_STR(airport_de("LGSR"), "Santorin");
        CHECK_STR(airport_de("LDDU"), "Dubrovnik");
        CHECK_STR(airport_de("LCLK"), "Larnaka");

        /* Turkey, the Gulf and the Middle East. */
        CHECK_STR(airport_de("LTFM"), "Istanbul");
        CHECK_STR(airport_de("OMDB"), "Dubai");
        CHECK_STR(airport_de("OERK"), "Riad");
        CHECK_STR(airport_de("ORBI"), "Bagdad");
        CHECK_STR(airport_de("OIIE"), "Teheran");
        CHECK_STR(airport_de("OSDI"), "Damaskus");

        /* North Africa. */
        CHECK_STR(airport_de("HECA"), "Kairo");
        CHECK_STR(airport_de("HESH"), "Sharm el Sheikh");
        CHECK_STR(airport_de("GMMX"), "Marrakesch");

        /* South Asia. */
        CHECK_STR(airport_de("VIDP"), "Neu-Delhi");
        CHECK_STR(airport_de("VECC"), "Kalkutta");
        CHECK_STR(airport_de("VABB"), "Mumbai");   /* not "Bombay" -- see below */
        CHECK_STR(airport_de("VRMM"), "Malé");

        /* East Asia. */
        CHECK_STR(airport_de("ZBAA"), "Peking");
        CHECK_STR(airport_de("RJTT"), "Tokio");
        CHECK_STR(airport_de("VHHH"), "Hongkong");
        CHECK_STR(airport_de("ZWWW"), "Ürümqi");

        /* Thailand, domestic and regional. */
        CHECK_STR(airport_de("VTBS"), "Bangkok");
        CHECK_STR(airport_de("VTCC"), "Chiang Mai");
        CHECK_STR(airport_de("VTSM"), "Koh Samui");
        CHECK_STR(airport_de("VTUU"), "Ubon Ratchathani");
        CHECK_STR(airport_de("WSSS"), "Singapur");
        CHECK_STR(airport_de("WIII"), "Jakarta");
        CHECK_STR(airport_de("RPLL"), "Manila");

        /* North America and the antipodes. */
        CHECK_STR(airport_de("KJFK"), "New York");
        CHECK_STR(airport_de("KLAX"), "Los Angeles");
        CHECK_STR(airport_de("MMMX"), "Mexiko-Stadt");
        CHECK_STR(airport_de("YSSY"), "Sydney");
        CHECK_STR(airport_de("NZAA"), "Auckland");
    }

    GROUP("one city, one name -- whichever of its airports the flight uses");
    {
        /* DESIGN.md's headline answers "where is it going", and the answer is a
         * city. Which of a city's fields the aircraft happens to use is not part
         * of the question, so every ICAO code of a city maps to one string. */
        CHECK_STR(airport_de("EGLL"), "London");
        CHECK_STR(airport_de("EGKK"), "London");
        CHECK_STR(airport_de("EGSS"), "London");
        CHECK_STR(airport_de("EGGW"), "London");
        CHECK_STR(airport_de("EGLC"), "London");
        CHECK_STR(airport_de("LFPG"), "Paris");
        CHECK_STR(airport_de("LFPO"), "Paris");
        CHECK_STR(airport_de("LFPB"), "Paris");
        CHECK_STR(airport_de("VTBS"), "Bangkok");
        CHECK_STR(airport_de("VTBD"), "Bangkok");
        CHECK_STR(airport_de("UUEE"), "Moskau");
        CHECK_STR(airport_de("UUDD"), "Moskau");
        CHECK_STR(airport_de("UUWW"), "Moskau");
        CHECK_STR(airport_de("ZBAA"), "Peking");
        CHECK_STR(airport_de("ZBAD"), "Peking");
        CHECK_STR(airport_de("RJTT"), "Tokio");
        CHECK_STR(airport_de("RJAA"), "Tokio");
    }

    GROUP("no invented German exonym, and no historical one either");
    {
        /* "Neu-York" is the failure this guards. The rule is the name he would
         * say out loud in Gloggnitz in 2026, so the current form wins over the
         * one his atlas printed: Mumbai over Bombay, Ljubljana over Laibach,
         * Bratislava over Pressburg. */
        CHECK_STR(airport_de("KJFK"), "New York");
        CHECK_STR(airport_de("VABB"), "Mumbai");
        CHECK_STR(airport_de("LJLJ"), "Ljubljana");
        CHECK_STR(airport_de("LZIB"), "Bratislava");
        CHECK_STR(airport_de("KSFO"), "San Francisco");
        CHECK_STR(airport_de("LEBL"), "Barcelona");
        CHECK_STR(airport_de("EHAM"), "Amsterdam");
    }

    GROUP("every airport name is usable as a hero");
    {
        /* The hero renders ONE city name at up to 100 px and must never be
         * truncated (DESIGN.md §3), so a name carrying a parenthetical
         * alternative is unrenderable by construction. Checked across the whole
         * table rather than per-entry, so a future addition cannot reintroduce it. */
        size_t n = 0;
        const str_lookup_t *e = tbl_airport_entries(&n);
        CHECK(n > 0);
        for (size_t i = 0; i < n; i++) {
            if (strchr(e[i].value, '(') != NULL) {
                printf("    FAIL  %s has a parenthetical name: \"%s\"\n",
                       e[i].key, e[i].value);
                CHECK(0);
            }
            /* 24 bytes is comfortably past the longest real entry
             * ("Palma de Mallorca", 17) and well inside VIEW_HERO_LEN. */
            if (strlen(e[i].value) > 24) {
                printf("    FAIL  %s name too long for a hero: \"%s\"\n",
                       e[i].key, e[i].value);
                CHECK(0);
            }
        }
    }

    GROUP("every airport row is a displayable city name, table-wide");
    {
        /* Table-wide invariants, so that adding 500 rows cannot quietly
         * introduce the one row that breaks the hero. Each is a failure that
         * has a real mechanism behind it, not a tidiness rule. */
        size_t n = 0;
        const str_lookup_t *e = tbl_airport_entries(&n);
        CHECK(n >= 500);
        for (size_t i = 0; i < n; i++) {
            const char *city = e[i].value;

            /* A NULL or empty value beats resolve_city()'s API fallback (it
             * checks de[0] != '\0' first) and then renders as a blank hero --
             * the one thing DESIGN.md forbids outright. */
            if (city == NULL || city[0] == '\0') {
                printf("    FAIL  %s has an empty city name\n", e[i].key);
                CHECK(0);
                continue;
            }

            /* Four uppercase letters means an ICAO code was pasted into the
             * city column -- the exact thing this whole table exists to stop
             * reaching the panel. */
            if (strlen(city) == 4 &&
                isupper((unsigned char)city[0]) && isupper((unsigned char)city[1]) &&
                isupper((unsigned char)city[2]) && isupper((unsigned char)city[3])) {
                printf("    FAIL  %s: \"%s\" is an ICAO code, not a city\n",
                       e[i].key, city);
                CHECK(0);
            }

            /* It must survive the copy into view_model_t.hero intact. A name
             * truncated at 47 bytes is not a shorter answer, it is a wrong one
             * -- and mid-sequence truncation of a multi-byte character makes it
             * unrenderable as well. */
            if (strlen(city) >= VIEW_HERO_LEN) {
                printf("    FAIL  %s: \"%s\" is %zu bytes, VIEW_HERO_LEN is %d\n",
                       e[i].key, city, strlen(city), (int)VIEW_HERO_LEN);
                CHECK(0);
            }

            /* Valid UTF-8 by structure: a row re-encoded as Latin-1 loses its
             * umlaut on the panel and logs nothing. */
            if (!utf8_valid(city)) {
                printf("    FAIL  %s: \"%s\" is not valid UTF-8\n", e[i].key, city);
                CHECK(0);
            }
        }
        /* The invariants are worth nothing if they silently checked an empty
         * table, and worth nothing if a wrong one passes -- so prove the
         * detectors fire. */
        CHECK(utf8_valid("Br\xC3\xBCssel") == 1);
        CHECK(utf8_valid("Br\xFCssel") == 0);      /* Latin-1 u-umlaut, lone byte */
        CHECK(utf8_valid("\xC3") == 0);             /* lead byte, no continuation */
        CHECK(utf8_valid("\xC3\x28") == 0);         /* bad continuation byte */
    }

    GROUP("emitter category A0 means 'no information', not a category");
    {
        /* Observed live: D-MAVT over Gloggnitz, 54 kt at 2125 ft, with no type,
         * no registration and category "A0". A0 is ICAO for "no ADS-B emitter
         * category information" — so it must NOT resolve. Everything about that
         * aircraft says light aircraft, and saying so would be inventing data
         * the feed explicitly declined to give. The hero says
         * "Unbekanntes Flugzeug" instead, which is honest. */
        CHECK(ac_category_de("A0") == NULL);
        CHECK(ac_category_de("") == NULL);
        CHECK(ac_category_de(NULL) == NULL);
        /* The ones that do carry information still must. */
        CHECK_STR(ac_category_de("A1"), "Leichtflugzeug");
        CHECK_STR(ac_category_de("A7"), "Hubschrauber");
        CHECK_STR(ac_category_de("B1"), "Segelflugzeug");
    }

    GROUP("actype_display_name never yields a raw ICAO code");
    {
        /* The list screen printed "DIMO" and "PA18" at him because
         * actype_full_or_code()'s last resort is the code itself. This is the
         * helper that exists so the hero and the list cannot disagree. */
        CHECK_STR(actype_display_name("DIMO", "A1"), "Diamond HK36 Super Dimona");
        CHECK_STR(actype_display_name("PA18", "A1"), "Piper PA-18 Super Cub");
        CHECK_STR(actype_display_name("B734", "A3"), "Boeing 737-400");

        /* Unknown type, known category -> the class, not the code. */
        CHECK_STR(actype_display_name("ZZZZ", "A1"), "Leichtflugzeug");
        CHECK_STR(actype_display_name("",     "A7"), "Hubschrauber");
        CHECK(actype_display_name(NULL, "B1") != NULL);

        /* Nothing known at all -> NULL, so the caller picks the wording. */
        CHECK(actype_display_name("ZZZZ", "A0") == NULL);
        CHECK(actype_display_name(NULL, NULL) == NULL);

        /* Whatever it returns is never the code that was passed in. */
        const char *codes[] = { "DIMO", "PA18", "B734", "AT75", "PC6T", "G2CA", "TWR" };
        for (unsigned i = 0; i < sizeof codes / sizeof codes[0]; i++) {
            const char *n = actype_display_name(codes[i], "A1");
            CHECK(n != NULL);
            if (n) CHECK(strcmp(n, codes[i]) != 0);
        }
    }

    GROUP("unknown keys return NULL, never a garbage pointer");
    {
        CHECK(airport_de("ZZZZ") == NULL);
        CHECK(airport_de("") == NULL);
        CHECK(airport_de(NULL) == NULL);

        CHECK(airline_de("ZZZ") == NULL);
        CHECK(airline_de("") == NULL);
        CHECK(airline_de(NULL) == NULL);

        CHECK(actype("ZZZZ") == NULL);
        CHECK(actype("") == NULL);
        CHECK(actype(NULL) == NULL);
    }

    GROUP("actype_full_or_code always returns something displayable");
    {
        /* Known type -> the nice full name. */
        CHECK_STR(actype_full_or_code("DV20"), "Diamond DV20 Katana");
        /* Unknown-but-well-formed code -> falls back to the raw code itself,
         * never NULL, never empty -- the panel must always show something. */
        CHECK_STR(actype_full_or_code("ZZZZ"), "ZZZZ");
        /* Degenerate input still never comes back NULL/empty. */
        const char *empty = actype_full_or_code("");
        CHECK(empty != NULL && empty[0] != '\0');
        const char *null_in = actype_full_or_code(NULL);
        CHECK(null_in != NULL && null_in[0] != '\0');
    }

    return test_summary();
}
