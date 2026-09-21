/* The place search: URL building, response parsing, and the IANA -> POSIX
 * timezone conversion that keeps AGENTS.md §6's promise about the clock.
 *
 * Fixtures are REAL responses captured from the endpoint on 2026-09-20, not
 * hand-written JSON — same rule the rest of this suite follows. "Wien"
 * returning three different Wiens is not a contrived case; it is the reason
 * a result row carries a region line at all.
 */
#include "test_util.h"
#include "geo_parse.h"
#include "settings.h"

#define MAXP 8

/* ------------------------------------------------------------------ URL */

static void test_url(void)
{
    GROUP("geo_build_url");

    char url[256];

    CHECK(geo_build_url("Gloggnitz", 6, url, sizeof url) > 0);
    CHECK_STR(url, "http://geocoding-api.open-meteo.com/v1/search"
                   "?name=Gloggnitz&count=6&language=de&format=json");

    /* A space is %20, not '+': the endpoint takes either, but '+' only means
     * a space inside application/x-www-form-urlencoded, and this is a path
     * query. Percent-encoding is right in both. */
    CHECK(geo_build_url("Bad Ischl", 6, url, sizeof url) > 0);
    CHECK(strstr(url, "name=Bad%20Ischl") != NULL);

    /* Anything he could type that is not RFC 3986 unreserved. The keyboard
     * is ASCII today, so the umlaut case is about not breaking when one is
     * added — ü is two UTF-8 bytes and both must be encoded. */
    CHECK(geo_build_url("M\xC3\xBCnchen", 6, url, sizeof url) > 0);
    CHECK(strstr(url, "name=M%C3%BCnchen") != NULL);
    CHECK(geo_build_url("St. P\xC3\xB6lten", 6, url, sizeof url) > 0);
    CHECK(strstr(url, "name=St.%20P%C3%B6lten") != NULL);
    CHECK(geo_build_url("a&b=c", 6, url, sizeof url) > 0);
    CHECK(strstr(url, "name=a%26b%3Dc") != NULL);

    /* Unreserved characters stay literal — an encoder that escapes '-' or
     * '.' works but produces URLs nobody can read in a log line. */
    CHECK(geo_build_url("Baden-Baden", 6, url, sizeof url) > 0);
    CHECK(strstr(url, "name=Baden-Baden") != NULL);

    GROUP("geo_build_url rejects what it should");
    /* The endpoint answers a one-character query with an empty result set,
     * so spending a round trip on it buys an empty list and a delay. */
    CHECK_INT(geo_build_url("W", 6, url, sizeof url), -1);
    CHECK_INT(geo_build_url("", 6, url, sizeof url), -1);
    CHECK_INT(geo_build_url(NULL, 6, url, sizeof url), -1);
    /* Never a truncated URL: half a query string is a valid request for the
     * wrong place, which is worse than no request at all. */
    CHECK_INT(geo_build_url("Gloggnitz", 6, url, 40), -1);
    CHECK_INT(geo_build_url("Gloggnitz", 6, url, 0), -1);

    GROUP("geo_build_url floors the count");
    CHECK(geo_build_url("Wien", 0, url, sizeof url) > 0);
    CHECK(strstr(url, "count=1") != NULL);
    CHECK(geo_build_url("Wien", -5, url, sizeof url) > 0);
    CHECK(strstr(url, "count=1") != NULL);
}

/* ------------------------------------------------------------- timezone */

static void test_tz(void)
{
    char tz[GEO_TZ_LEN];

    GROUP("geo_tz_posix: the table");
    /* This one is load-bearing: it must come back byte-identical to the
     * string AGENTS.md §6 and main/net/timesync.h already carry for
     * Gloggnitz, or a searched Austrian place would run a different clock
     * from the preset next to it on the same screen. */
    CHECK_STR(geo_tz_posix("Europe/Vienna", 16.37, tz, sizeof tz),
              "CET-1CEST,M3.5.0,M10.5.0/3");
    CHECK_STR(geo_tz_posix("Europe/Berlin", 13.40, tz, sizeof tz),
              "CET-1CEST,M3.5.0,M10.5.0/3");
    CHECK_STR(geo_tz_posix("Asia/Bangkok", 100.50, tz, sizeof tz), "<+07>-7");
    CHECK_STR(geo_tz_posix("Europe/London", -0.13, tz, sizeof tz),
              "GMT0BST,M3.5.0/1,M10.5.0");
    CHECK_STR(geo_tz_posix("Europe/Athens", 23.73, tz, sizeof tz),
              "EET-2EEST,M3.5.0/3,M10.5.0/4");
    CHECK_STR(geo_tz_posix("America/New_York", -74.0, tz, sizeof tz),
              "EST5EDT,M3.2.0,M11.1.0");
    /* Both spellings of the Ukrainian capital are in the table: tzdata
     * renamed Kiev to Kyiv and data sources moved at different times.
     *
     * TWO BUFFERS, DELIBERATELY. This was one — both calls wrote into `tz`
     * and returned it, so CHECK_STR compared the buffer with itself and
     * passed for any table at all, including one where both spellings were
     * missing and both fell through to the longitude guess. A check that
     * cannot fail is not a check. */
    char tz_kyiv[64], tz_kiev[64];
    CHECK_STR(geo_tz_posix("Europe/Kyiv", 30.5, tz_kyiv, sizeof tz_kyiv),
              geo_tz_posix("Europe/Kiev", 30.5, tz_kiev, sizeof tz_kiev));
    /* And pin the value, so that "both agree" cannot mean "both guessed". */
    CHECK_STR(tz_kyiv, "EET-2EEST,M3.5.0/3,M10.5.0/4");

    GROUP("geo_tz_posix: the longitude fallback");
    /* The POSIX sign is INVERTED against how people say it out loud: an
     * offset of UTC+7 is written "-7". Getting this backwards puts the clock
     * fourteen hours out, which is the single easiest way to break this
     * function, so both directions are pinned. */
    CHECK_STR(geo_tz_posix("Mars/Olympus", 100.8721, tz, sizeof tz), "<+07>-7");
    CHECK_STR(geo_tz_posix("Mars/Olympus", 15.9303, tz, sizeof tz), "<+01>-1");
    CHECK_STR(geo_tz_posix("Mars/Olympus", -74.0, tz, sizeof tz), "<-05>5");
    CHECK_STR(geo_tz_posix("Mars/Olympus", 0.0, tz, sizeof tz), "<+00>0");
    CHECK_STR(geo_tz_posix(NULL, 100.8721, tz, sizeof tz), "<+07>-7");
    CHECK_STR(geo_tz_posix("", 100.8721, tz, sizeof tz), "<+07>-7");

    GROUP("geo_tz_posix: the ends of the world");
    /* The dateline. Clamped rather than allowed to produce "<+13>-13" for a
     * longitude nobody can stand on. */
    CHECK_STR(geo_tz_posix("Mars/Olympus", 179.9, tz, sizeof tz), "<+12>-12");
    CHECK_STR(geo_tz_posix("Mars/Olympus", -179.9, tz, sizeof tz), "<-12>12");

    GROUP("geo_tz_posix: every rule fits GEO_TZ_LEN");
    /* The buffer is a fixed field in settings_t and goes into NVS, so a rule
     * longer than it would be silently truncated into a TZ string that
     * setenv() half-understands. Walk the whole table rather than trusting
     * the longest one anybody noticed. */
    {
        char small[GEO_TZ_LEN];
        /* A zone from each family, longest rules first. */
        const char *zones[] = {
            "Australia/Adelaide", "Australia/Sydney", "Asia/Jerusalem",
            "Europe/Athens", "Europe/Vienna", "Europe/London", "Europe/Lisbon",
            "America/Santiago", "Pacific/Auckland", "Asia/Kathmandu",
        };
        for (size_t i = 0; i < sizeof zones / sizeof zones[0]; i++) {
            const char *r = geo_tz_posix(zones[i], 0.0, small, sizeof small);
            CHECK(strlen(r) < GEO_TZ_LEN - 1);
        }
    }
}

/* ---------------------------------------------------------------- parse */

static void test_parse_wien(void)
{
    GROUP("geo_parse: three Wiens, told apart by their region");

    char *json = load_fixture("geocode_wien.json");
    geo_place_t p[MAXP];
    int n = geo_parse(json, strlen(json), p, MAXP);

    CHECK_INT(n, 4);

    /* The one he means is first, because the endpoint sorts by population
     * and the Austrian capital outweighs two American villages by four
     * orders of magnitude. Nothing in this code does the sorting; it is
     * asserted so that a day when the endpoint stops doing it is a failing
     * test rather than a father-in-law in Missouri. */
    CHECK_STR(p[0].name, "Wien");
    CHECK_STR(p[0].region, "Bundesland Wien \xC2\xB7 \xC3\x96sterreich");
    CHECK_STR(p[0].label, "Wien \xC2\xB7 Bundesland Wien");
    CHECK_NEAR(p[0].lat, 48.2085, 0.01);
    CHECK_NEAR(p[0].lon, 16.3721, 0.01);
    CHECK_STR(p[0].tz, "CET-1CEST,M3.5.0,M10.5.0/3");

    /* And the two that are not it say so on their own line. */
    CHECK_STR(p[1].name, "Wien");
    CHECK(strstr(p[1].region, "Missouri") != NULL);
    CHECK_STR(p[1].tz, "CST6CDT,M3.2.0,M11.1.0");
    CHECK(strcmp(p[0].region, p[1].region) != 0);

    free(json);
}

static void test_parse_pattaya(void)
{
    GROUP("geo_parse: Pattaya, and the other Pattaya");

    char *json = load_fixture("geocode_pattaya.json");
    geo_place_t p[MAXP];
    int n = geo_parse(json, strlen(json), p, MAXP);

    CHECK(n >= 2);
    CHECK_STR(p[0].name, "Pattaya");
    CHECK(strstr(p[0].region, "Thailand") != NULL);
    /* AGENTS.md §6's measured coordinates for the Thai home, to two places —
     * the geocoder points at the city and the flat is on Thappraya Rd, which
     * is the ~1 km of slack this design accepts on purpose. */
    CHECK_NEAR(p[0].lat, 12.92, 0.05);
    CHECK_NEAR(p[0].lon, 100.88, 0.05);
    CHECK_STR(p[0].tz, "<+07>-7");

    /* The Burmese one, which is why the country is on the row at all. */
    CHECK(strstr(p[1].region, "Myanmar") != NULL);

    free(json);
}

static void test_parse_empty(void)
{
    GROUP("geo_parse: nothing found is not an error");

    /* The endpoint answers a query that matched nothing with a body that has
     * no `results` key at all — {"generationtime_ms":0.1}. Reporting that as
     * a parse failure would put "Die Suche hat nicht geantwortet" on screen
     * for a man who simply mistyped a town name. */
    char *json = load_fixture("geocode_empty.json");
    geo_place_t p[MAXP];
    CHECK_INT(geo_parse(json, strlen(json), p, MAXP), 0);
    free(json);

    {
        const char *j = "{\"results\":[]}";
        CHECK_INT(geo_parse(j, strlen(j), p, MAXP), 0);
    }
}

static void test_parse_junk(void)
{
    GROUP("geo_parse: a body that is not JSON");

    geo_place_t p[MAXP];
    {
        const char *html = "<html>502 Bad Gateway</html>";
        const char *obj  = "{\"results\":[{}]}";
        CHECK_INT(geo_parse(html, strlen(html), p, MAXP), -1);
        CHECK_INT(geo_parse("", 0, p, MAXP), -1);
        CHECK_INT(geo_parse(NULL, 0, p, MAXP), -1);
        CHECK_INT(geo_parse(obj, strlen(obj), p, 0), -1);   /* max <= 0 */
    }

    GROUP("geo_parse: half a response");
    /* A truncated body (http_get.c reports it, and the caller may still pass
     * it in) must not be a crash. cJSON rejects it; what matters is that
     * nothing here reads past the length it was given. */
    char *json = load_fixture("geocode_wien.json");
    CHECK_INT(geo_parse(json, 60, p, MAXP), -1);
    free(json);
}

static void test_parse_hostile(void)
{
    GROUP("geo_parse: hits that cannot be shown are dropped");

    geo_place_t p[MAXP];

    {
        /* No coordinates: nothing to poll if he taps it. */
        const char *no_pos  = "{\"results\":[{\"name\":\"Nirgendwo\"}]}";
        /* No name: a blank row he might tap anyway. */
        const char *no_name = "{\"results\":[{\"latitude\":1,\"longitude\":2}]}";
        CHECK_INT(geo_parse(no_pos, strlen(no_pos), p, MAXP), 0);
        CHECK_INT(geo_parse(no_name, strlen(no_name), p, MAXP), 0);
    }

    /* A name with no glyphs in the font subset. LVGL draws a missing glyph
     * as NOTHING (AGENTS.md §7), so this would have been an empty row that
     * moves the device when tapped. Cyrillic here; in practice `language=de`
     * makes this rare, which is exactly why it needs a test rather than a
     * field report. */
    {
        const char *j = "{\"results\":[{\"name\":\"\xD0\x9C\xD0\xBE\xD1\x81\xD0\xBA\xD0\xB2\xD0\xB0\","
                        "\"latitude\":55.75,\"longitude\":37.61}]}";
        CHECK_INT(geo_parse(j, strlen(j), p, MAXP), 0);
    }

    /* But an unrenderable REGION only costs the region line — the place is
     * still findable and still has a name he can read. */
    {
        const char *j = "{\"results\":[{\"name\":\"Moskau\",\"latitude\":55.75,"
                        "\"longitude\":37.61,\"country\":\"\xD0\xA0\xD0\xBE\xD1\x81\xD1\x81\xD0\xB8\xD1\x8F\"}]}";
        CHECK_INT(geo_parse(j, strlen(j), p, MAXP), 1);
        CHECK_STR(p[0].name, "Moskau");
        CHECK_STR(p[0].region, "");
    }

    /* Umlauts and the Polish/Czech range the fonts DO carry must survive. */
    {
        const char *j = "{\"results\":[{\"name\":\"M\xC3\xBCnchen\",\"latitude\":48.14,"
                        "\"longitude\":11.58,\"admin1\":\"Bayern\"}]}";
        CHECK_INT(geo_parse(j, strlen(j), p, MAXP), 1);
        CHECK_STR(p[0].name, "M\xC3\xBCnchen");
        CHECK_STR(p[0].label, "M\xC3\xBCnchen \xC2\xB7 Bayern");
    }
}

static void test_parse_caps(void)
{
    GROUP("geo_parse: never writes past max");

    char *json = load_fixture("geocode_wien.json");
    geo_place_t p[MAXP];
    memset(p, 0xAA, sizeof p);

    int n = geo_parse(json, strlen(json), p, 2);
    CHECK_INT(n, 2);
    /* The third slot is untouched — the fixture has four hits. */
    CHECK(p[2].name[0] == (char)0xAA);

    free(json);

    GROUP("geo_parse: a long name truncates rather than overflowing");
    {
        char j[512];
        snprintf(j, sizeof j,
                 "{\"results\":[{\"name\":\"%s\",\"latitude\":1,\"longitude\":2}]}",
                 "Llanfairpwllgwyngyllgogerychwyrndrobwllllantysiliogogogoch"
                 "Llanfairpwllgwyngyllgogerychwyrndrobwllllantysiliogogogoch");
        geo_place_t q[1];
        CHECK_INT(geo_parse(j, strlen(j), q, 1), 1);
        CHECK_INT(strlen(q[0].name), GEO_NAME_LEN - 1);
    }
}

/* ------------------------------------------------- settings integration */

static void test_settings_tz(void)
{
    GROUP("settings_tz: the clock follows the place");

    settings_t s;
    settings_defaults(&s);

    /* A preset answers from the preset table, as it always did. */
    s.preset = LOC_GLOGGNITZ;
    CHECK_STR(settings_tz(&s), "CET-1CEST,M3.5.0,M10.5.0/3");
    s.preset = LOC_PATTAYA;
    CHECK_STR(settings_tz(&s), "ICT-7");
    s.preset = LOC_WIEN;
    CHECK_STR(settings_tz(&s), "CET-1CEST,M3.5.0,M10.5.0/3");

    /* A custom place answers from what the search stored. */
    s.preset = LOC_CUSTOM;
    snprintf(s.custom_tz, sizeof s.custom_tz, "%s", "<+07>-7");
    CHECK_STR(settings_tz(&s), "<+07>-7");

    /* AND AN UNSET CUSTOM PLACE ANSWERS GLOGGNITZ, NOT UTC. This is the bug
     * that started all of this: the preset table said "UTC0" for LOC_CUSTOM,
     * so tapping that card moved the panel clock two hours without moving
     * the device an inch. */
    s.custom_tz[0] = '\0';
    CHECK_STR(settings_tz(&s), "CET-1CEST,M3.5.0,M10.5.0/3");

    CHECK_STR(settings_tz(NULL), "CET-1CEST,M3.5.0,M10.5.0/3");
}

static void test_settings_custom_fields(void)
{
    GROUP("settings: the searched place survives a sanitise");

    settings_t s;
    settings_defaults(&s);
    CHECK_STR(s.custom_label, "");   /* never searched, and the card says so */
    CHECK_STR(s.custom_tz, "");

    s.preset = LOC_CUSTOM;
    s.custom_lat = 47.2683;
    s.custom_lon = 11.4008;
    snprintf(s.custom_label, sizeof s.custom_label, "%s", "Innsbruck \xC2\xB7 Tirol");
    snprintf(s.custom_tz, sizeof s.custom_tz, "%s", "CET-1CEST,M3.5.0,M10.5.0/3");
    settings_sanitise(&s);

    CHECK_STR(s.custom_label, "Innsbruck \xC2\xB7 Tirol");
    CHECK_INT(s.preset, LOC_CUSTOM);
    double lat = 0, lon = 0;
    settings_coords(&s, &lat, &lon);
    CHECK_NEAR(lat, 47.2683, 0.0001);
    CHECK_NEAR(lon, 11.4008, 0.0001);

    GROUP("settings: an unterminated blob cannot run off the end");
    /* What a corrupt or truncated NVS read looks like by the time it reaches
     * settings_sanitise(): no NUL anywhere in the field. Both are read as C
     * strings immediately afterwards — one by a label, one by setenv(). */
    memset(s.custom_label, 'X', sizeof s.custom_label);
    memset(s.custom_tz, 'Y', sizeof s.custom_tz);
    settings_sanitise(&s);
    CHECK_INT(strlen(s.custom_label), SETTINGS_LABEL_LEN - 1);
    CHECK_INT(strlen(s.custom_tz), SETTINGS_TZ_LEN - 1);

    GROUP("settings: the two length pairs agree");
    /* geo_place_t's strings are copied straight into settings_t's. They are
     * declared in different headers on purpose (the data layer does not
     * include the network layer), so nothing but this check stops them
     * drifting apart into a silent truncation. */
    CHECK_INT(SETTINGS_LABEL_LEN, GEO_LABEL_LEN);
    CHECK_INT(SETTINGS_TZ_LEN, GEO_TZ_LEN);
}

int main(void)
{
    printf("\ngeo — the place search\n");
    test_url();
    test_tz();
    test_parse_wien();
    test_parse_pattaya();
    test_parse_empty();
    test_parse_junk();
    test_parse_hostile();
    test_parse_caps();
    test_settings_tz();
    test_settings_custom_fields();
    return test_summary();
}
