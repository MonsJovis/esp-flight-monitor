#include "geocode.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "http_get.h"

static const char *TAG = "geocode";

/* Measured, 2026-09-20: count=8 with language=de comes back at 2,897 bytes
 * for "Wien". Eight KiB is ~2.8x that, which leaves room for a query whose
 * hits all carry long German administrative names without the truncation
 * path ever being the normal path. */
#define GEOCODE_BUF_SZ    (8 * 1024)

/* 20 s. A search that times out is a man standing in front of the device
 * having typed a word, so it is worth waiting before telling him it did not
 * work — a poll that is slow just gets retried twelve seconds later and he
 * never knows.
 *
 * THE COMMENT HERE USED TO SAY "longer than the 6 s poll timeout in
 * flight_source.c", and it had been wrong twice over: that timeout was 10 s
 * when this was written, and D70 raised it to 25 s on measured evidence —
 * requests taking 10 to 27 seconds on a -80 dBm link. So this one was left as
 * the only short timeout on the device, on exactly the link the other two
 * were widened for, and the place search would report "Die Suche hat nicht
 * geantwortet" while the radar filled normally.
 *
 * What makes 20 s affordable now and did not before is M11's bar: until
 * there was one moving thing on the screen, a long wait and a hung device
 * looked identical and the only defence was to give up early. The bar says
 * the device is still working, so the wait can be as long as the link
 * actually needs. DESIGN.md §4. */
#define GEOCODE_TIMEOUT_MS 20000

int geocode_lookup(const char *query, geo_place_t *out, int max)
{
    if (query == NULL || out == NULL || max <= 0) {
        return GEOCODE_ERR_INVAL;
    }

    char url[256];
    if (geo_build_url(query, max, url, sizeof url) < 0) {
        /* Almost always the two-character floor: he tapped Suchen after one
         * letter. Not worth a round trip, and not worth an error sound
         * either — the screen just says nothing was found. */
        return GEOCODE_ERR_INVAL;
    }

    /* PSRAM, like every other response buffer in this layer (flight_source.c,
     * ota.c). This board runs with ~24 KB of internal heap free in steady
     * state and there are 4.7 MB on the other side of the bus; an 8 KB
     * internal allocation on a screen he opens by hand is exactly the kind of
     * thing that makes the NEXT allocation fail somewhere unrelated. */
    char *body = heap_caps_malloc(GEOCODE_BUF_SZ, MALLOC_CAP_SPIRAM);
    if (body == NULL) {
        body = heap_caps_malloc(GEOCODE_BUF_SZ, MALLOC_CAP_DEFAULT);
    }
    if (body == NULL) {
        ESP_LOGE(TAG, "no memory for a %d byte response buffer", GEOCODE_BUF_SZ);
        return GEOCODE_ERR_NET;
    }

    int status = 0;
    bool truncated = false;
    int n = http_get(url, body, GEOCODE_BUF_SZ, GEOCODE_TIMEOUT_MS, &status, &truncated);

    int result;
    if (n < 0) {
        ESP_LOGW(TAG, "request failed (%d)", n);
        result = GEOCODE_ERR_NET;
    } else if (status != 200) {
        /* Plain HTTP with no redirect is the whole reason this endpoint was
         * chosen over Nominatim and Photon (geo_parse.h), so a 3xx here is
         * news: it means the service moved and the choice needs re-checking,
         * not that it should be quietly followed into TLS. */
        ESP_LOGW(TAG, "HTTP %d", status);
        result = GEOCODE_ERR_NET;
    } else if (truncated) {
        /* Half a JSON document can still parse into a plausible-looking list
         * whose last entry has the wrong coordinates. Refuse it outright,
         * the same way ota.c refuses a truncated manifest. */
        ESP_LOGW(TAG, "response larger than %d B; refusing it", GEOCODE_BUF_SZ);
        result = GEOCODE_ERR_PARSE;
    } else {
        result = geo_parse(body, (size_t)n, out, max);
        if (result < 0) {
            result = GEOCODE_ERR_PARSE;
        } else {
            ESP_LOGI(TAG, "%d place(s) for a %d byte query", result, (int)strlen(query));
        }
    }

    free(body);
    return result;
}
