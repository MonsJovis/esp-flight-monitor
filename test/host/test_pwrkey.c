/* main/power/pwrkey_policy.c — when a hold on the side key means "off" (D90).
 *
 * The hardware half cannot be exercised off the board; this is the half that
 * decides, and the two ways it could go wrong in front of him are both here:
 * switching off when he only pressed it to switch on, and not switching off
 * when he held it.
 */
#include "test_util.h"
#include "pwrkey_policy.h"

static const pwrkey_irq_t NONE = { false, false };
static const pwrkey_irq_t EDGE = { true,  false };
static const pwrkey_irq_t LONG = { false, true  };
static const pwrkey_irq_t BOTH = { true,  true  };

/* Runs a 100 ms poll from `from` to `to` with nothing latched; returns the
 * time it first said "off", or -1. */
static int64_t idle(pwrkey_state_t *st, int64_t from, int64_t to)
{
    for (int64_t t = from; t <= to; t += 100) {
        if (pwrkey_step(st, NONE, t)) {
            return t;
        }
    }
    return -1;
}

/* An armed watcher, as it stands a while after boot. */
static pwrkey_state_t armed(void)
{
    pwrkey_state_t st;
    pwrkey_init(&st, 0);
    CHECK(!pwrkey_step(&st, EDGE, 100));
    return st;
}

int main(void)
{
    GROUP("a 3 s hold switches off, at 3 s");
    {
        pwrkey_state_t st = armed();
        /* Press at 10 000: the down edge, then the chip's long flag at +2 s. */
        CHECK(!pwrkey_step(&st, EDGE, 10000));
        CHECK_INT((int)idle(&st, 10100, 11900), -1);
        CHECK(!pwrkey_step(&st, LONG, 12000));
        CHECK_INT((int)idle(&st, 12100, 12900), -1);
        CHECK(pwrkey_step(&st, NONE, 13000));
        CHECK_INT(PWRKEY_LONG_IRQ_MS + PWRKEY_OFF_AFTER_LONG_MS, 3000);
    }

    GROUP("let go before 3 s: stays on");
    {
        pwrkey_state_t st = armed();
        CHECK(!pwrkey_step(&st, EDGE, 10000));
        CHECK(!pwrkey_step(&st, LONG, 12000));
        CHECK(!pwrkey_step(&st, EDGE, 12700));          /* released at 2.7 s */
        CHECK_INT((int)idle(&st, 12800, 30000), -1);
    }

    GROUP("release in the same reading as the long flag: stays on");
    {
        pwrkey_state_t st = armed();
        CHECK(!pwrkey_step(&st, BOTH, 12000));
        CHECK_INT((int)idle(&st, 12100, 30000), -1);
    }

    GROUP("a short press does nothing");
    {
        pwrkey_state_t st = armed();
        CHECK(!pwrkey_step(&st, EDGE, 10000));
        CHECK(!pwrkey_step(&st, EDGE, 10300));
        CHECK_INT((int)idle(&st, 10400, 60000), -1);
    }

    GROUP("the press that switched it on does not switch it off");
    {
        /* He holds for 3 s to switch on. The PMIC starts the rails well
         * inside that, the watcher is running while his thumb is still down,
         * and the chip raises its long flag on that very hold. */
        pwrkey_state_t st;
        pwrkey_init(&st, 0);
        CHECK(!pwrkey_step(&st, LONG, 500));
        CHECK_INT((int)idle(&st, 600, 1400), -1);
        CHECK(!pwrkey_step(&st, EDGE, 1500));           /* the release arms it */
        CHECK_INT((int)idle(&st, 1600, 20000), -1);

        /* ... and the next hold is a real one. */
        CHECK(!pwrkey_step(&st, EDGE, 20000));
        CHECK(!pwrkey_step(&st, LONG, 22000));
        CHECK(pwrkey_step(&st, NONE, 23000));
    }

    GROUP("switched on by USB, the key never touched: arms after the grace");
    {
        pwrkey_state_t st;
        pwrkey_init(&st, 0);
        CHECK(!pwrkey_step(&st, LONG, PWRKEY_ARM_GRACE_MS - 100));  /* ignored */
        CHECK_INT((int)idle(&st, PWRKEY_ARM_GRACE_MS, PWRKEY_ARM_GRACE_MS + 5000), -1);
        CHECK(st.armed);
        CHECK(!pwrkey_step(&st, LONG, 20000));
        CHECK(pwrkey_step(&st, NONE, 21000));
    }

    GROUP("a bus fault reads as nothing, and only delays the decision");
    {
        pwrkey_state_t st = armed();
        CHECK(!pwrkey_step(&st, LONG, 12000));
        CHECK(!pwrkey_step(&st, NONE, 12400));
        /* 12 500 .. 12 900 lost to the bus: no calls at all */
        CHECK(pwrkey_step(&st, NONE, 13100));
    }

    GROUP("defensive");
    {
        pwrkey_init(NULL, 0);
        CHECK(!pwrkey_step(NULL, LONG, 0));
    }

    return test_summary();
}
