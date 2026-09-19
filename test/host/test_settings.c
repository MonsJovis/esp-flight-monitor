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
        CHECK_STR(location_name(LOC_WIEN),      "Wien");
        CHECK(location_name(LOC_CUSTOM)[0] != '\0');

        /* Vienna is in Austria, so it gets Austria's clock — bound to the
         * place, never set separately. */
        CHECK_STR(location_tz(LOC_WIEN), location_tz(LOC_GLOGGNITZ));

        /* Meiselstraße 79, 1140 Wien (Penzing). Loose bounds: the point is
         * that it is in Vienna and not, say, in the Atlantic with its
         * coordinates transposed — which is exactly what a 0.0/0.0 row or a
         * lat/lon swap looks like, and both are silent on a panel that only
         * ever shows a distance. */
        {
            settings_t w;
            settings_defaults(&w);
            w.preset = LOC_WIEN;
            double lat = 0, lon = 0;
            settings_coords(&w, &lat, &lon);
            CHECK(lat > 48.0 && lat < 48.4);
            CHECK(lon > 16.1 && lon < 16.6);
            /* Vienna is north-east of Gloggnitz, ~65 km. If these two ever
             * compare the other way round, one of the rows has been edited
             * into the wrong hemisphere. */
            settings_t g;
            settings_defaults(&g);
            double glat = 0, glon = 0;
            settings_coords(&g, &glat, &glon);
            CHECK(lat > glat);
            CHECK(lon > glon);
        }

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

    GROUP("the enum is persisted, so its numbering is append-only");
    {
        /* These four numbers are written into NVS. Renumbering them moves a
         * device that is already in the field to a different city, silently,
         * on a firmware update — the distances simply stop making sense and
         * he has no way to know why. A new place goes on the END. */
        CHECK_INT((int)LOC_GLOGGNITZ, 0);
        CHECK_INT((int)LOC_PATTAYA,   1);
        CHECK_INT((int)LOC_CUSTOM,    2);
        CHECK_INT((int)LOC_WIEN,      3);
    }

    GROUP("display order is every preset exactly once, escape hatch last");
    {
        /* The screen renders this order, not the enum's, which is what lets
         * the enum stay append-only. It has to be a permutation: a duplicate
         * means one place is unreachable by tap, and a gap means a card that
         * selects nothing. */
        int seen[LOC_COUNT];
        for (int i = 0; i < LOC_COUNT; i++) seen[i] = 0;
        for (int i = 0; i < LOC_COUNT; i++) {
            location_preset_t p = location_display_order(i);
            CHECK(p >= 0 && p < LOC_COUNT);
            /* CHECK records and CONTINUES. Without this guard, the very
             * failure above is followed by seen[p]++ writing off the end of
             * a stack array — so the one run that has something to report
             * corrupts the stack instead of reporting it. */
            if (p >= 0 && p < LOC_COUNT) {
                seen[p]++;
            }
        }
        for (int i = 0; i < LOC_COUNT; i++) CHECK_INT(seen[i], 1);

        /* "Eigener Ort" is the advanced escape hatch (AGENTS.md §6), so it
         * sits below the places he actually taps. */
        CHECK(location_display_order(LOC_COUNT - 1) == LOC_CUSTOM);
        CHECK(location_display_order(0) == LOC_GLOGGNITZ);

        /* Out of range degrades rather than reading off the end. */
        CHECK(location_display_order(-1)  == LOC_GLOGGNITZ);
        CHECK(location_display_order(999) == LOC_GLOGGNITZ);
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

        /* The two brightness levels, pinned to their actual values rather
         * than read back out of the same struct the assertions compare
         * against. Every check below used to read both sides from this one
         * settings_defaults() call, which made the whole group tautological:
         * setting the default dim_brightness_pct to 100 satisfied all
         * fourteen at once, Nachtabsenkung quietly stopped lowering anything
         * on the device, and the suite reported 0 failed. */
        CHECK_INT(s.brightness_pct, 100);
        CHECK_INT(s.dim_brightness_pct, 25);
        /* And the property that makes the feature a feature. Whatever the two
         * numbers become, dim has to be dimmer. */
        CHECK(s.dim_brightness_pct < s.brightness_pct);

        /* 22:00-07:00 wraps midnight — the normal case, and the one an
         * inclusive-range implementation gets wrong. Literal 25 / 100, not
         * s.dim_brightness_pct / s.brightness_pct, for the reason above. */
        CHECK_INT(settings_brightness_for_hour(&s, 22), 25);
        CHECK_INT(settings_brightness_for_hour(&s, 23), 25);
        CHECK_INT(settings_brightness_for_hour(&s,  0), 25);
        CHECK_INT(settings_brightness_for_hour(&s,  6), 25);
        CHECK_INT(settings_brightness_for_hour(&s,  7), 100);
        CHECK_INT(settings_brightness_for_hour(&s, 12), 100);
        CHECK_INT(settings_brightness_for_hour(&s, 21), 100);

        /* Switched off, it is always full brightness. */
        s.auto_dim = false;
        CHECK_INT(settings_brightness_for_hour(&s, 23), 100);

        /* A non-wrapping window still works. */
        s.auto_dim = true; s.dim_from_hour = 1; s.dim_to_hour = 5;
        CHECK_INT(settings_brightness_for_hour(&s, 0), 100);
        CHECK_INT(settings_brightness_for_hour(&s, 3), 25);
        CHECK_INT(settings_brightness_for_hour(&s, 5), 100);

        /* An empty window dims nothing rather than everything. */
        s.dim_from_hour = 4; s.dim_to_hour = 4;
        for (int h = 0; h < 24; h++) {
            CHECK_INT(settings_brightness_for_hour(&s, h), 100);
        }

        /* Out-of-range hours must not dim by accident. */
        CHECK_INT(settings_brightness_for_hour(&s, -1), 100);
        CHECK_INT(settings_brightness_for_hour(&s, 24), 100);

        /* The two levels are still whatever they were asked to be, not the
         * defaults: a dim level that only ever equals 25 would pass
         * everything above while ignoring the setting he actually changed. */
        settings_defaults(&s);
        s.brightness_pct = 80; s.dim_brightness_pct = 15;
        CHECK_INT(settings_brightness_for_hour(&s, 23), 15);
        CHECK_INT(settings_brightness_for_hour(&s, 12), 80);
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

        /* An out-of-range custom coordinate falls back rather than polling it.
         *
         * Asserting only "now in range" does not say that. Clamping 300.0 to
         * 90.0 and -9999.0 to -180.0 satisfies a range check perfectly, and
         * leaves the device politely polling the North Pole in the Bering
         * Sea — a place with no traffic, which looks exactly like a broken
         * radar. The value has to be Gloggnitz. */
        settings_defaults(&s);
        s.preset = LOC_CUSTOM; s.custom_lat = 300.0; s.custom_lon = -9999.0;
        settings_sanitise(&s);
        CHECK_NEAR(s.custom_lat, 47.6691, 0.0001);   /* Semmeringstraße 11 */
        CHECK_NEAR(s.custom_lon, 15.9303, 0.0001);
        CHECK(s.custom_lat >= -90.0 && s.custom_lat <= 90.0);
        CHECK(s.custom_lon >= -180.0 && s.custom_lon <= 180.0);
        /* And the coordinates the device would actually poll follow it. */
        {
            double flat = 0, flon = 0;
            settings_coords(&s, &flat, &flon);
            CHECK_NEAR(flat, 47.6691, 0.0001);
            CHECK_NEAR(flon, 15.9303, 0.0001);
        }

        /* A custom coordinate that IS valid must survive untouched, or the
         * fallback above would be indistinguishable from "always Gloggnitz". */
        settings_defaults(&s);
        s.preset = LOC_CUSTOM; s.custom_lat = 12.9211; s.custom_lon = 100.8721;
        settings_sanitise(&s);
        CHECK_NEAR(s.custom_lat, 12.9211, 0.0001);
        CHECK_NEAR(s.custom_lon, 100.8721, 0.0001);

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
        CHECK_INT(s.dim_brightness_pct, 25);
        CHECK(s.dim_brightness_pct < s.brightness_pct);
    }

    return test_summary();
}
