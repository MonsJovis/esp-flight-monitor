/* Host-side unit tests for the JSON parsers. No board, no network — see
 * AGENTS.md §7 and PLAN.md M2: parsing is where the bugs live and it is the
 * only part testable off-device in milliseconds.
 */
#include "test_util.h"

#include <string.h>

#include "adsb_parse.h"
#include "cJSON.h"
#include "flight_types.h"
#include "route_parse.h"

static const aircraft_t *find_ac_by_hex(const aircraft_t *acs, int n, const char *hex)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(acs[i].hex, hex) == 0) {
            return &acs[i];
        }
    }
    return NULL;
}

static void test_adsb_real_fixture(void)
{
    GROUP("adsb_parse: real 30 nm Gloggnitz fixture");

    char *json = load_fixture("adsblol_gloggnitz_30nm.json");
    aircraft_t acs[MAX_AIRCRAFT];

    int n = adsb_parse(json, strlen(json), acs, MAX_AIRCRAFT);
    CHECK_INT(n, 13);

    /* Sorted ascending by dst_nm, so acs[0] is "the plane overhead". */
    for (int i = 0; i + 1 < n; i++) {
        CHECK(acs[i].dst_nm <= acs[i + 1].dst_nm);
    }
    CHECK_STR(acs[0].hex, "447ac7"); /* FFMSNE, dst 7.731 nm — the nearest */

    /* flight is space-padded to 8 chars in the raw feed; must come out trimmed. */
    const aircraft_t *dlh = find_ac_by_hex(acs, n, "3c658c");
    CHECK(dlh != NULL);
    if (dlh != NULL) {
        CHECK_STR(dlh->flight, "DLH1JN");
        CHECK_INT(strlen(dlh->flight), 6);
    }

    const aircraft_t *aua = find_ac_by_hex(acs, n, "502d58");
    CHECK(aua != NULL);
    if (aua != NULL) {
        CHECK_STR(aua->flight, "AUA76PZ");
        CHECK_INT(strlen(aua->flight), 7);
    }

    /* OEVSO (440277) and OEANW (440475) have no "t" or "r" in the raw feed —
     * military/blocked-adjacent GA traffic. Must not crash, must yield "". */
    const aircraft_t *oevso = find_ac_by_hex(acs, n, "440277");
    CHECK(oevso != NULL);
    if (oevso != NULL) {
        CHECK_STR(oevso->type, "");
        CHECK_STR(oevso->reg, "");
    }

    const aircraft_t *oeanw = find_ac_by_hex(acs, n, "440475");
    CHECK(oeanw != NULL);
    if (oeanw != NULL) {
        CHECK_STR(oeanw->type, "");
        CHECK_STR(oeanw->reg, "");
    }

    /* FFMSNE (447ac7) has no "gs" or "track" in the raw feed. */
    const aircraft_t *ffmsne = find_ac_by_hex(acs, n, "447ac7");
    CHECK(ffmsne != NULL);
    if (ffmsne != NULL) {
        CHECK_INT(ffmsne->gs_kt, -1);
        CHECK(ffmsne->has_track == false);
        CHECK_STR(ffmsne->flight, "FFMSNE");
    }

    /* Sanity on a fully-populated entry. */
    if (dlh != NULL) {
        CHECK_STR(dlh->type, "A319");
        CHECK_STR(dlh->reg, "D-AILL");
        CHECK_INT(dlh->alt_ft, 31200);
        CHECK_INT(dlh->gs_kt, 402);
        CHECK(dlh->has_track == true);
        CHECK_NEAR(dlh->track_deg, 281.17, 0.01);
        CHECK_NEAR(dlh->dst_nm, 27.038, 0.001);
        CHECK_NEAR(dlh->lat, 47.799652, 0.000001);
        CHECK_NEAR(dlh->lon, 15.288109, 0.000001);
    }

    free(json);
}

static void test_adsb_alt_baro_edge_cases(void)
{
    GROUP("adsb_parse: alt_baro ground / absent");

    char *json = load_fixture("adsb_alt_edge.json");
    aircraft_t acs[MAX_AIRCRAFT];

    int n = adsb_parse(json, strlen(json), acs, MAX_AIRCRAFT);
    CHECK_INT(n, 2);

    const aircraft_t *grnd = find_ac_by_hex(acs, n, "aaaaaa");
    CHECK(grnd != NULL);
    if (grnd != NULL) {
        CHECK_INT(grnd->alt_ft, ALT_GROUND);
    }

    const aircraft_t *unk = find_ac_by_hex(acs, n, "bbbbbb");
    CHECK(unk != NULL);
    if (unk != NULL) {
        CHECK_INT(unk->alt_ft, ALT_UNKNOWN);
    }

    free(json);
}

static void test_adsb_wrapper_key_aircraft(void)
{
    GROUP("adsb_parse: \"aircraft\" wrapper key (adsb.fi v2)");

    char *json = load_fixture("adsb_wrapper_aircraft.json");
    aircraft_t acs[MAX_AIRCRAFT];

    int n = adsb_parse(json, strlen(json), acs, MAX_AIRCRAFT);
    CHECK_INT(n, 1);
    if (n == 1) {
        CHECK_STR(acs[0].hex, "cccccc");
        CHECK_STR(acs[0].flight, "WRAP0001");
        CHECK_INT(acs[0].alt_ft, 1000);
        CHECK_INT(acs[0].gs_kt, 100);
        CHECK(acs[0].has_track == true);
        CHECK_NEAR(acs[0].track_deg, 90.0, 0.001);
    }

    free(json);
}

static void test_adsb_empty_list(void)
{
    GROUP("adsb_parse: empty aircraft list");

    char *json = load_fixture("adsb_empty.json");
    aircraft_t acs[MAX_AIRCRAFT];

    int n = adsb_parse(json, strlen(json), acs, MAX_AIRCRAFT);
    CHECK_INT(n, 0);

    free(json);
}

static void test_adsb_malformed(void)
{
    GROUP("adsb_parse: malformed / truncated / wrong-shape JSON -> -1");

    char *json = load_fixture("adsb_malformed.json");
    aircraft_t acs[MAX_AIRCRAFT];
    CHECK_INT(adsb_parse(json, strlen(json), acs, MAX_AIRCRAFT), -1);
    free(json);

    /* Not JSON at all. */
    static const char garbage[] = "not even close to json {{{";
    CHECK_INT(adsb_parse(garbage, strlen(garbage), acs, MAX_AIRCRAFT), -1);

    /* Valid JSON, wrong shape: a bare array instead of an object. */
    static const char wrong_shape[] = "[1,2,3]";
    CHECK_INT(adsb_parse(wrong_shape, strlen(wrong_shape), acs, MAX_AIRCRAFT), -1);

    /* Valid JSON object, but neither "ac" nor "aircraft" present. */
    static const char no_wrapper[] = "{\"foo\":[1,2,3]}";
    CHECK_INT(adsb_parse(no_wrapper, strlen(no_wrapper), acs, MAX_AIRCRAFT), -1);

    /* NULL / degenerate inputs must not crash. */
    CHECK_INT(adsb_parse(NULL, 0, acs, MAX_AIRCRAFT), -1);
    CHECK_INT(adsb_parse("{}", 2, acs, 0), -1);
}

static void test_adsb_clamps_to_max(void)
{
    GROUP("adsb_parse: more aircraft than max -> clamped, no overflow");

    char *json = load_fixture("adsblol_gloggnitz_30nm.json"); /* 13 aircraft */

    /* buf[2] is a canary one past the requested max=2 window: if the parser
     * ever overflowed, it would land here first. */
    aircraft_t buf[3];
    memset(buf, 0, sizeof buf);
    strncpy(buf[2].hex, "CANARY", sizeof buf[2].hex - 1);

    int n = adsb_parse(json, strlen(json), buf, 2);
    CHECK_INT(n, 2);
    CHECK_STR(buf[2].hex, "CANARY"); /* untouched */

    free(json);
}

static void test_route_real_fixture(void)
{
    GROUP("route_parse: real routeset fixture");

    char *json = load_fixture("routeset_response.json");
    route_t routes[MAX_AIRCRAFT];

    int n = route_parse(json, strlen(json), routes, MAX_AIRCRAFT);
    CHECK_INT(n, 13);

    int resolved_count = 0;
    int plausible_count = 0;
    for (int i = 0; i < n; i++) {
        if (routes[i].resolved) {
            resolved_count++;
        }
        if (routes[i].plausible) {
            plausible_count++;
        }
    }
    CHECK_INT(resolved_count, 6);
    CHECK_INT(plausible_count, 5);

    const route_t *dlh = route_find(routes, n, "DLH1JN");
    CHECK(dlh != NULL);
    if (dlh != NULL) {
        CHECK(dlh->resolved == true);
        CHECK(dlh->plausible == true);
        CHECK_STR(dlh->airline_code, "DLH");
        CHECK_STR(dlh->orig_icao, "LRCL");
        CHECK_STR(dlh->dest_icao, "EDDM");
        CHECK_STR(dlh->orig_city, "Cluj-Napoca");
        CHECK_STR(dlh->dest_city, "Munich");
    }

    /* An unresolved entry: no flight plan is a NORMAL outcome, not an error. */
    const route_t *oeaam = route_find(routes, n, "OEAAM");
    CHECK(oeaam != NULL);
    if (oeaam != NULL) {
        CHECK(oeaam->resolved == false);
        CHECK(oeaam->plausible == false);
        CHECK_STR(oeaam->orig_icao, "");
        CHECK_STR(oeaam->dest_icao, "");
        CHECK_STR(oeaam->orig_city, "");
        CHECK_STR(oeaam->dest_city, "");
    }

    /* RYR4MR is resolved but explicitly implausible. */
    const route_t *ryr4mr = route_find(routes, n, "RYR4MR");
    CHECK(ryr4mr != NULL);
    if (ryr4mr != NULL) {
        CHECK(ryr4mr->resolved == true);
        CHECK(ryr4mr->plausible == false);
    }

    free(json);
}

static void test_route_find_semantics(void)
{
    GROUP("route_find: order-independent, case-insensitive, NULL on miss");

    char *json = load_fixture("routeset_response.json");
    route_t routes[MAX_AIRCRAFT];
    int n = route_parse(json, strlen(json), routes, MAX_AIRCRAFT);
    CHECK_INT(n, 13);

    /* Case-insensitive. */
    const route_t *mixed_case = route_find(routes, n, "dlh1jn");
    CHECK(mixed_case != NULL);
    if (mixed_case != NULL) {
        CHECK_STR(mixed_case->callsign, "DLH1JN");
    }

    /* Order-independent: shuffle the array and confirm lookups still land on
     * the right entry by content, not by position. */
    route_t shuffled[MAX_AIRCRAFT];
    memcpy(shuffled, routes, sizeof(route_t) * (size_t)n);
    route_t tmp = shuffled[0];
    shuffled[0] = shuffled[n - 1];
    shuffled[n - 1] = tmp;

    const route_t *first_now_last = route_find(shuffled, n, "AUA695J");
    CHECK(first_now_last != NULL);
    if (first_now_last != NULL) {
        CHECK_STR(first_now_last->callsign, "AUA695J");
    }
    const route_t *last_now_first = route_find(shuffled, n, "DLH1JN");
    CHECK(last_now_first != NULL);
    if (last_now_first != NULL) {
        CHECK_STR(last_now_first->callsign, "DLH1JN");
    }

    /* Absent callsign. */
    CHECK(route_find(routes, n, "ZZZ9999") == NULL);

    free(json);
}

static void test_route_build_request(void)
{
    GROUP("route_build_request: builds parseable JSON with \"lng\"");

    aircraft_t acs[3];
    memset(acs, 0, sizeof acs);

    strcpy(acs[0].flight, "AUA453");
    acs[0].lat = 47.6;
    acs[0].lon = 15.9;

    strcpy(acs[1].flight, ""); /* empty callsign — must be skipped */
    acs[1].lat = 1.0;
    acs[1].lon = 2.0;

    strcpy(acs[2].flight, "DLH1JN");
    acs[2].lat = 47.799652;
    acs[2].lon = 15.288109;

    char buf[512];
    int written = route_build_request(acs, 3, buf, sizeof buf);
    CHECK(written > 0);
    CHECK_INT(written, (int)strlen(buf));

    CHECK(strstr(buf, "\"lng\"") != NULL);
    CHECK(strstr(buf, "\"lon\"") == NULL);
    CHECK(strstr(buf, "AUA453") != NULL);
    CHECK(strstr(buf, "DLH1JN") != NULL);

    cJSON *root = cJSON_Parse(buf);
    CHECK(root != NULL);
    if (root != NULL) {
        cJSON *planes = cJSON_GetObjectItemCaseSensitive(root, "planes");
        CHECK(cJSON_IsArray(planes));
        CHECK_INT(cJSON_GetArraySize(planes), 2); /* the empty-callsign one is skipped */

        cJSON *p0 = cJSON_GetArrayItem(planes, 0);
        cJSON *cs0 = cJSON_GetObjectItemCaseSensitive(p0, "callsign");
        CHECK_STR(cJSON_IsString(cs0) ? cs0->valuestring : NULL, "AUA453");
        CHECK(cJSON_GetObjectItemCaseSensitive(p0, "lng") != NULL);
        CHECK(cJSON_GetObjectItemCaseSensitive(p0, "lon") == NULL);

        cJSON_Delete(root);
    }

    /* Buffer too small must fail cleanly, not truncate silently. */
    char tiny[5];
    CHECK_INT(route_build_request(acs, 3, tiny, sizeof tiny), -1);

    /* All-empty-callsign input still produces valid (empty-planes) JSON. */
    aircraft_t none[1];
    memset(none, 0, sizeof none);
    char buf2[64];
    int written2 = route_build_request(none, 1, buf2, sizeof buf2);
    CHECK(written2 > 0);
    CHECK_STR(buf2, "{\"planes\":[]}");
}

int main(void)
{
    test_adsb_real_fixture();
    test_adsb_alt_baro_edge_cases();
    test_adsb_wrapper_key_aircraft();
    test_adsb_empty_list();
    test_adsb_malformed();
    test_adsb_clamps_to_max();

    test_route_real_fixture();
    test_route_find_semantics();
    test_route_build_request();

    return test_summary();
}
