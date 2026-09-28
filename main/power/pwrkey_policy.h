/* When a press of the side PWRKEY means "switch off" — no I2C, no ESP-IDF.
 *
 * The owner: holding the key for 3 s switches the device on, and holding it
 * again does not switch it off (D90). The PMIC can do the second half by
 * itself, but only from 4 s up, and only if an eFuse bit this project never
 * set says so. So the firmware watches the key and asks for the power-off
 * itself at 3 s, the same hold that switches it on. axp2101.c also arms the
 * chip's own 4 s off as the backstop for a firmware that has stopped running.
 *
 * WHAT THE FIRMWARE CAN SEE. The PMIC's IRQ pin reaches no GPIO, so the key is
 * polled through the IRQ STATUS register, which latches three things and has
 * no "is it held right now" bit:
 *   - an edge (the key went down or came up — the chip reports both),
 *   - a long press, once, PWRKEY_LONG_IRQ_MS into a hold.
 * A hold is therefore: the long-press flag, and then no further edge for
 * PWRKEY_OFF_AFTER_LONG_MS. Any edge in the same reading as the long-press
 * flag, or after it, is the release.
 *
 * THE PRESS THAT SWITCHED IT ON MUST NOT SWITCH IT OFF. He holds the key for
 * 3 s, the PMIC starts the rails at 2 s at most, and the firmware is polling
 * well before his thumb comes off. That same hold then crosses the long-press
 * threshold. So nothing counts until the watcher is ARMED: by the first edge
 * it sees (that is the release of the power-on press), or by
 * PWRKEY_ARM_GRACE_MS after start on a device that was switched on by
 * plugging in USB and never saw the key at all.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The chip's long-press flag fires this far into a hold (REG27[5:4] = 2 s). */
#define PWRKEY_LONG_IRQ_MS        2000
/* ... and the firmware switches off this much later: 3 s in all. */
#define PWRKEY_OFF_AFTER_LONG_MS  1000
#define PWRKEY_ARM_GRACE_MS       8000

/* One reading of the key, as axp2101_pwrkey_poll() returns it. */
typedef struct {
    bool edge;   /* the key went down or came up since the last reading */
    bool lng;    /* the chip's long-press flag fired since the last reading */
} pwrkey_irq_t;

typedef struct {
    int64_t start_ms;
    int64_t long_at_ms;   /* when the long-press flag was seen; -1 when none is pending */
    bool    armed;
} pwrkey_state_t;

void pwrkey_init(pwrkey_state_t *st, int64_t now_ms);

/* Feeds one reading. Returns true exactly when the device should switch off
 * now. Call it on a steady tick even when nothing was latched, because the
 * decision is made by the ABSENCE of a release. */
bool pwrkey_step(pwrkey_state_t *st, pwrkey_irq_t irq, int64_t now_ms);

#ifdef __cplusplus
}
#endif
