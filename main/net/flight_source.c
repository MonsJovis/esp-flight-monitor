#include "flight_source.h"
#include "wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

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
#include "ground_filter.h"
#include "http_get.h"
#include "route_parse.h"
#include "source_logic.h"

static const char *TAG = "flight_source";

/* POLL_HTTP_TIMEOUT_MS and POLL_BUF_SZ live in flight_source.h, so the 'p'
 * probe can make exactly this request. The reasoning for the 25 s is there.
 *
 * THE COMMENT THAT USED TO BE HERE claimed the timeout was "kept below
 * SRC_POLL_INTERVAL_MS so a slow poll can never overlap the next one". It is
 * now 25 s against a 12 s cadence, so by its own words it should be a bug —
 * and it never was, because the invariant was never real: poll_task() is one
 * task running http_get() and then vTaskDelay(SRC_POLL_INTERVAL_MS) in
 * sequence, and a sequence cannot overlap itself. A slow poll delays the next
 * one; it does not race it. A plausible-sounding invariant that nothing
 * enforces is worse than none, because the next person to raise the timeout
 * reads it and believes they have broken something (AGENTS.md §11 rule 1). */
/* Same reasoning as POLL_HTTP_TIMEOUT_MS in the header, and the same measured
 * link: eight seconds on a marginal connection means "ROUTE WIRD GESUCHT"
 * that never resolves, which is the one thing §5.2's searching state was
 * built to avoid becoming permanent. */
#define ROUTE_HTTP_TIMEOUT_MS  20000

/* AGENTS.md §6: a 30 nm poll is ~4-8 KB; 16 KB leaves headroom for the
 * 100 nm case without letting a malicious/broken response grow unbounded. */


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
    /* False until this callsign has actually been sent to the API at least
     * once. A never-asked callsign is why the panel says "ROUTE WIRD GESUCHT"
     * right now, so it earns the fast interval; a repeat is just a retry and
     * keeps the slow one. Set on any completed attempt, success or failure, so
     * a failing POST cannot loop on the fast path. */
    bool           asked;
} route_cache_entry_t;

static struct {
    SemaphoreHandle_t mutex;

    /* Poll target. Rarely written (flight_source_set_location), read every
     * poll -- guarded by mutex either way, it's cheap. */
    double  lat, lon;
    int     radius_nm;
    /* Bumped whenever the poll point moves (flight_source_set_location). A
     * poll remembers the generation it was built for and throws its answer
     * away if the device has moved while it was on the wire: that answer's
     * dst/dir are measured from the OLD point, and published they would draw
     * the old place's sky around the new one. */
    uint32_t loc_gen;
    int64_t  last_request_ms;  /* start of the last poll request, 0 = none */
    TaskHandle_t task;         /* woken early when the location moves */

    /* Published snapshot -- what flight_source_snapshot() copies out. */
    aircraft_t aircraft[MAX_AIRCRAFT];
    int        aircraft_count;
    int        aircraft_radius_nm; /* the radius the snapshot was polled at */

    /* Route cache: front-packed array, evict-oldest on overflow. */
    route_cache_entry_t routes[ROUTE_CACHE_MAX];
    int                  route_count;
    int64_t              last_route_post_ms; /* 0 = never posted */
    int64_t              last_route_save_ms; /* 0 = never persisted */
    bool                 routes_dirty;       /* a route resolved since the save */

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
    int n_never_asked = 0;
    for (int i = 0; i < s.route_count; i++) {
        if (s.routes[i].status == ROUTE_STATUS_RESOLVING) {
            memset(&batch[n_batch], 0, sizeof batch[n_batch]);
            strncpy(batch[n_batch].flight, s.routes[i].callsign, sizeof batch[n_batch].flight - 1);
            batch[n_batch].lat = s.routes[i].lat;
            batch[n_batch].lon = s.routes[i].lon;
            if (!s.routes[i].asked) {
                n_never_asked++;
            }
            n_batch++;
        }
    }

    int64_t since_last_post = (s.last_route_post_ms == 0) ? INT64_MAX : (now_ms - s.last_route_post_ms);
    bool do_post = source_should_post_routes_ex(n_batch, n_never_asked, since_last_post);

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
    for (int i = 0; i < n_batch; i++) {
        route_cache_entry_t *e = cache_find_locked(batch[i].flight);
        if (e != NULL) {
            e->asked = true;
        }
    }
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
        /* Say what came back. "malformed JSON" alone cannot distinguish a body
         * cut short by the link from an error page the API returned instead. */
        ESP_LOGW(TAG, "routeset parse failed: %d bytes, starts \"%.40s\"",
                 resp_len, resp_buf);
        if (resp_len >= 24) {
            ESP_LOGW(TAG, "  ...ends \"%.24s\"", resp_buf + resp_len - 24);
        }
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
        if (e->status == ROUTE_STATUS_RESOLVED) {
            s.routes_dirty = true;
        }
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

/* ---- Route cache persistence (PLAN.md M4) --------------------------------
 *
 * A route never changes mid-flight, so a cache that survives a reboot means the
 * aircraft still overhead keeps its route instead of showing "ROUTE WIRD
 * GESUCHT" again while the API is re-asked. It also costs the free service one
 * fewer batch every time the device restarts.
 *
 * Only RESOLVED entries are stored. An unanswered lookup is worth nothing
 * across a reboot, and a persisted "no flight plan" would freeze a wrong answer
 * in flash. Writes are debounced and only happen when the set actually changed
 * — and D29 measured NVS commits at 3 us of render impact, so this is cheap.
 */
#define ROUTE_NVS_NAMESPACE  "flight"
#define ROUTE_NVS_KEY        "routes"
#define ROUTE_NVS_VERSION    1u
#define ROUTE_NVS_MAX        24        /* ~2.8 KB; well past a 30 nm ring */
#define ROUTE_SAVE_MIN_MS    60000

typedef struct {
    char    callsign[9];
    route_t route;
} route_persist_t;

typedef struct {
    uint32_t        version;
    uint32_t        count;
    route_persist_t entries[ROUTE_NVS_MAX];
} route_blob_t;

static void route_cache_load(void)
{
    nvs_handle_t h;
    if (nvs_open(ROUTE_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;                       /* nothing stored yet — normal first boot */
    }
    route_blob_t *blob = heap_caps_malloc(sizeof *blob, MALLOC_CAP_SPIRAM);
    if (blob == NULL) { nvs_close(h); return; }

    size_t len = sizeof *blob;
    esp_err_t err = nvs_get_blob(h, ROUTE_NVS_KEY, blob, &len);
    nvs_close(h);

    if (err != ESP_OK || len != sizeof *blob || blob->version != ROUTE_NVS_VERSION) {
        /* A stale format is not an error worth shouting about: drop it and
         * rebuild from the network, which takes one poll. */
        free(blob);
        return;
    }

    uint32_t n = blob->count > ROUTE_NVS_MAX ? ROUTE_NVS_MAX : blob->count;
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    for (uint32_t i = 0; i < n && s.route_count < ROUTE_CACHE_MAX; i++) {
        route_cache_entry_t *e = &s.routes[s.route_count++];
        memset(e, 0, sizeof *e);
        strncpy(e->callsign, blob->entries[i].callsign, sizeof e->callsign - 1);
        e->route  = blob->entries[i].route;
        e->status = ROUTE_STATUS_RESOLVED;
        e->used   = true;
        e->asked  = true;
    }
    xSemaphoreGive(s.mutex);
    ESP_LOGI(TAG, "restored %u cached route(s) from NVS", (unsigned)n);
    free(blob);
}

static void route_cache_save_if_due(int64_t now_ms)
{
    if (!s.routes_dirty) {
        return;
    }
    if (s.last_route_save_ms != 0 && now_ms - s.last_route_save_ms < ROUTE_SAVE_MIN_MS) {
        return;
    }

    route_blob_t *blob = heap_caps_calloc(1, sizeof *blob, MALLOC_CAP_SPIRAM);
    if (blob == NULL) {
        return;
    }
    blob->version = ROUTE_NVS_VERSION;

    xSemaphoreTake(s.mutex, portMAX_DELAY);
    for (int i = 0; i < s.route_count && blob->count < ROUTE_NVS_MAX; i++) {
        if (s.routes[i].status != ROUTE_STATUS_RESOLVED) {
            continue;
        }
        route_persist_t *d = &blob->entries[blob->count++];
        strncpy(d->callsign, s.routes[i].callsign, sizeof d->callsign - 1);
        d->route = s.routes[i].route;
    }
    s.routes_dirty = false;
    s.last_route_save_ms = now_ms;
    xSemaphoreGive(s.mutex);

    nvs_handle_t h;
    if (nvs_open(ROUTE_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        /* Deliberately NOT pausing LVGL around this: measured at 3 us of render
         * impact on this configuration, while holding the display lock across a
         * commit stalls rendering for seconds. See docs/DECISIONS.md D29. */
        if (nvs_set_blob(h, ROUTE_NVS_KEY, blob, sizeof *blob) == ESP_OK) {
            nvs_commit(h);
            ESP_LOGI(TAG, "persisted %u route(s)", (unsigned)blob->count);
        }
        nvs_close(h);
    }
    free(blob);
}

/* ---- Poll loop ----------------------------------------------------------- */

typedef struct {
    ground_memory_t *mem;
    uint32_t         now_s;   /* + 1 so it is never 0, which ground_keep rejects */
    int              hidden;  /* on the ground and not just landed, this poll */
    int              landed;  /* on the ground and seen landing: shown */
} ground_ctx_t;

static bool keep_aircraft(const aircraft_t *ac, void *ctx)
{
    ground_ctx_t *gc = ctx;
    bool keep = ground_keep(gc->mem, ac, gc->now_s);
    if (!keep) {
        gc->hidden++;
    } else if (ac->alt_ft == ALT_GROUND) {
        gc->landed++;
    }
    return keep;
}

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

    /* Who has been seen flying lately, so a plane on the ground can be shown
     * only if it has just landed (D83, ground_filter.h). PSRAM: 1.9 KB this
     * board's internal heap has better uses for (D81's memory table). If it
     * cannot be had, the filter is off and ground traffic shows as before. */
    ground_memory_t *ground = heap_caps_calloc(1, sizeof *ground, MALLOC_CAP_SPIRAM);
    if (ground == NULL) {
        ESP_LOGW(TAG, "no memory for the ground filter -- showing all ground traffic");
    }

    bool was_connected = false;

    for (;;) {
        /* A wake-up that arrived while the last poll was running has already
         * been served by reaching this line: drop it, or the wait at the end
         * of THIS iteration would return at once and poll twice in a row. */
        ulTaskNotifyTake(pdTRUE, 0);

        /* Failures accumulated while the radio was down are not evidence that
         * the API is unhappy with us, so they must not keep us in a five-minute
         * backoff once the network comes back. Without this, a router reboot —
         * or a first-time provisioning, which is exactly how this surfaced —
         * leaves the panel blank for minutes after the WiFi is fine again.
         * AGENTS.md §5's backoff is about protecting a free community service
         * from OUR retries; it should not punish us for their outage. */
        bool connected = wifi_is_connected();
        if (connected && !was_connected) {
            xSemaphoreTake(s.mutex, portMAX_DELAY);
            if (s.consec_failures > 0) {
                ESP_LOGI(TAG, "network back — clearing %d failure(s) and polling now",
                         s.consec_failures);
                s.consec_failures = 0;
            }
            xSemaphoreGive(s.mutex);
        }
        was_connected = connected;

        /* No radio, nothing to say: wait for it rather than burning a request
         * and counting a failure the source had nothing to do with. */
        if (!connected) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        /* The one place an early poll is held back. The normal cadence is far
         * slower than this; only a wake-up (a move) or a discarded answer can
         * bring two requests this close, and adsb.lol throttles bursts
         * (AGENTS.md §5) — two quick moves must not become a burst. */
        xSemaphoreTake(s.mutex, portMAX_DELAY);
        int64_t since_ms = esp_timer_get_time() / 1000 - s.last_request_ms;
        bool any_before = s.last_request_ms != 0;
        xSemaphoreGive(s.mutex);
        if (any_before && since_ms >= 0 && since_ms < SRC_MOVE_MIN_GAP_MS) {
            vTaskDelay(pdMS_TO_TICKS(SRC_MOVE_MIN_GAP_MS - since_ms));
        }

        xSemaphoreTake(s.mutex, portMAX_DELAY);
        double lat = s.lat, lon = s.lon;
        int radius = s.radius_nm;
        uint32_t gen = s.loc_gen;
        source_id_t src = s.active_source;
        /* Stamped under the lock the early wake-up reads it under. */
        s.last_request_ms = esp_timer_get_time() / 1000;
        xSemaphoreGive(s.mutex);

        /* Says when the first request for a new place actually leaves, so
         * "how long did the screen wait after a move" can be read off the log
         * rather than guessed. */
        static uint32_t logged_gen;
        if (gen != logged_gen) {
            logged_gen = gen;
            ESP_LOGW(TAG, "moved: polling the new place now (%.4f/%.4f)", lat, lon);
        }

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
            ground_ctx_t gc = { ground, (uint32_t)(esp_timer_get_time() / 1000000) + 1, 0, 0 };
            ac_n = adsb_parse_ex(poll_buf, (size_t)n, local_ac, MAX_AIRCRAFT,
                                 keep_aircraft, &gc);
            if (gc.hidden > 0 || gc.landed > 0) {
                ESP_LOGI(TAG, "on the ground: %d hidden, %d shown as just landed (D83)",
                         gc.hidden, gc.landed);
            }
            if (ac_n < 0) {
                /* Almost always a body cut short by the link rather than a
                 * genuinely malformed feed, so say how much arrived and how it
                 * ended — that distinguishes the two without a packet capture. */
                ESP_LOGW(TAG, "%s: parse failed after %d bytes (ends: \"%.16s\")",
                         source_name(src), n, n >= 16 ? poll_buf + n - 16 : poll_buf);
            } else {
                success = true;
            }
        }

        /* Smallest free stack this task has ever had, in bytes. Keep an eye on
         * it: the parse depth scales with how busy the sky is. */
        ESP_LOGI(TAG, "stack headroom: %u B",
                 (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));

        int64_t now = esp_timer_get_time() / 1000;
        route_cache_save_if_due(now);

        xSemaphoreTake(s.mutex, portMAX_DELAY);
        if (s.loc_gen != gen) {
            /* He moved the device while this request was out. Whatever came
             * back — aircraft or a failure — is about the place he left, so it
             * is neither published nor counted. set_location() has already
             * cleared the snapshot and woken us; go straight round. */
            xSemaphoreGive(s.mutex);
            ESP_LOGW(TAG, "location changed during the poll, answer discarded");
            continue;
        }
        if (success) {
            s.consec_failures = 0;
            s.last_success_ms = now;
            s.aircraft_count = ac_n;
            s.aircraft_radius_nm = radius;
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

        /* A notification, not a vTaskDelay, so a location change is answered
         * now rather than after a twelve-second (or five-minute) wait. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(source_backoff_delay_ms(failures)));
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

    /* 8192 overflowed for real, on the first poll that actually reached the
     * network: esp_http_client's connect path plus a cJSON parse of an 8 KB
     * document does not fit. It crashed and rebooted before any poll completed,
     * which presented as an endless run of ESP_ERR_HTTP_CONNECT rather than as a
     * stack problem. The task logs its own high-water mark each poll so this
     * number stays honest rather than superstitious. */
    /* Before the task starts, so the first poll already has whatever survived
     * the last power cycle. */
    route_cache_load();

    BaseType_t ok = xTaskCreate(flight_source_task, "flight_source", 16384, NULL, 5, &s.task);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* ---- "is there a poller at all?" ---------------------------------------
 *
 * Every public accessor below takes s.mutex, and s.mutex does not exist until
 * flight_source_start() creates it. That is not a theoretical window: main.c
 * only starts the poller when wifi_start() succeeds, so a device with NO
 * STORED CREDENTIALS — the state every brand-new board is in — never calls
 * flight_source_start() at all. The first ui_task tick two seconds later
 * called flight_source_snapshot(), which called xQueueSemaphoreTake(NULL),
 * and FreeRTOS asserted.
 *
 * Reproduced on hardware before fixing: four reboots in eighteen seconds,
 * `assert failed: xQueueSemaphoreTake queue.c:1709 (( pxQueue ))`, with
 * "no WiFi credentials stored — press 'w' to provision" scrolling past in
 * between. The device was unusable out of the box, and because the console
 * message was being washed away by the reboot loop, recovering it over serial
 * was a race as well.
 *
 * The answer is not to make the callers check. It is for this module to have
 * one honest answer for "I have not been started": no aircraft, no failures,
 * not stale, still resolving. Every one of those is true of a poller that
 * does not exist, and every one of them is what the screen already knows how
 * to render — AGENTS.md §1's "never show an empty screen" path handles it,
 * because that path was built for exactly this situation. */
static bool source_ready(void)
{
    return s.mutex != NULL;
}

void flight_source_set_location(double lat, double lon, int radius_nm)
{
    /* Not started: nothing to set, and nothing is lost — flight_source_start()
     * takes the location as arguments and zeroes `s` anyway. */
    if (!source_ready()) {
        return;
    }
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    /* apply_settings() calls this for every settings change — brightness,
     * the night window — so only a real move of the poll point counts. The
     * geocoder hands back five decimals and the presets are constants, so an
     * exact compare is not fragile here. */
    bool moved = (lat != s.lat || lon != s.lon);
    s.lat = lat;
    s.lon = lon;
    /* A radius change alone keeps the snapshot and the cadence: the aircraft
     * in it are still measured from the right point, the screens drop the
     * ones beyond a smaller ring themselves, and a bigger ring fills in at
     * the next poll. Not worth an extra request per slider release. */
    s.radius_nm = radius_nm;
    if (moved) {
        /* Everything published is the old place's sky. Clear it rather than
         * leave it up: its distances and bearings are from the old point, so
         * it would be drawn around the new one as if it were there. With no
         * snapshot and no success the screens say they are looking (D82),
         * which is true. The failures belonged to the old request too, and a
         * five-minute backoff must not keep him waiting at the new place. */
        s.loc_gen++;
        s.aircraft_count = 0;
        s.last_success_ms = 0;
        s.consec_failures = 0;
    }
    TaskHandle_t task = s.task;
    xSemaphoreGive(s.mutex);

    /* Poll now. Never blocks: the caller is usually the LVGL thread (a tap
     * on a search hit), and the poller enforces its own minimum gap. */
    if (moved && task != NULL) {
        xTaskNotifyGive(task);
    }
}

uint32_t flight_source_location_gen(void)
{
    if (!source_ready()) {
        return 0;
    }
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    uint32_t g = s.loc_gen;
    xSemaphoreGive(s.mutex);
    return g;
}

int flight_source_snapshot(aircraft_t *out, int max, route_t *routes, int max_routes)
{
    if (!source_ready()) {
        return 0;
    }

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
    if (!source_ready()) {
        return ROUTE_STATUS_RESOLVING;
    }

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
    if (!source_ready()) {
        return 0;
    }

    xSemaphoreTake(s.mutex, portMAX_DELAY);
    int f = s.consec_failures;
    xSemaphoreGive(s.mutex);
    return f;
}

const char *flight_source_current_source_name(void)
{
    if (!source_ready()) {
        return source_name(source_next_enabled((source_id_t)(SRC_COUNT - 1)));
    }
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    source_id_t src = s.active_source;
    xSemaphoreGive(s.mutex);
    return source_name(src);
}

int64_t flight_source_last_success_ms(void)
{
    if (!source_ready()) {
        return 0;
    }
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    int64_t t = s.last_success_ms;
    xSemaphoreGive(s.mutex);
    return t;
}

int flight_source_data_radius_nm(void)
{
    if (!source_ready()) {
        return 0;
    }
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    int r = s.aircraft_radius_nm;
    xSemaphoreGive(s.mutex);
    return r;
}

bool flight_source_has_data(void)
{
    return flight_source_last_success_ms() != 0;
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
    if (!source_ready()) {
        return false;
    }

    xSemaphoreTake(s.mutex, portMAX_DELAY);
    bool stale = s.consec_failures > 0;
    xSemaphoreGive(s.mutex);
    return stale;
}
