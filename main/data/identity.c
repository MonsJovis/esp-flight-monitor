#include "identity.h"

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

    /* Callsign first, registration as the fallback — see identity.h. Both are
     * fixed-size arrays in aircraft_t, so a missing one is an empty string
     * rather than NULL. */
    const char *who = (ac->flight[0] != '\0') ? ac->flight
                    : (ac->reg[0]    != '\0') ? ac->reg
                    : NULL;

    /* NULL when neither the table nor the emitter category knows anything.
     * Never a raw ICAO designator — that is the D46 bug, and this line would
     * be the fourth place to reintroduce it. */
    const char *model = with_model ? actype_display_name(ac->type, ac->category) : NULL;

    return identity_compose(who, model, out, outsz);
}
