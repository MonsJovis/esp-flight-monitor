#include "identity.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "strings_de.h"
#include "tables.h"

size_t identity_compose(const char *who, const char *model, char *out, size_t outsz)
{
    if (out == NULL || outsz == 0) {
        return 0;
    }
    out[0] = '\0';
    bool has_who   = (who   != NULL && who[0]   != '\0');
    bool has_model = (model != NULL && model[0] != '\0');

    if (has_who && has_model) {
        return (size_t)snprintf(out, outsz, "%s" STR_ID_SEP "%s", who, model);
    }
    if (has_who) {
        return (size_t)snprintf(out, outsz, "%s", who);
    }
    if (has_model) {
        return (size_t)snprintf(out, outsz, "%s", model);
    }
    return 0;   /* nothing known: an empty label disappears, a dash looks like a fault */
}

size_t flight_number(const char *callsign, char *out, size_t outsz)
{
    if (out == NULL || outsz == 0) {
        return 0;
    }
    out[0] = '\0';
    if (callsign == NULL || callsign[0] == '\0') {
        return 0;
    }

    /* Three letters of ICAO airline code, then one to four digits and nothing
     * else. Anything with a letter after the code — "RYR4MR", "DLH1JN" — is
     * an operational callsign the airline chose so as not to be confused
     * with a similar number on the same frequency, and the flight number
     * behind it is not in the callsign at all. */
    size_t len = strlen(callsign);
    bool   ok  = (len >= 4 && len <= 7);
    for (size_t i = 0; ok && i < len; i++) {
        ok = (i < 3) ? isupper((unsigned char)callsign[i])
                     : isdigit((unsigned char)callsign[i]);
    }

    const char *digits = callsign + 3;
    while (ok && *digits == '0') {
        digits++;
    }
    char code[4] = "";
    if (ok) {
        memcpy(code, callsign, 3);
    }
    const char *iata = (ok && *digits != '\0') ? airline_iata(code) : NULL;

    int n = (iata != NULL) ? snprintf(out, outsz, "%s %s", iata, digits)
                           : snprintf(out, outsz, "%s", callsign);
    if (n < 0) {
        out[0] = '\0';
        return 0;
    }
    return ((size_t)n < outsz) ? (size_t)n : outsz - 1;
}

size_t aircraft_identity(const aircraft_t *ac, bool with_model,
                         char *out, size_t outsz)
{
    if (out == NULL || outsz == 0) {
        return 0;
    }
    out[0] = '\0';
    if (ac == NULL) {
        return 0;
    }

    /* Flight number first, registration as the fallback — see identity.h. Both are
     * fixed-size arrays in aircraft_t, so a missing one is an empty string
     * rather than NULL. */
    char flight[16];
    aircraft_flight_number(ac, flight, sizeof flight);
    const char *who = (flight[0]  != '\0') ? flight
                    : (ac->reg[0] != '\0') ? ac->reg
                    : NULL;

    /* NULL when neither the table nor the emitter category knows anything.
     * Never a raw ICAO designator — that is the D46 bug, and this line would
     * be the fourth place to reintroduce it. */
    const char *model = with_model ? actype_display_name(ac->type, ac->category) : NULL;

    return identity_compose(who, model, out, outsz);
}

size_t aircraft_flight_number(const aircraft_t *ac, char *out, size_t outsz)
{
    if (out == NULL || outsz == 0) {
        return 0;
    }
    if (ac == NULL) {
        out[0] = '\0';
        return 0;
    }
    if (ac->flight_no[0] != '\0') {
        int n = snprintf(out, outsz, "%s", ac->flight_no);
        return (n < 0) ? 0 : ((size_t)n < outsz ? (size_t)n : outsz - 1);
    }
    return flight_number(ac->flight, out, outsz);
}
