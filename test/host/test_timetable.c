/* The timetable match (D94): the ticket's flight number for an alphanumeric
 * callsign, or nothing.
 *
 * Built on the pairs the 2026-10-19 research measured at Vienna: AUA25L from
 * Hamburg landed as OS 254, AUA26V from Stuttgart as OS 266, AUA25M left for
 * Hamburg as OS 257. The times below are that day's, rounded. Most of this
 * file is the cases where the answer must be NO NUMBER: a wrong number is the
 * one thing worse than the callsign (D89).
 */
#include <stdint.h>
#include <string.h>

#include "test_util.h"
#include "arrival.h"
#include "identity.h"
#include "timetable.h"

#define T0   1792368000LL            /* 2026-10-19 00:00 UTC */
#define HM(h, m) (T0 + (h) * 3600LL + (m) * 60LL)

static const char TABLE[] =
    "# esp-flight-monitor timetable 1\n"
    "# generated 1792411200\n"
    "# source: Lufthansa Open API, flight status at VIE\n"
    "OS,252,EDDH,LOWW,1792388700,1792394100\n"   /* 05:45 -> 07:15 */
    "OS,254,EDDH,LOWW,1792415100,1792420200\n"   /* 13:05 -> 14:30 */
    "OS,258,EDDH,LOWW,1792429200,1792434300\n"   /* 17:00 -> 18:25 */
    "OS,257,LOWW,EDDH,1792423200,1792428300\n"   /* 15:20 -> 16:45 */
    "OS,264,EDDS,LOWW,1792396800,1792401300\n"   /* 08:00 -> 09:15 */
    "OS,266,EDDS,LOWW,1792416600,1792420800\n"   /* 13:30 -> 14:40 */
    "LH,1340,LOWW,EDDF,1792407600,1792412700\n"  /* 11:00 -> 12:25 */
    "LH,1342,LOWW,EDDF,1792411200,1792416300\n"; /* 12:00 -> 13:25 */

static aircraft_t ac_mk(const char *flight)
{
    aircraft_t a;
    memset(&a, 0, sizeof a);
    snprintf(a.flight, sizeof a.flight, "%s", flight);
    a.alt_ft = ALT_UNKNOWN;     /* no estimate unless a test sets one up */
    a.gs_kt  = -1;
    return a;
}

static route_t rt_mk(const char *cs, const char *from, const char *to)
{
    route_t r;
    memset(&r, 0, sizeof r);
    snprintf(r.callsign, sizeof r.callsign, "%s", cs);
    snprintf(r.orig_icao, sizeof r.orig_icao, "%s", from);
    snprintf(r.dest_icao, sizeof r.dest_icao, "%s", to);
    r.resolved  = true;
    r.plausible = true;
    return r;
}

static tt_flight_t tt[TT_MAX_FLIGHTS];
static int         n_tt;

static const char *match(const char *cs, const char *from, const char *to, int64_t now)
{
    static char b[16];
    aircraft_t a = ac_mk(cs);
    route_t    r = rt_mk(cs, from, to);
    timetable_match(tt, n_tt, &a, &r, now, b, sizeof b);
    return b;
}

int main(void)
{
    int64_t gen = -1;
    n_tt = timetable_parse(TABLE, strlen(TABLE), tt, TT_MAX_FLIGHTS, &gen);

    GROUP("parse: the CI's file, line by line");
    {
        CHECK_INT(n_tt, 8);
        CHECK(gen == 1792411200LL);
        CHECK_STR(tt[1].carrier, "OS");
        CHECK_INT(tt[1].number, 254);
        CHECK_STR(tt[1].from, "EDDH");
        CHECK_STR(tt[1].to, "LOWW");
        CHECK(tt[1].dep == HM(13, 5));
        CHECK(tt[1].arr == HM(14, 30));
    }

    GROUP("parse: the bytes tools/build_timetable.py writes (its --selftest)");
    {
        /* The writer's selftest produces exactly this file from a canned API
         * answer, so a format change on either side fails one of the two. */
        char *csv = load_fixture("timetable_sample.csv");
        tt_flight_t x[8];
        int64_t g = 0;
        int nx = timetable_parse(csv, strlen(csv), x, 8, &g);
        CHECK_INT(nx, 3);
        CHECK(g == 1792411200LL);
        /* OS 254 carries its estimated arrival, 14:43, not the scheduled 14:30. */
        CHECK(x[0].arr == HM(14, 43));
        aircraft_t a = ac_mk("AUA25L");
        route_t    r = rt_mk("AUA25L", "EDDH", "LOWW");
        char b[16];
        timetable_match(x, nx, &a, &r, HM(14, 35), b, sizeof b);
        CHECK_STR(b, "OS 254");
        /* Basel in ICAO, the way the routeset names it, not BSL or EAP. */
        a = ac_mk("AUA12L");
        r = rt_mk("AUA12L", "LOWW", "LFSB");
        timetable_match(x, nx, &a, &r, HM(15, 30), b, sizeof b);
        CHECK_STR(b, "OS 127");
        free(csv);
    }

    GROUP("parse: a file that is not one of ours is refused, not half-read");
    {
        tt_flight_t x[4];
        int64_t g = 7;
        const char *page = "<!DOCTYPE html><html>404: Not Found</html>";
        CHECK_INT(timetable_parse(page, strlen(page), x, 4, &g), -1);
        CHECK(g == 0);
        const char *v2 = "# esp-flight-monitor timetable 2\nOS,254,EDDH,LOWW,1,2\n";
        CHECK_INT(timetable_parse(v2, strlen(v2), x, 4, NULL), -1);
        CHECK_INT(timetable_parse("", 0, x, 4, NULL), -1);
        CHECK_INT(timetable_parse(NULL, 0, x, 4, NULL), -1);
    }

    GROUP("parse: bad lines are skipped, good ones kept, CRLF is fine");
    {
        const char *t =
            "# esp-flight-monitor timetable 1\r\n"
            "OS,254,EDDH,LOWW,1792415100,1792420200\r\n"
            "OS,254,EDDH,LOWW,1792415100\r\n"                  /* five fields  */
            "OS,254,EDDH,LOWW,1792415100,1792420200,9\r\n"     /* seven        */
            "os,254,EDDH,LOWW,1792415100,1792420200\r\n"       /* lower case   */
            "OS,254,HAM,VIE,1792415100,1792420200\r\n"         /* IATA, not ICAO */
            "OS,0,EDDH,LOWW,1792415100,1792420200\r\n"         /* no number    */
            "OS,12345,EDDH,LOWW,1792415100,1792420200\r\n"     /* too long     */
            "OS,-254,EDDH,LOWW,1792415100,1792420200\r\n"      /* a sign       */
            "OS,254,EDDH,LOWW,1792420200,1792415100\r\n"       /* lands first  */
            "OS,254,EDDH,LOWW,1792415100,1792515100\r\n"       /* 27 h flight  */
            "\r\n"
            "4Y,72,LOWW,EDDF,1792415100,1792420200";           /* no final LF  */
        tt_flight_t x[8];
        CHECK_INT(timetable_parse(t, strlen(t), x, 8, NULL), 2);
        CHECK_STR(x[1].carrier, "4Y");
        CHECK_INT(x[1].number, 72);
        /* and `max` is a hard stop */
        CHECK_INT(timetable_parse(t, strlen(t), x, 1, NULL), 1);
    }

    GROUP("the friend's two flights, and the departure back to Hamburg");
    {
        CHECK_STR(match("AUA25L", "EDDH", "LOWW", HM(14, 20)), "OS 254");
        CHECK_STR(match("AUA26V", "EDDS", "LOWW", HM(14, 0)),  "OS 266");
        CHECK_STR(match("AUA25M", "LOWW", "EDDH", HM(15, 40)), "OS 257");
        /* The same callsign in the morning is the morning flight. */
        CHECK_STR(match("AUA25L", "EDDH", "LOWW", HM(7, 0)),   "OS 252");
    }

    GROUP("Austrian's digits must lead the number");
    {
        /* Right route, right time, but 26 is not how 254 starts. */
        CHECK_STR(match("AUA26L", "EDDH", "LOWW", HM(14, 20)), "");
        /* All three digits kept is still a prefix: AUA235D = OS 235. */
        static const char t235[] =
            "# esp-flight-monitor timetable 1\n"
            "OS,235,LOWW,EDDB,1792418400,1792422600\n";
        tt_flight_t x[2];
        int nx = timetable_parse(t235, strlen(t235), x, 2, NULL);
        aircraft_t a = ac_mk("AUA235D");
        route_t    r = rt_mk("AUA235D", "LOWW", "EDDB");
        char b[16];
        timetable_match(x, nx, &a, &r, HM(14, 30), b, sizeof b);
        CHECK_STR(b, "OS 235");
    }

    GROUP("no number: wrong route, wrong time, unknown airline");
    {
        CHECK_STR(match("AUA25L", "EDDS", "LOWW", HM(14, 20)), "");  /* Stuttgart  */
        CHECK_STR(match("AUA25L", "LOWW", "EDDH", HM(14, 20)), "");  /* reversed   */
        CHECK_STR(match("AUA25L", "EDDH", "LOWW", HM(10, 0)),  "");  /* between    */
        CHECK_STR(match("AUA25L", "EDDH", "LOWW", HM(12, 30)), "");  /* 35 min early */
        CHECK_STR(match("AUA25L", "EDDH", "LOWW", HM(15, 31)), "");  /* 61 min late  */
        CHECK_STR(match("AUA25L", "EDDH", "LOWW", HM(15, 29)), "OS 254");
        CHECK_STR(match("XYZ25L", "EDDH", "LOWW", HM(14, 20)), "");  /* not in tables.h */
        CHECK_STR(match("WZZ25L", "EDDH", "LOWW", HM(14, 20)), "");  /* out since D93 */
    }

    GROUP("no number: not an alphanumeric airline callsign");
    {
        /* Numeric: flight_number() already says OS 254 (D89). */
        CHECK_STR(match("AUA254",  "EDDH", "LOWW", HM(14, 20)), "");
        CHECK_STR(match("OELBA",   "EDDH", "LOWW", HM(14, 20)), "");  /* a registration */
        CHECK_STR(match("AUAL25",  "EDDH", "LOWW", HM(14, 20)), "");  /* no digit first */
        CHECK_STR(match("AUA25LXX","EDDH", "LOWW", HM(14, 20)), "");  /* too long */
        CHECK_STR(match("",        "EDDH", "LOWW", HM(14, 20)), "");
    }

    GROUP("two flights fit: the callsign stays");
    {
        /* LH 1340 and LH 1342 are both plausibly in the air at 12:15. The
         * letters of DLH6HE say nothing, so there is no way to choose. */
        CHECK_STR(match("DLH6HE", "LOWW", "EDDF", HM(12, 15)), "");
        /* At 10:45 only LH 1340 can be up. */
        CHECK_STR(match("DLH6HE", "LOWW", "EDDF", HM(10, 45)), "LH 1340");
    }

    GROUP("the same flight number twice counts once");
    {
        static const char dup[] =
            "# esp-flight-monitor timetable 1\n"
            "OS,254,EDDH,LOWW,1792415100,1792420200\n"
            "OS,254,EDDH,LOWW,1792415100,1792420200\n";
        tt_flight_t x[4];
        int nx = timetable_parse(dup, strlen(dup), x, 4, NULL);
        aircraft_t a = ac_mk("AUA25L");
        route_t    r = rt_mk("AUA25L", "EDDH", "LOWW");
        char b[16];
        CHECK(timetable_match(x, nx, &a, &r, HM(14, 20), b, sizeof b) == 6);
        CHECK_STR(b, "OS 254");
    }

    GROUP("the arrival estimate separates a delayed flight from the next one");
    {
        /* OS 254 lands 14:30, a later OS 256 at 15:25. At 14:10 both are in
         * their windows; an aircraft 15 minutes out can only be OS 254. */
        static const char two[] =
            "# esp-flight-monitor timetable 1\n"
            "OS,254,EDDH,LOWW,1792415100,1792420200\n"   /* 13:05 -> 14:30 */
            "OS,256,EDDH,LOWW,1792418400,1792423500\n";  /* 14:00 -> 15:25 */
        tt_flight_t x[4];
        int nx = timetable_parse(two, strlen(two), x, 4, NULL);
        route_t r = rt_mk("AUA25L", "EDDH", "LOWW");
        char b[16];

        aircraft_t a = ac_mk("AUA25L");
        timetable_match(x, nx, &a, &r, HM(14, 10), b, sizeof b);
        CHECK_STR(b, "");                       /* no estimate: two fit */

        r.has_coords = true;
        r.orig_lat = 53.6304f; r.orig_lon = 9.98823f;    /* EDDH */
        r.dest_lat = 48.1103f; r.dest_lon = 16.5697f;    /* LOWW */
        a.lat = 48.82; a.lon = 15.50;                    /* ~60 nm NW of LOWW */
        a.alt_ft = 25000;
        a.gs_kt = 400;
        a.has_track = true;
        a.track_deg = 135.0f;
        int eta = arrival_minutes(&a, &r);
        CHECK(eta >= 10 && eta <= 25);         /* the setup does what it says */
        timetable_match(x, nx, &a, &r, HM(14, 10), b, sizeof b);
        CHECK_STR(b, "OS 254");

        /* And an estimate that fits neither is no number at all. */
        timetable_match(x, nx, &a, &r, HM(12, 40), b, sizeof b);
        CHECK_STR(b, "");
    }

    GROUP("defensive: no clock, no route, an implausible route, tiny buffers");
    {
        CHECK_STR(match("AUA25L", "EDDH", "LOWW", 0), "");
        aircraft_t a = ac_mk("AUA25L");
        route_t    r = rt_mk("AUA25L", "EDDH", "LOWW");
        char b[16] = "x";
        r.plausible = false;
        CHECK(timetable_match(tt, n_tt, &a, &r, HM(14, 20), b, sizeof b) == 0);
        CHECK_STR(b, "");
        r = rt_mk("AUA25L", "EDDH", "LOWW");
        r.resolved = false;
        CHECK(timetable_match(tt, n_tt, &a, &r, HM(14, 20), b, sizeof b) == 0);
        r = rt_mk("AUA25L", "EDDH", "LOWW");
        CHECK(timetable_match(tt, n_tt, &a, NULL, HM(14, 20), b, sizeof b) == 0);
        CHECK(timetable_match(NULL, 0, &a, &r, HM(14, 20), b, sizeof b) == 0);
        CHECK(timetable_match(tt, n_tt, &a, &r, HM(14, 20), NULL, 8) == 0);
        char tiny[4];
        CHECK(timetable_match(tt, n_tt, &a, &r, HM(14, 20), tiny, sizeof tiny) == 3);
        CHECK_STR(tiny, "OS ");
    }

    GROUP("identity: a matched number replaces the callsign everywhere");
    {
        aircraft_t a = ac_mk("AUA25L");
        snprintf(a.type, sizeof a.type, "A321");
        char b[64];
        aircraft_flight_number(&a, b, sizeof b);
        CHECK_STR(b, "AUA25L");                 /* no match: the callsign, D89 */
        snprintf(a.flight_no, sizeof a.flight_no, "OS 254");
        aircraft_flight_number(&a, b, sizeof b);
        CHECK_STR(b, "OS 254");
        aircraft_identity(&a, false, b, sizeof b);
        CHECK_STR(b, "OS 254");
        /* A numeric callsign without a match still converts as before. */
        aircraft_t c = ac_mk("AUA453");
        aircraft_flight_number(&c, b, sizeof b);
        CHECK_STR(b, "OS 453");
        CHECK(aircraft_flight_number(NULL, b, sizeof b) == 0);
        CHECK_STR(b, "");
    }

    return test_summary();
}
