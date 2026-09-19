#include "ota_policy.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "compat.h"

static const char *TAG = "ota";

/* ---- manifest ---------------------------------------------------------- */

static bool copy_field(const cJSON *obj, const char *key, char *out, size_t outsz)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(it) || it->valuestring == NULL) {
        ESP_LOGW(TAG, "manifest: \"%s\" missing or not a string", key);
        return false;
    }
    size_t n = strlen(it->valuestring);
    if (n == 0 || n >= outsz) {
        /* Silently truncating a URL produces a request to a DIFFERENT address
         * that may well answer. Refuse instead. */
        ESP_LOGW(TAG, "manifest: \"%s\" is %u bytes, need 1..%u",
                 key, (unsigned)n, (unsigned)(outsz - 1));
        return false;
    }
    memcpy(out, it->valuestring, n + 1);
    return true;
}

bool ota_manifest_parse(const char *json, size_t len, ota_manifest_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof *out);
    if (json == NULL || len == 0) {
        return false;
    }

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (root == NULL) {
        ESP_LOGW(TAG, "manifest: not JSON");
        return false;
    }

    bool ok = cJSON_IsObject(root) &&
              copy_field(root, "version", out->version, sizeof out->version) &&
              copy_field(root, "url", out->url, sizeof out->url);

    if (ok) {
        /* Optional. A manifest without it still works; the size is only used
         * to reject an obviously wrong image before spending a flash cycle. */
        const cJSON *sz = cJSON_GetObjectItemCaseSensitive(root, "size");
        if (cJSON_IsNumber(sz) && sz->valuedouble > 0 &&
            sz->valuedouble < 16.0 * 1024 * 1024) {
            out->size = (uint32_t)sz->valuedouble;
        }
    }

    cJSON_Delete(root);
    if (!ok) {
        memset(out, 0, sizeof *out);
    }
    return ok;
}

/* ---- version comparison ------------------------------------------------ */

/* Reads one numeric component and advances *p past it and any single
 * separating '.'. Stops at anything that is not a digit or a dot, which is
 * how "0.4.2-3-gdeadbee" compares equal to "0.4.2". */
static int next_component(const char **p, bool *ran_out)
{
    const char *s = *p;
    if (!isdigit((unsigned char)*s)) {
        *ran_out = true;
        return 0;                       /* missing components are zero */
    }
    int v = 0;
    while (isdigit((unsigned char)*s)) {
        /* Clamp rather than overflow. A version number this large is someone
         * else's bug, and wrapping it would make an old build look new. */
        if (v < 100000) {
            v = v * 10 + (*s - '0');
        }
        s++;
    }
    if (*s == '.') {
        s++;
    }
    *p = s;
    return v;
}

int ota_version_cmp(const char *a, const char *b)
{
    if (a == NULL) a = "";
    if (b == NULL) b = "";
    if (*a == 'v' || *a == 'V') a++;
    if (*b == 'v' || *b == 'V') b++;

    /* Four components is more than semver needs and costs nothing. */
    for (int i = 0; i < 4; i++) {
        bool a_out = false, b_out = false;
        int va = next_component(&a, &a_out);
        int vb = next_component(&b, &b_out);
        if (va != vb) {
            return (va < vb) ? -1 : 1;
        }
        if (a_out && b_out) {
            break;
        }
    }
    return 0;
}

/* ---- when ------------------------------------------------------------- */

bool ota_hour_in_window(int hour, int from_hour, int to_hour)
{
    if (hour < 0 || hour > 23) {
        return false;
    }
    if (from_hour == to_hour) {
        return false;                               /* empty window */
    }
    if (from_hour < to_hour) {
        return (hour >= from_hour && hour < to_hour);
    }
    /* Wraps midnight, which is the normal case: 22:00 to 07:00. */
    return (hour >= from_hour || hour < to_hour);
}

bool ota_should_check(const ota_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->have_url || !ctx->online || !ctx->clock_valid) {
        return false;
    }
    if (ctx->last_check_ms <= 0) {
        return true;                                /* never checked */
    }
    /* Guard against a clock that jumped backwards — SNTP correcting a large
     * offset does exactly that, and an unsigned-looking comparison here would
     * park the device on "not yet" for however long the jump was. */
    int64_t since = ctx->now_ms - ctx->last_check_ms;
    if (since < 0) {
        return true;
    }
    return since >= OTA_CHECK_INTERVAL_MS;
}

bool ota_should_install(const ota_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->have_url || !ctx->online || !ctx->clock_valid) {
        return false;
    }
    if (!ctx->auto_dim) {
        return false;                               /* no night window, no safe hour */
    }
    return ota_hour_in_window(ctx->hour, ctx->dim_from_hour, ctx->dim_to_hour);
}
