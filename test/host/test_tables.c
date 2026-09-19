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
        CHECK(n >= 200);
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
