/* German city-name lookup, keyed on the 4-letter ICAO airport code.
 *
 * The routeset API returns ICAO codes in `airport_codes` ("LOWW-EGLL") and
 * English city names in `location` ("Vienna"). This table supplies the German
 * exonym where Austrian usage genuinely has one (Mailand, Prag, Warschau,
 * Laibach, Krakau, Klausenburg) and otherwise deliberately keeps the local
 * spelling (Amsterdam, Madrid, Barcelona) rather than inventing a German name
 * nobody uses. Sorted ascending by ICAO code for bsearch — test_tables.c
 * checks sortedness and uniqueness directly against tbl_airport_entries().
 */
#include "tables.h"

#include <stdlib.h>
#include <string.h>

static const str_lookup_t AIRPORTS[] = {
    {"BIKF", "Reykjavik"},
    {"EBBR", "Brüssel"},
    {"EDDB", "Berlin"},
    {"EDDF", "Frankfurt"},
    {"EDDH", "Hamburg"},
    {"EDDK", "Köln"},
    {"EDDL", "Düsseldorf"},
    {"EDDM", "München"},
    {"EDDN", "Nürnberg"},
    {"EDDP", "Leipzig"},
    {"EDDS", "Stuttgart"},
    {"EDDV", "Hannover"},
    {"EDDW", "Bremen"},
    {"EETN", "Tallinn"},
    {"EFHK", "Helsinki"},
    {"EGCC", "Manchester"},
    {"EGKK", "London"},
    {"EGLL", "London"},
    {"EGSS", "London"},
    {"EHAM", "Amsterdam"},
    {"EIDW", "Dublin"},
    {"EKCH", "Kopenhagen"},
    {"ENGM", "Oslo"},
    {"EPKK", "Krakau"},
    {"EPWA", "Warschau"},
    {"ESSA", "Stockholm"},
    {"EVRA", "Riga"},
    {"EYVI", "Wilna"},
    {"HECA", "Kairo"},
    {"LBSF", "Sofia"},
    {"LEBL", "Barcelona"},
    {"LEMD", "Madrid"},
    {"LEPA", "Palma de Mallorca"},
    {"LFPG", "Paris"},
    {"LFPO", "Paris"},
    {"LGAV", "Athen"},
    {"LGSM", "Samos"},
    {"LHBP", "Budapest"},
    {"LIMC", "Mailand"},
    {"LIPZ", "Venedig"},
    {"LIRF", "Rom"},
    {"LIRZ", "Perugia"},
    {"LJLJ", "Laibach"},
    {"LKPR", "Prag"},
    {"LLBG", "Tel Aviv"},
    {"LOWG", "Graz"},
    {"LOWI", "Innsbruck"},
    {"LOWK", "Klagenfurt"},
    {"LOWL", "Linz"},
    {"LOWS", "Salzburg"},
    {"LOWW", "Wien"},
    {"LPPT", "Lissabon"},
    {"LRCL", "Klausenburg (Cluj-Napoca)"},
    {"LROP", "Bukarest"},
    {"LSGG", "Genf"},
    {"LSZH", "Zürich"},
    {"LTAI", "Antalya"},
    {"LTBA", "Istanbul"},
    {"LTFM", "Istanbul"},
    {"LWSK", "Skopje"},
    {"LYBE", "Belgrad"},
    {"LZIB", "Pressburg (Bratislava)"},
    {"OLBA", "Beirut"},
    {"OMDB", "Dubai"},
    {"OTHH", "Doha"},
    {"UKBB", "Kiew"},
    {"UUEE", "Moskau"},
    {"VTBD", "Bangkok"},
    {"VTBS", "Bangkok"},
    {"VTCC", "Chiang Mai"},
    {"VTSG", "Krabi"},
    {"VTSM", "Koh Samui"},
    {"VTSP", "Phuket"},
    {"VTUU", "Ubon Ratchathani"},
};

#define AIRPORT_COUNT (sizeof(AIRPORTS) / sizeof(AIRPORTS[0]))

static int airport_cmp(const void *key, const void *elem)
{
    const char *k = (const char *)key;
    const str_lookup_t *e = (const str_lookup_t *)elem;
    return strcmp(k, e->key);
}

const char *airport_de(const char *icao)
{
    if (icao == NULL || icao[0] == '\0') {
        return NULL;
    }
    const str_lookup_t *hit = (const str_lookup_t *)bsearch(
        icao, AIRPORTS, AIRPORT_COUNT, sizeof(AIRPORTS[0]), airport_cmp);
    return hit ? hit->value : NULL;
}

const str_lookup_t *tbl_airport_entries(size_t *count)
{
    *count = AIRPORT_COUNT;
    return AIRPORTS;
}
