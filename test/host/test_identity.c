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

    GROUP("the flight number is the ticket's, not the ATC callsign (D89)");
    {
        /* What he reported: the panel said "AUA453" and the boarding pass
         * says "OS 453". Same flight, different airline code. */
        aircraft_t a = mk("AUA453", "OE-LBA", "A20N", "A3");
        aircraft_identity(&a, true, b, sizeof b);
        CHECK_STR(b, "OS 453 \xC2\xB7 Airbus A320neo");

        flight_number("RYR2381", b, sizeof b);  CHECK_STR(b, "FR 2381");
        flight_number("DLH1234", b, sizeof b);  CHECK_STR(b, "LH 1234");
        flight_number("EWG9", b, sizeof b);     CHECK_STR(b, "EW 9");
        flight_number("THA931", b, sizeof b);   CHECK_STR(b, "TG 931");
        flight_number("BAW012", b, sizeof b);   CHECK_STR(b, "BA 12");   /* leading zeros */
    }

    GROUP("where no flight number can be derived, the callsign stays");
    {
        /* Letters in the suffix: an operational callsign. The flight number
         * behind it is not in it, and inventing one would be worse than
         * showing what the aircraft actually transmits. */
        static const char *const keep[] = {
            "RYR4MR", "DLH1JN", "AUA695J", "EZY12AB",   /* alphanumeric suffix       */
            "XYZ123",                                   /* airline not in the table  */
            "WZZ839",                                   /* digits are not the number:
                                                           W6 2274, not W6 839 (D93) */
            "OEABC", "DEVSL",                           /* a registration as callsign */
            "BAW0000", "AUA", "AUA12345", "aua453",     /* no number / too long / case */
        };
        for (size_t i = 0; i < sizeof keep / sizeof keep[0]; i++) {
            flight_number(keep[i], b, sizeof b);
            CHECK_STR(b, keep[i]);
        }
        flight_number("", b, sizeof b);    CHECK_STR(b, "");
        flight_number(NULL, b, sizeof b);  CHECK_STR(b, "");
        CHECK_INT((int)flight_number("AUA453", NULL, 8), 0);

        char tiny[4];
        CHECK_INT((int)flight_number("AUA453", tiny, sizeof tiny), 3);
        CHECK_STR(tiny, "OS ");                        /* truncated, never overflowed */
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
        CHECK(strstr(b, "OS 453") != NULL);
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
            CHECK_STR(b, "OS 453");            /* the model is simply omitted */

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
