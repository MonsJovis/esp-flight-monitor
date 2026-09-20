/* Settings model: presets, the dim schedule, and the clamping that stands
 * between a corrupt NVS blob and a black panel. */
#include "test_util.h"
#include <stdint.h>
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

    /* ==================================================================
     * The NVS blob
     *
     * This is the code that decides whether a device that has just taken a
     * firmware update still knows where it is. There is exactly one such
     * device, it is not in the room, and for half the year it is 9,000 km
     * away — so the migration path is tested harder than anything else in
     * this file.
     * ================================================================== */

    /* The version 1 layout, transcribed. This is deliberately a SECOND copy
     * of the struct as it stood before the place search added two fields:
     * settings.c has its own, and if the two ever disagree that is the test
     * doing its job. It is not #included from anywhere, because a shared
     * definition would move when settings_t moves and prove nothing. */
    typedef struct {
        location_preset_t preset;
        double            custom_lat, custom_lon;
        int               radius_nm;
        int               brightness_pct;
        bool              auto_dim;
        int               dim_from_hour;
        int               dim_to_hour;
        int               dim_brightness_pct;
    } v1_settings_t;

    typedef struct {
        uint32_t      version;
        v1_settings_t s;
    } v1_blob_t;

    GROUP("blob: a round trip through the current layout");
    {
        settings_t in, out;
        settings_defaults(&in);
        in.preset         = LOC_CUSTOM;
        in.custom_lat     = 47.2683;
        in.custom_lon     = 11.4008;
        in.radius_nm      = 45;
        in.brightness_pct = 70;
        in.auto_dim       = false;
        in.dim_from_hour  = 23;
        in.dim_to_hour    = 6;
        snprintf(in.custom_label, sizeof in.custom_label, "%s", "Innsbruck \xC2\xB7 Tirol");
        snprintf(in.custom_tz, sizeof in.custom_tz, "%s", "CET-1CEST,M3.5.0,M10.5.0/3");

        unsigned char buf[512];
        CHECK(settings_blob_size() <= sizeof buf);
        settings_encode_blob(&in, buf);

        CHECK(settings_decode_blob(buf, settings_blob_size(), &out));
        CHECK_INT(out.preset, LOC_CUSTOM);
        CHECK_NEAR(out.custom_lat, 47.2683, 0.0001);
        CHECK_NEAR(out.custom_lon, 11.4008, 0.0001);
        CHECK_INT(out.radius_nm, 45);
        CHECK_INT(out.brightness_pct, 70);
        CHECK_INT(out.auto_dim, 0);
        CHECK_INT(out.dim_from_hour, 23);
        CHECK_INT(out.dim_to_hour, 6);
        CHECK_STR(out.custom_label, "Innsbruck \xC2\xB7 Tirol");
        CHECK_STR(out.custom_tz, "CET-1CEST,M3.5.0,M10.5.0/3");
    }

    GROUP("blob: a version 1 blob keeps everything it had");
    {
        /* What a device flashed in M6 has sitting in NVS right now. */
        v1_blob_t old = {
            .version = 1u,
            .s = {
                .preset             = LOC_PATTAYA,
                .custom_lat         = 47.6691,
                .custom_lon         = 15.9303,
                .radius_nm          = 60,
                .brightness_pct     = 80,
                .auto_dim           = true,
                .dim_from_hour      = 21,
                .dim_to_hour        = 8,
                .dim_brightness_pct = 15,
            },
        };

        settings_t out;
        CHECK(settings_decode_blob(&old, sizeof old, &out));

        /* Every field he ever set, still set. The failure this guards against
         * is not a crash — it is a device that comes back up in Austria while
         * standing in Thailand, with the brightness reset, and nothing on
         * screen to say why. */
        CHECK_INT(out.preset, LOC_PATTAYA);
        CHECK_INT(out.radius_nm, 60);
        CHECK_INT(out.brightness_pct, 80);
        CHECK_INT(out.auto_dim, 1);
        CHECK_INT(out.dim_from_hour, 21);
        CHECK_INT(out.dim_to_hour, 8);
        CHECK_INT(out.dim_brightness_pct, 15);
        CHECK_NEAR(out.custom_lat, 47.6691, 0.0001);
        CHECK_NEAR(out.custom_lon, 15.9303, 0.0001);

        /* And the two fields version 1 never had are EMPTY, not garbage read
         * off the end of a shorter buffer. Empty is a state the settings card
         * has a line for; garbage is a label full of stack. */
        CHECK_STR(out.custom_label, "");
        CHECK_STR(out.custom_tz, "");
        /* Which means the clock falls back to Gloggnitz rather than to UTC. */
        out.preset = LOC_CUSTOM;
        CHECK_STR(settings_tz(&out), "CET-1CEST,M3.5.0,M10.5.0/3");
    }

    GROUP("blob: the two layouts are distinguishable at all");
    {
        /* The decoder tells them apart by SIZE. If the new fields had been
         * added in a way that left the struct the same size, a v1 blob would
         * be read as a v2 one and the label would be whatever those bytes
         * happened to be. */
        CHECK(settings_blob_size() != sizeof(v1_blob_t));
        CHECK(settings_blob_read_size() >= settings_blob_size());
        CHECK(settings_blob_read_size() >= sizeof(v1_blob_t));
    }

    GROUP("blob: nothing else is accepted");
    {
        settings_t out;
        unsigned char buf[512];

        /* Right size, wrong version word: a downgrade, or a future layout
         * this firmware has never heard of. Defaults, not a reinterpretation. */
        settings_defaults(&out);
        settings_encode_blob(&out, buf);
        buf[0] = 99;
        CHECK(!settings_decode_blob(buf, settings_blob_size(), &out));
        CHECK_INT(out.preset, LOC_GLOGGNITZ);
        CHECK_INT(out.radius_nm, 30);

        /* Right version word, wrong size: a truncated or torn write. */
        settings_encode_blob(&out, buf);
        CHECK(!settings_decode_blob(buf, settings_blob_size() - 1, &out));
        CHECK(!settings_decode_blob(buf, settings_blob_size() + 1, &out));
        CHECK(!settings_decode_blob(buf, 0, &out));
        CHECK(!settings_decode_blob(NULL, settings_blob_size(), &out));
        CHECK(!settings_decode_blob(buf, sizeof(v1_blob_t), &out));  /* v1 size, v2 version */
        CHECK(!settings_decode_blob(buf, settings_blob_size(), NULL));
    }

    GROUP("blob: a corrupt blob still cannot produce a black screen");
    {
        /* Random bytes at exactly the right length and with the right version
         * word — the one case that gets past both checks. Everything in it is
         * nonsense, and settings_sanitise() inside the decoder is what stops
         * that nonsense reaching the backlight or the poll radius. */
        settings_t out;
        unsigned char buf[512];
        memset(buf, 0x7F, sizeof buf);
        uint32_t v = 2u;
        memcpy(buf, &v, sizeof v);

        CHECK(settings_decode_blob(buf, settings_blob_size(), &out));
        CHECK(out.brightness_pct >= 10 && out.brightness_pct <= 100);
        CHECK(out.radius_nm >= 10 && out.radius_nm <= 100);
        CHECK(out.preset >= 0 && out.preset < LOC_COUNT);
        CHECK(out.dim_from_hour >= 0 && out.dim_from_hour <= 23);
        CHECK(out.dim_brightness_pct <= out.brightness_pct);
        /* And the two strings are terminated, whatever was in those bytes. */
        CHECK_INT(strlen(out.custom_label), SETTINGS_LABEL_LEN - 1);
        CHECK(strlen(out.custom_tz) <= SETTINGS_TZ_LEN - 1);
    }

    return test_summary();
}
