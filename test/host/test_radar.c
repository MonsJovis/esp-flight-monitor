/* The radar's three silent decisions (radar_logic.h, D76).
 *
 * Each of these fails without looking broken: a band threshold off by a factor
 * of 3.28 draws every airliner as though it were overhead, hysteresis the
 * wrong way round makes the magenta mark stick to an aircraft that has flown
 * away, and a trail that never forgets draws a line across the scope the first
 * time he comes back from the Liste.
 */
#include <string.h>

#include "test_util.h"
#include "radar_logic.h"

static aircraft_t mk(const char *hex, float dst_nm, float dir_deg, int32_t alt_ft)
{
    aircraft_t a;
    memset(&a, 0, sizeof a);
    snprintf(a.hex, sizeof a.hex, "%s", hex);
    a.dst_nm  = dst_nm;
    a.dir_deg = dir_deg;
    a.alt_ft  = alt_ft;
    return a;
}

int main(void)
{
    GROUP("altitude band: feet, not metres, and the edges where they are drawn");
    {
        CHECK_INT(radar_alt_band(1200),  RADAR_ALT_LOW);    /* circuit height */
        CHECK_INT(radar_alt_band(4999),  RADAR_ALT_LOW);
        CHECK_INT(radar_alt_band(5000),  RADAR_ALT_MID);
        CHECK_INT(radar_alt_band(19999), RADAR_ALT_MID);
        CHECK_INT(radar_alt_band(20000), RADAR_ALT_HIGH);
        CHECK_INT(radar_alt_band(37000), RADAR_ALT_HIGH);   /* cruise */
    }

    GROUP("no airborne altitude makes no size claim");
    {
        /* A taxiing airliner is not the one he hears, and must not be drawn
         * as though it were the lowest thing in the sky. */
        CHECK_INT(radar_alt_band(ALT_GROUND),  RADAR_ALT_MID);
        CHECK_INT(radar_alt_band(ALT_UNKNOWN), RADAR_ALT_MID);
        CHECK_INT(radar_alt_band(-50),         RADAR_ALT_MID);
    }

    GROUP("nearest: plain minimum when there is no history");
    {
        aircraft_t ac[] = { mk("aaa", 9.0f, 0, 3000), mk("bbb", 4.0f, 0, 3000),
                            mk("ccc", 6.0f, 0, 3000) };
        CHECK_INT(radar_pick_nearest(ac, 3, NULL), 1);
        CHECK_INT(radar_pick_nearest(ac, 3, ""), 1);
        CHECK_INT(radar_pick_nearest(ac, 3, "zzz"), 1);     /* prev has gone */
    }

    GROUP("nearest: skips aircraft with no distance, -1 when nobody has one");
    {
        aircraft_t ac[] = { mk("aaa", DST_UNKNOWN, 0, 0), mk("bbb", 7.0f, 0, 0) };
        CHECK_INT(radar_pick_nearest(ac, 2, NULL), 1);
        CHECK_INT(radar_pick_nearest(ac, 1, NULL), -1);
        CHECK_INT(radar_pick_nearest(ac, 0, NULL), -1);
        CHECK_INT(radar_pick_nearest(NULL, 2, NULL), -1);
    }

    GROUP("nearest: the incumbent keeps it through a near-tie");
    {
        /* bbb was nearest last time. aaa is now 5 % closer: not enough. */
        aircraft_t ac[] = { mk("aaa", 10.0f, 0, 0), mk("bbb", 10.5f, 0, 0) };
        CHECK_INT(radar_pick_nearest(ac, 2, "bbb"), 1);
        /* ...and without the history, aaa wins, so the rule is doing it. */
        CHECK_INT(radar_pick_nearest(ac, 2, NULL), 0);
    }

    GROUP("nearest: the incumbent loses it to a clear winner");
    {
        aircraft_t ac[] = { mk("aaa", 10.0f, 0, 0), mk("bbb", 11.5f, 0, 0) };
        CHECK_INT(radar_pick_nearest(ac, 2, "bbb"), 0);     /* 15 % is clear */
    }

    GROUP("nearest: the absolute floor covers aircraft right overhead");
    {
        /* At 0.3 nm a 10 % band is 55 m, which two aircraft circling the same
         * field would cross every poll. The 0.25 nm floor holds it. */
        aircraft_t ac[] = { mk("aaa", 0.30f, 0, 0), mk("bbb", 0.50f, 0, 0) };
        CHECK_INT(radar_pick_nearest(ac, 2, "bbb"), 1);
        aircraft_t far[] = { mk("aaa", 0.30f, 0, 0), mk("bbb", 0.70f, 0, 0) };
        CHECK_INT(radar_pick_nearest(far, 2, "bbb"), 0);
    }

    radar_trails_t tr;

    GROUP("trail: the first sighting is one fix, and repeats inside a step add none");
    {
        radar_trails_reset(&tr);
        aircraft_t ac[] = { mk("aaa", 10.0f, 90.0f, 0) };
        radar_trails_observe(&tr, ac, 1, 1000);
        const radar_trail_t *t = radar_trails_find(&tr, "aaa");
        CHECK(t != NULL);
        CHECK_INT(t->count, 1);
        ac[0].dst_nm = 9.9f;
        radar_trails_observe(&tr, ac, 1, 1000 + RADAR_TRAIL_STEP_MS - 1);
        CHECK_INT(t->count, 1);
        CHECK_NEAR(t->fix[0].dst_nm, 10.0f, 1e-6);          /* not overwritten */
    }

    GROUP("trail: newest first, capped at RADAR_TRAIL_LEN");
    {
        radar_trails_reset(&tr);
        aircraft_t ac[] = { mk("aaa", 10.0f, 90.0f, 0) };
        for (int k = 0; k < RADAR_TRAIL_LEN + 3; k++) {
            ac[0].dst_nm = 10.0f - (float)k;
            radar_trails_observe(&tr, ac, 1, 1000 + (uint32_t)k * RADAR_TRAIL_STEP_MS);
        }
        const radar_trail_t *t = radar_trails_find(&tr, "aaa");
        CHECK_INT(t->count, RADAR_TRAIL_LEN);
        CHECK_NEAR(t->fix[0].dst_nm, 10.0f - (float)(RADAR_TRAIL_LEN + 2), 1e-6);
        CHECK_NEAR(t->fix[1].dst_nm, 10.0f - (float)(RADAR_TRAIL_LEN + 1), 1e-6);
        CHECK(t->fix[0].t_ms > t->fix[1].t_ms);
    }

    GROUP("trail: an aircraft that leaves is forgotten, and its slot reused");
    {
        radar_trails_reset(&tr);
        aircraft_t two[] = { mk("aaa", 10.0f, 0, 0), mk("bbb", 12.0f, 0, 0) };
        radar_trails_observe(&tr, two, 2, 0);
        aircraft_t one[] = { mk("bbb", 12.0f, 0, 0) };
        radar_trails_observe(&tr, one, 1, 100);
        CHECK(radar_trails_find(&tr, "aaa") == NULL);
        CHECK(radar_trails_find(&tr, "bbb") != NULL);

        /* A full sky of newcomers still fits — nobody is silently untracked. */
        aircraft_t full[MAX_AIRCRAFT];
        for (int i = 0; i < MAX_AIRCRAFT; i++) {
            char hex[8];
            snprintf(hex, sizeof hex, "n%02d", i);
            full[i] = mk(hex, 5.0f + (float)i, 0, 0);
        }
        radar_trails_observe(&tr, full, MAX_AIRCRAFT, 200);
        int found = 0;
        for (int i = 0; i < MAX_AIRCRAFT; i++) {
            found += radar_trails_find(&tr, full[i].hex) != NULL;
        }
        CHECK_INT(found, MAX_AIRCRAFT);
    }

    GROUP("trail: fixes age out, so a return from the Liste draws no long jump");
    {
        radar_trails_reset(&tr);
        aircraft_t ac[] = { mk("aaa", 10.0f, 0, 0) };
        radar_trails_observe(&tr, ac, 1, 0);
        ac[0].dst_nm = 20.0f;   /* five minutes later, somewhere else entirely */
        radar_trails_observe(&tr, ac, 1, 300000);
        const radar_trail_t *t = radar_trails_find(&tr, "aaa");
        CHECK_INT(t->count, 1);
        CHECK_NEAR(t->fix[0].dst_nm, 20.0f, 1e-6);
    }

    GROUP("trail: while stale, it fades out instead of piling up");
    {
        radar_trails_reset(&tr);
        aircraft_t ac[] = { mk("aaa", 10.0f, 0, 0) };
        radar_trails_observe(&tr, ac, 1, 0);
        radar_trails_age(&tr, RADAR_TRAIL_MAX_AGE_MS);        /* exactly at the edge: kept */
        CHECK_INT(radar_trails_find(&tr, "aaa")->count, 1);
        radar_trails_age(&tr, RADAR_TRAIL_MAX_AGE_MS + 1);
        CHECK_INT(radar_trails_find(&tr, "aaa")->count, 0);
    }

    GROUP("trail: a jump no aircraft could make starts the trail over");
    {
        radar_trails_reset(&tr);
        aircraft_t ac[] = { mk("aaa", 20.0f, 300.0f, 0) };
        radar_trails_observe(&tr, ac, 1, 0);
        radar_trails_observe(&tr, ac, 1, RADAR_TRAIL_STEP_MS);        /* 2 fixes */
        CHECK_INT(radar_trails_find(&tr, "aaa")->count, 2);
        ac[0].dir_deg = 60.0f;       /* ~35 nm away, 2 s later */
        radar_trails_observe(&tr, ac, 1, RADAR_TRAIL_STEP_MS + 2000);
        const radar_trail_t *t = radar_trails_find(&tr, "aaa");
        CHECK_INT(t->count, 1);
        CHECK_NEAR(t->fix[0].dir_deg, 60.0f, 1e-6);
        /* ...and straight back again inside one step: the ghost at 60 deg
         * must not survive either. This is the case that is easy to miss. */
        ac[0].dir_deg = 300.0f;
        radar_trails_observe(&tr, ac, 1, RADAR_TRAIL_STEP_MS + 4000);
        t = radar_trails_find(&tr, "aaa");
        CHECK_INT(t->count, 1);
        CHECK_NEAR(t->fix[0].dir_deg, 300.0f, 1e-6);
    }

    GROUP("trail: a fast jet's real movement is not mistaken for a jump");
    {
        /* 600 kt for 16 s is 2.7 nm — inside 800 kt x 16 s + 1 nm. */
        radar_trails_reset(&tr);
        aircraft_t ac[] = { mk("aaa", 20.0f, 0.0f, 0) };
        radar_trails_observe(&tr, ac, 1, 0);
        ac[0].dst_nm = 20.0f - 2.7f;
        radar_trails_observe(&tr, ac, 1, 16000);
        CHECK_INT(radar_trails_find(&tr, "aaa")->count, 2);
    }

    GROUP("trail: the millisecond clock wrapping is not a five-week-old fix");
    {
        radar_trails_reset(&tr);
        aircraft_t ac[] = { mk("aaa", 10.0f, 0, 0) };
        uint32_t before = 0xFFFFFFFFu - 1000u;
        radar_trails_observe(&tr, ac, 1, before);
        ac[0].dst_nm = 9.0f;
        radar_trails_observe(&tr, ac, 1, before + RADAR_TRAIL_STEP_MS);   /* wraps */
        CHECK_INT(radar_trails_find(&tr, "aaa")->count, 2);
    }

    return test_summary();
}
