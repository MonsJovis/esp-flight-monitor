/* The OTA rules: is this version newer, and is now a safe moment.
 *
 * Both questions are answered off-device on purpose. "Is 0.10.0 newer than
 * 0.9.0" is a string-versus-number trap that a strcmp gets backwards, and
 * getting it backwards means the device silently refuses the fix you sent it.
 * "Is now safe" depends on the hour and on a window that wraps midnight, and
 * the alternative to testing it here is waiting until 3 a.m. on real hardware.
 */
#include <string.h>

#include "test_util.h"
#include "ota_policy.h"
#include "settings.h"
#include <stdio.h>

/* ---- manifest ---------------------------------------------------------- */

static void test_manifest_good(void)
{
    GROUP("ota_manifest_parse: a well-formed manifest");

    const char *j =
        "{\"version\":\"0.4.2\","
        "\"url\":\"https://example.org/esp-flight-monitor-0.4.2.bin\","
        "\"size\":2209600}";
    ota_manifest_t m;
    CHECK(ota_manifest_parse(j, strlen(j), &m));
    CHECK_STR(m.version, "0.4.2");
    CHECK_STR(m.url, "https://example.org/esp-flight-monitor-0.4.2.bin");
    CHECK_INT((int)m.size, 2209600);

    GROUP("ota_manifest_parse: size is optional");
    const char *j2 = "{\"version\":\"1.0\",\"url\":\"http://h/a.bin\"}";
    CHECK(ota_manifest_parse(j2, strlen(j2), &m));
    CHECK_STR(m.version, "1.0");
    CHECK_INT((int)m.size, 0);

    GROUP("ota_manifest_parse: extra keys are ignored, not fatal");
    const char *j3 = "{\"notes\":\"x\",\"version\":\"2.0\",\"url\":\"http://h/b.bin\"}";
    CHECK(ota_manifest_parse(j3, strlen(j3), &m));
    CHECK_STR(m.version, "2.0");
}

static void test_manifest_rejects(void)
{
    GROUP("ota_manifest_parse: everything malformed is REJECTED, not defaulted");

    /* The distinction that matters: a manifest that does not parse must not
     * look like "no update available", because the caller has to be able to
     * log the difference. Every one of these returns false AND zeroes out. */
    const char *bad[] = {
        "",                                          /* empty */
        "not json at all",
        "[]",                                        /* array, not object */
        "{}",                                        /* both fields missing */
        "{\"version\":\"1.0\"}",                     /* no url */
        "{\"url\":\"http://h/a.bin\"}",              /* no version */
        "{\"version\":1,\"url\":\"http://h/a.bin\"}",/* version not a string */
        "{\"version\":\"1.0\",\"url\":123}",         /* url not a string */
        "{\"version\":\"\",\"url\":\"http://h/a.bin\"}",  /* empty version */
        "{\"version\":\"1.0\",\"url\":\"\"}",             /* empty url */
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ota_manifest_t m;
        memset(&m, 0xAA, sizeof m);
        CHECK(!ota_manifest_parse(bad[i], strlen(bad[i]), &m));
        CHECK(m.version[0] == '\0');
        CHECK(m.url[0] == '\0');
        CHECK_INT((int)m.size, 0);
    }

    GROUP("ota_manifest_parse: an over-long url is refused, never truncated");
    /* Truncating a URL yields a request to a DIFFERENT address that may well
     * answer — with something that is not our firmware. */
    char big[OTA_URL_LEN + 64];
    int n = snprintf(big, sizeof big, "{\"version\":\"1.0\",\"url\":\"http://h/");
    memset(big + n, 'a', OTA_URL_LEN + 4);
    snprintf(big + n + OTA_URL_LEN + 4, sizeof big - n - OTA_URL_LEN - 4, "\"}");
    ota_manifest_t m;
    CHECK(!ota_manifest_parse(big, strlen(big), &m));
    CHECK(m.url[0] == '\0');

    GROUP("ota_manifest_parse: NULL and zero length");
    CHECK(!ota_manifest_parse(NULL, 0, &m));
    CHECK(!ota_manifest_parse("{}", 0, &m));
    CHECK(!ota_manifest_parse("{}", 2, NULL));
}

/* ---- version comparison ------------------------------------------------ */

static void test_version_cmp(void)
{
    GROUP("ota_version_cmp: equal");
    CHECK(ota_version_cmp("1.2.3", "1.2.3") == 0);
    CHECK(ota_version_cmp("1.2", "1.2.0") == 0);
    CHECK(ota_version_cmp("1", "1.0.0") == 0);
    CHECK(ota_version_cmp("v1.2.3", "1.2.3") == 0);
    CHECK(ota_version_cmp("V1.2.3", "v1.2.3") == 0);

    GROUP("ota_version_cmp: ordering");
    CHECK(ota_version_cmp("1.2.3", "1.2.4") < 0);
    CHECK(ota_version_cmp("1.2.4", "1.2.3") > 0);
    CHECK(ota_version_cmp("1.3.0", "1.2.9") > 0);
    CHECK(ota_version_cmp("2.0.0", "1.99.99") > 0);
    CHECK(ota_version_cmp("1.2.3", "1.2.3.1") < 0);

    GROUP("ota_version_cmp: NUMERIC, not lexical — the trap");
    /* strcmp says "0.10.0" < "0.9.0". It is not, and believing it means the
     * device refuses every update after the ninth. */
    CHECK(ota_version_cmp("0.10.0", "0.9.0") > 0);
    CHECK(ota_version_cmp("0.9.0", "0.10.0") < 0);
    CHECK(ota_version_cmp("1.0.10", "1.0.9") > 0);
    CHECK(ota_version_cmp("10.0.0", "9.0.0") > 0);

    GROUP("ota_version_cmp: git describe suffixes compare as the base version");
    /* This is what esp_app_desc_t actually carries on a working tree. */
    CHECK(ota_version_cmp("0.4.2-3-gdeadbee", "0.4.2") == 0);
    CHECK(ota_version_cmp("0.4.2-dirty", "0.4.2") == 0);
    CHECK(ota_version_cmp("0.4.2-dirty", "0.4.3") < 0);
    CHECK(ota_version_cmp("1.0.0-rc1", "1.0.0") == 0);

    GROUP("ota_version_cmp: junk degrades to 0.0.0 rather than crashing");
    CHECK(ota_version_cmp("", "") == 0);
    CHECK(ota_version_cmp(NULL, NULL) == 0);
    CHECK(ota_version_cmp("", "0.0.1") < 0);
    CHECK(ota_version_cmp("garbage", "0.0.1") < 0);
    CHECK(ota_version_cmp("1.0.0", "garbage") > 0);

    GROUP("ota_version_cmp: absurd components clamp, they do not wrap");
    /* A wrapped component could make an OLD build compare as newer, which is
     * the one failure mode here with teeth.
     *
     * "99999999999" alone does NOT test that: unguarded, it wraps to
     * 1215752191 — still huge, still positive, so the assertion holds either
     * way and deleting the clamp survives the whole suite. The input that
     * catches it is 2^32, which wraps to exactly 0:
     *
     *                                    with clamp   without
     *     cmp("4294967296.0.0", "1.0.0")     +1         -1
     *     cmp("99999999999.0.0","1.0.0")     +1         +1
     */
    CHECK(ota_version_cmp("4294967296.0.0", "1.0.0") > 0);      /* 2^32 -> 0 */
    CHECK(ota_version_cmp("1.0.0", "4294967296.0.0") < 0);
    CHECK(ota_version_cmp("0.4294967296.0", "0.1.0") > 0);      /* and not only in the first component */
    CHECK(ota_version_cmp("99999999999.0.0", "1.0.0") > 0);
    CHECK(ota_version_cmp("1.0.0", "99999999999.0.0") < 0);
    /* "Does not go negative" is only half the contract. A clamp that stops
     * accumulating part-way is not saturating: at a ceiling of 100000,
     * "1000000" came out as 100000 while "999999" came out whole, so the
     * LARGER version compared as smaller — the old-build-looks-newer failure
     * this group exists to prevent, moved further up the number line rather
     * than removed. Ordering has to survive the clamp boundary. */
    CHECK(ota_version_cmp("1000000.0.0", "999999.0.0") > 0);
    CHECK(ota_version_cmp("100000.0.0", "99999.0.0") > 0);
    CHECK(ota_version_cmp("4294967296.0.0", "4294967295.0.0") > 0);
    /* Monotonic all the way up the ladder, not just at one step. */
    {
        static const char *const ascending[] = {
            "9.0.0", "99.0.0", "999.0.0", "9999.0.0", "99999.0.0",
            "100000.0.0", "999999.0.0", "1000000.0.0", "99999999.0.0",
            "4294967295.0.0", "4294967296.0.0",
        };
        for (unsigned i = 1; i < sizeof ascending / sizeof ascending[0]; i++) {
            CHECK(ota_version_cmp(ascending[i], ascending[i - 1]) > 0);
            CHECK(ota_version_cmp(ascending[i - 1], ascending[i]) < 0);
        }
    }
}

/* ---- the midnight wrap -------------------------------------------------- */

static void test_hour_window(void)
{
    GROUP("ota_hour_in_window: 22:00-07:00, the default, wraps midnight");
    /* Walk all 24 hours rather than spot-checking: an off-by-one at either
     * edge is the whole bug class here. */
    for (int h = 0; h < 24; h++) {
        bool expect = (h >= 22 || h < 7);
        CHECK(ota_hour_in_window(h, 22, 7) == expect);
    }

    GROUP("ota_hour_in_window: 01:00-05:00, a window that does not wrap");
    for (int h = 0; h < 24; h++) {
        bool expect = (h >= 1 && h < 5);
        CHECK(ota_hour_in_window(h, 1, 5) == expect);
    }

    GROUP("ota_hour_in_window: from == to is EMPTY, not all day");
    for (int h = 0; h < 24; h++) {
        CHECK(!ota_hour_in_window(h, 3, 3));
    }

    GROUP("ota_hour_in_window: nonsense hours are outside every window");
    CHECK(!ota_hour_in_window(-1, 22, 7));
    CHECK(!ota_hour_in_window(24, 22, 7));
    CHECK(!ota_hour_in_window(99, 0, 23));

    GROUP("ota_hour_in_window: agrees with settings_brightness_for_hour");
    /* Two implementations of one midnight wrap, in two translation units that
     * cannot share code. If they ever disagree, the device dims at one hour
     * and updates at another, and only one of those is visible. */
    for (int from = 0; from < 24; from++) {
        for (int to = 0; to < 24; to++) {
            for (int h = 0; h < 24; h++) {
                settings_t s;
                settings_defaults(&s);
                s.auto_dim = true;
                s.dim_from_hour = from;
                s.dim_to_hour = to;
                s.brightness_pct = 100;
                s.dim_brightness_pct = 20;
                bool dim_says = (settings_brightness_for_hour(&s, h) == 20);
                CHECK(ota_hour_in_window(h, from, to) == dim_says);
            }
        }
    }
}

/* ---- the two decisions -------------------------------------------------- */

static ota_ctx_t ready_ctx(void)
{
    ota_ctx_t c = {
        .have_url = true, .online = true, .clock_valid = true,
        .auto_dim = true, .hour = 3, .dim_from_hour = 22, .dim_to_hour = 7,
        .now_ms = 1000 * 1000, .last_check_ms = 0,
    };
    return c;
}

static void test_should_check(void)
{
    GROUP("ota_should_check: never checked, everything ready -> yes");
    ota_ctx_t c = ready_ctx();
    CHECK(ota_should_check(&c));

    GROUP("ota_should_check: any missing precondition -> no");
    c = ready_ctx(); c.have_url    = false; CHECK(!ota_should_check(&c));
    c = ready_ctx(); c.online      = false; CHECK(!ota_should_check(&c));
    c = ready_ctx(); c.clock_valid = false; CHECK(!ota_should_check(&c));
    CHECK(!ota_should_check(NULL));

    GROUP("ota_should_check: honours the 24 h interval");
    c = ready_ctx();
    c.last_check_ms = 1000;
    c.now_ms = 1000 + OTA_CHECK_INTERVAL_MS - 1;
    CHECK(!ota_should_check(&c));
    c.now_ms = 1000 + OTA_CHECK_INTERVAL_MS;
    CHECK(ota_should_check(&c));

    GROUP("ota_should_check: a clock that jumps BACKWARDS re-checks");
    /* SNTP correcting a large offset does exactly this. Without the guard the
     * device parks on "not yet" for as long as the jump was. */
    c = ready_ctx();
    c.last_check_ms = 5 * OTA_CHECK_INTERVAL_MS;
    c.now_ms = 1000;
    CHECK(ota_should_check(&c));

    GROUP("ota_should_check: no URL configured is the shipped default");
    /* The device leaves the factory with nowhere to update from, and must
     * therefore never try. */
    ota_ctx_t blank = { 0 };
    CHECK(!ota_should_check(&blank));
}

static void test_should_install(void)
{
    GROUP("ota_should_install: inside the night window -> yes");
    ota_ctx_t c = ready_ctx();
    CHECK(ota_should_install(&c));

    GROUP("ota_should_install: NOT during the day, at any daytime hour");
    /* Writing 2 MB to flash tears this panel (esp-bsp#570). Every hour he
     * might look at it is refused. */
    for (int h = 7; h < 22; h++) {
        c = ready_ctx(); c.hour = h;
        CHECK(!ota_should_install(&c));
    }

    GROUP("ota_should_install: every hour inside the window is allowed");
    for (int h = 0; h < 24; h++) {
        c = ready_ctx(); c.hour = h;
        CHECK(ota_should_install(&c) == (h >= 22 || h < 7));
    }

    GROUP("ota_should_install: auto_dim off means there is no safe hour");
    for (int h = 0; h < 24; h++) {
        c = ready_ctx(); c.auto_dim = false; c.hour = h;
        CHECK(!ota_should_install(&c));
    }

    GROUP("ota_should_install: any missing precondition -> no");
    c = ready_ctx(); c.have_url    = false; CHECK(!ota_should_install(&c));
    c = ready_ctx(); c.online      = false; CHECK(!ota_should_install(&c));
    c = ready_ctx(); c.clock_valid = false; CHECK(!ota_should_install(&c));
    CHECK(!ota_should_install(NULL));
}

int main(void)
{
    test_manifest_good();
    test_manifest_rejects();
    test_version_cmp();
    test_hour_window();
    test_should_check();
    test_should_install();
    return test_summary();
}
