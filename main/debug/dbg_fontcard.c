/* The M1 font gate, as a screen.
 *
 * It lives here and not in main.c because the strings below are German drawn
 * into LVGL labels, which is exactly what tools/check_strings.py exists to
 * forbid — and they are legitimate, because they are FONT SPECIMENS: their
 * whole job is to be the glyphs most likely to be missing from the subset.
 * Keeping them in main.c meant listing eight literals by spelling in the
 * checker's exemption table, which is the kind of list that quietly grows.
 * main/debug/ is exempt as a directory, and this is a developer diagnostic,
 * so this is where it belongs. The font COVERAGE gate still scans this file —
 * a missing glyph is missing on a developer screen too.
 */
#include "dbg_fontcard.h"

#include "esp_log.h"
#include "lvgl.h"

#include "ui/display.h"
#include "ui/fonts/fonts.h"
#include "ui/theme.h"

static const char *TAG = "fontcard";

/* The M1 font gate, drawn so a screenshot answers it: every tier of the scale,
 * with the glyphs that are NOT in the default ASCII subset. If the subsetting
 * is wrong these render as blanks, and blanks are the whole point of the check.
 * Magenta sits directly beside white and cyan because AC 25-11A flags that pair
 * specifically — the open question in AGENTS.md §8.2. */
void dbg_font_card(void)
{
    display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, THEME_GROUND, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    struct { const lv_font_t *f; const char *txt; lv_color_t col; int y; } rows[] = {
        { &plex_sans_cond_100, "München",          THEME_WHITE,         10  },
        { &plex_sans_cond_76,  "Zürich",           THEME_MAGENTA,       118 },
        { &plex_sans_cond_56,  "Wien → Graz",      THEME_WHITE,         206 },
        { &plex_sans_cond_34,  "Großraum · 42°",   THEME_CYAN,          272 },
        { &plex_sans_cond_22,  "Straße, süß, Öl",  THEME_TEXT_PRIMARY,  318 },
        { &plex_mono_32,       "9.100 m  12,4 km", THEME_CYAN,          350 },
        { &plex_mono_17,       "ÄÖÜäöüß °·—→",     THEME_TEXT_LABEL,    396 },
        { &plex_mono_13,       "KEIN FLUGPLAN",    THEME_AMBER,         424 },
        { &plex_mono_12,       "AUA453 · BCS3",    THEME_TEXT_TERTIARY, 448 },
    };
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        lv_obj_t *l = lv_label_create(scr);
        lv_label_set_text(l, rows[i].txt);
        lv_obj_set_style_text_font(l, rows[i].f, 0);
        lv_obj_set_style_text_color(l, rows[i].col, 0);
        lv_obj_set_pos(l, THEME_SIDE_PADDING, rows[i].y);
    }
    display_unlock();
    ESP_LOGI(TAG, "font card drawn");
}
