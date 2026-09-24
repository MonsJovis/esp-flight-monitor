#include <stdbool.h>
#include "adsb_parse.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "compat.h"

static const char *TAG = "adsb_parse";

/* Copies `src` into `dst` (size dst_sz), trimming leading/trailing ASCII
 * whitespace and uppercasing what remains. Truncates safely and always
 * NUL-terminates. NULL `src` becomes "". */
static void copy_trim_upper(char *dst, size_t dst_sz, const char *src)
{
    if (dst_sz == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    size_t start = 0;
    while (src[start] != '\0' && isspace((unsigned char)src[start])) {
        start++;
    }
    size_t end = strlen(src);
    while (end > start && isspace((unsigned char)src[end - 1])) {
        end--;
    }

    size_t n = end - start;
    if (n > dst_sz - 1) {
        n = dst_sz - 1;
    }
    for (size_t i = 0; i < n; i++) {
        dst[i] = (char)toupper((unsigned char)src[start + i]);
    }
    dst[n] = '\0';
}

/* Copies `src` into `dst` (size dst_sz) verbatim, truncating safely and
 * always NUL-terminating. NULL `src` becomes "". */
static void copy_str(char *dst, size_t dst_sz, const char *src)
{
    if (dst_sz == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n > dst_sz - 1) {
        n = dst_sz - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static const char *str_field(const cJSON *obj, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static int cmp_dst_nm(const void *a, const void *b)
{
    const aircraft_t *aa = (const aircraft_t *)a;
    const aircraft_t *bb = (const aircraft_t *)b;
    /* Unknown distance sorts last: out[0] is read as "the plane overhead", so an
     * aircraft we cannot place must never win that slot. */
    const bool a_unk = (aa->dst_nm == DST_UNKNOWN);
    const bool b_unk = (bb->dst_nm == DST_UNKNOWN);
    if (a_unk != b_unk) {
        return a_unk ? 1 : -1;
    }
    if (aa->dst_nm < bb->dst_nm) {
        return -1;
    }
    if (aa->dst_nm > bb->dst_nm) {
        return 1;
    }
    return 0;
}

void adsb_sort_by_distance(aircraft_t *ac, int n)
{
    if (ac == NULL || n <= 1) {
        return;
    }
    qsort(ac, (size_t)n, sizeof(aircraft_t), cmp_dst_nm);
}

int adsb_parse(const char *json, size_t len, aircraft_t *out, int max)
{
    if (json == NULL || out == NULL || max <= 0) {
        return -1;
    }

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (root == NULL) {
        ESP_LOGW(TAG, "malformed JSON");
        return -1;
    }
    if (!cJSON_IsObject(root)) {
        ESP_LOGW(TAG, "top level is not an object");
        cJSON_Delete(root);
        return -1;
    }

    /* adsb.lol v2 and adsb.fi v3 wrap the array as "ac"; adsb.fi v2 uses
     * "aircraft". One parser serves both — see AGENTS.md §4. */
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "ac");
    if (!cJSON_IsArray(arr)) {
        arr = cJSON_GetObjectItemCaseSensitive(root, "aircraft");
    }
    if (!cJSON_IsArray(arr)) {
        ESP_LOGW(TAG, "no \"ac\" or \"aircraft\" array");
        cJSON_Delete(root);
        return -1;
    }

    /* Every aircraft in the array is parsed, and the NEAREST `max` are kept.
     *
     * This used to stop at the first `max` in array order. adsb.lol does not
     * sort by distance — over the Vienna preset at 33 nm the array opened
     * 31.6, 23.5, 31.3 nm — so as soon as the sky held more than MAX_AIRCRAFT
     * the parser kept aircraft on the rim and dropped ones inside it, and the
     * aircraft overhead was as likely as any other to be the one thrown
     * away. Unnoticed because the 16 KB poll buffer truncated every response
     * that big anyway (see POLL_BUF_SZ). Once full, a newcomer replaces the
     * farthest kept aircraft if it is nearer; one with no distance never
     * displaces one with a distance, which is cmp_dst_nm's rule too. */
    int count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr)
    {
        if (!cJSON_IsObject(item)) {
            continue;
        }

        aircraft_t tmp;
        aircraft_t *ac = &tmp;
        memset(ac, 0, sizeof(*ac));

        copy_str(ac->hex, sizeof ac->hex, str_field(item, "hex"));
        copy_trim_upper(ac->flight, sizeof ac->flight, str_field(item, "flight"));
        copy_str(ac->type, sizeof ac->type, str_field(item, "t"));
        copy_str(ac->reg, sizeof ac->reg, str_field(item, "r"));

        cJSON *alt_baro = cJSON_GetObjectItemCaseSensitive(item, "alt_baro");
        if (alt_baro == NULL) {
            ac->alt_ft = ALT_UNKNOWN;
        } else if (cJSON_IsString(alt_baro)) {
            /* "ground" is the only documented string value; anything else
             * unexpected-but-a-string is treated as unknown rather than
             * guessed at. */
            ac->alt_ft = (alt_baro->valuestring != NULL &&
                          strcmp(alt_baro->valuestring, "ground") == 0)
                             ? ALT_GROUND
                             : ALT_UNKNOWN;
        } else if (cJSON_IsNumber(alt_baro)) {
            ac->alt_ft = (int32_t)alt_baro->valuedouble;
        } else {
            ac->alt_ft = ALT_UNKNOWN;
        }

        copy_str(ac->category, sizeof ac->category,
                 str_field(item, "category"));

        /* Not everything in the feed is an aircraft, and the list is sorted by
         * distance, so a non-aircraft near the house becomes the default screen.
         * In our own 30 nm capture the NEAREST target was "FFMSNE" — t="TWR",
         * type="mlat", no groundspeed: a fixed ground reference transmitter used
         * for MLAT synchronisation. Shown as-is it reads as a plane overhead.
         *   - t == "TWR" is the community convention for that beacon
         *   - ICAO category C* is surface vehicles and fixed obstacles
         * Dropped here, at the boundary, so no consumer has to know. */
        if (strcmp(ac->type, "TWR") == 0 || ac->category[0] == 'C') {
            continue;
        }

        cJSON *dst = cJSON_GetObjectItemCaseSensitive(item, "dst");
        ac->dst_nm = cJSON_IsNumber(dst) ? (float)dst->valuedouble : DST_UNKNOWN;

        cJSON *dir = cJSON_GetObjectItemCaseSensitive(item, "dir");
        ac->dir_deg = cJSON_IsNumber(dir) ? (float)dir->valuedouble : 0.0f;

        cJSON *track = cJSON_GetObjectItemCaseSensitive(item, "track");
        if (cJSON_IsNumber(track)) {
            ac->track_deg = (float)track->valuedouble;
            ac->has_track = true;
        } else {
            ac->track_deg = 0.0f;
            ac->has_track = false;
        }

        cJSON *gs = cJSON_GetObjectItemCaseSensitive(item, "gs");
        ac->gs_kt = cJSON_IsNumber(gs) ? (int32_t)gs->valuedouble : -1;

        cJSON *lat = cJSON_GetObjectItemCaseSensitive(item, "lat");
        ac->lat = cJSON_IsNumber(lat) ? lat->valuedouble : 0.0;

        cJSON *lon = cJSON_GetObjectItemCaseSensitive(item, "lon");
        ac->lon = cJSON_IsNumber(lon) ? lon->valuedouble : 0.0;

        if (count < max) {
            out[count++] = tmp;
            continue;
        }
        int worst = 0;
        for (int i = 1; i < count; i++) {
            if (cmp_dst_nm(&out[i], &out[worst]) > 0) {
                worst = i;
            }
        }
        if (cmp_dst_nm(&tmp, &out[worst]) < 0) {
            out[worst] = tmp;
        }
    }

    cJSON_Delete(root);

    qsort(out, (size_t)count, sizeof(aircraft_t), cmp_dst_nm);

    return count;
}
