#include "timetable_fetch.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"

#include "http_get.h"
#include "route_parse.h"
#include "timetable.h"
#include "timetable_crypt.h"

static const char *TAG = "timetable";

/* About 36 bytes a pair and a few thousand pairs is ~100 KB; this is room
 * for TT_MAX_PAIRS of them. PSRAM, and only for the length of a fetch. */
#define TT_BUF_SZ          (TT_MAX_PAIRS * 40 + 4096)
#define TT_HTTP_TIMEOUT_MS 30000

/* The same namespace the settings and the OTA URL live in: one thing to
 * erase. */
#define TT_NVS_NS   "flight"
#define TT_NVS_KEY  "tt_key"

static SemaphoreHandle_t s_mutex;
/* Guarded by s_mutex. The network task swaps in a whole new table; the UI
 * task reads it. Nothing edits a table in place. */
static tt_pair_t *s_tt;
static int        s_n;
static int64_t    s_generated;
/* Guarded by s_mutex too: the console writes it, the network task reads it. */
static uint8_t    s_key[TT_KEY_LEN];
static bool       s_have_key;
static bool       s_fetch_now;
/* Only the network task touches these. */
static int64_t    s_last_try_ms;     /* 0 = never tried */
static bool       s_said_no_key;

/* The last few callsigns that got a number, so the log says "AUA25L is OS 254"
 * once rather than every two seconds. UI task only. */
#define TT_LOGGED 16
static char s_logged[TT_LOGGED][9];
static int  s_logged_next;

void timetable_init(void)
{
    if (s_mutex != NULL) {
        return;
    }
    s_mutex = xSemaphoreCreateMutex();

    nvs_handle_t h;
    if (nvs_open(TT_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof s_key;
        s_have_key = nvs_get_blob(h, TT_NVS_KEY, s_key, &len) == ESP_OK &&
                     len == sizeof s_key;
        nvs_close(h);
    }
    ESP_LOGI(TAG, "key %s", s_have_key ? "stored" : "not stored (console: T)");
}

bool timetable_has_key(void)
{
    if (s_mutex == NULL) {
        return false;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool have = s_have_key;
    xSemaphoreGive(s_mutex);
    return have;
}

bool timetable_set_key(const uint8_t key[TT_KEY_LEN])
{
    if (s_mutex == NULL || key == NULL) {
        return false;
    }
    nvs_handle_t h;
    if (nvs_open(TT_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_blob(h, TT_NVS_KEY, key, TT_KEY_LEN);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        return false;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(s_key, key, TT_KEY_LEN);
    s_have_key  = true;
    s_fetch_now = true;
    xSemaphoreGive(s_mutex);
    return true;
}

bool timetable_clear_key(void)
{
    if (s_mutex == NULL) {
        return false;
    }
    nvs_handle_t h;
    if (nvs_open(TT_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, TT_NVS_KEY);
        nvs_commit(h);
        nvs_close(h);
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    mbedtls_platform_zeroize(s_key, sizeof s_key);
    s_have_key = false;
    /* Without the key the pairs it opened are not ours to keep either. */
    free(s_tt);
    s_tt = NULL;
    s_n  = 0;
    s_generated = 0;
    xSemaphoreGive(s_mutex);
    return true;
}

void timetable_fetch_if_due(void)
{
    if (s_mutex == NULL) {
        return;
    }
    uint8_t key[TT_KEY_LEN];
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool have = s_have_key;
    bool now_asked = s_fetch_now;
    s_fetch_now = false;
    if (have) {
        memcpy(key, s_key, sizeof key);
    }
    xSemaphoreGive(s_mutex);

    if (!have) {
        if (!s_said_no_key) {
            ESP_LOGW(TAG, "no key stored, so nothing is fetched (console: T)");
            s_said_no_key = true;
        }
        return;
    }
    s_said_no_key = false;

    int64_t now_ms = esp_timer_get_time() / 1000;
    if (!now_asked && s_last_try_ms != 0 &&
        now_ms - s_last_try_ms < TIMETABLE_FETCH_EVERY_MS) {
        mbedtls_platform_zeroize(key, sizeof key);
        return;
    }
    /* Stamped before the request, so a failure waits the full hour too: one
     * request an hour to a CDN is nothing, and a retry loop would be. */
    s_last_try_ms = now_ms;

    uint8_t   *buf   = heap_caps_malloc(TT_BUF_SZ, MALLOC_CAP_SPIRAM);
    tt_pair_t *fresh = heap_caps_malloc(sizeof(tt_pair_t) * TT_MAX_PAIRS,
                                        MALLOC_CAP_SPIRAM);
    if (buf == NULL || fresh == NULL) {
        ESP_LOGW(TAG, "no memory for the timetable; trying again in an hour");
        goto done;
    }

    int  status    = 0;
    bool truncated = false;
    int  len = http_get(TIMETABLE_URL, (char *)buf, TT_BUF_SZ, TT_HTTP_TIMEOUT_MS,
                        &status, &truncated);
    if (len < 0 || status != 200) {
        /* A 404 is the normal state until the workflow has run (D95). */
        ESP_LOGW(TAG, "not fetched (n=%d, http=%d); aircraft keep their callsigns",
                 len, status);
        goto done;
    }
    if (truncated) {
        ESP_LOGW(TAG, "larger than %d B; not using a cut-off timetable", TT_BUF_SZ - 1);
        goto done;
    }

    int plain = timetable_decrypt(key, buf, (size_t)len);
    if (plain < 0) {
        /* -3 is a wrong key, or a file written with another one. */
        ESP_LOGW(TAG, "could not open the timetable (%d, %d B)", plain, len);
        goto done;
    }

    int64_t gen = 0;
    int n = timetable_parse((const char *)buf, (size_t)plain, fresh, TT_MAX_PAIRS, &gen);
    if (n < 0) {
        ESP_LOGW(TAG, "opened, but not a timetable this firmware reads");
        goto done;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    tt_pair_t *old = s_tt;
    s_tt        = fresh;
    s_n         = n;
    s_generated = gen;
    xSemaphoreGive(s_mutex);
    fresh = old;                  /* the table it replaced, freed below */
    ESP_LOGI(TAG, "%d pair(s) loaded, %d B, generated at %lld", n, len, (long long)gen);

done:
    mbedtls_platform_zeroize(key, sizeof key);
    if (buf != NULL) {
        mbedtls_platform_zeroize(buf, TT_BUF_SZ);
    }
    free(buf);
    free(fresh);
}

static void log_once(const char *callsign, const char *number)
{
    for (int i = 0; i < TT_LOGGED; i++) {
        if (strcmp(s_logged[i], callsign) == 0) {
            return;
        }
    }
    strncpy(s_logged[s_logged_next], callsign, sizeof s_logged[0] - 1);
    s_logged[s_logged_next][sizeof s_logged[0] - 1] = '\0';
    s_logged_next = (s_logged_next + 1) % TT_LOGGED;
    ESP_LOGI(TAG, "%s is %s", callsign, number);
}

int timetable_annotate(aircraft_t *ac, int n, const route_t *rt, int n_rt,
                       int64_t now_utc)
{
    if (ac == NULL || n <= 0) {
        return 0;
    }
    for (int i = 0; i < n; i++) {
        ac[i].flight_no[0] = '\0';
    }
    if (s_mutex == NULL || rt == NULL) {
        return 0;
    }

    int hits = 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_tt != NULL && s_n > 0) {
        for (int i = 0; i < n; i++) {
            const route_t *r = route_find(rt, n_rt, ac[i].flight);
            if (r != NULL &&
                timetable_lookup(s_tt, s_n, ac[i].flight, r, now_utc, ac[i].flight_no,
                                 sizeof ac[i].flight_no) > 0) {
                hits++;
            }
        }
    }
    xSemaphoreGive(s_mutex);

    for (int i = 0; i < n; i++) {
        if (ac[i].flight_no[0] != '\0') {
            log_once(ac[i].flight, ac[i].flight_no);
        }
    }
    return hits;
}

void timetable_status(int *pairs, int64_t *generated)
{
    int     p = 0;
    int64_t g = 0;
    if (s_mutex != NULL) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        p = s_n;
        g = s_generated;
        xSemaphoreGive(s_mutex);
    }
    if (pairs != NULL) {
        *pairs = p;
    }
    if (generated != NULL) {
        *generated = g;
    }
}
