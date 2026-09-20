#include "nav.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "ui/theme.h"
#include "ui/fonts/fonts.h"

static const char *TAG = "nav";

#define NAV_MAX_PAGES 4
#define DOT_SIZE      8
#define DOT_GAP       10
#define DOT_Y         (THEME_SCREEN_HEIGHT - 16)
/* The badge's baseline gap from the panel edge. Four px less than the
 * dots' own band so a 17 px face sits inside the same strip rather than
 * hanging off the bottom of it. */
#define BADGE_BOTTOM  4

/* DESIGN.md §6: only from §5.3, and only after 30 s without a touch. */
#define AUTO_RETURN_MS 30000
/* A long-press that is too short fires while he is just resting a finger;
 * too long and he gives up.
 *
 * THIS CONSTANT USED TO BE DEAD. It sat here for the whole build describing a
 * threshold nothing enforced: LVGL sends LV_EVENT_LONG_PRESSED at
 * CONFIG_LV_INDEV_DEF_LONG_PRESS_TIME, which is 400 ms in this build, and
 * on_longpress() acted on that event directly. So the real threshold was a
 * third of the documented one, and resting a finger on the glass for half a
 * second opened Einstellungen — which is exactly the failure the comment
 * above was written to prevent. AGENTS.md §11 rule 1: the comment had
 * drifted from the code, and the code was believed because the comment
 * sounded confident. */
#define LONGPRESS_MS   1200

static lv_obj_t *s_tiles;
static lv_obj_t *s_page[NAV_MAX_PAGES];
static lv_obj_t *s_dot[NAV_MAX_PAGES];
static lv_obj_t *s_badge;
static int       s_n_pages;
static int       s_page_idx;

static lv_obj_t *s_overlay;
static void    (*s_longpress_cb)(void);

static int64_t  s_last_touch_ms;

/* When the finger went down, and whether this press has already opened
 * something. Both reset on every new press. */
static int64_t  s_press_start_ms;
static bool     s_longpress_fired;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/* Width of dot `i` for the currently selected page. Computed, never measured:
 * lv_obj_get_width() returns the width from the LAST layout pass, and
 * lv_obj_set_width() only marks the object dirty — so reading a dot back
 * immediately after widening it yields the OLD 8 px. That is not theory: the
 * device shipped with the measured version and drew the second dot 16 px
 * (DOT_SIZE * 3 - DOT_SIZE) too far left, half-swallowed by the active pill,
 * so a three-page deck looked like a two-page one with a smear on it. Found
 * by reading the panel's own framebuffer back (tools/grab_screen.py) during
 * the M7 sweep — no host test can see it, because the bug is in LVGL's
 * layout timing and not in the arithmetic.
 *
 * An lv_obj_update_layout() between the two loops would also work. This is
 * better: it removes the dependency on layout timing instead of satisfying
 * it, and the widths are two constants we already know. */
static int32_t dot_width(int i)
{
    return (i == s_page_idx) ? DOT_SIZE * 3 : DOT_SIZE;
}

static void paint_dots(void)
{
    for (int i = 0; i < s_n_pages; i++) {
        /* The active dot is brighter AND wider — never colour alone
         * (DO-257A §2.1.6), even for something this small. */
        bool on = (i == s_page_idx);
        lv_obj_set_width(s_dot[i], dot_width(i));
        lv_obj_set_style_bg_color(s_dot[i], on ? THEME_CYAN : THEME_BORDER_IDLE, 0);
    }
    /* Re-centre: the row's width changes when the active dot widens. */
    int32_t total = 0;
    for (int i = 0; i < s_n_pages; i++) {
        total += dot_width(i) + (i ? DOT_GAP : 0);
    }
    int32_t x = (THEME_SCREEN_WIDTH - total) / 2;
    for (int i = 0; i < s_n_pages; i++) {
        lv_obj_set_pos(s_dot[i], x, DOT_Y);
        x += dot_width(i) + DOT_GAP;
    }
}

static void on_tile_change(lv_event_t *e)
{
    (void)e;
    lv_obj_t *act = lv_tileview_get_tile_active(s_tiles);
    for (int i = 0; i < s_n_pages; i++) {
        if (s_page[i] == act) {
            s_page_idx = i;
            break;
        }
    }
    s_last_touch_ms = now_ms();
    paint_dots();
}

static void on_press(lv_event_t *e)
{
    (void)e;
    s_last_touch_ms   = now_ms();
    s_press_start_ms  = s_last_touch_ms;
    s_longpress_fired = false;
}

/* Bound to BOTH long-press events, and neither of them is the threshold.
 *
 * LVGL fires LV_EVENT_LONG_PRESSED once at 400 ms and then
 * LV_EVENT_LONG_PRESSED_REPEAT every 100 ms, so the repeats are used purely
 * as a clock and the real test is how long the finger has actually been
 * down. Raising CONFIG_LV_INDEV_DEF_LONG_PRESS_TIME instead would have been
 * one line, but it is global: it also sets how long the WLAN keyboard waits
 * before a held backspace starts repeating, and 1.2 s there is a keyboard
 * that feels broken.
 *
 * Both events stop arriving the moment LVGL decides the press is a scroll,
 * which is what keeps a slow swipe between Radar and Liste from opening
 * Einstellungen. That is the reason this is not done from LV_EVENT_PRESSING,
 * which keeps coming during a drag. */
static void on_longpress(lv_event_t *e)
{
    (void)e;
    int64_t t = now_ms();
    s_last_touch_ms = t;

    if (s_longpress_fired || t - s_press_start_ms < LONGPRESS_MS) {
        return;
    }
    s_longpress_fired = true;
    if (s_longpress_cb && !nav_overlay_open()) {
        s_longpress_cb();
    }
}

void nav_create(const nav_page_t *pages, int n_pages)
{
    if (n_pages > NAV_MAX_PAGES) n_pages = NAV_MAX_PAGES;
    s_n_pages = n_pages;
    s_page_idx = 0;

    lv_obj_t *root = lv_screen_active();
    lv_obj_set_style_bg_color(root, THEME_GROUND, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    s_tiles = lv_tileview_create(root);
    lv_obj_set_size(s_tiles, THEME_SCREEN_WIDTH, THEME_SCREEN_HEIGHT);
    lv_obj_set_pos(s_tiles, 0, 0);
    lv_obj_set_style_bg_color(s_tiles, THEME_GROUND, 0);
    lv_obj_set_style_border_width(s_tiles, 0, 0);
    /* The tileview draws its own scrollbar across the bottom, right where the
     * page indicator lives. The dots already say which page this is. */
    lv_obj_set_scrollbar_mode(s_tiles, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tiles, on_tile_change, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_tiles, on_press, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_tiles, on_longpress, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(s_tiles, on_longpress, LV_EVENT_LONG_PRESSED_REPEAT, NULL);

    for (int i = 0; i < n_pages; i++) {
        /* Horizontal deck: the first tile may only be left, the last only
         * right, so a swipe never falls off the end into a blank tile. */
        lv_dir_t dir = 0;
        if (i > 0)            dir |= LV_DIR_LEFT;
        if (i < n_pages - 1)  dir |= LV_DIR_RIGHT;

        s_page[i] = lv_tileview_add_tile(s_tiles, (uint8_t)i, 0, dir);
        lv_obj_set_style_bg_color(s_page[i], THEME_GROUND, 0);
        lv_obj_set_style_pad_all(s_page[i], 0, 0);
        lv_obj_set_scrollbar_mode(s_page[i], LV_SCROLLBAR_MODE_OFF);
        if (pages[i].create) {
            pages[i].create(s_page[i]);
        }

        s_dot[i] = lv_obj_create(root);
        lv_obj_set_size(s_dot[i], DOT_SIZE, DOT_SIZE);
        lv_obj_set_style_radius(s_dot[i], DOT_SIZE / 2, 0);
        lv_obj_set_style_border_width(s_dot[i], 0, 0);
        lv_obj_set_scrollbar_mode(s_dot[i], LV_SCROLLBAR_MODE_OFF);
    }

    /* The device-level badge (nav.h). Created after the dots so it draws over
     * them if it ever grows wide enough to reach the middle, and before any
     * overlay so that Einstellungen covers it rather than the other way
     * round.
     *
     * Right-aligned with lv_obj_align() rather than by measuring the label:
     * lv_obj_get_width() returns the width from the last layout pass, which
     * is exactly the trap dot_width() above exists to avoid, and this label's
     * width changes every time the percentage does. */
    s_badge = lv_label_create(root);
    lv_obj_set_style_text_font(s_badge, &plex_mono_17, 0);
    lv_obj_set_style_text_color(s_badge, THEME_TEXT_LABEL, 0);
    lv_label_set_text(s_badge, "");
    lv_obj_align(s_badge, LV_ALIGN_BOTTOM_RIGHT, -THEME_SIDE_PADDING, -BADGE_BOTTOM);
    lv_obj_set_hidden(s_badge, true);

    /* One page is not a deck — hide the indicator rather than show a lone dot
     * that suggests there is somewhere else to go. */
    if (n_pages < 2) {
        for (int i = 0; i < n_pages; i++) lv_obj_set_hidden(s_dot[i], true);
    } else {
        paint_dots();
    }

    s_last_touch_ms = now_ms();
    ESP_LOGI(TAG, "deck built with %d page(s)", n_pages);
}

void nav_set_badge(const char *text, bool caution)
{
    if (s_badge == NULL) {
        return;
    }
    if (text == NULL || text[0] == '\0') {
        lv_obj_set_hidden(s_badge, true);
        return;
    }
    lv_label_set_text(s_badge, text);
    lv_obj_set_style_text_color(s_badge, caution ? THEME_AMBER : THEME_TEXT_LABEL, 0);
    lv_obj_set_hidden(s_badge, false);
}

int nav_page(void) { return s_page_idx; }

void nav_go_to(int page, bool animate)
{
    if (page < 0 || page >= s_n_pages || s_tiles == NULL) return;
    lv_tileview_set_tile_by_index(s_tiles, (uint32_t)page, 0,
                                  animate ? LV_ANIM_ON : LV_ANIM_OFF);
    s_page_idx = page;
    paint_dots();
}

void nav_open_overlay(void (*create)(lv_obj_t *parent), const char *name)
{
    if (s_overlay != NULL) {
        nav_close_overlay();
    }
    lv_obj_t *root = lv_screen_active();
    s_overlay = lv_obj_create(root);
    lv_obj_set_size(s_overlay, THEME_SCREEN_WIDTH, THEME_SCREEN_HEIGHT);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, THEME_GROUND, 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_set_scrollbar_mode(s_overlay, LV_SCROLLBAR_MODE_OFF);
    if (create) create(s_overlay);
    ESP_LOGI(TAG, "overlay open: %s", name ? name : "?");
    s_last_touch_ms = now_ms();
}

void nav_close_overlay(void)
{
    if (s_overlay == NULL) return;
    lv_obj_delete(s_overlay);
    s_overlay = NULL;
    s_last_touch_ms = now_ms();
    ESP_LOGI(TAG, "overlay closed");
}

bool nav_overlay_open(void) { return s_overlay != NULL; }

void nav_set_longpress_cb(void (*cb)(void)) { s_longpress_cb = cb; }

void nav_tick(bool empty_sky)
{
    if (s_tiles == NULL || nav_overlay_open()) return;
    if (s_page_idx == 0) return;

    /* The rule that keeps the device from being rude: it may only take the
     * screen back when there is nothing to look at anyway, and only when he
     * has clearly stopped touching it. Everything else waits for him. */
    if (!empty_sky) return;
    if (now_ms() - s_last_touch_ms < AUTO_RETURN_MS) return;

    ESP_LOGI(TAG, "auto-return to page 0 (empty sky, %d s untouched)",
             AUTO_RETURN_MS / 1000);
    nav_go_to(0, true);
}
