/* Settings model: presets, the dim schedule, and the clamping that stands
 * between a corrupt NVS blob and a black panel. */
#include "test_util.h"
#include "settings.h"

int main(void)
{
    GROUP("presets carry a name and a timezone, and never fall off the end");
    {
        CHECK_STR(location_name(LOC_GLOGGNITZ), "Gloggnitz");
        CHECK_STR(location_name(LOC_PATTAYA),   "Pattaya");
        CHECK(location_name(LOC_CUSTOM)[0] != '\0');

        /* The timezone is bound to the place because he never sets a clock
         * (AGENTS.md §6). Austria has DST rules; Thailand has none. */
        CHECK(strstr(location_tz(LOC_GLOGGNITZ), "CET") != NULL);
        CHECK(strstr(location_tz(LOC_GLOGGNITZ), "M3.5.0") != NULL);
        CHECK_STR(location_tz(LOC_PATTAYA), "ICT-7");
        CHECK(strstr(location_tz(LOC_PATTAYA), "M3.") == NULL);  /* no DST */

        /* A nonsense preset must degrade, not index out of bounds. */
        CHECK(location_name((location_preset_t)99) != NULL);
        CHECK(location_tz((location_preset_t)-1) != NULL);
    }

    GROUP("coordinates resolve, and never to 0,0");
    {
        settings_t s;
        settings_defaults(&s);
        double lat = 0, lon = 0;

        settings_coords(&s, &lat, &lon);
        CHECK_NEAR(lat, 47.6691, 0.0001);      /* Semmeringstraße 11 */
        CHECK_NEAR(lon, 15.9303, 0.0001);

        s.preset = LOC_PATTAYA;
        settings_coords(&s, &lat, &lon);
        CHECK_NEAR(lat, 12.9211, 0.0001);      /* 154 Thappraya Rd */
        CHECK_NEAR(lon, 100.8721, 0.0001);

        s.preset = LOC_CUSTOM;
        s.custom_lat = 51.5; s.custom_lon = -0.12;
        settings_coords(&s, &lat, &lon);
        CHECK_NEAR(lat, 51.5, 0.0001);
        CHECK_NEAR(lon, -0.12, 0.0001);

        /* A corrupt preset must not poll the middle of the Atlantic, which
         * would look like a broken device rather than a misconfigured one. */
        s.preset = (location_preset_t)77;
        settings_coords(&s, &lat, &lon);
        CHECK(!(lat == 0.0 && lon == 0.0));
        CHECK_NEAR(lat, 47.6691, 0.0001);
    }

    GROUP("auto-dim is on by default, and the window wraps midnight");
    {
        settings_t s;
        settings_defaults(&s);
        /* DESIGN.md §7: settled, not optional. Someone who never opens the
         * settings screen is exactly who this protects. */
        CHECK(s.auto_dim == true);
        CHECK_INT(s.dim_from_hour, 22);
        CHECK_INT(s.dim_to_hour, 7);

        /* 22:00-07:00 wraps midnight — the normal case, and the one an
         * inclusive-range implementation gets wrong. */
        CHECK_INT(settings_brightness_for_hour(&s, 22), s.dim_brightness_pct);
        CHECK_INT(settings_brightness_for_hour(&s, 23), s.dim_brightness_pct);
        CHECK_INT(settings_brightness_for_hour(&s,  0), s.dim_brightness_pct);
        CHECK_INT(settings_brightness_for_hour(&s,  6), s.dim_brightness_pct);
        CHECK_INT(settings_brightness_for_hour(&s,  7), s.brightness_pct);
        CHECK_INT(settings_brightness_for_hour(&s, 12), s.brightness_pct);
        CHECK_INT(settings_brightness_for_hour(&s, 21), s.brightness_pct);

        /* Switched off, it is always full brightness. */
        s.auto_dim = false;
        CHECK_INT(settings_brightness_for_hour(&s, 23), s.brightness_pct);

        /* A non-wrapping window still works. */
        s.auto_dim = true; s.dim_from_hour = 1; s.dim_to_hour = 5;
        CHECK_INT(settings_brightness_for_hour(&s, 0), s.brightness_pct);
        CHECK_INT(settings_brightness_for_hour(&s, 3), s.dim_brightness_pct);
        CHECK_INT(settings_brightness_for_hour(&s, 5), s.brightness_pct);

        /* An empty window dims nothing rather than everything. */
        s.dim_from_hour = 4; s.dim_to_hour = 4;
        for (int h = 0; h < 24; h++) {
            CHECK_INT(settings_brightness_for_hour(&s, h), s.brightness_pct);
        }

        /* Out-of-range hours must not dim by accident. */
        CHECK_INT(settings_brightness_for_hour(&s, -1), s.brightness_pct);
        CHECK_INT(settings_brightness_for_hour(&s, 24), s.brightness_pct);
    }

    GROUP("sanitise: a bad blob must never produce a black screen");
    {
        settings_t s;
        memset(&s, 0, sizeof s);          /* as if read from garbage */
        settings_sanitise(&s);
        /* The floor is the point: a device configurable to invisible gives him
         * no way back, because he cannot see the control that got him there. */
        CHECK(s.brightness_pct >= 10);
        CHECK(s.dim_brightness_pct >= 5);
        CHECK(s.radius_nm >= 10);
        CHECK(s.preset >= 0 && s.preset < LOC_COUNT);

        settings_defaults(&s);
        s.brightness_pct = 1000; s.radius_nm = 9999; s.dim_brightness_pct = -5;
        s.dim_from_hour = 99; s.dim_to_hour = -3;
        settings_sanitise(&s);
        CHECK_INT(s.brightness_pct, 100);
        CHECK_INT(s.radius_nm, 100);
        CHECK(s.dim_brightness_pct >= 5);
        CHECK(s.dim_from_hour >= 0 && s.dim_from_hour <= 23);
        CHECK(s.dim_to_hour   >= 0 && s.dim_to_hour   <= 23);

        /* "Dim" must never be brighter than normal, however it was stored. */
        settings_defaults(&s);
        s.brightness_pct = 30; s.dim_brightness_pct = 90;
        settings_sanitise(&s);
        CHECK(s.dim_brightness_pct <= s.brightness_pct);

        /* An out-of-range custom coordinate falls back rather than polling it. */
        settings_defaults(&s);
        s.preset = LOC_CUSTOM; s.custom_lat = 300.0; s.custom_lon = -9999.0;
        settings_sanitise(&s);
        CHECK(s.custom_lat >= -90.0 && s.custom_lat <= 90.0);
        CHECK(s.custom_lon >= -180.0 && s.custom_lon <= 180.0);

        /* NULL is not a crash. */
        settings_sanitise(NULL);
        settings_defaults(NULL);
        settings_coords(NULL, NULL, NULL);
        CHECK(1);
    }

    GROUP("defaults are the documented ones");
    {
        settings_t s;
        settings_defaults(&s);
        CHECK_INT(s.preset, LOC_GLOGGNITZ);
        /* 30 nm is ~55 km: what "overhead" means, and it keeps the payload
         * near 4 KB instead of 60 KB at 100 nm (AGENTS.md §6). */
        CHECK_INT(s.radius_nm, 30);
        CHECK_INT(s.brightness_pct, 100);
    }

    return test_summary();
}
