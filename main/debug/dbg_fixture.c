#include "dbg_fixture.h"
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "ui/display.h"
#include "ui/view_model.h"
#include "ui/screen_overhead.h"
#include "data/view_build.h"
#include "net/adsb_parse.h"
#include "net/route_parse.h"

static const char *TAG = "fixture";

/* The real 30 nm capture over Gloggnitz, 2026-09-18, embedded so the screens can
 * be driven with traffic that actually flew — before the device has ever joined
 * a network, and without waiting for the sky to produce a particular case. */
extern const uint8_t ac_json_start[]   asm("_binary_adsblol_gloggnitz_30nm_json_start");
extern const uint8_t ac_json_end[]     asm("_binary_adsblol_gloggnitz_30nm_json_end");
extern const uint8_t rt_json_start[]   asm("_binary_routeset_response_json_start");
extern const uint8_t rt_json_end[]     asm("_binary_routeset_response_json_end");

/* 09:47 on Friday 18 September 2026 — the moment the capture was taken, so the
 * clock and date on screen match the traffic on screen. */
static struct tm capture_time(void)
{
    struct tm t = {0};
    t.tm_year = 126; t.tm_mon = 8; t.tm_mday = 18;
    t.tm_wday = 5;   t.tm_hour = 9; t.tm_min = 47;
    return t;
}

void dbg_fixture_show(int n)
{
    static aircraft_t ac[MAX_AIRCRAFT];
    static route_t    rt[MAX_AIRCRAFT];

    int nac = adsb_parse((const char *)ac_json_start,
                         (size_t)(ac_json_end - ac_json_start), ac, MAX_AIRCRAFT);
    int nrt = route_parse((const char *)rt_json_start,
                          (size_t)(rt_json_end - rt_json_start), rt, MAX_AIRCRAFT);
    if (nac <= 0) { ESP_LOGE(TAG, "fixture parse failed (%d)", nac); return; }

    struct tm now = capture_time();
    view_model_t vm;
    const char *what = "";

    if (n == 3) {
        view_build_empty(&now, &ac[0], true, &vm);
        what = "§5.3 Himmel frei";            /* LOG-ONLY */
    } else {
        /* Pick a real aircraft matching the state we want to look at, rather
         * than inventing one — the point is to render what actually flew. */
        int pick = 0;
        for (int i = 0; i < nac; i++) {
            const route_t *r = route_find(rt, nrt, ac[i].flight);
            bool routed = (r != NULL && r->resolved && r->plausible);
            if (n == 1 && routed) { pick = i; what = "§5.1 Über dir jetzt"; break; } /* LOG-ONLY */
            if (n == 2 && !routed) { pick = i; what = "§5.2 Ohne Route"; break; }    /* LOG-ONLY */
            if (n == 4 && routed) {
                /* Longest destination, to exercise the hero shrink ladder. */
                const route_t *best = route_find(rt, nrt, ac[pick].flight);
                if (best == NULL || !best->resolved ||
                    strlen(r->dest_city) > strlen(best->dest_city)) {
                    pick = i;
                }
                what = "§5.1 longest destination"; /* LOG-ONLY */
            }
        }
        view_build(&ac[pick], route_find(rt, nrt, ac[pick].flight),
                   &now, nac, true, &vm);
        ESP_LOGW(TAG, "%s — %s (%s)", what, ac[pick].flight,
                 ac[pick].type[0] ? ac[pick].type : "no type");
    }

    ESP_LOGW(TAG, "hero=\"%s\" origin=\"%s\" alt=\"%s\" dist=\"%s %s\"",
             vm.hero, vm.origin, vm.altitude, vm.distance, vm.direction_word);

    display_lock(0);
    screen_overhead_update(&vm);
    display_unlock();
}
