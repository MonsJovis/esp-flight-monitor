/* Host-side unit tests for the pure logic behind flight_source: backoff
 * timing, the source table, the route-cache set-difference, and the
 * compass abbreviation. No board, no network (AGENTS.md §7, PLAN.md M2).
 *
 * NOT wired into the Makefile's MODULE_SRCS glob yet (it only picks up
 * main/data and main/net files ending in _parse.c) — add
 * main/net/source_logic.c to build and run this file.
 */
#include "test_util.h"

#include <stdint.h>
#include <string.h>

#include "source_logic.h"
#include "flight_types.h"
/* flight_source.h pulls in esp_err.h, which the host runner has no business
 * linking; take just the two buffer sizes it declares. */
#define ROUTE_REQ_BUF_SZ       8192
#define ROUTE_RESP_BUF_SZ     24576


static void test_backoff_schedule(void)
{
    GROUP("source_backoff_delay_ms: doubles from the poll floor, caps at 5 min");

    CHECK_INT(source_backoff_delay_ms(0), SRC_POLL_INTERVAL_MS);
    CHECK_INT(source_backoff_delay_ms(-3), SRC_POLL_INTERVAL_MS); /* never negative-weird */

    CHECK_INT(source_backoff_delay_ms(1), 12000);
    CHECK_INT(source_backoff_delay_ms(2), 24000);
    CHECK_INT(source_backoff_delay_ms(3), 48000);
    CHECK_INT(source_backoff_delay_ms(4), 96000);
    CHECK_INT(source_backoff_delay_ms(5), 192000);
    CHECK_INT(source_backoff_delay_ms(6), 300000); /* capped: 384000 would exceed it */
    CHECK_INT(source_backoff_delay_ms(7), 300000);
    CHECK_INT(source_backoff_delay_ms(20), 300000); /* stays capped, never overflows */

    /* Never faster than the 10 s floor AGENTS.md §5 demands, at any failure count. */
    for (int n = 0; n <= 20; n++) {
        CHECK(source_backoff_delay_ms(n) >= 10000);
    }
}

static void test_throttle_status(void)
{
    GROUP("source_is_throttle_status: exact codes only, never a 3xx range");

    CHECK(source_is_throttle_status(429) == true);
    CHECK(source_is_throttle_status(503) == true);
    CHECK(source_is_throttle_status(308) == true);

    /* A real redirect must NOT be classified as throttling (coordinator
     * correction: adsb.fi's 301 to https:// is a genuine redirect, not a
     * throttle signal, and generalising 308-handling to "any 3xx" would
     * misfire on it). */
    CHECK(source_is_throttle_status(301) == false);
    CHECK(source_is_throttle_status(302) == false);
    CHECK(source_is_throttle_status(307) == false);

    CHECK(source_is_throttle_status(200) == false);
    CHECK(source_is_throttle_status(404) == false);
    CHECK(source_is_throttle_status(500) == false);
}

static void test_source_table(void)
{
    GROUP("source table: exactly one enabled entry (adsb.lol point)");

    CHECK(source_is_enabled(SRC_ADSB_LOL_POINT) == true);
    CHECK(source_is_enabled(SRC_ADSB_LOL_LATLON) == false);

    int enabled_count = 0;
    for (int i = 0; i < SRC_COUNT; i++) {
        if (source_is_enabled((source_id_t)i)) {
            enabled_count++;
        }
    }
    CHECK_INT(enabled_count, 1);

    /* Round-robin always lands back on the sole enabled entry today. */
    CHECK_INT(source_next_enabled(SRC_ADSB_LOL_POINT), SRC_ADSB_LOL_POINT);
    CHECK_INT(source_next_enabled(SRC_ADSB_LOL_LATLON), SRC_ADSB_LOL_POINT);

    CHECK_STR(source_name(SRC_ADSB_LOL_POINT), "adsb.lol (point)");
    CHECK(source_name((source_id_t)999) != NULL); /* out-of-range: must not crash */
}

static void test_build_url(void)
{
    GROUP("source_build_url: both adsb.lol URL shapes, plain HTTP");

    char buf[128];
    int n = source_build_url(SRC_ADSB_LOL_POINT, 47.6691, 15.9303, 30, buf, sizeof buf);
    CHECK(n > 0);
    CHECK_STR(buf, "http://api.adsb.lol/v2/point/47.6691/15.9303/30");

    n = source_build_url(SRC_ADSB_LOL_LATLON, 12.9211, 100.8721, 30, buf, sizeof buf);
    CHECK(n > 0);
    CHECK_STR(buf, "http://api.adsb.lol/v2/lat/12.9211/lon/100.8721/dist/30");

    /* No https:// anywhere — plain HTTP only (AGENTS.md architecture decision). */
    CHECK(strstr(buf, "https://") == NULL);

    /* Buffer too small must fail safely, not truncate silently. */
    char tiny[8];
    n = source_build_url(SRC_ADSB_LOL_POINT, 47.6691, 15.9303, 30, tiny, sizeof tiny);
    CHECK_INT(n, -1);

    CHECK_INT(source_build_url((source_id_t)999, 0, 0, 0, buf, sizeof buf), -1);
}

static void test_find_uncached(void)
{
    GROUP("source_find_uncached: set difference, dedup, skips empty callsigns");

    char onscreen[5][9] = { "AUA453", "DLH1JN", "", "OEVSO", "AUA453" }; /* dup + one blank */
    char known[2][9]    = { "DLH1JN", "OEVSO" };
    char pending[8][9];

    int n = source_find_uncached(onscreen, 5, known, 2, pending, 8);
    CHECK_INT(n, 1);
    CHECK_STR(pending[0], "AUA453");

    /* Nothing known yet -> every non-empty callsign, deduplicated. */
    n = source_find_uncached(onscreen, 5, NULL, 0, pending, 8);
    CHECK_INT(n, 3);
    CHECK_STR(pending[0], "AUA453");
    CHECK_STR(pending[1], "DLH1JN");
    CHECK_STR(pending[2], "OEVSO");

    /* Capacity limit is respected. */
    n = source_find_uncached(onscreen, 5, NULL, 0, pending, 1);
    CHECK_INT(n, 1);

    /* Everything already known -> nothing pending. */
    char known_all[3][9] = { "AUA453", "DLH1JN", "OEVSO" };
    n = source_find_uncached(onscreen, 5, known_all, 3, pending, 8);
    CHECK_INT(n, 0);
}

static void test_should_post_routes(void)
{
    GROUP("source_should_post_routes: rate-limited batching");

    CHECK(source_should_post_routes(0, INT64_MAX) == false); /* nothing to ask */
    CHECK(source_should_post_routes(3, 0) == false);         /* just posted */
    CHECK(source_should_post_routes(3, SRC_ROUTE_POST_MIN_INTERVAL_MS - 1) == false);
    CHECK(source_should_post_routes(3, SRC_ROUTE_POST_MIN_INTERVAL_MS) == true);
    CHECK(source_should_post_routes(1, INT64_MAX) == true); /* never posted before */
}

static void test_compass_abbrev(void)
{
    GROUP("source_compass_abbrev_en: 8-point English compass");

    CHECK_STR(source_compass_abbrev_en(0.0f), "N");
    CHECK_STR(source_compass_abbrev_en(44.0f), "NE");
    CHECK_STR(source_compass_abbrev_en(45.0f), "NE");
    CHECK_STR(source_compass_abbrev_en(90.0f), "E");
    CHECK_STR(source_compass_abbrev_en(135.0f), "SE");
    CHECK_STR(source_compass_abbrev_en(180.0f), "S");
    CHECK_STR(source_compass_abbrev_en(225.0f), "SW");
    CHECK_STR(source_compass_abbrev_en(270.0f), "W");
    CHECK_STR(source_compass_abbrev_en(315.0f), "NW");
    CHECK_STR(source_compass_abbrev_en(359.9f), "N");

    /* Wrap-around and negative bearings must not crash or index out of range. */
    CHECK_STR(source_compass_abbrev_en(360.0f), "N");
    CHECK_STR(source_compass_abbrev_en(720.0f + 90.0f), "E");
    CHECK_STR(source_compass_abbrev_en(-45.0f), "NW");
    CHECK_STR(source_compass_abbrev_en(-1.0f), "N");
}

int main(void)
{
    test_backoff_schedule();
    test_throttle_status();
    test_source_table();
    test_build_url();
    test_find_uncached();
    test_should_post_routes();
    test_compass_abbrev();

    GROUP("routeset buffers are sized against the real captured response");
    {
        /* This is the test that should have existed before the device ran: the
         * fixture was captured at 5,833 bytes while the buffer was 4,096, so
         * every live route truncated mid-JSON, failed to parse, and the panel
         * showed "route pending" forever. A buffer size is a claim about the
         * data, and the data was sitting in the repo the whole time. */
        char *rt = load_fixture("routeset_response.json");
        size_t n = strlen(rt);
        CHECK(n > 4096);                       /* the old size really was too small */
        CHECK(n < ROUTE_RESP_BUF_SZ);          /* the new one fits */
        /* MAX_AIRCRAFT is nearly double the 13 in the capture, so insist on
         * room for that many rather than merely fitting today's sample. */
        CHECK(n * MAX_AIRCRAFT / 13 < ROUTE_RESP_BUF_SZ);
        free(rt);
    }

    return test_summary();
}
