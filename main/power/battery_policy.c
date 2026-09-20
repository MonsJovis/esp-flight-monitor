#include "battery_policy.h"

#include <stdio.h>
#include <string.h>

#include "strings_de.h"

/* ---- open-circuit curve ------------------------------------------------
 *
 * A single Li-ion cell, lightly loaded, at room temperature. Eleven points
 * with straight lines between them: the curve is flat in the middle and
 * steep at both ends, which is exactly where a linear fit between two
 * measured points is at its best and a single formula is at its worst.
 *
 * The top of the table is the CHARGER's target, not the chemistry's 4.2 V
 * ceiling: on this device a cell never gets above 4.1 V, so anything at or
 * above that is full and saying 95 % would be the panel disagreeing with its
 * own charger.
 *
 * Deliberately NOT the primary source of the number — the AXP2101 has a
 * coulomb-counting gauge that learns the cell (datasheet §6.11), and a
 * learned gauge beats a table under load every time. This is what answers
 * while the gauge is still a liar, which is the first charge cycle of a new
 * cell and every cycle after a PMIC reset.
 */
static const struct { int mv; int pct; } k_ocv[] = {
    { BAT_CHARGE_TARGET_MV, 100 },   /* 4.1 V — what this charger calls full */
    { 4050,  90 },
    { 3950,  75 },
    { 3870,  60 },
    { 3800,  50 },
    { 3720,  35 },
    { 3650,  25 },
    { 3550,  15 },
    { 3450,   8 },
    { 3350,   4 },
    { 3200,   0 },
};

int battery_percent_from_mv(int mv)
{
    const int n = (int)(sizeof k_ocv / sizeof k_ocv[0]);

    if (mv >= k_ocv[0].mv)     return 100;
    if (mv <= k_ocv[n - 1].mv) return 0;

    for (int i = 0; i < n - 1; i++) {
        int hi_mv = k_ocv[i].mv, lo_mv = k_ocv[i + 1].mv;
        if (mv <= hi_mv && mv >= lo_mv) {
            int hi_pct = k_ocv[i].pct, lo_pct = k_ocv[i + 1].pct;
            int span   = hi_mv - lo_mv;           /* > 0 by construction */
            /* Rounded, not truncated: truncation makes the curve
             * non-monotonic at the join between two segments. */
            return lo_pct + ((mv - lo_mv) * (hi_pct - lo_pct) + span / 2) / span;
        }
    }
    return 0;   /* unreachable while the table is sorted */
}

/* ---- judgement --------------------------------------------------------- */

/* The gauge's answer, or the curve's when the gauge has not got one.
 *
 * 0 is treated as "no answer" rather than as empty. A fresh PMIC reports 0
 * until the gauge has seen a cycle, and printing "0 %" beside a cell sitting
 * at 3.9 V is the kind of wrong that makes him distrust everything else on
 * the screen. A genuinely empty cell reads ~3.2 V and the curve says 0 too,
 * so nothing is lost by refusing to believe the register. */
static int choose_percent(const battery_raw_t *in)
{
    if (in->gauge_pct >= 1 && in->gauge_pct <= 100) {
        return in->gauge_pct;
    }
    return battery_percent_from_mv(in->mv);
}

static bool charging_now(uint8_t s)
{
    return s == BAT_CHG_TRICKLE || s == BAT_CHG_PRE ||
           s == BAT_CHG_CC      || s == BAT_CHG_CV;
}

void battery_eval(const battery_raw_t *in, const battery_status_t *prev,
                  battery_status_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    out->percent            = -1;
    out->brightness_cap_pct = BAT_CAP_NORMAL_PCT;
    if (in == NULL) {
        out->state = BAT_ABSENT;
        return;
    }

    out->mv = in->mv;

    if (!in->present) {
        /* No cell fitted. Not a fault, and not something to warn about: it is
         * how every one of these devices leaves the bench. */
        out->state = BAT_ABSENT;
        return;
    }

    out->percent = choose_percent(in);

    if (in->vbus_good) {
        if (charging_now(in->chg_status)) {
            out->state = BAT_CHARGING;
        } else if (in->chg_status == BAT_CHG_DONE || out->percent >= 90) {
            /* Both arms are needed. The charger reports DONE for a while and
             * then falls back to "not charging" (REG01 = 101b) once it has
             * left the termination state, so a healthy full cell spends most
             * of its life in the same register value as a charger that has
             * been blocked. The percentage is what tells the two apart, and
             * getting it wrong the other way is the worse failure: it would
             * print "wird nicht geladen" at a man whose device is fine. */
            out->state = BAT_FULL;
        } else {
            /* Plugged in, not charging, not full. Rare and worth saying out
             * loud: it is exactly what a TS pin still gating the charger
             * looks like (AGENTS.md §2, axp2101.c), and the alternative is a
             * device that quietly never charges. */
            out->state = BAT_ON_USB;
        }
        /* Warnings and dimming are for the case he can do something about.
         * On the charger there is nothing to save and nothing to warn. */
        return;
    }

    out->state = BAT_ON_BATTERY;

    /* Hysteresis, both bands. Without it a gauge resting on the threshold
     * flickers the badge on and off every ten seconds, which is worse than
     * either state on its own. */
    bool was_low  = prev && prev->low;
    bool was_crit = prev && prev->critical;

    out->low      = was_low  ? (out->percent <  BAT_LOW_CLEAR_PCT)
                             : (out->percent <= BAT_LOW_PCT);
    out->critical = was_crit ? (out->percent <  BAT_CRIT_CLEAR_PCT)
                             : (out->percent <= BAT_CRIT_PCT);

    /* Critical implies low. They are drawn from one number, so they cannot be
     * allowed to disagree just because the two hysteresis bands crossed in a
     * strange order. */
    if (out->critical) {
        out->low = true;
    }

    out->brightness_cap_pct = out->critical ? BAT_CAP_CRIT_PCT
                            : out->low      ? BAT_CAP_LOW_PCT
                                            : BAT_CAP_NORMAL_PCT;
}

/* ---- the words --------------------------------------------------------- */

bool battery_badge_text(const battery_status_t *st, char *out, size_t out_sz)
{
    if (out == NULL || out_sz == 0) {
        return false;
    }
    out[0] = '\0';
    if (st == NULL || st->state != BAT_ON_BATTERY || st->percent < 0) {
        return false;
    }
    snprintf(out, out_sz, FMT_BATTERY_BADGE, st->percent);
    return true;
}

void battery_line_text(const battery_status_t *st, char *out, size_t out_sz)
{
    if (out == NULL || out_sz == 0) {
        return;
    }
    if (st == NULL || st->state == BAT_ABSENT || st->percent < 0) {
        snprintf(out, out_sz, "%s", STR_BATTERY_NONE);
        return;
    }
    const char *fmt;
    switch (st->state) {
        case BAT_CHARGING:   fmt = FMT_BATTERY_CHARGING; break;
        case BAT_FULL:       fmt = FMT_BATTERY_FULL;     break;
        case BAT_ON_USB:     fmt = FMT_BATTERY_IDLE;     break;
        case BAT_ON_BATTERY:
        default:             fmt = FMT_BATTERY_RUNNING;  break;
    }
    snprintf(out, out_sz, fmt, st->percent);
}
