/* main/power/battery_policy.c — the judgement half of the battery feature.
 *
 * Everything here runs without a cell, a PMIC or a board, which is the whole
 * reason the policy was split out of the driver: the interesting cases are a
 * flat battery, a lying fuel gauge and a blocked charger, and none of those
 * are states you can ask real hardware to be in on demand.
 */
#include "test_util.h"
#include "battery_policy.h"
#include "strings_de.h"

/* A reading that is plainly healthy, on USB, charging. Tests copy it and
 * change the one field they are about. */
static battery_raw_t healthy(void)
{
    battery_raw_t r = {
        .present   = true,
        .vbus_good = true,
        .chg_status = BAT_CHG_CC,
        .mv        = 3900,
        .gauge_pct = 70,
    };
    return r;
}

static battery_status_t eval1(const battery_raw_t *in)
{
    battery_status_t st;
    battery_eval(in, NULL, &st);
    return st;
}

static void test_ocv_curve(void)
{
    GROUP("open-circuit curve");

    /* Both ends clamp, and clamp hard: a reading off the end of the table is
     * not an excuse to extrapolate into a percentage that cannot exist. */
    CHECK_INT(battery_percent_from_mv(4400), 100);
    CHECK_INT(battery_percent_from_mv(4150), 100);
    /* Full is what the CHARGER calls full. axp2101.c terminates at 4.1 V, so
     * a curve topping out at the chemistry's 4.2 V would have printed
     * "95 % - voll geladen" on a cell that was finished charging. This check
     * is what stops the two drifting apart again. */
    CHECK_INT(battery_percent_from_mv(BAT_CHARGE_TARGET_MV), 100);
    CHECK_INT(battery_percent_from_mv(3200), 0);
    CHECK_INT(battery_percent_from_mv(2500), 0);
    CHECK_INT(battery_percent_from_mv(0), 0);
    CHECK_INT(battery_percent_from_mv(-100), 0);

    /* The table's own points come back exactly. If interpolation ever gets
     * an off-by-one these are the first things to move. */
    CHECK_INT(battery_percent_from_mv(4050), 90);
    CHECK_INT(battery_percent_from_mv(3950), 75);
    CHECK_INT(battery_percent_from_mv(3800), 50);
    CHECK_INT(battery_percent_from_mv(3650), 25);
    CHECK_INT(battery_percent_from_mv(3450), 8);

    /* Halfway along a segment is halfway along its percentage. */
    CHECK_INT(battery_percent_from_mv(3760), 43);   /* 3720..3800 -> 35..50 */
    CHECK_INT(battery_percent_from_mv(4075), 95);   /* 4050..4100 -> 90..100 */

    /* MONOTONIC ACROSS THE WHOLE RANGE. This is the property that actually
     * matters: a curve that dips by one percent somewhere in the middle
     * makes a resting cell look like it is recovering charge, and he would
     * be right to conclude the device is guessing. A per-segment rounding
     * that truncated instead of rounding broke exactly this. */
    int prev = battery_percent_from_mv(2500);
    for (int mv = 2500; mv <= 4400; mv++) {
        int p = battery_percent_from_mv(mv);
        CHECK(p >= prev);
        CHECK(p >= 0 && p <= 100);
        prev = p;
    }
}

static void test_states(void)
{
    GROUP("states");

    battery_raw_t r = healthy();
    CHECK_INT(eval1(&r).state, BAT_CHARGING);

    r.chg_status = BAT_CHG_PRE;
    CHECK_INT(eval1(&r).state, BAT_CHARGING);
    r.chg_status = BAT_CHG_CV;
    CHECK_INT(eval1(&r).state, BAT_CHARGING);
    r.chg_status = BAT_CHG_TRICKLE;
    CHECK_INT(eval1(&r).state, BAT_CHARGING);

    /* Termination reported outright. */
    r.chg_status = BAT_CHG_DONE;
    CHECK_INT(eval1(&r).state, BAT_FULL);

    /* And the case that had to be got right: a full cell whose charger has
     * dropped out of DONE back to "not charging". Same register value as a
     * charger that has been blocked, so the percentage is what separates
     * them. */
    r.chg_status = BAT_CHG_STOP;
    r.gauge_pct  = 99;
    CHECK_INT(eval1(&r).state, BAT_FULL);
    r.gauge_pct  = 90;
    CHECK_INT(eval1(&r).state, BAT_FULL);

    r.gauge_pct  = 89;
    CHECK_INT(eval1(&r).state, BAT_ON_USB);
    r.gauge_pct  = 40;
    CHECK_INT(eval1(&r).state, BAT_ON_USB);

    /* Unplugged. */
    r = healthy();
    r.vbus_good = false;
    r.chg_status = BAT_CHG_STOP;
    CHECK_INT(eval1(&r).state, BAT_ON_BATTERY);

    /* No cell fitted — the state every one of these devices is in today. */
    r = healthy();
    r.present = false;
    battery_status_t st = eval1(&r);
    CHECK_INT(st.state, BAT_ABSENT);
    CHECK_INT(st.percent, -1);          /* never "0 %" for "no battery" */
    CHECK_INT(st.low, false);
    CHECK_INT(st.critical, false);
    CHECK_INT(st.brightness_cap_pct, BAT_CAP_NORMAL_PCT);

    /* A NULL reading is the same as no battery, not a crash. */
    battery_eval(NULL, NULL, &st);
    CHECK_INT(st.state, BAT_ABSENT);
    CHECK_INT(st.percent, -1);
}

static void test_gauge_plausibility(void)
{
    GROUP("gauge vs curve");

    battery_raw_t r = healthy();

    /* A gauge in range is believed, even where it disagrees with the curve:
     * it has seen the cell charge and discharge and the curve has not. */
    r.mv = 3900; r.gauge_pct = 55;
    CHECK_INT(eval1(&r).percent, 55);

    /* 0 is not an answer. A PMIC that has just come up reports it while the
     * cell is plainly at 3.9 V, and printing "0 %" there is the kind of wrong
     * that makes every other number on the panel suspect. */
    r.gauge_pct = 0;
    CHECK_INT(eval1(&r).percent, battery_percent_from_mv(3900));

    /* Neither is anything above 100. */
    r.gauge_pct = 101;
    CHECK_INT(eval1(&r).percent, battery_percent_from_mv(3900));
    r.gauge_pct = 255;
    CHECK_INT(eval1(&r).percent, battery_percent_from_mv(3900));

    /* A genuinely empty cell still reads empty through the fallback, so
     * refusing to believe the register costs nothing at the bottom. */
    r.mv = 3200; r.gauge_pct = 0;
    CHECK_INT(eval1(&r).percent, 0);
}

static void test_warnings(void)
{
    GROUP("warnings and hysteresis");

    battery_raw_t r = healthy();
    r.vbus_good = false;
    r.chg_status = BAT_CHG_STOP;

    r.gauge_pct = 50;
    battery_status_t st = eval1(&r);
    CHECK_INT(st.low, false);
    CHECK_INT(st.critical, false);
    CHECK_INT(st.brightness_cap_pct, BAT_CAP_NORMAL_PCT);

    r.gauge_pct = BAT_LOW_PCT;            /* the boundary is inclusive */
    st = eval1(&r);
    CHECK_INT(st.low, true);
    CHECK_INT(st.critical, false);
    CHECK_INT(st.brightness_cap_pct, BAT_CAP_LOW_PCT);

    r.gauge_pct = BAT_CRIT_PCT;
    st = eval1(&r);
    CHECK_INT(st.critical, true);
    CHECK_INT(st.low, true);              /* critical always implies low */
    CHECK_INT(st.brightness_cap_pct, BAT_CAP_CRIT_PCT);

    /* HYSTERESIS. Once low, it stays low until it is clearly not, so a gauge
     * resting on 20 cannot blink the badge on and off every ten seconds. */
    battery_status_t prev = st;
    prev.low = true; prev.critical = false;
    r.gauge_pct = BAT_LOW_PCT + 1;        /* 21: above the trip, below the clear */
    battery_eval(&r, &prev, &st);
    CHECK_INT(st.low, true);

    r.gauge_pct = BAT_LOW_CLEAR_PCT;      /* 25: clear */
    battery_eval(&r, &prev, &st);
    CHECK_INT(st.low, false);

    prev.low = true; prev.critical = true;
    r.gauge_pct = BAT_CRIT_PCT + 2;
    battery_eval(&r, &prev, &st);
    CHECK_INT(st.critical, true);
    r.gauge_pct = BAT_CRIT_CLEAR_PCT;
    battery_eval(&r, &prev, &st);
    CHECK_INT(st.critical, false);

    /* Plugging in clears everything at once. There is nothing to warn about
     * and nothing to save once it is on the charger, whatever the level. */
    r.vbus_good = true;
    r.chg_status = BAT_CHG_CC;
    r.gauge_pct = 3;
    prev.low = true; prev.critical = true;
    battery_eval(&r, &prev, &st);
    CHECK_INT(st.state, BAT_CHARGING);
    CHECK_INT(st.low, false);
    CHECK_INT(st.critical, false);
    CHECK_INT(st.brightness_cap_pct, BAT_CAP_NORMAL_PCT);

    /* Every level, on battery, produces a cap that is a real brightness. */
    r.vbus_good = false;
    r.chg_status = BAT_CHG_STOP;
    for (int pct = 1; pct <= 100; pct++) {
        r.gauge_pct = pct;
        st = eval1(&r);
        CHECK(st.brightness_cap_pct >= 10 && st.brightness_cap_pct <= 100);
        CHECK(!st.critical || st.low);
    }
}

static void test_text(void)
{
    GROUP("the words");

    char buf[48];
    battery_raw_t r = healthy();

    /* The badge speaks only while he is running on the cell. On USB it is
     * chrome he would stop seeing, and this one has to be noticed. */
    battery_status_t st = eval1(&r);
    CHECK_INT(battery_badge_text(&st, buf, sizeof buf), false);
    CHECK_STR(buf, "");

    r.present = false;
    st = eval1(&r);
    CHECK_INT(battery_badge_text(&st, buf, sizeof buf), false);

    r = healthy();
    r.vbus_good = false;
    r.chg_status = BAT_CHG_STOP;
    r.gauge_pct = 43;
    st = eval1(&r);
    CHECK_INT(battery_badge_text(&st, buf, sizeof buf), true);
    CHECK_STR(buf, "AKKU 43 %");

    r.gauge_pct = 7;
    st = eval1(&r);
    CHECK_INT(battery_badge_text(&st, buf, sizeof buf), true);
    CHECK_STR(buf, "AKKU 7 %");

    /* The settings row always answers, including the answer that matters on
     * the day a cell is first plugged in. */
    r = healthy();
    r.present = false;
    st = eval1(&r);
    battery_line_text(&st, buf, sizeof buf);
    CHECK_STR(buf, STR_BATTERY_NONE);

    r = healthy();
    r.gauge_pct = 78;
    st = eval1(&r);
    battery_line_text(&st, buf, sizeof buf);
    CHECK_STR(buf, "78 % \xC2\xB7 wird geladen");

    r.chg_status = BAT_CHG_DONE;
    r.gauge_pct = 100;
    st = eval1(&r);
    battery_line_text(&st, buf, sizeof buf);
    CHECK_STR(buf, "100 % \xC2\xB7 voll geladen");

    r.chg_status = BAT_CHG_STOP;
    r.gauge_pct = 61;
    st = eval1(&r);
    battery_line_text(&st, buf, sizeof buf);
    CHECK_STR(buf, "61 % \xC2\xB7 wird nicht geladen");

    r.vbus_good = false;
    st = eval1(&r);
    battery_line_text(&st, buf, sizeof buf);
    CHECK_STR(buf, "61 % \xC2\xB7 l\xC3\xA4uft mit Akku");

    /* Nothing writes past the end of a short buffer, and nothing leaves it
     * unterminated. The screens hand these fixed arrays. */
    for (size_t n = 1; n <= 24; n++) {
        char small[32];
        memset(small, '#', sizeof small);
        battery_line_text(&st, small, n);
        CHECK(memchr(small, '\0', n) != NULL);   /* terminated inside the buffer */
        CHECK(small[n] == '#');                  /* and nothing beyond it touched */

        memset(small, '#', sizeof small);
        (void)battery_badge_text(&st, small, n);
        CHECK(memchr(small, '\0', n) != NULL);
        CHECK(small[n] == '#');
    }

    /* A zero-length buffer is a no-op, not a one-byte overrun. */
    char none[2] = { '#', '#' };
    battery_line_text(&st, none, 0);
    CHECK(none[0] == '#');
    CHECK_INT(battery_badge_text(&st, none, 0), false);
    CHECK(none[0] == '#');
}

/* The full/not-charging line, and the band around it.
 *
 * It matters more than a cosmetic threshold: one side prints "voll geladen"
 * and the other is the alarm for a charger that is plugged in and not
 * charging (a TS pin still gating it, AGENTS.md §2). It was the only
 * threshold in this file without hysteresis, and it sits exactly on a knee of
 * the OCV table — k_ocv[1] = {4050, 90} — so on the fallback curve a few
 * millivolts of ADC noise walked it back and forth every poll. */
static void test_full_hysteresis(void)
{
    GROUP("full vs not-charging, and the band around it");

    battery_raw_t r = healthy();
    r.chg_status = BAT_CHG_STOP;   /* plugged in, charger says it is not charging */
    r.gauge_pct  = -1;             /* gauge unlearned: fall back to the OCV curve */

    /* Coming UP, the line is 90. */
    r.mv = 4050;                                   /* exactly the knee */
    battery_status_t st = eval1(&r);
    CHECK_INT(st.state, BAT_FULL);

    r.mv = 3950;                                   /* 75 % */
    CHECK_INT(eval1(&r).state, BAT_ON_USB);

    /* Once FULL, it takes a real drop to leave — not one noisy sample. This
     * is the case that used to flip: 4050 -> 4040 -> 4050 is a few mV. */
    battery_status_t full = { .state = BAT_FULL };
    battery_status_t out;

    r.mv = 4040;
    battery_eval(&r, &full, &out);
    CHECK_INT(out.state, BAT_FULL);
    CHECK(out.percent < BAT_FULL_PCT);             /* below the entry line... */
    CHECK(out.percent >= BAT_FULL_CLEAR_PCT);      /* ...but inside the band */

    /* And it does still leave, when the cell has genuinely fallen away. */
    r.mv = 3900;
    battery_eval(&r, &full, &out);
    CHECK(out.percent < BAT_FULL_CLEAR_PCT);
    CHECK_INT(out.state, BAT_ON_USB);

    /* The band never swallows the alarm the other way: a charger that is
     * blocked on a genuinely empty cell still reports BAT_ON_USB however it
     * got there. */
    r.mv = 3650;                                   /* 25 % */
    battery_eval(&r, &full, &out);
    CHECK_INT(out.state, BAT_ON_USB);

    /* DONE always wins, at any percentage — the charger saying so is better
     * evidence than the curve. */
    r.chg_status = BAT_CHG_DONE;
    r.mv = 3650;
    CHECK_INT(eval1(&r).state, BAT_FULL);
}

int main(void)
{
    printf("\n== battery policy\n");
    test_ocv_curve();
    test_states();
    test_gauge_plausibility();
    test_warnings();
    test_text();
    test_full_hysteresis();
    return test_summary();
}
