#include "route_parse.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "compat.h"

static const char *TAG = "route_parse";

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

static bool ci_str_eq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b; /* both must have hit their NUL together */
}

int route_parse(const char *json, size_t len, route_t *out, int max)
{
    if (json == NULL || out == NULL || max <= 0) {
        return -1;
    }

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (root == NULL) {
        ESP_LOGW(TAG, "malformed JSON");
        return -1;
    }
    /* The routeset response's top level is an ARRAY, not an object. */
    if (!cJSON_IsArray(root)) {
        ESP_LOGW(TAG, "top level is not an array");
        cJSON_Delete(root);
        return -1;
    }

    int count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root)
    {
        if (count >= max) {
            break;
        }
        if (!cJSON_IsObject(item)) {
            continue;
        }

        route_t *rt = &out[count];
        memset(rt, 0, sizeof(*rt));

        copy_trim_upper(rt->callsign, sizeof rt->callsign, str_field(item, "callsign"));

        const char *airline = str_field(item, "airline_code");
        if (airline != NULL && strcmp(airline, "unknown") != 0) {
            copy_str(rt->airline_code, sizeof rt->airline_code, airline);
        }

        /* "unknown" airport_codes is a NORMAL outcome — a private/GA aircraft
         * with no flight plan, not a parse error (AGENTS.md / PLAN.md §5.2). */
        const char *codes = str_field(item, "airport_codes");
        if (codes != NULL && strcmp(codes, "unknown") != 0) {
            rt->resolved = true;

            const char *dash = strchr(codes, '-');
            if (dash != NULL) {
                size_t orig_len = (size_t)(dash - codes);
                if (orig_len > sizeof(rt->orig_icao) - 1) {
                    orig_len = sizeof(rt->orig_icao) - 1;
                }
                memcpy(rt->orig_icao, codes, orig_len);
                rt->orig_icao[orig_len] = '\0';
                copy_str(rt->dest_icao, sizeof rt->dest_icao, dash + 1);
            } else {
                /* Defensive: no separator found. Keep the whole string as
                 * origin rather than guessing at a split. */
                copy_str(rt->orig_icao, sizeof rt->orig_icao, codes);
            }
        } else {
            rt->resolved = false;
        }

        /* `_airports` can have more than two legs; take the first as origin
         * and the last as destination rather than assuming exactly two. */
        cJSON *airports = cJSON_GetObjectItemCaseSensitive(item, "_airports");
        if (cJSON_IsArray(airports)) {
            int n_airports = cJSON_GetArraySize(airports);
            if (n_airports > 0) {
                cJSON *first_ap = cJSON_GetArrayItem(airports, 0);
                cJSON *last_ap = cJSON_GetArrayItem(airports, n_airports - 1);
                copy_str(rt->orig_city, sizeof rt->orig_city,
                         str_field(first_ap, "location"));
                copy_str(rt->dest_city, sizeof rt->dest_city,
                         str_field(last_ap, "location"));
            }
        }

        /* `plausible` is absent on every unresolved entry; default false. */
        cJSON *plausible = cJSON_GetObjectItemCaseSensitive(item, "plausible");
        rt->plausible = cJSON_IsTrue(plausible) ? true : false;

        count++;
    }

    cJSON_Delete(root);
    return count;
}

const route_t *route_find(const route_t *routes, int n, const char *callsign)
{
    if (routes == NULL || callsign == NULL) {
        return NULL;
    }

    char key[sizeof ((route_t *)0)->callsign];
    copy_trim_upper(key, sizeof key, callsign);

    for (int i = 0; i < n; i++) {
        if (ci_str_eq(routes[i].callsign, key)) {
            return &routes[i];
        }
    }
    return NULL;
}

/* Appends a printf-style fragment at *pos within buf (size buf_sz), failing
 * (without partial garbage left dangling beyond *pos) if it would not fit. */
static int append(char *buf, size_t buf_sz, size_t *pos, const char *fmt, ...)
{
    if (*pos >= buf_sz) {
        return -1;
    }
    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(buf + *pos, buf_sz - *pos, fmt, ap);
    va_end(ap);
    if (written < 0 || (size_t)written >= buf_sz - *pos) {
        return -1;
    }
    *pos += (size_t)written;
    return 0;
}

int route_build_request(const aircraft_t *ac, int n, char *out, size_t out_sz)
{
    if (out == NULL || out_sz == 0 || n < 0 || (ac == NULL && n > 0)) {
        return -1;
    }

    size_t pos = 0;
    if (append(out, out_sz, &pos, "{\"planes\":[") != 0) {
        return -1;
    }

    bool first = true;
    for (int i = 0; i < n; i++) {
        if (ac[i].flight[0] == '\0') {
            continue; /* skip aircraft with an empty callsign */
        }
        if (append(out, out_sz, &pos,
                    "%s{\"callsign\":\"%s\",\"lat\":%.6f,\"lng\":%.6f}",
                    first ? "" : ",", ac[i].flight, ac[i].lat, ac[i].lon) != 0) {
            return -1;
        }
        first = false;
    }

    if (append(out, out_sz, &pos, "]}") != 0) {
        return -1;
    }

    return (int)pos;
}
