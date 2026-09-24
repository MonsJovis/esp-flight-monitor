#include "radar_logic.h"

#include <math.h>
#include <string.h>

radar_alt_band_t radar_alt_band(int32_t alt_ft)
{
    if (alt_ft == ALT_GROUND || alt_ft == ALT_UNKNOWN || alt_ft < 0) {
        return RADAR_ALT_MID;
    }
    if (alt_ft < RADAR_ALT_LOW_FT) {
        return RADAR_ALT_LOW;
    }
    if (alt_ft >= RADAR_ALT_HIGH_FT) {
        return RADAR_ALT_HIGH;
    }
    return RADAR_ALT_MID;
}

int radar_pick_nearest(const aircraft_t *ac, int n, const char *prev_hex)
{
    if (ac == NULL || n <= 0) {
        return -1;
    }
    int best = -1, prev = -1;
    for (int i = 0; i < n; i++) {
        if (!(ac[i].dst_nm >= 0.0f)) {      /* DST_UNKNOWN, and NaN */
            continue;
        }
        if (best < 0 || ac[i].dst_nm < ac[best].dst_nm) {
            best = i;
        }
        if (prev_hex != NULL && prev_hex[0] != '\0' && strcmp(ac[i].hex, prev_hex) == 0) {
            prev = i;
        }
    }
    if (best < 0 || prev < 0 || prev == best) {
        return best;
    }
    float keep_until = ac[best].dst_nm * (1.0f + RADAR_NEAREST_FRAC) + RADAR_NEAREST_NM;
    return (ac[prev].dst_nm <= keep_until) ? prev : best;
}

void radar_trails_reset(radar_trails_t *tr)
{
    if (tr != NULL) {
        memset(tr, 0, sizeof *tr);
    }
}

static void drop_old(radar_trail_t *t, uint32_t now_ms)
{
    /* Newest first, so the first fix too old ends the trail. Unsigned
     * subtraction is the wrap-safe age. */
    for (int k = 0; k < t->count; k++) {
        if ((uint32_t)(now_ms - t->fix[k].t_ms) > RADAR_TRAIL_MAX_AGE_MS) {
            t->count = k;
            break;
        }
    }
}

void radar_trails_age(radar_trails_t *tr, uint32_t now_ms)
{
    if (tr == NULL) {
        return;
    }
    for (int s = 0; s < MAX_AIRCRAFT; s++) {
        if (tr->slot[s].hex[0] != '\0') {
            drop_old(&tr->slot[s], now_ms);
        }
    }
}

/* Straight-line distance between two (distance, bearing) positions from the
 * same home point. Flat-earth, which at a 30 nm radius is off by far less
 * than the slack it is compared against. */
static float polar_gap_nm(float d1, float b1, float d2, float b2)
{
    const float k = 0.017453292519943295f;
    float x1 = d1 * sinf(b1 * k), y1 = d1 * cosf(b1 * k);
    float x2 = d2 * sinf(b2 * k), y2 = d2 * cosf(b2 * k);
    return hypotf(x1 - x2, y1 - y2);
}

static radar_trail_t *find_mut(radar_trails_t *tr, const char *hex)
{
    for (int s = 0; s < MAX_AIRCRAFT; s++) {
        if (tr->slot[s].hex[0] != '\0' && strcmp(tr->slot[s].hex, hex) == 0) {
            return &tr->slot[s];
        }
    }
    return NULL;
}

const radar_trail_t *radar_trails_find(const radar_trails_t *tr, const char *hex)
{
    if (tr == NULL || hex == NULL || hex[0] == '\0') {
        return NULL;
    }
    return find_mut((radar_trails_t *)tr, hex);
}

void radar_trails_observe(radar_trails_t *tr, const aircraft_t *ac, int n, uint32_t now_ms)
{
    if (tr == NULL) {
        return;
    }
    if (ac == NULL || n < 0) {
        n = 0;
    }

    /* Forget whoever has left. Done first so their slots are free for any
     * newcomers below — there are exactly MAX_AIRCRAFT slots and at most
     * MAX_AIRCRAFT aircraft, so a free slot always exists after this. */
    for (int s = 0; s < MAX_AIRCRAFT; s++) {
        radar_trail_t *t = &tr->slot[s];
        if (t->hex[0] == '\0') {
            continue;
        }
        bool present = false;
        for (int i = 0; i < n; i++) {
            if (strcmp(ac[i].hex, t->hex) == 0 && ac[i].dst_nm >= 0.0f) {
                present = true;
                break;
            }
        }
        if (!present) {
            memset(t, 0, sizeof *t);
        }
    }

    for (int i = 0; i < n && i < MAX_AIRCRAFT; i++) {
        if (!(ac[i].dst_nm >= 0.0f) || ac[i].hex[0] == '\0') {
            continue;
        }
        radar_trail_t *t = find_mut(tr, ac[i].hex);
        if (t == NULL) {
            for (int s = 0; s < MAX_AIRCRAFT; s++) {
                if (tr->slot[s].hex[0] == '\0') {
                    t = &tr->slot[s];
                    memcpy(t->hex, ac[i].hex, sizeof t->hex);
                    t->hex[sizeof t->hex - 1] = '\0';
                    t->count = 0;
                    break;
                }
            }
            if (t == NULL) {
                continue;   /* cannot happen, see above; never write out of bounds */
            }
        }
        drop_old(t, now_ms);
        if (t->count > 0) {
            float dt_h  = (float)(uint32_t)(now_ms - t->fix[0].t_ms) / 3600000.0f;
            float reach = RADAR_TRAIL_MAX_KT * dt_h + RADAR_TRAIL_SLACK_NM;
            if (polar_gap_nm(t->fix[0].dst_nm, t->fix[0].dir_deg,
                             ac[i].dst_nm, ac[i].dir_deg) > reach) {
                t->count = 0;          /* impossible jump: start over from here */
            }
        }
        if (t->count > 0 && (uint32_t)(now_ms - t->fix[0].t_ms) < RADAR_TRAIL_STEP_MS) {
            continue;
        }
        int keep = (t->count < RADAR_TRAIL_LEN) ? t->count : RADAR_TRAIL_LEN - 1;
        memmove(&t->fix[1], &t->fix[0], sizeof t->fix[0] * (size_t)keep);
        t->fix[0].dst_nm  = ac[i].dst_nm;
        t->fix[0].dir_deg = ac[i].dir_deg;
        t->fix[0].t_ms    = now_ms;
        t->count          = keep + 1;
    }
}
