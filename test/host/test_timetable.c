/* The callsign pairs (D94, D95): the ticket's flight number for a callsign, or
 * nothing — and the encrypted file they arrive in.
 *
 * Built on the pairs the 2026-10-19 research measured at Vienna: AUA25L from
 * Hamburg landed as OS 254, AUA26V from Stuttgart as OS 266, AUA25M left for
 * Hamburg as OS 257, and WZZ839 flew as W6 2274. Most of this file is the
 * cases where the answer must be NO NUMBER: a wrong number is the one thing
 * worse than the callsign (D89).
 *
 * The two fixtures are written by tools/build_timetable.py --selftest, which
 * the host suite also runs, so the CI's writer and the panel's reader are
 * held to the same bytes from both sides.
 */
#include <stdint.h>
#include <string.h>

#include "test_util.h"
#include "identity.h"
#include "timetable.h"
#include "timetable_crypt.h"

#define NOW 1792411200LL                 /* Mon 2026-10-19 12:00 UTC */
#define DAY (24 * 3600LL)

static tt_pair_t tt[TT_MAX_PAIRS];
static int       n_tt;

static route_t rt_mk(const char *from, const char *to)
{
    route_t r;
    memset(&r, 0, sizeof r);
    snprintf(r.orig_icao, sizeof r.orig_icao, "%s", from);
    snprintf(r.dest_icao, sizeof r.dest_icao, "%s", to);
    r.resolved  = true;
    r.plausible = true;
    return r;
}

static const char *look_in(const tt_pair_t *t, int n, const char *cs,
                           const char *from, const char *to, int64_t now)
{
    static char b[16];
    route_t r = rt_mk(from, to);
    timetable_lookup(t, n, cs, &r, now, b, sizeof b);
    return b;
}

static const char *look(const char *cs, const char *from, const char *to, int64_t now)
{
    return look_in(tt, n_tt, cs, from, to, now);
}

/* A table of one or two lines, for the cases the fixture does not hold. */
static int small(const char *lines, tt_pair_t *out, int max)
{
    static char text[512];
    snprintf(text, sizeof text, "%s\n%s", TT_HEADER, lines);
    return timetable_parse(text, strlen(text), out, max, NULL);
}

int main(void)
{
    char *csv = load_fixture("timetable_sample.csv");
    int64_t gen = -1;
    n_tt = timetable_parse(csv, strlen(csv), tt, TT_MAX_PAIRS, &gen);

    GROUP("parse: the plaintext tools/build_timetable.py writes");
    {
        CHECK_INT(n_tt, 6);
        CHECK(gen == NOW);
        CHECK_STR(tt[1].callsign, "AUA25L");
        CHECK_STR(tt[1].carrier, "OS");
        CHECK_INT(tt[1].number, 254);
        CHECK_STR(tt[1].from, "EDDH");
        CHECK_STR(tt[1].to, "LOWW");
        CHECK(tt[1].seen == NOW);
        CHECK(tt[4].seen == NOW - 2 * DAY);       /* FIN5H, kept from before */
    }

    GROUP("parse: a file that is not one of ours is refused, not half-read");
    {
        tt_pair_t x[4];
        int64_t g = 7;
        const char *page = "<!DOCTYPE html><html>404: Not Found</html>";
        CHECK_INT(timetable_parse(page, strlen(page), x, 4, &g), -1);
        CHECK(g == 0);
        const char *v1 = "# esp-flight-monitor timetable 1\nOS,254,EDDH,LOWW,1,2\n";
        CHECK_INT(timetable_parse(v1, strlen(v1), x, 4, NULL), -1);
        CHECK_INT(timetable_parse("", 0, x, 4, NULL), -1);
        CHECK_INT(timetable_parse(NULL, 0, x, 4, NULL), -1);
    }

    GROUP("parse: bad lines are skipped, good ones kept, CRLF is fine");
    {
        const char *t =
            "# esp-flight-monitor timetable 2\r\n"
            "AUA25L,OS,254,EDDH,LOWW,1792411200\r\n"
            "AUA25L,OS,254,EDDH,LOWW\r\n"                    /* five fields     */
            "AUA25L,OS,254,EDDH,LOWW,1792411200,9\r\n"       /* seven           */
            "aua25l,OS,254,EDDH,LOWW,1792411200\r\n"         /* lower case      */
            "AUAL25,OS,254,EDDH,LOWW,1792411200\r\n"         /* no digit first  */
            "AUA25LXX,OS,254,EDDH,LOWW,1792411200\r\n"       /* three letters   */
            "AUA25L,OSX,254,EDDH,LOWW,1792411200\r\n"        /* not IATA        */
            "AUA25L,OS,0,EDDH,LOWW,1792411200\r\n"           /* no number       */
            "AUA25L,OS,12345,EDDH,LOWW,1792411200\r\n"       /* too long        */
            "AUA25L,OS,-254,EDDH,LOWW,1792411200\r\n"        /* a sign          */
            "AUA25L,OS,254,HAM,VIE,1792411200\r\n"           /* IATA, not ICAO  */
            "AUA25L,OS,254,EDDH,LOWW,0\r\n"                  /* never seen      */
            "\r\n"
            "OCN72,4Y,72,LOWW,EDDF,1792411200";              /* no final LF     */
        tt_pair_t x[8];
        CHECK_INT(timetable_parse(t, strlen(t), x, 8, NULL), 2);
        CHECK_STR(x[1].carrier, "4Y");
        CHECK_INT(x[1].number, 72);
        CHECK_INT(timetable_parse(t, strlen(t), x, 1, NULL), 1);   /* max is a hard stop */
    }

    GROUP("the friend's two flights, the way back, and Basel");
    {
        CHECK_STR(look("AUA25L", "EDDH", "LOWW", NOW), "OS 254");
        CHECK_STR(look("AUA26V", "EDDS", "LOWW", NOW), "OS 266");
        CHECK_STR(look("AUA25M", "LOWW", "EDDH", NOW), "OS 257");
        CHECK_STR(look("AUA12L", "LOWW", "LFSB", NOW), "OS 127");
    }

    GROUP("a numeric callsign that is not its flight number (D93)");
    {
        CHECK_STR(look("WZZ839", "LOWW", "LHBP", NOW), "W6 2274");
    }

    GROUP("no number: wrong route, unknown callsign, a near miss");
    {
        CHECK_STR(look("AUA25L", "EDDS", "LOWW", NOW), "");    /* Stuttgart        */
        CHECK_STR(look("AUA25L", "LOWW", "EDDH", NOW), "");    /* reversed         */
        CHECK_STR(look("AUA26L", "EDDH", "LOWW", NOW), "");    /* never seen       */
        CHECK_STR(look("AUA25",  "EDDH", "LOWW", NOW), "");    /* a prefix of it   */
        CHECK_STR(look("RYR4MR", "LEMD", "LOWW", NOW), "");
        CHECK_STR(look("",       "EDDH", "LOWW", NOW), "");
        CHECK_STR(look("OELBA",  "EDDH", "LOWW", NOW), "");    /* a registration   */
    }

    GROUP("seven days and no longer, when the clock is known");
    {
        CHECK_STR(look("AUA25L", "EDDH", "LOWW", NOW + 7 * DAY), "OS 254");
        CHECK_STR(look("AUA25L", "EDDH", "LOWW", NOW + 7 * DAY + 1), "");
        /* FIN5H was last seen two days before NOW. */
        CHECK_STR(look("FIN5H", "EFHK", "LOWW", NOW + 5 * DAY), "AY 1476");
        CHECK_STR(look("FIN5H", "EFHK", "LOWW", NOW + 5 * DAY + 1), "");
        /* No clock: the age cannot be judged, the pair still names it. */
        CHECK_STR(look("AUA25L", "EDDH", "LOWW", 0), "OS 254");
    }

    GROUP("Austrian's digits come from the number, in order: a pair that breaks it is bad");
    {
        tt_pair_t x[2];
        int nx = small("AUA25L,OS,264,EDDH,LOWW,1792411200", x, 2);
        CHECK_INT(nx, 1);
        CHECK_STR(look_in(x, nx, "AUA25L", "EDDH", "LOWW", NOW), "");   /* no 5 */
        /* All the digits kept: AUA235D = OS 235. */
        nx = small("AUA235D,OS,235,LOWW,EDDB,1792411200", x, 2);
        CHECK_STR(look_in(x, nx, "AUA235D", "LOWW", "EDDB", NOW), "OS 235");
        /* A middle digit dropped, from the first real file (2026-10-20). */
        nx = small("AUA20C,OS,250,EDDH,LOWW,1792411200", x, 2);
        CHECK_STR(look_in(x, nx, "AUA20C", "EDDH", "LOWW", NOW), "OS 250");
        nx = small("AUA71RD,OS,731,LGTS,LOWW,1792411200", x, 2);
        CHECK_STR(look_in(x, nx, "AUA71RD", "LGTS", "LOWW", NOW), "OS 731");
        /* In the number, but not in order, or not from the first digit. */
        nx = small("AUA21C,OS,125,EDDH,LOWW,1792411200", x, 2);
        CHECK_STR(look_in(x, nx, "AUA21C", "EDDH", "LOWW", NOW), "");
        nx = small("AUA2R,OS,46,KORD,LOWW,1792411200", x, 2);
        CHECK_STR(look_in(x, nx, "AUA2R", "KORD", "LOWW", NOW), "");
        nx = small("AUA8KS,OS,428,LEPA,LOWW,1792411200", x, 2);
        CHECK_STR(look_in(x, nx, "AUA8KS", "LEPA", "LOWW", NOW), "");
        /* Other airlines' digits say nothing: DLH6HE = LH 1340. */
        nx = small("DLH6HE,LH,1340,LOWW,EDDF,1792411200", x, 2);
        CHECK_STR(look_in(x, nx, "DLH6HE", "LOWW", "EDDF", NOW), "LH 1340");
    }

    GROUP("two flight numbers for one callsign and route: the callsign stays");
    {
        tt_pair_t x[4];
        int nx = small("DLH7KP,LH,1341,EDDF,LOWW,1792411200\n"
                       "DLH7KP,LH,1343,EDDF,LOWW,1792411200", x, 4);
        CHECK_STR(look_in(x, nx, "DLH7KP", "EDDF", "LOWW", NOW), "");
        nx = small("DLH7KP,LH,1341,EDDF,LOWW,1792411200\n"
                   "DLH7KP,LX,1341,EDDF,LOWW,1792411200", x, 4);
        CHECK_STR(look_in(x, nx, "DLH7KP", "EDDF", "LOWW", NOW), "");
        /* The same pair twice is still one flight. */
        nx = small("DLH7KP,LH,1341,EDDF,LOWW,1792411200\n"
                   "DLH7KP,LH,1341,EDDF,LOWW,1792324800", x, 4);
        CHECK_STR(look_in(x, nx, "DLH7KP", "EDDF", "LOWW", NOW), "LH 1341");
    }

    GROUP("defensive: no route, an implausible one, tiny buffers");
    {
        char b[16] = "x";
        route_t r = rt_mk("EDDH", "LOWW");
        r.plausible = false;
        CHECK(timetable_lookup(tt, n_tt, "AUA25L", &r, NOW, b, sizeof b) == 0);
        CHECK_STR(b, "");
        r = rt_mk("EDDH", "LOWW");
        r.resolved = false;
        CHECK(timetable_lookup(tt, n_tt, "AUA25L", &r, NOW, b, sizeof b) == 0);
        r = rt_mk("", "LOWW");
        CHECK(timetable_lookup(tt, n_tt, "AUA25L", &r, NOW, b, sizeof b) == 0);
        r = rt_mk("EDDH", "LOWW");
        CHECK(timetable_lookup(tt, n_tt, "AUA25L", NULL, NOW, b, sizeof b) == 0);
        CHECK(timetable_lookup(NULL, 0, "AUA25L", &r, NOW, b, sizeof b) == 0);
        CHECK(timetable_lookup(tt, n_tt, NULL, &r, NOW, b, sizeof b) == 0);
        CHECK(timetable_lookup(tt, n_tt, "AUA25L", &r, NOW, NULL, 8) == 0);
        char tiny[4];
        CHECK(timetable_lookup(tt, n_tt, "AUA25L", &r, NOW, tiny, sizeof tiny) == 3);
        CHECK_STR(tiny, "OS ");
    }

    GROUP("decrypt: the file the CI selftest encrypted opens to the plaintext");
    {
        /* tools/build_timetable.py TEST_KEY: the bytes 0x00..0x1f, published
         * on purpose and used for nothing else. */
        uint8_t key[TT_KEY_LEN];
        for (int i = 0; i < TT_KEY_LEN; i++) key[i] = (uint8_t)i;

        char *blob = load_fixture("timetable_sample.tt");
        size_t blen = TT_OVERHEAD + strlen(csv);        /* load_fixture can't say */
        int plain = timetable_decrypt(key, (uint8_t *)blob, blen);
        CHECK_INT(plain, (int)strlen(csv));
        CHECK(plain > 0 && memcmp(blob, csv, (size_t)plain) == 0);
        CHECK(plain > 0 && blob[plain] == '\0');
        tt_pair_t x[8];
        CHECK_INT(timetable_parse(blob, (size_t)(plain > 0 ? plain : 0), x, 8, NULL), 6);
        free(blob);

        /* A wrong key and a damaged file are errors, never garbage. */
        blob = load_fixture("timetable_sample.tt");
        uint8_t wrong[TT_KEY_LEN] = { 0 };
        CHECK_INT(timetable_decrypt(wrong, (uint8_t *)blob, blen), TT_CRYPT_ERR_TAG);
        blob[40] ^= 1;
        CHECK_INT(timetable_decrypt(key, (uint8_t *)blob, blen), TT_CRYPT_ERR_TAG);
        free(blob);

        /* The old plaintext format, a 404 page: not this container at all. */
        char v1[] = "# esp-flight-monitor timetable 1\nOS,254,EDDH,LOWW,1792415100,1792420200\n";
        CHECK_INT(timetable_decrypt(key, (uint8_t *)v1, strlen(v1)), TT_CRYPT_ERR_MAGIC);
        uint8_t shortbuf[TT_OVERHEAD - 1] = "ESPFMTT2";
        CHECK_INT(timetable_decrypt(key, shortbuf, sizeof shortbuf), TT_CRYPT_ERR_SHORT);
        CHECK_INT(timetable_decrypt(NULL, shortbuf, sizeof shortbuf), TT_CRYPT_ERR_SHORT);
    }

    GROUP("the key as the console takes it: 64 hex digits, nothing else");
    {
        uint8_t k[TT_KEY_LEN];
        CHECK_INT(timetable_key_from_hex(
            "000102030405060708090a0b0c0d0e0f101112131415161718191A1B1C1D1E1F", k), 0);
        CHECK_INT(k[0], 0x00);
        CHECK_INT(k[31], 0x1f);
        CHECK_INT(timetable_key_from_hex("0001", k), -1);
        CHECK_INT(timetable_key_from_hex(
            "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1g", k), -1);
        CHECK_INT(timetable_key_from_hex(
            "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f00", k), -1);
        CHECK_INT(timetable_key_from_hex(NULL, k), -1);
    }

    GROUP("identity: a looked-up number replaces the callsign everywhere");
    {
        aircraft_t a;
        memset(&a, 0, sizeof a);
        snprintf(a.flight, sizeof a.flight, "AUA25L");
        snprintf(a.type, sizeof a.type, "A321");
        char b[64];
        aircraft_flight_number(&a, b, sizeof b);
        CHECK_STR(b, "AUA25L");                 /* no pair: the callsign, D89 */
        snprintf(a.flight_no, sizeof a.flight_no, "OS 254");
        aircraft_flight_number(&a, b, sizeof b);
        CHECK_STR(b, "OS 254");
        aircraft_identity(&a, false, b, sizeof b);
        CHECK_STR(b, "OS 254");
        /* A numeric callsign without a pair still converts as before. */
        memset(&a, 0, sizeof a);
        snprintf(a.flight, sizeof a.flight, "AUA453");
        aircraft_flight_number(&a, b, sizeof b);
        CHECK_STR(b, "OS 453");
        CHECK(aircraft_flight_number(NULL, b, sizeof b) == 0);
        CHECK_STR(b, "");
    }

    free(csv);
    return test_summary();
}
