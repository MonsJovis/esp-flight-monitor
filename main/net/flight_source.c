#include "flight_source.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "adsb_parse.h"
#include "http_get.h"
#include "route_parse.h"
#include "source_logic.h"

static const char *TAG = "flight_source";

#define POLL_HTTP_TIMEOUT_MS   6000
#define ROUTE_HTTP_TIMEOUT_MS  8000

/* AGENTS.md §6: a 30 nm poll is ~4-8 KB; 16 KB leaves headroom for the
 * 100 nm case without letting a malicious/broken response grow unbounded. */
#define POLL_BUF_SZ            (16 * 1024)
#define ROUTE_REQ_BUF_SZ       4096
#define ROUTE_RESP_BUF_SZ      4096

/* Generous relative to MAX_AIRCRAFT (24): aircraft turn over through the day,
 * and a RESOLVED/NONE entry is worth keeping for the rest of a flight even
 * after the aircraft leaves the 30 nm ring (AGENTS.md §5 — "cache routes for
 * the whole flight"). */
#define ROUTE_CACHE_MAX        64

#define ROUTESET_URL "http://adsb.im/api/0/routeset"

typedef struct {
    char           callsign[9];
    route_status_t status;
    double         lat, lon; /* last known position -- routeset needs one even
                                 if the aircraft has since left the ring */
    route_t        route;    /* valid iff status == ROUTE_STATUS_RESOLVED */
    bool           used;
} route_cache_entry_t;

static struct {
    SemaphoreHandle_t mutex;

    /* Poll target. Rarely written (flight_source_set_location), read every
     * poll -- guarded by mutex either way, it's cheap. */
    double  lat, lon;
    int     radius_nm;

    /* Published snapshot -- what flight_source_snapshot() copies out. */
    aircraft_t aircraft[MAX_AIRCRAFT];
    int        aircraft_count;

    /* Route cache: front-packed array, evict-oldest on overflow. */
    route_cache_entry_t routes[ROUTE_CACHE_MAX];
    int                  route_count;
    int64_t              last_route_post_ms; /* 0 = never posted */

    /* Health. */
    source_id_t active_source;
    int         consec_failures;
    int64_t     last_success_ms; /* 0 = never succeeded */
} s;

static bool g_started;

/* ---- Route cache (mutex must already be held by the caller) ------------ */

static route_cache_entry_t *cache_find_locked(const char *callsign)
{
    if (callsign == NULL) {
        return NULL;
    }
    for (int i = 0; i < s.route_count; i++) {
        if (strcasecmp(s.routes[i].callsign, callsign) == 0) {
            return &s.routes[i];
        }
    }
    return NULL;
}

/* Registers a callsign as RESOLVING with its current position. If the cache
 * is full, evicts the oldest entry (index 0) -- a simple, adequate policy
 * given ROUTE_CACHE_MAX is generous relative to MAX_AIRCRAFT. */
static void cache_insert_locked(const char *callsign, double lat, double lon)
{
    if (s.route_count >= ROUTE_CACHE_MAX) {
        memmove(&s.routes[0], &s.routes[1], sizeof(s.routes[0]) * (size_t)(ROUTE_CACHE_MAX - 1));
        s.route_count = ROUTE_CACHE_MAX - 1;
    }
    route_cache_entry_t *e = &s.routes[s.route_count++];
    memset(e, 0, sizeof *e);
    strncpy(e->callsign, callsign, sizeof e->callsign - 1);
    e->status = ROUTE_STATUS_RESOLVING;
    e->lat = lat;
    e->lon = lon;
    e->used = true;
}

/* ---- Route resolution ---------------------------------------------------
 *
 * Batched and rate-limited (AGENTS.md §5: "one POST every few minutes, not
 * one per poll"). Every poll, newly-seen callsigns are registered as
 * RESOLVING immediately (so flight_source_route_status() reflects "asked"
 * right away); the actual POST only fires when source_should_post_routes()
 * says enough time has passed, and it always asks about every callsign
 * still RESOLVING, not just the ones that appeared this cycle -- an entry
 * that missed a rate-limited window stays queued rather than being lost.
 */
static void poll_routes(const aircraft_t *ac, int n, int64_t now_ms)
{
    xSemaphoreTake(s.mutex, portMAX_DELAY);

    for (int i = 0; i < n; i++) {
        if (ac[i].flight[0] == '\0') {
            continue;
        }
        if (cache_find_locked(ac[i].flight) == NULL) {
            cache_insert_locked(ac[i].flight, ac[i].lat, ac[i].lon);
        }
    }

    aircraft_t batch[ROUTE_CACHE_MAX];
    int n_batch = 0;
    for (int i = 0; i < s.route_count; i++) {
        if (s.routes[i].status == ROUTE_STATUS_RESOLVING) {
            memset(&batch[n_batch], 0, sizeof batch[n_batch]);
            strncpy(batch[n_batch].flight, s.routes[i].callsign, sizeof batch[n_batch].flight - 1);
            batch[n_batch].lat = s.routes[i].lat;
            batch[n_batch].lon = s.routes[i].lon;
            n_batch++;
        }
    }

    int64_t since_last_post = (s.last_route_post_ms == 0) ? INT64_MAX : (now_ms - s.last_route_post_ms);
    bool do_post = source_should_post_routes(n_batch, since_last_post);

    xSemaphoreGive(s.mutex);

    if (!do_post) {
        return;
    }

    /* In PSRAM, not .bss. These are 8 KB together, and internal SRAM is the
     * scarce resource once WiFi is up — 42 KB free, 31 KB largest block. The
     * routeset POST happens at most once every two minutes, so allocating for
     * it is far cheaper than holding the space permanently. */
    char *req_buf  = heap_caps_malloc(ROUTE_REQ_BUF_SZ,  MALLOC_CAP_SPIRAM);
    char *resp_buf = heap_caps_malloc(ROUTE_RESP_BUF_SZ, MALLOC_CAP_SPIRAM);
    /* route_t[64] is ~6.9 KB. It was a stack array, on an 8 KB task stack —
     * a latent overflow that could only fire once a routeset POST actually
     * succeeded, which had never happened yet because the device has no
     * credentials. PSRAM, like the other two. */
    route_t *results = heap_caps_malloc(sizeof(route_t) * ROUTE_CACHE_MAX,
                                        MALLOC_CAP_SPIRAM);
    if (req_buf == NULL || resp_buf == NULL || results == NULL) {
        ESP_LOGW(TAG, "no memory for routeset buffers; skipping this batch");
        free(req_buf);
        free(resp_buf);
        free(results);
        return;
    }

    int req_len = route_build_request(batch, n_batch, req_buf, ROUTE_REQ_BUF_SZ);
    if (req_len < 0) {
        ESP_LOGW(TAG, "routeset request for %d callsigns does not fit the buffer", n_batch);
        free(req_buf);
        free(resp_buf);
        return;
    }

    int status = 0;
    bool truncated = false;
    int resp_len = http_post_json(ROUTESET_URL, req_buf, resp_buf, ROUTE_RESP_BUF_SZ,
                                   ROUTE_HTTP_TIMEOUT_MS, &status, &truncated);

    xSemaphoreTake(s.mutex, portMAX_DELAY);
    s.last_route_post_ms = now_ms; /* count the attempt even on failure -- don't hammer on error */
    xSemaphoreGive(s.mutex);

    if (resp_len < 0 || status != 200) {
        ESP_LOGW(TAG, "routeset POST failed (n=%d, http=%d) -- %d callsign(s) stay queued",
                 resp_len, status, n_batch);
        goto done;
    }
    if (truncated) {
        ESP_LOGW(TAG, "routeset response truncated at %d bytes", resp_len);
    }

    int n_results = route_parse(resp_buf, (size_t)resp_len, results, ROUTE_CACHE_MAX);
    if (n_results < 0) {
        ESP_LOGW(TAG, "routeset response failed to parse");
        goto done;
    }

    xSemaphoreTake(s.mutex, portMAX_DELAY);
    for (int i = 0; i < n_batch; i++) {
        route_cache_entry_t *e = cache_find_locked(batch[i].flight);
        if (e == NULL) {
            continue; /* evicted meanwhile */
        }
        const route_t *r = route_find(results, n_results, batch[i].flight);
        if (r == NULL) {
            continue; /* no answer this round; stays RESOLVING, retried next batch */
        }
        e->route = *r;
        e->status = r->resolved ? ROUTE_STATUS_RESOLVED : ROUTE_STATUS_NONE;
    }
    xSemaphoreGive(s.mutex);

done:
    free(req_buf);
    free(resp_buf);
    free(results);
}

/* ---- Logging (PLAN.md M2 done-when) ------------------------------------- */

static void log_nearest(const aircraft_t *ac, int n)
{
    if (n <= 0) {
        ESP_LOGI(TAG, "no aircraft in range");
        return;
    }

    const aircraft_t *nearest = &ac[0]; /* adsb_parse sorts ascending by dst_nm */
    const char *type = nearest->type[0] ? nearest->type : "?";
    const char *flight = nearest->flight[0] ? nearest->flight : "?????";

    char route_str[2 * CITY_NAME_LEN + 8]; /* "orig_city -> dest_city", worst case */
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    route_cache_entry_t *e = cache_find_locked(nearest->flight);
    route_status_t st = (e != NULL) ? e->status : ROUTE_STATUS_RESOLVING;
    route_t rt;
    bool have_rt = false;
    if (e != NULL && e->status == ROUTE_STATUS_RESOLVED) {
        rt = e->route;
        have_rt = true;
    }
    xSemaphoreGive(s.mutex);

    if (have_rt) {
        /* English city names and un-translated codes are CORRECT here --
         * PLAN.md M2's done-when is explicit that a later layer (M2.5)
         * translates to German. Do not reach into main/data from here. */
        snprintf(route_str, sizeof route_str, "%s -> %s",
                 rt.orig_city[0] ? rt.orig_city : (rt.orig_icao[0] ? rt.orig_icao : "?"),
                 rt.dest_city[0] ? rt.dest_city : (rt.dest_icao[0] ? rt.dest_icao : "?"));
    } else if (st == ROUTE_STATUS_NONE) {
        snprintf(route_str, sizeof route_str, "no route");
    } else {
        snprintf(route_str, sizeof route_str, "route pending");
    }

    ESP_LOGI(TAG, "%s | %s | %s | %.1f nm %s",
             flight, type, route_str, (double)nearest->dst_nm,
             source_compass_abbrev_en(nearest->dir_deg));
}

/* ---- Poll loop ----------------------------------------------------------- */

static void flight_source_task(void *arg)
{
    (void)arg;

    char *poll_buf = heap_caps_malloc(POLL_BUF_SZ, MALLOC_CAP_SPIRAM);
    if (poll_buf == NULL) {
        poll_buf = heap_caps_malloc(POLL_BUF_SZ, MALLOC_CAP_DEFAULT);
    }
    if (poll_buf == NULL) {
        ESP_LOGE(TAG, "could not allocate %d byte poll buffer -- task exiting", POLL_BUF_SZ);
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        xSemaphoreTake(s.mutex, portMAX_DELAY);
        double lat = s.lat, lon = s.lon;
        int radius = s.radius_nm;
        source_id_t src = s.active_source;
        xSemaphoreGive(s.mutex);

        char url[160];
        if (source_build_url(src, lat, lon, radius, url, sizeof url) < 0) {
            ESP_LOGE(TAG, "failed to build poll URL -- retrying after backoff");
            vTaskDelay(pdMS_TO_TICKS(SRC_POLL_INTERVAL_MS));
            continue;
        }

        int status = 0;
        bool truncated = false;
        int n = http_get(url, poll_buf, POLL_BUF_SZ, POLL_HTTP_TIMEOUT_MS, &status, &truncated);

        bool success = false;
        aircraft_t local_ac[MAX_AIRCRAFT];
        int ac_n = 0;

        if (n < 0) {
            ESP_LOGW(TAG, "%s: request failed (%d)", source_name(src), n);
        } else if (status == 308) {
            /* Documented adsb.lol quirk (AGENTS.md §5, §7): a spurious 308
             * emitted while throttling, not a real redirect. Do NOT treat
             * any other 3xx this way -- see source_is_throttle_status(). */
            ESP_LOGW(TAG, "%s: throttled (spurious 308)", source_name(src));
        } else if (status == 429 || status == 503) {
            ESP_LOGW(TAG, "%s: throttled (HTTP %d)", source_name(src), status);
        } else if (status != 200) {
            /* Includes a genuine 3xx (e.g. a redirect to https://, which
             * http_get() never follows automatically) -- logged as a hard
             * error and backed off like any other failure, per the
             * coordinator's correction: 308-as-throttling must not
             * generalise to "any 3xx". */
            ESP_LOGW(TAG, "%s: HTTP %d, treating as failure", source_name(src), status);
        } else {
            if (truncated) {
                ESP_LOGW(TAG, "%s: response truncated at %d bytes", source_name(src), n);
            }
            ac_n = adsb_parse(poll_buf, (size_t)n, local_ac, MAX_AIRCRAFT);
            if (ac_n < 0) {
                ESP_LOGW(TAG, "%s: parse failed", source_name(src));
            } else {
                success = true;
            }
        }

        int64_t now = esp_timer_get_time() / 1000;

        xSemaphoreTake(s.mutex, portMAX_DELAY);
        if (success) {
            s.consec_failures = 0;
            s.last_success_ms = now;
            s.aircraft_count = ac_n;
            memcpy(s.aircraft, local_ac, sizeof(aircraft_t) * (size_t)ac_n);
        } else {
            s.consec_failures++;
            /* Deliberately does NOT touch s.aircraft / s.aircraft_count:
             * AGENTS.md §1 says a blank panel reads as broken, so the last
             * good snapshot stays published. flight_source_is_stale() tells
             * the UI to caution about it instead of hiding it (coordinator's
             * "degrade, don't switch" correction -- there is nowhere else to
             * switch to). */
        }
        int failures = s.consec_failures;
        xSemaphoreGive(s.mutex);

        if (success) {
            poll_routes(local_ac, ac_n, now);
            log_nearest(local_ac, ac_n);
        } else {
            ESP_LOGW(TAG, "%d consecutive failure(s), published snapshot is stale", failures);
        }

        vTaskDelay(pdMS_TO_TICKS(source_backoff_delay_ms(failures)));
    }
}

/* ---- Public API ----------------------------------------------------------- */

esp_err_t flight_source_start(double lat, double lon, int radius_nm)
{
    if (g_started) {
        ESP_LOGW(TAG, "flight_source_start called twice; ignoring");
        return ESP_ERR_INVALID_STATE;
    }
    g_started = true;

    memset(&s, 0, sizeof s);
    s.lat = lat;
    s.lon = lon;
    s.radius_nm = radius_nm;
    s.active_source = source_next_enabled((source_id_t)(SRC_COUNT - 1));

    s.mutex = xSemaphoreCreateMutex();
    if (s.mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(flight_source_task, "flight_source", 8192, NULL, 5, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void flight_source_set_location(double lat, double lon, int radius_nm)
{
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    s.lat = lat;
    s.lon = lon;
    s.radius_nm = radius_nm;
    xSemaphoreGive(s.mutex);
}

int flight_source_snapshot(aircraft_t *out, int max, route_t *routes, int max_routes)
{
    if (out == NULL || max <= 0) {
        return 0;
    }

    xSemaphoreTake(s.mutex, portMAX_DELAY);
    int n = s.aircraft_count;
    if (n > max) {
        n = max;
    }
    memcpy(out, s.aircraft, sizeof(aircraft_t) * (size_t)n);

    if (routes != NULL && max_routes > 0) {
        int rn = (n < max_routes) ? n : max_routes;
        for (int i = 0; i < rn; i++) {
            route_cache_entry_t *e = cache_find_locked(out[i].flight);
            if (e != NULL && e->status == ROUTE_STATUS_RESOLVED) {
                routes[i] = e->route;
            } else {
                memset(&routes[i], 0, sizeof routes[i]);
                strncpy(routes[i].callsign, out[i].flight, sizeof routes[i].callsign - 1);
                routes[i].resolved = false;
            }
        }
    }
    xSemaphoreGive(s.mutex);
    return n;
}

route_status_t flight_source_route_status(const char *callsign)
{
    if (callsign == NULL) {
        return ROUTE_STATUS_RESOLVING;
    }
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    route_cache_entry_t *e = cache_find_locked(callsign);
    route_status_t st = (e != NULL) ? e->status : ROUTE_STATUS_RESOLVING;
    xSemaphoreGive(s.mutex);
    return st;
}

int flight_source_consecutive_failures(void)
{
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    int f = s.consec_failures;
    xSemaphoreGive(s.mutex);
    return f;
}

const char *flight_source_current_source_name(void)
{
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    source_id_t src = s.active_source;
    xSemaphoreGive(s.mutex);
    return source_name(src);
}

int64_t flight_source_last_success_ms(void)
{
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    int64_t t = s.last_success_ms;
    xSemaphoreGive(s.mutex);
    return t;
}

int64_t flight_source_last_success_age_ms(void)
{
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    int64_t last = s.last_success_ms;
    xSemaphoreGive(s.mutex);
    if (last == 0) {
        return INT64_MAX;
    }
    return (esp_timer_get_time() / 1000) - last;
}

bool flight_source_is_stale(void)
{
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    bool stale = s.consec_failures > 0;
    xSemaphoreGive(s.mutex);
    return stale;
}
