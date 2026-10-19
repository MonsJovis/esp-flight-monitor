#include "timetable_fetch.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "http_get.h"
#include "route_parse.h"
#include "timetable.h"

static const char *TAG = "timetable";

/* About 40 bytes a line and ~600 flights is ~25 KB; this is room for the
 * busiest summer day twice over. PSRAM, and only for the length of a fetch. */
#define TT_BUF_SZ          (96 * 1024)
#define TT_HTTP_TIMEOUT_MS 20000

static SemaphoreHandle_t s_mutex;
/* Guarded by s_mutex. The network task swaps in a whole new table; the UI task
 * reads it. Nothing edits a table in place. */
static tt_flight_t *s_tt;
static int          s_n;
static int64_t      s_generated;
/* Only the network task touches this. 0 = never tried. */
static int64_t      s_last_try_ms;

/* The last few callsigns that got a number, so the log says "AUA25L is OS 254"
 * once rather than every two seconds. UI task only. */
#define TT_LOGGED 16
static char s_logged[TT_LOGGED][9];
static int  s_logged_next;

void timetable_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
}

void timetable_fetch_if_due(void)
{
    if (s_mutex == NULL) {
        return;
    }
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (s_last_try_ms != 0 && now_ms - s_last_try_ms < TIMETABLE_FETCH_EVERY_MS) {
        return;
    }
    /* Stamped before the request, so a failure waits the full hour too: one
     * request an hour to a CDN is nothing, and a retry loop would be. */
    s_last_try_ms = now_ms;

    char        *buf   = heap_caps_malloc(TT_BUF_SZ, MALLOC_CAP_SPIRAM);
    tt_flight_t *fresh = heap_caps_malloc(sizeof(tt_flight_t) * TT_MAX_FLIGHTS,
                                          MALLOC_CAP_SPIRAM);
    if (buf == NULL || fresh == NULL) {
        ESP_LOGW(TAG, "no memory for the timetable; trying again in an hour");
        goto done;
    }

    int  status    = 0;
    bool truncated = false;
    int  len = http_get(TIMETABLE_URL, buf, TT_BUF_SZ, TT_HTTP_TIMEOUT_MS,
                        &status, &truncated);
    if (len < 0 || status != 200) {
        /* A 404 is the normal state until the workflow is switched on (D94). */
        ESP_LOGW(TAG, "not fetched (n=%d, http=%d); aircraft keep their callsigns",
                 len, status);
        goto done;
    }
    if (truncated) {
        ESP_LOGW(TAG, "larger than %d B; not using a cut-off timetable", TT_BUF_SZ - 1);
        goto done;
    }

    int64_t gen = 0;
    int n = timetable_parse(buf, (size_t)len, fresh, TT_MAX_FLIGHTS, &gen);
    if (n < 0) {
        ESP_LOGW(TAG, "not a timetable: %d B, starts \"%.32s\"", len, buf);
        goto done;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    tt_flight_t *old = s_tt;
    s_tt        = fresh;
    s_n         = n;
    s_generated = gen;
    xSemaphoreGive(s_mutex);
    fresh = old;                  /* the table it replaced, freed below */
    ESP_LOGI(TAG, "%d flight(s) loaded, %d B, generated at %lld", n, len,
             (long long)gen);

done:
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
    if (s_mutex == NULL || now_utc <= 0 || rt == NULL) {
        return 0;
    }

    int hits = 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_tt != NULL && s_n > 0) {
        for (int i = 0; i < n; i++) {
            const route_t *r = route_find(rt, n_rt, ac[i].flight);
            if (r != NULL &&
                timetable_match(s_tt, s_n, &ac[i], r, now_utc, ac[i].flight_no,
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

void timetable_status(int *flights, int64_t *generated)
{
    int     f = 0;
    int64_t g = 0;
    if (s_mutex != NULL) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        f = s_n;
        g = s_generated;
        xSemaphoreGive(s_mutex);
    }
    if (flights != NULL) {
        *flights = f;
    }
    if (generated != NULL) {
        *generated = g;
    }
}
