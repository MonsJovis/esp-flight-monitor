/* radar_logic.h — the decisions the radar scope makes, without the scope.
 *
 * Three rules live here rather than in screen_radar.c because each of them is
 * arithmetic that goes wrong silently, and AGENTS.md §10 says anything that
 * can be tested on the host is: which altitude band a mark is drawn at, which
 * aircraft counts as "nearest" when two are neck and neck, and the short
 * history each mark trails behind it. None of them touches LVGL; the screen
 * asks and draws. D76 has the reasoning for all three.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "flight_types.h"

/* ---- Altitude band -------------------------------------------------------
 *
 * Drawn as mark SIZE, bigger = lower. The question it answers is "which of
 * these is the one I can hear", and it is height, not distance, that decides
 * that: a jet 3 km out at 11 000 m is silent, a Cessna 8 km out at 700 m is
 * rattling the window.
 *
 * Unknown altitude and "on the ground" both get the middle size. The size only
 * makes a claim when there is an airborne altitude to back it — a taxiing
 * airliner at Schwechat drawn as the biggest thing on the scope would be a
 * confident answer to the wrong question. */
typedef enum {
    RADAR_ALT_LOW,     /* below RADAR_ALT_LOW_FT: likely audible, drawn large   */
    RADAR_ALT_MID,     /* in between, or unknown, or on the ground              */
    RADAR_ALT_HIGH,    /* at or above RADAR_ALT_HIGH_FT: cruise, drawn small    */
} radar_alt_band_t;

#define RADAR_ALT_LOW_FT   5000    /* ~1 500 m */
#define RADAR_ALT_HIGH_FT  20000   /* ~6 100 m */

radar_alt_band_t radar_alt_band(int32_t alt_ft);

/* ---- Nearest, with hysteresis --------------------------------------------
 *
 * Returns the index of the aircraft to draw magenta, or -1 if none has a
 * distance. `prev_hex` is last time's answer (may be NULL or empty).
 *
 * The previous nearest KEEPS the title while it is within RADAR_NEAREST_FRAC
 * of the true nearest, plus RADAR_NEAREST_NM. Without that, two aircraft at
 * near-equal range swap on every poll and the magenta mark jumps between them
 * with nobody touching anything — a highlight that moves on its own teaches
 * him that it means nothing. */
#define RADAR_NEAREST_FRAC 0.10f
#define RADAR_NEAREST_NM   0.25f   /* ~460 m: the floor for aircraft right overhead */

int radar_pick_nearest(const aircraft_t *ac, int n, const char *prev_hex);

/* ---- Trails --------------------------------------------------------------
 *
 * The comet tail every real PPI draws: where a mark has been, a few dots,
 * fading. It shows direction and speed with no text, and it shows direction
 * HONESTLY for an aircraft that reports no track — the dot-not-arrow rule in
 * screen_radar.c refuses to claim a heading nobody sent, and a trail claims
 * nothing, it only remembers.
 *
 * Positions are stored as (distance, bearing) from home, not as screen pixels,
 * so a trail survives the radius being changed in Einstellungen.
 *
 * fix[0] is the NEWEST. A new fix is taken at most every RADAR_TRAIL_STEP_MS,
 * and any fix older than RADAR_TRAIL_MAX_AGE_MS is dropped — which is what
 * stops a stale trail from drawing a long jump when the radar comes back on
 * screen after a minute on the Liste (the screen only updates while visible). */
#define RADAR_TRAIL_LEN         4
#define RADAR_TRAIL_STEP_MS     15000u
#define RADAR_TRAIL_MAX_AGE_MS  (RADAR_TRAIL_STEP_MS * (RADAR_TRAIL_LEN + 1))

/* A position no aircraft could have reached since its last fix starts the
 * trail over instead of extending it. ADS-B does produce the odd wild
 * position, and a trail that keeps it draws a dot tens of kilometres from the
 * aircraft it belongs to — a phantom on the scope, which is worse than no
 * trail. 800 kt is faster than any airliner's ground speed over Europe even
 * with a jet stream behind it; the 1 nm slack absorbs extrapolation error and
 * position noise between two polls. Checked on EVERY observation, not only
 * when a fix is due, or a jump and a jump back inside one step would leave
 * the ghost in place. */
#define RADAR_TRAIL_MAX_KT      800.0f
#define RADAR_TRAIL_SLACK_NM    1.0f

typedef struct {
    float    dst_nm;
    float    dir_deg;
    uint32_t t_ms;
} radar_fix_t;

typedef struct {
    char        hex[sizeof ((aircraft_t *)0)->hex];   /* "" = free slot */
    radar_fix_t fix[RADAR_TRAIL_LEN];
    int         count;
} radar_trail_t;

typedef struct {
    radar_trail_t slot[MAX_AIRCRAFT];
} radar_trails_t;

void radar_trails_reset(radar_trails_t *tr);

/* Records where every aircraft with a distance is now, and forgets any trail
 * whose aircraft is no longer in `ac`. `now_ms` is a free-running millisecond
 * clock; wrap-around is handled. */
void radar_trails_observe(radar_trails_t *tr, const aircraft_t *ac, int n, uint32_t now_ms);

/* Drops fixes that have aged out, without adding any. Used while the data is
 * stale: the positions are frozen, so a fix recorded then would carry a fresh
 * timestamp for a stale position, and be drawn as recent history once the
 * data returns. The trail fades out instead — itself a sign nothing is
 * moving — and restarts clean. */
void radar_trails_age(radar_trails_t *tr, uint32_t now_ms);

/* NULL when there is no trail for this aircraft. */
const radar_trail_t *radar_trails_find(const radar_trails_t *tr, const char *hex);
