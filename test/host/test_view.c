/* Host tests for view_build (docs/PLAN.md M2.5/M3): the layer that turns a
 * parsed aircraft_t/route_t into the exact German text view_model_t
 * describes. No board, no network — everything here runs against the real
 * captured fixtures over Gloggnitz (test/fixtures/adsblol_gloggnitz_30nm.json,
 * routeset_response.json) plus a couple of synthetic edge cases the real
 * capture does not happen to exercise (an unmapped airport, an unknown
 * airline code, DST_UNKNOWN).
 *
 * The single most valuable test in this file is the last one: it scans
 * every output string field of several built models for English giveaway
 * words. No English may leak into any output field — that is this module's
 * acceptance criterion (see the task brief / AGENTS.md).
 */
#include "test_util.h"

#include <ctype.h>
#include <string.h>

#include "adsb_parse.h"
#include "fmt_de.h"
#include "flight_types.h"
#include "route_parse.h"
#include "view_build.h"
#include "view_model.h"

/* ---- the exact German sentences view_build.c chooses by category -------
 * Kept here as named constants, matching main/strings_de.h verbatim, so
 * every assertion below reads as "does it say the right thing" rather than
 * a pile of inline literals.
 *
 * These are a deliberate SECOND COPY, not an include of strings_de.h. A test
 * that asserts STR_REASON_NONE == STR_REASON_NONE asserts nothing: it would
 * pass whatever anyone typed into the header. Changing what the device says
 * to him is supposed to cost two edits in two files, and the failing test in
 * between is the point. */
static const char *const REASON_PRIVATE    = "Eine Route gibt es nur bei Linienflügen.";
static const char *const REASON_HELICOPTER = "Hubschrauber fliegen meist ohne festen Flugplan.";
static const char *const REASON_MILITARY   = "Militärflüge scheinen in keinem öffentlichen Flugplan auf.";
static const char *const REASON_AIRLINER   = "Der Flugplan ist im Moment nicht verfügbar.";
static const char *const REASON_UNKNOWN    = "Zu diesem Flug ist keine Route bekannt.";

/* ---- shared fixture-loading helpers -------------------------------------- */

static const aircraft_t *find_ac_by_hex(const aircraft_t *acs, int n, const char *hex)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(acs[i].hex, hex) == 0) {
            return &acs[i];
        }
    }
    return NULL;
}

static struct tm make_now(void)
{
    struct tm t;
    memset(&t, 0, sizeof t);
    t.tm_year = 126; /* 2026 */
    t.tm_mon  = 8;   /* September, 0-based */
    t.tm_mday = 18;
    t.tm_hour = 9;
    t.tm_min  = 47;
    t.tm_wday = 5;   /* Friday */
    return t;
}

/* Asserts the fields view_build derives straight from fmt_de (altitude,
 * distance, direction, bearing) against an independently-computed expected
 * value, rather than hand-computed numbers — this is what actually proves
 * view_build passes the RIGHT raw value into each formatter. */
static void check_numeric_fields(const aircraft_t *ac, const view_model_t *vm)
{
    char exp_alt[32];
    fmt_altitude_m(ac->alt_ft, exp_alt, sizeof exp_alt);
    CHECK_STR(vm->altitude, exp_alt);

    if (ac->dst_nm == DST_UNKNOWN) {
        CHECK_STR(vm->distance, "\xE2\x80\x94"); /* em dash */
        CHECK_STR(vm->direction_word, "");
        CHECK_STR(vm->direction_abbr, "");
        /* Never a rendered negative number anywhere near distance/direction. */
        CHECK(strchr(vm->distance, '-') == NULL);
    } else {
        char exp_dist[32];
        fmt_distance_km(ac->dst_nm, exp_dist, sizeof exp_dist);
        CHECK_STR(vm->distance, exp_dist);
        /* The ADVERB ("nordöstlich"), so the band reads "16,8 km
         * nordöstlich". PLAN.md M7. The second check is the anti-regression
         * one: compass_de_word() is still exported and still tested, and a
         * one-word edit here would put the stranded noun back on the panel
         * without any other test noticing. */
        CHECK_STR(vm->direction_word, compass_de_adv(ac->dir_deg));
        CHECK(strcmp(vm->direction_word, compass_de_word(ac->dir_deg)) != 0);
        CHECK_STR(vm->direction_abbr, compass_de_abbr(ac->dir_deg));
        /* "O" for Ost, never "E" — AGENTS.md §1 and §10 name this as the
         * classic bug. No German 16-point abbreviation contains an E. */
        CHECK(strchr(vm->direction_abbr, 'E') == NULL);
    }
    CHECK_NEAR(vm->bearing_deg, ac->dir_deg, 0.001);
}

/* ---- §5.3 Himmel frei ----------------------------------------------------- */

static void test_empty_sky_no_history(void)
{
    GROUP("view_build_empty: no history — clock and date must still be usable");

    struct tm now = make_now();
    view_model_t vm;
    view_build_empty(&now, NULL, false, &vm);

    CHECK_INT(vm.state, VIEW_EMPTY_SKY);
    CHECK_STR(vm.clock, "09:47");
    CHECK_STR(vm.date_line, "Freitag, 18. September 2026");
    CHECK_INT(vm.traffic_count, 0);
    CHECK(vm.online == false);

    /* Never render "(null)" or leave stray garbage even with no aircraft. */
    CHECK(strstr(vm.hero, "null") == NULL);
    CHECK(strstr(vm.origin, "null") == NULL);
}

static void test_empty_sky_with_history(void)
{
    GROUP("view_build_empty: last-seen aircraft degrades to a usable summary");

    aircraft_t last_seen;
    memset(&last_seen, 0, sizeof last_seen);
    strcpy(last_seen.hex, "440169");
    strcpy(last_seen.flight, "OEAAM");
    strcpy(last_seen.type, "DV20");
    strcpy(last_seen.reg, "OE-AAM");
    last_seen.alt_ft = 5425;
    last_seen.dst_nm = 20.002f;
    last_seen.dir_deg = 215.8f;

    struct tm now = make_now();
    view_model_t vm;
    view_build_empty(&now, &last_seen, true, &vm);

    CHECK_INT(vm.state, VIEW_EMPTY_SKY);
    CHECK_STR(vm.clock, "09:47");
    CHECK(vm.online == true);
    CHECK_INT(vm.traffic_count, 0);

    CHECK_STR(vm.hero, "Diamond DV20 Katana");             /* plain-language type, per §5.2's rule */
    CHECK_STR(vm.callsign, "OEAAM");
    CHECK_STR(vm.registration, "OE-AAM");
    CHECK_STR(vm.type_full, "");
    CHECK_STR(vm.size_class, "Zweisitzer");
    CHECK_STR(vm.origin, "");                /* no route context for history */
    CHECK(vm.has_origin == false);
    CHECK_STR(vm.airline, "");
    CHECK_STR(vm.reason, "");                /* reserved for VIEW_NO_ROUTE only */
    check_numeric_fields(&last_seen, &vm);
}

static void test_view_build_null_aircraft(void)
{
    GROUP("view_build: NULL aircraft -> VIEW_EMPTY_SKY, chrome still passed through");

    struct tm now = make_now();
    view_model_t vm;
    view_build(NULL, NULL, &now, 5, true, &vm);

    CHECK_INT(vm.state, VIEW_EMPTY_SKY);
    CHECK_STR(vm.clock, "09:47");
    CHECK_STR(vm.date_line, "Freitag, 18. September 2026");
    CHECK_INT(vm.traffic_count, 5);
    CHECK(vm.online == true);
}

/* ---- ALT_GROUND / ALT_UNKNOWN end to end ---------------------------------- */

static void test_altitude_ground_and_unknown(void)
{
    GROUP("view_build: ALT_GROUND -> \"am Boden\", ALT_UNKNOWN -> em dash, end to end");

    char *json = load_fixture("adsb_alt_edge.json");
    aircraft_t acs[MAX_AIRCRAFT];
    int n = adsb_parse(json, strlen(json), acs, MAX_AIRCRAFT);
    CHECK_INT(n, 2);

    const aircraft_t *grnd = find_ac_by_hex(acs, n, "aaaaaa");
    const aircraft_t *unk  = find_ac_by_hex(acs, n, "bbbbbb");
    CHECK(grnd != NULL);
    CHECK(unk != NULL);

    struct tm now = make_now();
    view_model_t vm;

    if (grnd != NULL) {
        view_build(grnd, NULL, &now, 1, true, &vm);
        CHECK_STR(vm.altitude, "am Boden");
    }
    if (unk != NULL) {
        view_build(unk, NULL, &now, 1, true, &vm);
        CHECK_STR(vm.altitude, "\xE2\x80\x94");
    }

    free(json);
}

/* ---- DST_UNKNOWN end to end (coordinator addendum) ------------------------ */

static void test_dst_unknown_end_to_end(void)
{
    GROUP("view_build: DST_UNKNOWN -> em-dash distance, no negative km, blank direction");

    char *json = load_fixture("adsb_no_dst.json");
    aircraft_t acs[MAX_AIRCRAFT];
    int n = adsb_parse(json, strlen(json), acs, MAX_AIRCRAFT);
    CHECK_INT(n, 2);

    /* The parser must sort the DST_UNKNOWN entry LAST, not to the front —
     * that was the actual bug: an uncomputed position must never become
     * "the plane overhead" just because 0.0 used to sort first. */
    CHECK_STR(acs[n - 1].flight, "NODST1");
    CHECK(acs[n - 1].dst_nm == DST_UNKNOWN);

    const aircraft_t *nodst = find_ac_by_hex(acs, n, "aaaaaa");
    CHECK(nodst != NULL);
    if (nodst != NULL) {
        CHECK(nodst->dst_nm == DST_UNKNOWN);

        struct tm now = make_now();
        view_model_t vm;
        view_build(nodst, NULL, &now, 1, true, &vm);

        CHECK_STR(vm.distance, "\xE2\x80\x94");
        CHECK_STR(vm.direction_word, "");
        CHECK_STR(vm.direction_abbr, "");
        CHECK(strchr(vm.distance, '-') == NULL);
        /* Altitude is unaffected by the distance sentinel. */
        char exp_alt[32];
        fmt_altitude_m(nodst->alt_ft, exp_alt, sizeof exp_alt);
        CHECK_STR(vm.altitude, exp_alt);
    }

    const aircraft_t *hasdst = find_ac_by_hex(acs, n, "bbbbbb");
    CHECK(hasdst != NULL);
    if (hasdst != NULL) {
        CHECK_NEAR(hasdst->dst_nm, 9.5, 0.001);

        struct tm now = make_now();
        view_model_t vm;
        view_build(hasdst, NULL, &now, 1, true, &vm);
        check_numeric_fields(hasdst, &vm);
    }

    free(json);
}

/* ---- real fixture: VIEW_OVERHEAD ------------------------------------------ */

/* Keeps the models built here around so the final "no English leaks" test
 * can scan all of them without re-parsing. */
#define N_SCAN_MODELS 8
static view_model_t g_scan_models[N_SCAN_MODELS];
static const char  *g_scan_labels[N_SCAN_MODELS];
static int          g_scan_count = 0;

static void remember_for_scan(const char *label, const view_model_t *vm)
{
    if (g_scan_count < N_SCAN_MODELS) {
        g_scan_labels[g_scan_count] = label;
        g_scan_models[g_scan_count] = *vm;
        g_scan_count++;
    }
}

static void test_overhead_dlh1jn_munich_via_klausenburg(void)
{
    GROUP("view_build: DLH1JN (LRCL->EDDM) -> VIEW_OVERHEAD, Muenchen/Klausenburg, not English");

    char *ac_json = load_fixture("adsblol_gloggnitz_30nm.json");
    char *rt_json = load_fixture("routeset_response.json");
    aircraft_t acs[MAX_AIRCRAFT];
    route_t routes[MAX_AIRCRAFT];
    int nac = adsb_parse(ac_json, strlen(ac_json), acs, MAX_AIRCRAFT);
    int nrt = route_parse(rt_json, strlen(rt_json), routes, MAX_AIRCRAFT);
    /* 12, not 13: the MLAT ground beacon is filtered in adsb_parse. The
     * routeset fixture still has 13 records — it was built from the raw feed. */
    CHECK_INT(nac, 12);
    CHECK_INT(nrt, 13);

    const aircraft_t *dlh = find_ac_by_hex(acs, nac, "3c658c");
    CHECK(dlh != NULL);
    if (dlh != NULL) {
        const route_t *rt = route_find(routes, nrt, dlh->flight);
        CHECK(rt != NULL);

        struct tm now = make_now();
        view_model_t vm;
        view_build(dlh, rt, &now, 13, true, &vm);

        CHECK_INT(vm.state, VIEW_OVERHEAD);
        /* airport_de(LRCL) = "Klausenburg" — checked against the table
         * (main/data/tbl_airport.c), not assumed (task brief). */
        CHECK_STR(vm.hero, "München");
        CHECK_STR(vm.origin, "Klausenburg");
        CHECK(vm.has_origin == true);
        /* Explicitly not the English API spelling. */
        CHECK(strcmp(vm.hero, "Munich") != 0);
        CHECK(strcmp(vm.origin, "Cluj-Napoca") != 0);

        CHECK_STR(vm.airline, "Lufthansa");
        CHECK_STR(vm.type_full, "Airbus A319");
        CHECK_STR(vm.size_class, "Mittelstreckenjet");
        CHECK_STR(vm.callsign, "DLH1JN");
        CHECK_STR(vm.registration, "D-AILL");
        CHECK_STR(vm.reason, "");

        check_numeric_fields(dlh, &vm);
        remember_for_scan("DLH1JN overhead", &vm);
    }

    free(ac_json);
    free(rt_json);
}

static void test_overhead_aua_flights(void)
{
    GROUP("view_build: AUA76PZ / AUA695J -> VIEW_OVERHEAD via Wien");

    char *ac_json = load_fixture("adsblol_gloggnitz_30nm.json");
    char *rt_json = load_fixture("routeset_response.json");
    aircraft_t acs[MAX_AIRCRAFT];
    route_t routes[MAX_AIRCRAFT];
    int nac = adsb_parse(ac_json, strlen(ac_json), acs, MAX_AIRCRAFT);
    int nrt = route_parse(rt_json, strlen(rt_json), routes, MAX_AIRCRAFT);

    const aircraft_t *aua76pz = find_ac_by_hex(acs, nac, "502d58"); /* LWSK -> LOWW */
    CHECK(aua76pz != NULL);
    if (aua76pz != NULL) {
        const route_t *rt = route_find(routes, nrt, aua76pz->flight);
        struct tm now = make_now();
        view_model_t vm;
        view_build(aua76pz, rt, &now, 13, true, &vm);

        CHECK_INT(vm.state, VIEW_OVERHEAD);
        CHECK_STR(vm.hero, "Wien");
        CHECK_STR(vm.origin, "Skopje");
        CHECK_STR(vm.airline, "Austrian Airlines");
        CHECK_STR(vm.type_full, "Airbus A220-300");
        CHECK_STR(vm.size_class, "Regionaljet");
        CHECK_STR(vm.registration, "YL-ABG");
        remember_for_scan("AUA76PZ overhead", &vm);
    }

    const aircraft_t *aua695j = find_ac_by_hex(acs, nac, "502d79"); /* LOWW -> LROP */
    CHECK(aua695j != NULL);
    if (aua695j != NULL) {
        const route_t *rt = route_find(routes, nrt, aua695j->flight);
        struct tm now = make_now();
        view_model_t vm;
        view_build(aua695j, rt, &now, 13, true, &vm);

        CHECK_INT(vm.state, VIEW_OVERHEAD);
        CHECK_STR(vm.hero, "Bukarest");
        CHECK_STR(vm.origin, "Wien");
        CHECK_STR(vm.airline, "Austrian Airlines");
    }

    free(ac_json);
    free(rt_json);
}

/* ---- real fixture: VIEW_NO_ROUTE, category-driven reasons ----------------- */

static void test_no_route_private_dv20s(void)
{
    GROUP("view_build: real DV20s -> VIEW_NO_ROUTE, private-aircraft reason");

    char *ac_json = load_fixture("adsblol_gloggnitz_30nm.json");
    char *rt_json = load_fixture("routeset_response.json");
    aircraft_t acs[MAX_AIRCRAFT];
    route_t routes[MAX_AIRCRAFT];
    int nac = adsb_parse(ac_json, strlen(ac_json), acs, MAX_AIRCRAFT);
    int nrt = route_parse(rt_json, strlen(rt_json), routes, MAX_AIRCRAFT);

    static const char *const hexes[] = {"440169" /* OEAAM */, "3d3dfb" /* DEVSL */};
    for (size_t i = 0; i < sizeof hexes / sizeof hexes[0]; i++) {
        const aircraft_t *ac = find_ac_by_hex(acs, nac, hexes[i]);
        CHECK(ac != NULL);
        if (ac == NULL) {
            continue;
        }
        const route_t *rt = route_find(routes, nrt, ac->flight);

        struct tm now = make_now();
        view_model_t vm;
        view_build(ac, rt, &now, 13, true, &vm);

        CHECK_INT(vm.state, VIEW_NO_ROUTE);
        CHECK_STR(vm.hero, "Diamond DV20 Katana");
        CHECK(vm.has_origin == false);
        CHECK_STR(vm.origin, "");
        CHECK_STR(vm.reason, REASON_PRIVATE);
        CHECK_STR(vm.type_full, "");
        CHECK_STR(vm.size_class, "Zweisitzer");
        check_numeric_fields(ac, &vm);

        if (i == 0) {
            remember_for_scan("OEAAM no-route", &vm);
        }
    }

    free(ac_json);
    free(rt_json);
}

static void test_no_route_helicopter_ec35(void)
{
    GROUP("view_build: real EC35 (H135) -> VIEW_NO_ROUTE, helicopter reason");

    char *ac_json = load_fixture("adsblol_gloggnitz_30nm.json");
    char *rt_json = load_fixture("routeset_response.json");
    aircraft_t acs[MAX_AIRCRAFT];
    route_t routes[MAX_AIRCRAFT];
    int nac = adsb_parse(ac_json, strlen(ac_json), acs, MAX_AIRCRAFT);
    int nrt = route_parse(rt_json, strlen(rt_json), routes, MAX_AIRCRAFT);

    const aircraft_t *ec35 = find_ac_by_hex(acs, nac, "4404a7"); /* OEBXP */
    CHECK(ec35 != NULL);
    if (ec35 != NULL) {
        const route_t *rt = route_find(routes, nrt, ec35->flight);
        struct tm now = make_now();
        view_model_t vm;
        view_build(ec35, rt, &now, 13, true, &vm);

        CHECK_INT(vm.state, VIEW_NO_ROUTE);
        CHECK_STR(vm.hero, "Airbus H135");
        CHECK_STR(vm.reason, REASON_HELICOPTER);
        CHECK_STR(vm.type_full, "");
        CHECK_STR(vm.size_class, "Hubschrauber");
        remember_for_scan("OEBXP helicopter no-route", &vm);
    }

    free(ac_json);
    free(rt_json);
}

static void test_no_route_airliner_route_unavailable(void)
{
    GROUP("view_build: RYR4MR resolved-but-implausible -> VIEW_NO_ROUTE, "
          "\"not available\" not \"does not exist\"");

    char *ac_json = load_fixture("adsblol_gloggnitz_30nm.json");
    char *rt_json = load_fixture("routeset_response.json");
    aircraft_t acs[MAX_AIRCRAFT];
    route_t routes[MAX_AIRCRAFT];
    int nac = adsb_parse(ac_json, strlen(ac_json), acs, MAX_AIRCRAFT);
    int nrt = route_parse(rt_json, strlen(rt_json), routes, MAX_AIRCRAFT);

    const aircraft_t *ryr = find_ac_by_hex(acs, nac, "4d2303"); /* RYR4MR */
    CHECK(ryr != NULL);
    if (ryr != NULL) {
        const route_t *rt = route_find(routes, nrt, ryr->flight);
        CHECK(rt != NULL);
        if (rt != NULL) {
            CHECK(rt->resolved == true);
            CHECK(rt->plausible == false);
        }

        struct tm now = make_now();
        view_model_t vm;
        view_build(ryr, rt, &now, 13, true, &vm);

        CHECK_INT(vm.state, VIEW_NO_ROUTE);
        CHECK_STR(vm.hero, "Boeing 737 MAX 8");
        CHECK_STR(vm.reason, REASON_AIRLINER);
        /* We still know the airline even though the route is unusable —
         * airline_code is populated on this entry regardless of plausible. */
        CHECK_STR(vm.airline, "Ryanair");
        remember_for_scan("RYR4MR airliner no-route", &vm);
    }

    free(ac_json);
    free(rt_json);
}

static void test_no_route_unknown_no_type_info(void)
{
    GROUP("view_build: OEVSO (no type, no reg) -> VIEW_NO_ROUTE, neutral reason, "
          "degrades gracefully (never \"(null)\")");

    char *ac_json = load_fixture("adsblol_gloggnitz_30nm.json");
    char *rt_json = load_fixture("routeset_response.json");
    aircraft_t acs[MAX_AIRCRAFT];
    route_t routes[MAX_AIRCRAFT];
    int nac = adsb_parse(ac_json, strlen(ac_json), acs, MAX_AIRCRAFT);
    int nrt = route_parse(rt_json, strlen(rt_json), routes, MAX_AIRCRAFT);

    const aircraft_t *oevso = find_ac_by_hex(acs, nac, "440277");
    CHECK(oevso != NULL);
    if (oevso != NULL) {
        CHECK_STR(oevso->type, "");
        CHECK_STR(oevso->reg, "");

        const route_t *rt = route_find(routes, nrt, oevso->flight);
        struct tm now = make_now();
        view_model_t vm;
        view_build(oevso, rt, &now, 13, true, &vm);

        CHECK_INT(vm.state, VIEW_NO_ROUTE);
        CHECK_STR(vm.reason, REASON_UNKNOWN);
        CHECK_STR(vm.registration, "");     /* never "(null)" */
        CHECK(strcmp(vm.hero, "(null)") != 0);
        CHECK(strcmp(vm.type_full, "(null)") != 0);
        CHECK(vm.hero[0] != '\0');           /* never a blank hero either */
        /* type_full MAY be empty here, deliberately: with no type designator the
         * hero already says "Leichtflugzeug" from the emitter category, and
         * repeating it underneath is noise. An empty supporting line disappears;
         * what must never happen is "(null)" or a bare "?". */
        remember_for_scan("OEVSO unknown no-route", &vm);
    }

    free(ac_json);
    free(rt_json);
}

static void test_no_route_private_g2ca(void)
{
    GROUP("view_build: real G2CA experimental -> VIEW_NO_ROUTE, private reason");

    char *ac_json = load_fixture("adsblol_gloggnitz_30nm.json");
    char *rt_json = load_fixture("routeset_response.json");
    aircraft_t acs[MAX_AIRCRAFT];
    route_t routes[MAX_AIRCRAFT];
    int nac = adsb_parse(ac_json, strlen(ac_json), acs, MAX_AIRCRAFT);
    int nrt = route_parse(rt_json, strlen(rt_json), routes, MAX_AIRCRAFT);

    const aircraft_t *g2ca = find_ac_by_hex(acs, nac, "4401d8"); /* OEXNC */
    CHECK(g2ca != NULL);
    if (g2ca != NULL) {
        const route_t *rt = route_find(routes, nrt, g2ca->flight);
        struct tm now = make_now();
        view_model_t vm;
        view_build(g2ca, rt, &now, 13, true, &vm);

        CHECK_INT(vm.state, VIEW_NO_ROUTE);
        /* A Guimbal Cabri G2 — a training helicopter, per its own A7 emitter
         * category — so the hero is the model name and the reason is the
         * helicopter one, not the generic private-aircraft sentence. */
        CHECK_STR(vm.hero, "Guimbal Cabri G2");
        CHECK_STR(vm.reason, REASON_HELICOPTER);
    }

    free(ac_json);
    free(rt_json);
}

/* The nearest aircraft in the real fixture (dst 7.731 nm) is FFMSNE — an MLAT
 * ground-reference target ("t":"TWR","r":"TWR"), not a real aircraft. That is
 * exactly why it is used here: it is the real integration case ("view_build
 * the nearest aircraft"), and it exercises the AC_CAT_UNKNOWN reason path
 * with a type that IS in the table but categorised UNKNOWN, not the
 * no-type-at-all path (covered separately by OEVSO above). */
static void test_nearest_aircraft_field_by_field(void)
{
    GROUP("view_build: nearest real aircraft (acs[0], FFMSNE) — every field");

    char *ac_json = load_fixture("adsblol_gloggnitz_30nm.json");
    char *rt_json = load_fixture("routeset_response.json");
    aircraft_t acs[MAX_AIRCRAFT];
    route_t routes[MAX_AIRCRAFT];
    int nac = adsb_parse(ac_json, strlen(ac_json), acs, MAX_AIRCRAFT);
    int nrt = route_parse(rt_json, strlen(rt_json), routes, MAX_AIRCRAFT);
    CHECK_INT(nac, 12);

    /* The nearest REAL aircraft over Gloggnitz. It used to be FFMSNE, a fixed
     * MLAT ground-reference beacon at 7.731 nm that adsb_parse now drops —
     * otherwise the default screen answered "what is that plane?" with
     * "Bodenreferenz". */
    const aircraft_t *nearest = &acs[0];
    CHECK_STR(nearest->hex, "4404a7");
    CHECK_STR(nearest->flight, "OEBXP");

    const route_t *rt = route_find(routes, nrt, nearest->flight);
    CHECK(rt != NULL);
    if (rt != NULL) {
        CHECK(rt->resolved == false);
    }

    struct tm now = make_now();
    view_model_t vm;
    view_build(nearest, rt, &now, nac, true, &vm);

    CHECK_INT(vm.state, VIEW_NO_ROUTE);
    CHECK_STR(vm.hero, "Airbus H135");
    CHECK_STR(vm.origin, "");
    CHECK(vm.has_origin == false);
    CHECK_STR(vm.airline, "");
    CHECK_STR(vm.type_full, "");
    CHECK_STR(vm.size_class, "Hubschrauber");
    CHECK_STR(vm.callsign, "OEBXP");
    CHECK_STR(vm.registration, "OE-BXP");
    CHECK_STR(vm.reason, REASON_HELICOPTER);
    CHECK_STR(vm.clock, "09:47");
    CHECK_STR(vm.date_line, "Freitag, 18. September 2026");
    CHECK_INT(vm.traffic_count, 12);
    CHECK(vm.online == true);
    check_numeric_fields(nearest, &vm);

    remember_for_scan("nearest (FFMSNE)", &vm);

    free(ac_json);
    free(rt_json);
}

/* ---- synthetic edge cases the real fixture does not happen to exercise --- */

static void test_synthetic_unmapped_airport_falls_back_to_api_city(void)
{
    GROUP("view_build: airport not in the table falls back to the API's city, "
          "never a bare ICAO code");

    aircraft_t ac;
    memset(&ac, 0, sizeof ac);
    strcpy(ac.hex, "abcdef");
    strcpy(ac.flight, "TST123");
    strcpy(ac.type, "A320");
    strcpy(ac.reg, "OE-TST");
    ac.alt_ft = 10000;
    ac.dst_nm = 15.0f;
    ac.dir_deg = 45.0f;

    route_t rt;
    memset(&rt, 0, sizeof rt);
    strcpy(rt.callsign, "TST123");
    strcpy(rt.airline_code, "ZZZ");     /* not in tbl_airline.c */
    strcpy(rt.orig_icao, "XXXX");       /* not in tbl_airport.c */
    strcpy(rt.dest_icao, "YYYY");       /* not in tbl_airport.c, and no API city either */
    strcpy(rt.orig_city, "Zaragoza");   /* API's own English/local name */
    /* rt.dest_city intentionally left "" — both table AND API are silent. */
    rt.resolved = true;
    rt.plausible = true;

    struct tm now = make_now();
    view_model_t vm;
    view_build(&ac, &rt, &now, 1, true, &vm);

    CHECK_INT(vm.state, VIEW_OVERHEAD);
    CHECK_STR(vm.origin, "Zaragoza");     /* table miss -> API fallback */
    CHECK_STR(vm.hero, "unbekannt");      /* table miss AND API miss -> German placeholder */
    /* Never the bare ICAO code anywhere. */
    CHECK(strstr(vm.hero, "YYYY") == NULL);
    CHECK(strstr(vm.origin, "XXXX") == NULL);
    /* Unknown airline code: never print the raw 3-letter code. */
    CHECK_STR(vm.airline, "");
    CHECK(strstr(vm.airline, "ZZZ") == NULL);

    remember_for_scan("synthetic unmapped airport", &vm);
}

static void test_synthetic_military_reason(void)
{
    GROUP("view_build: military category -> military reason (no real military "
          "aircraft in the fixture, so this is synthetic)");

    aircraft_t ac;
    memset(&ac, 0, sizeof ac);
    strcpy(ac.hex, "aabbcc");
    strcpy(ac.flight, "");   /* military traffic is often blocked -- no callsign either */
    strcpy(ac.type, "F16");
    strcpy(ac.reg, "");
    ac.alt_ft = ALT_UNKNOWN;
    ac.dst_nm = 12.0f;
    ac.dir_deg = 300.0f;

    struct tm now = make_now();
    view_model_t vm;
    view_build(&ac, NULL, &now, 1, true, &vm);

    CHECK_INT(vm.state, VIEW_NO_ROUTE);
    CHECK_STR(vm.hero, "General Dynamics F-16 Fighting Falcon");
    CHECK_STR(vm.reason, REASON_MILITARY);
    CHECK_STR(vm.callsign, "");
    CHECK_STR(vm.registration, "");
    remember_for_scan("synthetic military no-route", &vm);
}

/* ---- the gate: a raw ICAO designator may never be the hero ---------------- */

static void test_hero_is_never_a_bare_code(void)
{
    GROUP("view_build: an unknown type is named, never spelled as its code");

    /* Real capture, 2026-09-19: a Cessna 177 Cardinal, type "C177", not in
     * tbl_actype.c and broadcasting no emitter category. The panel put C177
     * in the hero at 76 px — the bare designator AGENTS.md §1 forbids, and
     * the bug D36 removed from the list while leaving it standing here.
     *
     * The designators below are deliberately NOT in the table: the point is
     * the behaviour when lookup fails, so adding any of them to the table
     * later must not quietly disarm this test. */
    static const char *const unknown_types[] = {
        "C177", "ZZZZ", "QQ12", "X", "AB", "7777", "----",
    };

    for (size_t i = 0; i < sizeof unknown_types / sizeof unknown_types[0]; i++) {
        aircraft_t ac;
        memset(&ac, 0, sizeof ac);
        snprintf(ac.hex, sizeof ac.hex, "abc12%d", (int)i);
        snprintf(ac.flight, sizeof ac.flight, "OEXYZ");
        snprintf(ac.type, sizeof ac.type, "%s", unknown_types[i]);
        ac.category[0] = '\0';               /* nothing from the category either */
        ac.alt_ft = 5300;
        ac.dst_nm = 2.5f;
        ac.dir_deg = 270.0f;

        time_t now = 1789000000;
        view_model_t vm;
        view_build(&ac, NULL, &now, 1, true, &vm);

        /* The hero must not BE the code, must not START with it, and must not
         * contain it: "C177" alone, "C177 Flugzeug" and "Typ C177" are all the
         * same failure wearing different hats. */
        CHECK(strstr(vm.hero, unknown_types[i]) == NULL);
        CHECK(vm.hero[0] != '\0');
        CHECK_STR(vm.hero, "Unbekanntes Flugzeug");
        /* And not in the supporting line either. Fixing only the hero left
         * "C177" printed directly underneath "Unbekanntes Flugzeug" — the
         * same bug, one font size down, found by the screenshot taken to
         * confirm the first fix. */
        CHECK(strstr(vm.type_full, unknown_types[i]) == NULL);
        CHECK(strstr(vm.size_class, unknown_types[i]) == NULL);
        CHECK(strstr(vm.airline, unknown_types[i]) == NULL);
        remember_for_scan("unknown type designator", &vm);
    }

    GROUP("view_build: a KNOWN type is still named, not blanked");
    /* The fix above must not have turned every hero into "Unbekanntes
     * Flugzeug" — that would pass the checks above and destroy the product. */
    {
        aircraft_t ac;
        memset(&ac, 0, sizeof ac);
        snprintf(ac.hex, sizeof ac.hex, "abcdef");
        snprintf(ac.type, sizeof ac.type, "A20N");
        ac.alt_ft = 31000;
        ac.dst_nm = 9.0f;
        time_t now = 1789000000;
        view_model_t vm;
        view_build(&ac, NULL, &now, 1, true, &vm);
        CHECK(strcmp(vm.hero, "Unbekanntes Flugzeug") != 0);
        CHECK(strstr(vm.hero, "A20N") == NULL);
        CHECK(strstr(vm.hero, "Airbus") != NULL);
    }
}

/* ---- the gate: no English may leak into any output field ----------------- */

static const char *const GIVEAWAY_WORDS[] = {
    "Vienna", "Munich", "Prague", "Airport", "unknown", "null", "None",
    /* M7 additions. The first group is more English that a table or an API
     * could push through untranslated; the second is not English at all but
     * belongs in the same gate, because each one reaches the panel the same
     * way — as text he reads that means the device is broken. */
    "undefined", "Unknown", "Error", "Failed", "Loading", "N/A",
    "NaN", "(null)", "nil", "TODO", "FIXME",
    /* An unsubstituted conversion specification in an output field means a
     * format string reached the screen instead of its result. Nothing in
     * German, or in any airport/airline/type name, contains "%s" or "%d". */
    "%s", "%d",
};
#define N_GIVEAWAY (sizeof(GIVEAWAY_WORDS) / sizeof(GIVEAWAY_WORDS[0]))

static void ascii_lower_copy(char *dst, size_t dst_sz, const char *src)
{
    size_t i = 0;
    for (; src[i] != '\0' && i + 1 < dst_sz; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static void check_field_no_english(const char *model_label, const char *field_label,
                                    const char *value)
{
    if (value == NULL) {
        return;
    }
    char lower[256];
    ascii_lower_copy(lower, sizeof lower, value);

    for (size_t i = 0; i < N_GIVEAWAY; i++) {
        char needle[32];
        ascii_lower_copy(needle, sizeof needle, GIVEAWAY_WORDS[i]);
        t_run++;
        if (strstr(lower, needle) != NULL) {
            FAIL_("%s.%s = \"%s\" contains English giveaway word \"%s\"",
                  model_label, field_label, value, GIVEAWAY_WORDS[i]);
        }
    }
}

static void scan_model_for_english_leaks(const char *label, const view_model_t *vm)
{
    check_field_no_english(label, "hero", vm->hero);
    check_field_no_english(label, "origin", vm->origin);
    check_field_no_english(label, "airline", vm->airline);
    check_field_no_english(label, "type_full", vm->type_full);
    check_field_no_english(label, "size_class", vm->size_class);
    check_field_no_english(label, "callsign", vm->callsign);
    check_field_no_english(label, "registration", vm->registration);
    check_field_no_english(label, "altitude", vm->altitude);
    check_field_no_english(label, "distance", vm->distance);
    check_field_no_english(label, "direction_word", vm->direction_word);
    check_field_no_english(label, "direction_abbr", vm->direction_abbr);
    check_field_no_english(label, "reason", vm->reason);
    check_field_no_english(label, "clock", vm->clock);
    check_field_no_english(label, "date_line", vm->date_line);
}

static void test_no_english_leaks_anywhere(void)
{
    GROUP("view_build: NO English may leak into any output field (the gate)");

    CHECK(g_scan_count > 0);
    for (int i = 0; i < g_scan_count; i++) {
        scan_model_for_english_leaks(g_scan_labels[i], &g_scan_models[i]);
    }
}

int main(void)
{
    test_empty_sky_no_history();
    test_empty_sky_with_history();
    test_view_build_null_aircraft();

    test_altitude_ground_and_unknown();
    test_dst_unknown_end_to_end();

    test_overhead_dlh1jn_munich_via_klausenburg();
    test_overhead_aua_flights();

    test_no_route_private_dv20s();
    test_no_route_helicopter_ec35();
    test_no_route_airliner_route_unavailable();
    test_no_route_unknown_no_type_info();
    test_no_route_private_g2ca();

    test_nearest_aircraft_field_by_field();

    test_synthetic_unmapped_airport_falls_back_to_api_city();
    test_synthetic_military_reason();

    /* Must run last: it scans every model remember_for_scan() collected above. */
    test_hero_is_never_a_bare_code();
    test_no_english_leaks_anywhere();


    GROUP("view_build_empty: before SNTP, say so rather than show 1970");
    {
        /* The first screen the device ever draws, and the one he sees on
         * arrival in Thailand before any network exists. */
        struct tm epoch = {0};
        epoch.tm_year = 70;   /* 1970 */
        epoch.tm_mon = 0; epoch.tm_mday = 1; epoch.tm_wday = 4;
        epoch.tm_hour = 1; epoch.tm_min = 5;

        view_model_t vm;
        view_build_empty(&epoch, NULL, false, &vm);
        CHECK_INT(vm.state, VIEW_EMPTY_SKY);
        CHECK_STR(vm.hero, "Kein Netz");
        CHECK_STR(vm.clock, "--:--");
        CHECK(strstr(vm.date_line, "1970") == NULL);
        CHECK(strstr(vm.date_line, "Jänner") == NULL);
        CHECK(vm.date_line[0] != '\0');          /* never blank */
        CHECK(vm.online == false);

        /* A real clock must still behave exactly as before. */
        struct tm good = make_now();
        view_build_empty(&good, NULL, true, &vm);
        CHECK_STR(vm.clock, "09:47");
        CHECK_STR(vm.date_line, "Freitag, 18. September 2026");
        CHECK(strcmp(vm.hero, "Kein Netz") != 0);
    }


    GROUP("\"still looking\" must not be reported as \"no flight plan\"");
    {
        /* The 2E0LXY lesson (PLAN.md M4): during the seconds between sending a
         * callsign to routeset and getting an answer, the panel used to assert
         * there was no flight plan — then contradict itself. A panel that
         * corrects itself is one he stops believing. */
        char *ac_json = load_fixture("adsblol_gloggnitz_30nm.json");
        aircraft_t acs[MAX_AIRCRAFT];
        int nac = adsb_parse(ac_json, strlen(ac_json), acs, MAX_AIRCRAFT);
        CHECK(nac > 0);

        struct tm now = make_now();
        view_model_t pending, settled;

        view_build_ex(&acs[0], NULL, true,  &now, nac, true, &pending);
        view_build_ex(&acs[0], NULL, false, &now, nac, true, &settled);

        /* Same screen, same layout — only the explanation differs. */
        CHECK_INT(pending.state, VIEW_NO_ROUTE);
        CHECK_INT(settled.state, VIEW_NO_ROUTE);
        CHECK(pending.route_searching == true);
        CHECK(settled.route_searching == false);
        CHECK(strcmp(pending.reason, settled.reason) != 0);
        CHECK(pending.reason[0] != '\0');
        /* Must not claim absence while still asking. */
        CHECK(strstr(pending.reason, "gibt es nur") == NULL);
        CHECK_STR(pending.hero, settled.hero);

        /* The plain view_build() keeps its old meaning: settled, not searching. */
        view_model_t plain;
        view_build(&acs[0], NULL, &now, nac, true, &plain);
        CHECK(plain.route_searching == false);
        CHECK_STR(plain.reason, settled.reason);

        free(ac_json);
    }

    return test_summary();
}
