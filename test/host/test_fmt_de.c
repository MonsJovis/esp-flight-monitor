/* Host tests for main/data/fmt_de.{h,c} — the German words-and-numbers
 * formatters (docs/PLAN.md M2.5). Pure data/string work, no hardware.
 */
#include "fmt_de.h"
#include "flight_types.h"
#include "test_util.h"

#include <stdint.h>
#include <string.h>

/* Independent copies of the expected compass tables, so the loop tests
 * below check fmt_de.c's behaviour against a spec written here, not
 * against itself. */
static const char *const ABBR16[16] = {
    "N", "NNO", "NO", "ONO", "O", "OSO", "SO", "SSO",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
};
static const char *const WORD8[8] = {
    "Norden", "Nordosten", "Osten", "Südosten",
    "Süden", "Südwesten", "Westen", "Nordwesten",
};
static const char *const ADV8[8] = {
    "nördlich", "nordöstlich", "östlich", "südöstlich",
    "südlich", "südwestlich", "westlich", "nordwestlich",
};

int main(void)
{
    GROUP("nm_to_km / ft_to_m");
    CHECK_NEAR(nm_to_km(1.0f), 1.852, 1e-4);
    CHECK_NEAR(nm_to_km(0.0f), 0.0, 1e-6);
    CHECK_NEAR(nm_to_km(10.0f), 18.52, 1e-3);

    CHECK_INT(ft_to_m(0), 0);
    CHECK_INT(ft_to_m(1000), 305);       /* 304.8 -> rounds up */
    CHECK_INT(ft_to_m(-1000), -305);     /* -304.8 -> rounds away from zero */
    CHECK_INT(ft_to_m(5), 2);            /* 1.524 -> 2 */
    CHECK_INT(ft_to_m(3), 1);            /* 0.9144 -> 1 */
    CHECK_INT(ft_to_m(30000), 9144);     /* exact */

    GROUP("fmt_int_de");
    {
        char buf[64];

        fmt_int_de(0, buf, sizeof buf);
        CHECK_STR(buf, "0");

        fmt_int_de(5, buf, sizeof buf);
        CHECK_STR(buf, "5");             /* single digit */

        fmt_int_de(-5, buf, sizeof buf);
        CHECK_STR(buf, "-5");            /* negative, single digit */

        fmt_int_de(999, buf, sizeof buf);
        CHECK_STR(buf, "999");           /* below grouping threshold */

        fmt_int_de(1000, buf, sizeof buf);
        CHECK_STR(buf, "1.000");         /* exactly 1000 */

        fmt_int_de(9100, buf, sizeof buf);
        CHECK_STR(buf, "9.100");

        fmt_int_de(999999, buf, sizeof buf);
        CHECK_STR(buf, "999.999");       /* exactly 999999 */

        fmt_int_de(-1234567, buf, sizeof buf);
        CHECK_STR(buf, "-1.234.567");

        fmt_int_de(INT32_MIN, buf, sizeof buf);
        CHECK_STR(buf, "-2.147.483.648"); /* can't negate in int32_t */
    }

    GROUP("fmt_dec1_de — rounding, half away from zero");
    {
        char buf[64];

        fmt_dec1_de(12.35f, buf, sizeof buf);
        CHECK_STR(buf, "12,4");

        fmt_dec1_de(1234.5f, buf, sizeof buf);
        CHECK_STR(buf, "1.234,5");

        fmt_dec1_de(0.0f, buf, sizeof buf);
        CHECK_STR(buf, "0,0");           /* zero */

        fmt_dec1_de(-12.35f, buf, sizeof buf);
        CHECK_STR(buf, "-12,4");         /* negative */

        /* .05-boundary rounding: each of these is stored as a float
         * fractionally off its decimal value (see fmt_de.c), so this
         * also exercises the epsilon nudge. */
        fmt_dec1_de(0.05f, buf, sizeof buf);
        CHECK_STR(buf, "0,1");
        fmt_dec1_de(-0.05f, buf, sizeof buf);
        CHECK_STR(buf, "-0,1");
        fmt_dec1_de(0.15f, buf, sizeof buf);
        CHECK_STR(buf, "0,2");
        fmt_dec1_de(0.25f, buf, sizeof buf);
        CHECK_STR(buf, "0,3");
        fmt_dec1_de(-0.25f, buf, sizeof buf);
        CHECK_STR(buf, "-0,3");
        fmt_dec1_de(0.35f, buf, sizeof buf);
        CHECK_STR(buf, "0,4");
        fmt_dec1_de(-0.35f, buf, sizeof buf);
        CHECK_STR(buf, "-0,4");
        fmt_dec1_de(0.45f, buf, sizeof buf);
        CHECK_STR(buf, "0,5");
        fmt_dec1_de(12.34f, buf, sizeof buf);
        CHECK_STR(buf, "12,3");          /* not a boundary: must not round up */
    }

    GROUP("fmt_distance_km / fmt_altitude_m");
    {
        char buf[64];

        fmt_distance_km(10.0f, buf, sizeof buf);
        CHECK_STR(buf, "18,5 km");

        fmt_distance_km(0.0f, buf, sizeof buf);
        CHECK_STR(buf, "0,0 km");

        fmt_altitude_m(1000, buf, sizeof buf);
        CHECK_STR(buf, "305 m");

        fmt_altitude_m(30000, buf, sizeof buf);
        CHECK_STR(buf, "9.144 m");

        fmt_altitude_m(ALT_GROUND, buf, sizeof buf);
        CHECK_STR(buf, "am Boden");

        fmt_altitude_m(ALT_UNKNOWN, buf, sizeof buf);
        CHECK_STR(buf, "\xE2\x80\x94");   /* U+2014 EM DASH, byte-for-byte */
    }

    GROUP("compass_de_abbr — spot checks");
    CHECK_STR(compass_de_abbr(0.0f), "N");
    CHECK_STR(compass_de_abbr(348.75f), "N");     /* low edge, from spec */
    CHECK_STR(compass_de_abbr(359.99f), "N");
    CHECK_STR(compass_de_abbr(-11.25f), "N");     /* negative, same as 348.75 */
    CHECK_STR(compass_de_abbr(360.0f), "N");      /* wraps to 0 */
    CHECK_STR(compass_de_abbr(725.0f), "N");      /* >360, wraps to 5 */
    CHECK_STR(compass_de_abbr(180.0f), "S");
    CHECK_STR(compass_de_abbr(90.0f), "O");       /* Ost, not East */

    GROUP("compass_de_abbr — every 16-point boundary");
    for (int k = 0; k < 16; k++) {
        float center = (float)k * 22.5f;
        float low    = center - 11.25f;   /* inclusive low edge of sector k */
        float high   = center + 11.25f;   /* == low edge of sector k+1 */

        CHECK_STR(compass_de_abbr(center), ABBR16[k]);
        CHECK_STR(compass_de_abbr(low), ABBR16[k]);
        CHECK_STR(compass_de_abbr(high), ABBR16[(k + 1) % 16]);
    }

    GROUP("compass_de_word — spot checks");
    CHECK_STR(compass_de_word(0.0f), "Norden");
    CHECK_STR(compass_de_word(45.0f), "Nordosten");
    CHECK_STR(compass_de_word(90.0f), "Osten");
    CHECK_STR(compass_de_word(135.0f), "Südosten");
    CHECK_STR(compass_de_word(180.0f), "Süden");
    CHECK_STR(compass_de_word(225.0f), "Südwesten");
    CHECK_STR(compass_de_word(270.0f), "Westen");
    CHECK_STR(compass_de_word(315.0f), "Nordwesten");

    GROUP("compass_de_word — every 8-point boundary");
    for (int k = 0; k < 8; k++) {
        float center = (float)k * 45.0f;
        float low    = center - 22.5f;
        float high   = center + 22.5f;

        CHECK_STR(compass_de_word(center), WORD8[k]);
        CHECK_STR(compass_de_word(low), WORD8[k]);
        CHECK_STR(compass_de_word(high), WORD8[(k + 1) % 8]);
    }

    GROUP("compass_de_adv — spot checks");
    CHECK_STR(compass_de_adv(0.0f), "nördlich");
    CHECK_STR(compass_de_adv(45.0f), "nordöstlich");
    CHECK_STR(compass_de_adv(90.0f), "östlich");
    CHECK_STR(compass_de_adv(135.0f), "südöstlich");
    CHECK_STR(compass_de_adv(180.0f), "südlich");
    CHECK_STR(compass_de_adv(225.0f), "südwestlich");
    CHECK_STR(compass_de_adv(270.0f), "westlich");
    CHECK_STR(compass_de_adv(315.0f), "nordwestlich");

    /* Umlauts byte-for-byte: a mangled ö renders as nothing at all on
     * this panel, so "nordöstlich" would silently become "nordstlich". */
    CHECK_STR(compass_de_adv(45.0f), "nord\xC3\xB6stlich");
    CHECK_STR(compass_de_adv(135.0f), "s\xC3\xBC" "d\xC3\xB6stlich");

    GROUP("compass_de_adv — normalisation");
    CHECK_STR(compass_de_adv(-22.5f), "nördlich");    /* negative, == 337.5 */
    CHECK_STR(compass_de_adv(-45.0f), "nordwestlich");  /* negative, == 315 */
    CHECK_STR(compass_de_adv(360.0f), "nördlich");    /* wraps to 0 */
    CHECK_STR(compass_de_adv(725.0f), "nördlich");    /* >360, wraps to 5 */
    CHECK_STR(compass_de_adv(405.0f), "nordöstlich"); /* >360, wraps to 45 */

    GROUP("compass_de_adv — every 8-point boundary");
    for (int k = 0; k < 8; k++) {
        float center = (float)k * 45.0f;
        float low    = center - 22.5f;
        float high   = center + 22.5f;

        CHECK_STR(compass_de_adv(center), ADV8[k]);
        CHECK_STR(compass_de_adv(low), ADV8[k]);
        CHECK_STR(compass_de_adv(high), ADV8[(k + 1) % 8]);
    }

    GROUP("compass_de_adv — an adverb, never the noun table");
    for (int k = 0; k < 8; k++) {
        /* The likeliest silent regression here is the noun table getting
         * copy-pasted into the adverb slot — which puts "16,8 km Nordosten"
         * back on the panel. Two cheap invariants catch it for all eight. */
        float deg = (float)k * 45.0f;
        const char *adv  = compass_de_adv(deg);
        const char *noun = compass_de_word(deg);

        CHECK(strcmp(adv, noun) != 0);

        size_t len = strlen(adv);
        CHECK(len > 4 && strcmp(adv + len - 4, "lich") == 0);
    }

    GROUP("weekday_de / month_de");
    {
        static const char *const wd[7] = {
            "Sonntag", "Montag", "Dienstag", "Mittwoch",
            "Donnerstag", "Freitag", "Samstag",
        };
        for (int i = 0; i < 7; i++) CHECK_STR(weekday_de(i), wd[i]);

        static const char *const mo[12] = {
            "Jänner", "Februar", "März", "April", "Mai", "Juni",
            "Juli", "August", "September", "Oktober", "November", "Dezember",
        };
        for (int i = 0; i < 12; i++) CHECK_STR(month_de(i), mo[i]);

        /* Austrian, byte-for-byte — the classic locale bug is exactly
         * "Januar" instead of "Jänner", or a mangled ä. */
        CHECK_STR(month_de(0), "J\xC3\xA4nner");
        CHECK_STR(month_de(2), "M\xC3\xA4rz");
    }

    GROUP("fmt_date_de / fmt_time_de");
    {
        struct tm t = {0};
        t.tm_wday = 5;      /* Friday */
        t.tm_mday = 18;
        t.tm_mon  = 8;      /* September, 0-indexed */
        t.tm_year = 126;    /* 2026 - 1900 */
        t.tm_hour = 9;
        t.tm_min  = 47;

        char buf[64];
        fmt_date_de(&t, buf, sizeof buf);
        CHECK_STR(buf, "Freitag, 18. September 2026");

        fmt_time_de(&t, buf, sizeof buf);
        CHECK_STR(buf, "09:47");
    }

    GROUP("buffer-too-small truncation safety");
    {
        /* fmt_int_de into a 4-byte buffer inside a larger sentinel-filled
         * array: bytes at and beyond index 4 must never be touched. */
        char arr[8];
        memset(arr, 0x7F, sizeof arr);
        size_t w = fmt_int_de(1234567, arr, 4);
        CHECK_INT(w, 3);
        CHECK(arr[w] == '\0');
        CHECK_STR(arr, "1.2");                 /* first 3 chars of "1.234.567" */
        CHECK((unsigned char)arr[4] == 0x7Fu);
        CHECK((unsigned char)arr[5] == 0x7Fu);
        CHECK((unsigned char)arr[6] == 0x7Fu);
        CHECK((unsigned char)arr[7] == 0x7Fu);

        /* n == 0: not even a NUL may be written. */
        char arr2[4] = { (char)1, (char)2, (char)3, (char)4 };
        size_t w2 = fmt_int_de(5, arr2, 0);
        CHECK_INT(w2, 0);
        CHECK((unsigned char)arr2[0] == 1u);

        /* Same discipline for fmt_dec1_de. */
        char arr3[8];
        memset(arr3, 0x7F, sizeof arr3);
        size_t w3 = fmt_dec1_de(1234.5f, arr3, 5);
        CHECK_INT(w3, 4);
        CHECK(arr3[w3] == '\0');
        CHECK_STR(arr3, "1.23");                /* first 4 chars of "1.234,5" */
        CHECK((unsigned char)arr3[5] == 0x7Fu);
        CHECK((unsigned char)arr3[6] == 0x7Fu);
        CHECK((unsigned char)arr3[7] == 0x7Fu);

        /* And fmt_date_de, which composes several formatters. */
        struct tm t = {0};
        t.tm_wday = 5; t.tm_mday = 18; t.tm_mon = 8; t.tm_year = 126;
        char arr4[10];
        memset(arr4, 0x7F, sizeof arr4);
        size_t w4 = fmt_date_de(&t, arr4, 6);
        CHECK_INT(w4, 5);
        CHECK(arr4[w4] == '\0');
        CHECK_STR(arr4, "Freit");               /* first 5 chars, truncated mid-word */
        CHECK((unsigned char)arr4[6] == 0x7Fu);
        CHECK((unsigned char)arr4[7] == 0x7Fu);
        CHECK((unsigned char)arr4[8] == 0x7Fu);
        CHECK((unsigned char)arr4[9] == 0x7Fu);
    }

    return test_summary();
}
