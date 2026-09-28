#include "pwrkey_policy.h"

#include <stddef.h>

void pwrkey_init(pwrkey_state_t *st, int64_t now_ms)
{
    if (st == NULL) {
        return;
    }
    st->start_ms   = now_ms;
    st->long_at_ms = -1;
    st->armed      = false;
}

bool pwrkey_step(pwrkey_state_t *st, pwrkey_irq_t irq, int64_t now_ms)
{
    if (st == NULL) {
        return false;
    }

    if (!st->armed) {
        /* Everything seen before arming belongs to the press that switched
         * the device on, including its long-press flag. */
        if (irq.edge || now_ms - st->start_ms >= PWRKEY_ARM_GRACE_MS) {
            st->armed = true;
        }
        st->long_at_ms = -1;
        return false;
    }

    if (irq.edge) {
        /* The release. In the same reading as the long-press flag it is
         * ambiguous which came first, and "stay on" is the side to err on. */
        st->long_at_ms = -1;
        return false;
    }
    if (irq.lng && st->long_at_ms < 0) {
        st->long_at_ms = now_ms;
    }
    return st->long_at_ms >= 0 &&
           now_ms - st->long_at_ms >= PWRKEY_OFF_AFTER_LONG_MS;
}
