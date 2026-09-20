/* The line that names the individual aircraft.
 *
 * Small, but it is the fourth place a raw ICAO designator could reach the
 * panel (D36 removed it from the list, D46 from the hero and the supporting
 * line), so the bulk of this file is making sure it cannot be the fourth.
 */
#include <string.h>

#include "test_util.h"
#include "identity.h"

static aircraft_t mk(const char *flight, const char *reg, const char *type, const char *cat)
{
    aircraft_t a;
    memset(&a, 0, sizeof a);
    snprintf(a.hex, sizeof a.hex, "abc123");
    snprintf(a.flight, sizeof a.flight, "%s", flight);
    snprintf(a.reg, sizeof a.reg, "%s", reg);
    snprintf(a.type, sizeof a.type, "%s", type);
    snprintf(a.category, sizeof a.category, "%s", cat);
    return a;
}

int main(void)
{
    char b[80];

    GROUP("an airliner is named by its flight number");
    {
        aircraft_t a = mk("RYR4MR", "EI-DWG", "B738", "A3");
        aircraft_identity(&a, true, b, sizeof b);
        CHECK_STR(b, "RYR4MR \xC2\xB7 Boeing 737-800");
    }

    GROUP("a private aircraft is named by its registration");
    {
        /* No flight number exists for it, so the registration is not a
         * fallback in the apologetic sense — it is the identifier, and it is
         * the one painted on the tail he is looking at. */
        aircraft_t a = mk("", "OE-ABC", "C172", "A1");
        aircraft_identity(&a, true, b, sizeof b);
        CHECK_STR(b, "OE-ABC \xC2\xB7 Cessna 172 Skyhawk");
    }

    GROUP("never both identifiers at once");
    {
        aircraft_t a = mk("AUA453", "OE-LBA", "A20N", "A3");
        aircraft_identity(&a, true, b, sizeof b);
        CHECK(strstr(b, "AUA453") != NULL);
        CHECK(strstr(b, "OE-LBA") == NULL);   /* two codes side by side is a database row */
    }

    GROUP("with_model=false, for callers whose headline is already the model");
    {
        aircraft_t a = mk("", "OE-ABC", "C172", "A1");
        aircraft_identity(&a, false, b, sizeof b);
        CHECK_STR(b, "OE-ABC");
        CHECK(strstr(b, "Cessna") == NULL);
    }

    GROUP("a raw ICAO designator must never appear — the fourth place it could");
    {
        /* Deliberately designators no table will ever hold, for the same
         * reason test_view.c stopped using C177 as its fixture. */
        static const char *const unknown[] = { "ZZZZ", "QQ12", "7777", "----" };
        for (size_t i = 0; i < sizeof unknown / sizeof unknown[0]; i++) {
            aircraft_t a = mk("AUA453", "OE-LBA", unknown[i], "");
            aircraft_identity(&a, true, b, sizeof b);
            CHECK(strstr(b, unknown[i]) == NULL);
            CHECK_STR(b, "AUA453");            /* the model is simply omitted */

            aircraft_t g = mk("", "", unknown[i], "");
            aircraft_identity(&g, true, b, sizeof b);
            CHECK(strstr(b, unknown[i]) == NULL);
            CHECK_STR(b, "");                  /* nothing known: say nothing */
        }
    }

    GROUP("an unknown type still resolves through the emitter category");
    {
        aircraft_t a = mk("", "OE-5XY", "ZZZZ", "B1");
        aircraft_identity(&a, true, b, sizeof b);
        CHECK_STR(b, "OE-5XY \xC2\xB7 Segelflugzeug");
    }

    GROUP("nothing at all yields an empty line, not a placeholder");
    {
        aircraft_t a = mk("", "", "", "");
        CHECK_INT((int)aircraft_identity(&a, true, b, sizeof b), 0);
        CHECK_STR(b, "");
        /* An empty label disappears; a "?" or a dash looks like a fault. */
    }

    GROUP("model alone, when there is no identifier");
    {
        aircraft_t a = mk("", "", "C172", "A1");
        aircraft_identity(&a, true, b, sizeof b);
        CHECK_STR(b, "Cessna 172 Skyhawk");
        CHECK(strstr(b, "\xC2\xB7") == NULL);   /* no dangling separator */
    }

    GROUP("defensive: NULLs and a buffer too small to hold anything useful");
    {
        CHECK_INT((int)aircraft_identity(NULL, true, b, sizeof b), 0);
        aircraft_t a = mk("RYR4MR", "", "B738", "A3");
        CHECK_INT((int)aircraft_identity(&a, true, NULL, 10), 0);
        CHECK_INT((int)aircraft_identity(&a, true, b, 0), 0);

        char tiny[8];
        aircraft_identity(&a, true, tiny, sizeof tiny);
        CHECK(strlen(tiny) < sizeof tiny);       /* truncated, never overflowed */
        CHECK(tiny[sizeof tiny - 1] == '\0');
    }

    return test_summary();
}
