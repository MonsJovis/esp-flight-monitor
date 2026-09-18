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
    if (aa->dst_nm < bb->dst_nm) {
        return -1;
    }
    if (aa->dst_nm > bb->dst_nm) {
        return 1;
    }
    return 0;
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

    int count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr)
    {
        if (count >= max) {
            break;
        }
        if (!cJSON_IsObject(item)) {
            continue;
        }

        aircraft_t *ac = &out[count];
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

        cJSON *dst = cJSON_GetObjectItemCaseSensitive(item, "dst");
        ac->dst_nm = cJSON_IsNumber(dst) ? (float)dst->valuedouble : 0.0f;

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

        count++;
    }

    cJSON_Delete(root);

    qsort(out, (size_t)count, sizeof(aircraft_t), cmp_dst_nm);

    return count;
}
