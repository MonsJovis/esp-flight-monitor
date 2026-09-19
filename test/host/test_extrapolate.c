/* Carrying an aircraft forward between polls.
 *
 * The arithmetic is small and the failure modes are all silent: a swapped
 * atan2 argument order mirrors the whole sky about the north-east diagonal,
 * and an hours/seconds slip moves an airliner across Austria between two
 * redraws. Neither would look obviously wrong on a 480 px scope, which is why
 * this is tested here rather than by squinting at the panel.
 */
#include <math.h>
#include <string.h>

#include "test_util.h"
#include "extrapolate.h"

static aircraft_t mk(float dst_nm, float dir_deg, float track_deg, int gs_kt)
{
    aircraft_t a;
    memset(&a, 0, sizeof a);
    snprintf(a.hex, sizeof a.hex, "abc123");
    a.dst_nm = dst_nm;
    a.dir_deg = dir_deg;
    a.track_deg = track_deg;
    a.gs_kt = gs_kt;
    a.has_track = true;
    a.alt_ft = 10000;
    return a;
}

int main(void)
{
    GROUP("flying straight at the house closes the distance");
    {
        /* 10 nm due north, tracking due south at 600 kt: 1 nm per 6 s. */
        aircraft_t a = mk(10.0f, 0.0f, 180.0f, 600), o;
        CHECK(aircraft_extrapolate(&a, 6.0f, &o));
        CHECK_NEAR(o.dst_nm, 9.0f, 0.001);
        CHECK_NEAR(o.dir_deg, 0.0f, 0.01);      /* still due north */
    }

    GROUP("flying away opens it, by the same amount");
    {
        aircraft_t a = mk(10.0f, 0.0f, 0.0f, 600), o;
        CHECK(aircraft_extrapolate(&a, 6.0f, &o));
        CHECK_NEAR(o.dst_nm, 11.0f, 0.001);
        CHECK_NEAR(o.dir_deg, 0.0f, 0.01);
    }

    GROUP("knots are nautical miles per HOUR");
    {
        /* The conversion that cannot be got wrong quietly: at 360 kt an
         * aircraft covers exactly 0.1 nm per second. Out by 60 and it crosses
         * the country between two redraws; out by 3600 and it never moves. */
        aircraft_t a = mk(50.0f, 0.0f, 0.0f, 360), o;
        CHECK(aircraft_extrapolate(&a, 10.0f, &o));
        CHECK_NEAR(o.dst_nm, 51.0f, 0.001);
    }

    GROUP("bearing is a COMPASS bearing, measured from north through east");
    {
        /* 10 nm due east, tracking north. After 1 nm of travel the aircraft
         * is at (east 10, north 1), which is a bearing just under 90 deg --
         * NOT just over. Swap the atan2 arguments and this mirrors. */
        aircraft_t a = mk(10.0f, 90.0f, 0.0f, 600), o;
        CHECK(aircraft_extrapolate(&a, 6.0f, &o));
        CHECK(o.dir_deg < 90.0f);
        CHECK(o.dir_deg > 80.0f);
        CHECK_NEAR(o.dir_deg, 84.289f, 0.01);
        CHECK_NEAR(o.dst_nm, sqrtf(101.0f), 0.001);

        /* And the mirror image, to pin the sign on the other side. */
        aircraft_t b = mk(10.0f, 270.0f, 0.0f, 600), p;
        CHECK(aircraft_extrapolate(&b, 6.0f, &p));
        CHECK_NEAR(p.dir_deg, 360.0f - 84.289f, 0.01);
    }

    GROUP("the result is always a normalised bearing");
    {
        for (int dir = 0; dir < 360; dir += 7) {
            for (int trk = 0; trk < 360; trk += 11) {
                aircraft_t a = mk(8.0f, (float)dir, (float)trk, 300), o;
                aircraft_extrapolate(&a, 12.0f, &o);
                CHECK(o.dir_deg >= 0.0f && o.dir_deg < 360.0f);
                CHECK(o.dst_nm >= 0.0f);
                CHECK(isfinite(o.dst_nm) && isfinite(o.dir_deg));
            }
        }
    }

    GROUP("a full lap returns the aircraft to where it started");
    {
        /* Twelve 30-degree steps of a circle flown around the house: the
         * distance must come back. Catches a drift that a single step hides. */
        aircraft_t a = mk(20.0f, 0.0f, 90.0f, 600);
        float start = a.dst_nm;
        for (int i = 0; i < 12; i++) {
            a.track_deg = a.dir_deg + 90.0f;    /* always tangential */
            if (a.track_deg >= 360.0f) a.track_deg -= 360.0f;
            aircraft_extrapolate(&a, 10.0f, &a); /* aliasing is allowed */
        }
        CHECK(a.dst_nm > start);                 /* a polygon bulges outward */
        CHECK(a.dst_nm < start * 1.05f);         /* but only slightly */
    }

    GROUP("refuses to invent a position, and leaves the mark where it was");
    {
        aircraft_t base = mk(10.0f, 45.0f, 180.0f, 400), o;

        aircraft_t no_track = base; no_track.has_track = false;
        CHECK(!aircraft_extrapolate(&no_track, 6.0f, &o));
        CHECK_NEAR(o.dst_nm, 10.0f, 0.001);      /* unchanged, not zeroed */
        CHECK_NEAR(o.dir_deg, 45.0f, 0.001);

        aircraft_t parked = base; parked.gs_kt = 0;
        CHECK(!aircraft_extrapolate(&parked, 6.0f, &o));
        CHECK_NEAR(o.dst_nm, 10.0f, 0.001);

        aircraft_t nodist = base; nodist.dst_nm = DST_UNKNOWN;
        CHECK(!aircraft_extrapolate(&nodist, 6.0f, &o));
        CHECK(o.dst_nm == DST_UNKNOWN);          /* sentinel survives intact */

        CHECK(!aircraft_extrapolate(&base, 0.0f, &o));
        CHECK(!aircraft_extrapolate(&base, -5.0f, &o));
        CHECK(!aircraft_extrapolate(&base, NAN, &o));
        CHECK(!aircraft_extrapolate(&base, INFINITY, &o));
        CHECK(!aircraft_extrapolate(NULL, 6.0f, &o));
        CHECK(!aircraft_extrapolate(&base, 6.0f, NULL));
    }

    GROUP("stops extrapolating once the fix is too old");
    {
        aircraft_t a = mk(10.0f, 0.0f, 180.0f, 600), o;
        CHECK(aircraft_extrapolate(&a, EXTRAPOLATE_MAX_AGE_S, &o));
        CHECK(!aircraft_extrapolate(&a, EXTRAPOLATE_MAX_AGE_S + 0.1f, &o));
        /* Past the limit the display goes still, which is the honest signal
         * that data has stopped arriving -- not a mark drawn confidently
         * somewhere the aircraft is not. */
        CHECK_NEAR(o.dst_nm, 10.0f, 0.001);
    }

    GROUP("everything else about the aircraft is carried through untouched");
    {
        aircraft_t a = mk(10.0f, 0.0f, 90.0f, 400), o;
        snprintf(a.flight, sizeof a.flight, "AUA123");
        snprintf(a.type, sizeof a.type, "A20N");
        a.alt_ft = 31000;
        CHECK(aircraft_extrapolate(&a, 12.0f, &o));
        CHECK_STR(o.flight, "AUA123");
        CHECK_STR(o.type, "A20N");
        CHECK_INT(o.alt_ft, 31000);
        CHECK_INT(o.gs_kt, 400);
        CHECK_NEAR(o.track_deg, 90.0f, 0.001);   /* track itself does not change */
    }

    return test_summary();
}
