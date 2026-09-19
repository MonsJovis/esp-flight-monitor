/* Host tests for the German lookup tables (docs/PLAN.md M2.5).
 *
 * These tables are what makes the panel say "Wien -> London" and "Austrian
 * Airlines" instead of "LOWW -> EGLL" and "AUA". The highest-value check
 * here is sortedness/uniqueness -- that is the property that makes bsearch
 * correct in the first place, and a single out-of-order entry would make
 * lookups silently fail for everything after it.
 */
#include "test_util.h"

#include "tables.h"
#include "flight_types.h"

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

int main(void)
{
    GROUP("airport table: sorted, unique keys");
    {
        size_t n = 0;
        const str_lookup_t *tbl = tbl_airport_entries(&n);
        CHECK(n >= 60);
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
