#include "source_logic.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Table described in source_logic.h. SRC_ADSB_LOL_LATLON is disabled on
 * purpose — see the header for why it is not a real failover target. */
typedef struct {
    const char *name;
    bool        enabled;
} source_info_t;

static const source_info_t SOURCE_TABLE[SRC_COUNT] = {
    [SRC_ADSB_LOL_POINT]  = { "adsb.lol (point)",        true  },
    [SRC_ADSB_LOL_LATLON] = { "adsb.lol (lat/lon/dist)", false },
};

int64_t source_backoff_delay_ms(int consec_failures)
{
    if (consec_failures <= 0) {
        return SRC_POLL_INTERVAL_MS;
    }
    int64_t delay = SRC_BACKOFF_BASE_MS;
    for (int i = 1; i < consec_failures; i++) {
        if (delay >= SRC_BACKOFF_CAP_MS) {
            return SRC_BACKOFF_CAP_MS;
        }
        delay *= 2;
    }
    return (delay > SRC_BACKOFF_CAP_MS) ? SRC_BACKOFF_CAP_MS : delay;
}

bool source_is_throttle_status(int http_status)
{
    /* Exactly these three literal codes — never a range. See the header:
     * generalising this to "any 3xx" would misclassify a genuine redirect
     * (301/302) as throttling and could send us chasing it. */
    return http_status == 429 || http_status == 503 || http_status == 308;
}

const char *source_name(source_id_t src)
{
    if (src < 0 || src >= SRC_COUNT) {
        return "?";
    }
    return SOURCE_TABLE[src].name;
}

bool source_is_enabled(source_id_t src)
{
    if (src < 0 || src >= SRC_COUNT) {
        return false;
    }
    return SOURCE_TABLE[src].enabled;
}

source_id_t source_next_enabled(source_id_t from)
{
    for (int i = 1; i <= SRC_COUNT; i++) {
        source_id_t candidate = (source_id_t)(((int)from + i) % SRC_COUNT);
        if (SOURCE_TABLE[candidate].enabled) {
            return candidate;
        }
    }
    return from; /* degenerate: nothing enabled, caller keeps whatever it had */
}

int source_build_url(source_id_t src, double lat, double lon, int radius_nm,
                      char *buf, size_t buf_sz)
{
    if (buf == NULL || buf_sz == 0) {
        return -1;
    }

    int n;
    switch (src) {
    case SRC_ADSB_LOL_POINT:
        n = snprintf(buf, buf_sz, "http://api.adsb.lol/v2/point/%.4f/%.4f/%d",
                     lat, lon, radius_nm);
        break;
    case SRC_ADSB_LOL_LATLON:
        n = snprintf(buf, buf_sz, "http://api.adsb.lol/v2/lat/%.4f/lon/%.4f/dist/%d",
                     lat, lon, radius_nm);
        break;
    default:
        return -1;
    }

    if (n < 0 || (size_t)n >= buf_sz) {
        return -1;
    }
    return n;
}

int source_find_uncached(const char (*onscreen)[9], int n_onscreen,
                          const char (*known)[9], int n_known,
                          char (*out_pending)[9], int max_pending)
{
    if (onscreen == NULL || out_pending == NULL || max_pending <= 0) {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < n_onscreen && count < max_pending; i++) {
        if (onscreen[i][0] == '\0') {
            continue;
        }

        bool already_known = false;
        for (int k = 0; k < n_known; k++) {
            if (strcmp(onscreen[i], known[k]) == 0) {
                already_known = true;
                break;
            }
        }
        if (already_known) {
            continue;
        }

        bool dup = false;
        for (int p = 0; p < count; p++) {
            if (strcmp(out_pending[p], onscreen[i]) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }

        strncpy(out_pending[count], onscreen[i], 8);
        out_pending[count][8] = '\0';
        count++;
    }
    return count;
}

bool source_should_post_routes(int n_pending, int64_t ms_since_last_post)
{
    return n_pending > 0 && ms_since_last_post >= SRC_ROUTE_POST_MIN_INTERVAL_MS;
}

const char *source_compass_abbrev_en(float bearing_deg)
{
    static const char *names[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };

    float b = fmodf(bearing_deg, 360.0f);
    if (b < 0.0f) {
        b += 360.0f;
    }
    int idx = ((int)((b + 22.5f) / 45.0f)) % 8;
    if (idx < 0) {
        idx += 8;
    }
    return names[idx];
}
