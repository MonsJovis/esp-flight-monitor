/* What the battery MEANS — no I2C, no ESP-IDF, no hardware. Pure decisions,
 * so every threshold, every hysteresis band and every German word can be
 * tested on the host in milliseconds instead of by flattening a cell.
 *
 * The register-level driver is main/power/axp2101.c. This file is the part
 * with the judgement in it, the same split ota.c / ota_policy.c uses.
 *
 * The one rule this file exists to keep: the UI formats nothing (AGENTS.md
 * §10). battery_badge_text() and battery_line_text() produce the finished
 * German, and the screens position it.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Five states, because there are five things he can be told. "Charging" and
 * "full" are not the same thing to someone about to unplug it and walk out to
 * the terrace, and BAT_ON_USB — plugged in, cell fitted, not charging — is
 * the one that says a charger has been blocked rather than letting the device
 * quietly never charge. */
typedef enum {
    BAT_ABSENT = 0,     /* no cell on the PH2.0 header — every device until one is fitted */
    BAT_CHARGING,
    BAT_FULL,           /* on USB, charger says done */
    BAT_ON_USB,         /* on USB, cell fitted, charger idle and the cell not full */
    BAT_ON_BATTERY,
} battery_state_t;

/* Straight off the PMIC, unjudged. Filled by axp2101_read(); a test fills it
 * by hand. */
typedef struct {
    bool    present;      /* REG00[3] battery present */
    bool    vbus_good;    /* REG00[5] a usable supply is plugged in */
    uint8_t chg_status;   /* REG01[2:0], see BAT_CHG_* below */
    int     mv;           /* REG34/35, millivolts */
    int     gauge_pct;    /* REG A4, 0..100 — see battery_percent() for why this is not trusted blindly */
} battery_raw_t;

/* REG01[2:0] as the datasheet numbers them (§6.13.2.2). Named here rather
 * than in the driver because the policy is what reads them. */
#define BAT_CHG_TRICKLE 0
#define BAT_CHG_PRE     1
#define BAT_CHG_CC      2
#define BAT_CHG_CV      3
#define BAT_CHG_DONE    4
#define BAT_CHG_STOP    5

/* What the rest of the device is allowed to see. */
typedef struct {
    battery_state_t state;
    int  percent;             /* 0..100; -1 when absent, so "no cell" can never print as "0 %" */
    int  mv;
    bool low;                 /* tell him */
    bool critical;            /* tell him louder, and start saving power */
    int  brightness_cap_pct;  /* ceiling over settings_brightness_for_hour(); 100 = no cap */
} battery_status_t;

/* Warn early enough that he can walk back inside, not so early that the
 * panel nags for an hour. At the measured draw a 2000 mAh cell does roughly
 * four hours, so 20 % is about fifty minutes of warning. */
#define BAT_LOW_PCT        20
#define BAT_LOW_CLEAR_PCT  25   /* hysteresis: a gauge sitting on 20 must not blink the badge */
#define BAT_CRIT_PCT        7
#define BAT_CRIT_CLEAR_PCT 12

/* Dimming is a saving AND a signal, which is why it starts at `low` and not
 * before. Capping brightness while the cell is healthy would optimise the
 * wrong thing: the backlight is most of the draw, but four hours is already
 * eight times what he asked for, and a panel that dims the moment it is
 * unplugged reads as a fault. */
#define BAT_CAP_NORMAL_PCT  100
#define BAT_CAP_LOW_PCT      40
#define BAT_CAP_CRIT_PCT     25

/* What "full" is on THIS device, in millivolts.
 *
 * Not 4.2 V: axp2101.c charges to 4.1 V deliberately (REG64 = 0x02) to keep
 * a permanently-charged, permanently-warm cell alive longer. The curve below
 * has to agree with the charger, or a cell the charger calls finished reads
 * 95 % on the panel and "95 % \xc2\xb7 voll geladen" is a device arguing with
 * itself. If the charge target in axp2101.c ever moves, move this with it.
 */
#define BAT_CHARGE_TARGET_MV 4100

/* Open-circuit estimate for a single Li-ion cell, 0..100. Monotonic, clamped
 * at both ends. Only a FALLBACK — see battery_eval(). Under load the terminal
 * voltage sags and this reads low; while charging it is pulled up and this
 * reads high. Both are acceptable for a number that only has to be roughly
 * right when the gauge has not yet learned the cell. */
int battery_percent_from_mv(int mv);

/* Turns a raw reading into the judged status.
 *
 * `prev` is the previous result and carries the hysteresis; pass NULL for the
 * first call. `out` may not alias `prev`.
 */
void battery_eval(const battery_raw_t *in, const battery_status_t *prev,
                  battery_status_t *out);

/* The chrome badge: "AKKU 43 %", or "" when there is nothing to say.
 *
 * Nothing to say means: no cell fitted, or running on USB like it does for
 * 23 hours of every day. A badge that is always there is chrome he stops
 * seeing, and this one has to be noticed the one time it matters.
 *
 * Returns true when it wrote something visible.
 */
bool battery_badge_text(const battery_status_t *st, char *out, size_t out_sz);

/* The Einstellungen row: "78 % · wird geladen", "Kein Akku". Always writes
 * something — this row answers "is the cell even connected?", which is the
 * question on the day one is fitted. */
void battery_line_text(const battery_status_t *st, char *out, size_t out_sz);

#ifdef __cplusplus
}
#endif
