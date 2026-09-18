#include "dbg_metrics.h"
#include <string.h>
#include "esp_log.h"
#include "lvgl.h"
#include "ui/theme.h"
#include "ui/fonts/fonts.h"
#include "data/tables.h"

static const char *TAG = "metrics";

/* 480 px panel, 20 px padding each side (DESIGN.md §4). */
#define HERO_MAX_W (THEME_SCREEN_WIDTH - 2 * THEME_SIDE_PADDING)

static int32_t text_w(const char *s, const lv_font_t *f)
{
    lv_point_t p;
    lv_text_get_size(&p, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return p.x;
}

void dbg_metrics_hero(void)
{
    const struct { const lv_font_t *f; int px; } ladder[] = {
        { &plex_sans_cond_100, 100 },
        { &plex_sans_cond_76,   76 },
        { &plex_sans_cond_56,   56 },
    };

    size_t n = 0;
    const str_lookup_t *e = tbl_airport_entries(&n);

    int fit[3] = {0, 0, 0}, none = 0;
    const char *longest_at_100 = "";
    int32_t longest_w = 0;

    ESP_LOGW(TAG, "=== hero fit, %d px content width, %u names ===",
             (int)HERO_MAX_W, (unsigned)n);

    for (size_t i = 0; i < n; i++) {
        const char *name = e[i].value;
        int step = -1;
        for (int k = 0; k < 3; k++) {
            if (text_w(name, ladder[k].f) <= HERO_MAX_W) { step = k; break; }
        }
        if (step < 0) {
            none++;
            ESP_LOGE(TAG, "  DOES NOT FIT EVEN AT 56px: \"%s\" (%dpx)",
                     name, (int)text_w(name, ladder[2].f));
            continue;
        }
        fit[step]++;
        if (step == 0) {
            int32_t w = text_w(name, ladder[0].f);
            if (w > longest_w) { longest_w = w; longest_at_100 = name; }
        }
    }

    ESP_LOGW(TAG, "fit at 100px: %d   at 76px: %d   at 56px: %d   none: %d",
             fit[0], fit[1], fit[2], none);
    ESP_LOGW(TAG, "longest name that fits at 100px: \"%s\" (%d px of %d)",
             longest_at_100, (int)longest_w, (int)HERO_MAX_W);

    /* The three names DESIGN.md §3 calls out by name, so the doc's claim gets
     * checked against the real face rather than restated. */
    const char *claims[] = { "London", "Kopenhagen", "Thessaloniki", "München", "Bratislava" };
    for (unsigned i = 0; i < sizeof claims / sizeof claims[0]; i++) {
        ESP_LOGW(TAG, "  %-13s 100px=%4d  76px=%4d  56px=%4d  -> %s",
                 claims[i],
                 (int)text_w(claims[i], ladder[0].f),
                 (int)text_w(claims[i], ladder[1].f),
                 (int)text_w(claims[i], ladder[2].f),
                 text_w(claims[i], ladder[0].f) <= HERO_MAX_W ? "fits at 100" :
                 text_w(claims[i], ladder[1].f) <= HERO_MAX_W ? "shrinks to 76" :
                 text_w(claims[i], ladder[2].f) <= HERO_MAX_W ? "shrinks to 56" : "NO FIT");
    }
}
