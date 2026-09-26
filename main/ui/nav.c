#include "nav.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "ui/theme.h"
#include "ui/fonts/fonts.h"
#include "ui/widget_signal.h"

static const char *TAG = "nav";

#define NAV_MAX_PAGES 4
#define DOT_SIZE      8
#define DOT_GAP       10
#define DOT_Y         (THEME_SCREEN_HEIGHT - 16)
/* The badge's baseline gap from the panel edge. Four px less than the
 * dots' own band so a 17 px face sits inside the same strip rather than
 * hanging off the bottom of it. */
#define BADGE_BOTTOM  4

/* Where the signal meter's FEET go — the top chrome band, 24 px down.
 *
 * The same 24 as RADAR_CLOCK_Y in screen_radar.c, and not by accident: the
 * clock there is plex_mono_13, whose line height is 18 with a 4 px descender,
 * so its baseline lands at 24 + 18 - 4 = 38. A 14 px meter whose top is at 24
 * has its bottom on that same 38, and the two read as one row of chrome
 * rather than as two things that ended up near each other. Move one and the
 * other needs moving too; there is a matching note beside RADAR_CLOCK_Y. */
#define SIG_TOP       24

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
static lv_obj_t *s_signal;
/* The meter's state, owned here and handed to the widget (widget_signal.h):
 * a file-scope static, so there is nothing to allocate and nothing to free
 * when lv_obj_clean() takes the deck apart under us. */
static widget_signal_t s_signal_state;
static int       s_n_pages;
static int       s_page_idx;

static lv_obj_t *s_overlay;
/* WHICH overlay, identified by the function that built it. Callers used to
 * keep their own flag for this and it went stale the moment a second overlay
 * replaced the first without going through them — see nav_overlay_is(). */
static void (*s_overlay_create)(lv_obj_t *parent);
static void    (*s_longpress_cb)(void);

static int64_t  s_last_touch_ms;

/* When the finger went down, and whether this press has already opened
 * something. Both reset on every new press. */
static int64_t  s_press_start_ms;
static bool     s_longpress_fired;

/* Touch bookkeeping, for the console. The long press cannot be exercised
 * from the build host, so the only way to tell "he did not press" from "the
 * press never arrived" is to have the device remember. */
static uint32_t s_press_seen;
static uint32_t s_longpress_seen;
static int64_t  s_longpress_last_ms;

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

static void (*s_page_cb)(int page);

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
    if (s_page_cb != NULL) {
        s_page_cb(s_page_idx);   /* the new page should show current data now */
    }
}

static void on_press(lv_event_t *e)
{
    (void)e;
    s_last_touch_ms   = now_ms();
    s_press_start_ms  = s_last_touch_ms;
    s_longpress_fired = false;
    s_press_seen++;
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
    s_longpress_fired    = true;
    s_longpress_seen++;
    s_longpress_last_ms  = t - s_press_start_ms;
    if (s_longpress_cb && !nav_overlay_open()) {
        s_longpress_cb();
    }
}

/* Marks every purely decorative descendant as "pass it on".
 *
 * lv_obj_get_event_count() is the test: zero means nothing was ever wired to
 * this object, so it exists to be looked at. Recursion stops at anything
 * that does have a callback — that object owns its touches, and so does
 * everything inside it.
 *
 * Bubbled events stop at the tile: nav_create() deliberately does not set
 * this flag on s_page[i] itself, so a scroll inside a page cannot reach
 * lv_tileview's own LV_EVENT_SCROLL_END handler and snap the deck to another
 * page. */
static void bubble_decorative(lv_obj_t *parent)
{
    uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        if (lv_obj_get_event_count(child) != 0) {
            continue;
        }
        lv_obj_set_event_bubble(child, true);
        bubble_decorative(child);
    }
}

/* THE WHOLE DECK IS GONE, AND LVGL IS THE ONLY THING THAT KNOWS.
 *
 * nav_create() cleared `s_overlay` on the way IN, which covers the caller
 * that rebuilds the deck. It does not cover the callers that tear it down and
 * do not rebuild it: dbg_fontcard.c and dbg_bench.c both call
 * lv_obj_clean(lv_screen_active()) to take the panel over, which deletes the
 * tileview, the dots, the badge, the meter and any open overlay — and tells
 * this file nothing. Every pointer here was then dangling, and the NULL
 * guards in nav_set_badge() and nav_set_signal() were dead code. Press 'e'
 * to open an overlay, 'f' to draw the font card, then anything that opens an
 * overlay again: nav_open_overlay() starts by calling lv_obj_delete() on the
 * one it still believes in. That is the LoadProhibited nav_create()'s comment
 * says was fixed, through the second door.
 *
 * Bound to the tileview because it is this file's root object: whatever
 * deletes the deck deletes it too, whether that is lv_obj_clean() or a
 * caller. Nothing has to remember to call anything, which is the only version
 * of this that stays true — the same argument screen_wifi.c's on_main_deleted
 * makes for its own tree. */
static void on_deck_deleted(lv_event_t *e)
{
    (void)e;
    s_tiles  = NULL;
    s_badge  = NULL;
    s_signal = NULL;
    /* The overlay is a sibling on the same root, so the clean that took the
     * deck took it as well. Forgetting it here is what stops the next
     * nav_open_overlay() from deleting freed memory. */
    s_overlay        = NULL;
    s_overlay_create = NULL;
    for (int i = 0; i < NAV_MAX_PAGES; i++) {
        s_page[i] = NULL;
        s_dot[i]  = NULL;
    }
    s_n_pages = 0;
}

void nav_create(const nav_page_t *pages, int n_pages)
{
    /* FORGET ANY OVERLAY FIRST. Every caller of this function has just
     * emptied the active screen (lv_obj_clean), which deletes an open overlay
     * along with everything else — but nothing told this file, so s_overlay
     * was left pointing at freed memory and the NEXT nav_open_overlay() dealt
     * with it by calling lv_obj_delete() on it. That is a LoadProhibited in
     * lv_obj_get_parent(), and it is reachable today: open any overlay from
     * the serial console, press '0' to restore the live view, open one again.
     *
     * Found by the M11 stress run rather than by reading, which is the point
     * of the stress run. */
    s_overlay        = NULL;
    s_overlay_create = NULL;

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
    lv_obj_add_event_cb(s_tiles, on_deck_deleted, LV_EVENT_DELETE, NULL);
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

        bubble_decorative(s_page[i]);

        /* WHY THE LONG PRESS NEEDS THIS.
         *
         * The handlers above are on the tileview, which is underneath every
         * page. In LVGL 9 the lv_obj constructor sets obj->clickable = 1, so
         * the full-page container each screen creates (screen_radar.c:447,
         * screen_list.c:832) is a hit target in its own right — and LVGL does
         * not pass an event to a parent unless the child asks it to. So every
         * press landed on the page's own background and stopped there, and
         * the long press to Einstellungen has never once worked by finger.
         * Swiping kept working because scrolling searches UP the parent chain
         * for a scrollable ancestor; clicking does not.
         *
         * bubble_decorative() decides who passes a touch on: an object with no
         * event callback of its own is scenery, and scenery has no business
         * eating a press. An object that DOES have one — an aircraft caption,
         * a list row — owns its taps and is left alone, along with everything
         * under it, so a long press on a row cannot open the detail view and
         * the settings on top of it.
         *
         * One level of bubbling was tried first and was not enough: the radar
         * draws its range rings as lv_obj_create() circles whose bounding
         * boxes cover most of the scope, two levels below the tile, so nearly
         * every press inside the ring landed on scenery and stopped there.
         *
         * The handlers are added to the tile as well as the tileview, because
         * one level of bubbling is exactly one level: the event reaches the
         * tile and stops. */
        lv_obj_add_event_cb(s_page[i], on_press, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(s_page[i], on_longpress, LV_EVENT_LONG_PRESSED, NULL);
        lv_obj_add_event_cb(s_page[i], on_longpress, LV_EVENT_LONG_PRESSED_REPEAT, NULL);

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

    /* The signal meter, top right (nav.h). Created after the pages, like the
     * badge, so a page's own chrome cannot be drawn over it — the radar's
     * clock is right-aligned into this same corner and keeps
     * WIDGET_SIGNAL_CHROME_SLOT clear for it.
     *
     * It starts crossed out, which is honest rather than pessimistic: at this
     * point in boot the radio has not associated, and a meter that opened on
     * four bars and fell to zero a second later would be the panel's first
     * statement of the day being wrong. */
    s_signal = widget_signal_create(root, &s_signal_state,
                                    WIDGET_SIGNAL_CHROME_BAR_W,
                                    WIDGET_SIGNAL_CHROME_GAP,
                                    WIDGET_SIGNAL_CHROME_H);
    lv_obj_set_pos(s_signal,
                   THEME_SCREEN_WIDTH - THEME_SIDE_PADDING -
                       WIDGET_SIGNAL_W(WIDGET_SIGNAL_CHROME_BAR_W,
                                       WIDGET_SIGNAL_CHROME_GAP),
                   SIG_TOP);

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

void nav_set_signal(int rssi_dbm, bool linked)
{
    widget_signal_set(s_signal, rssi_dbm, linked);
}

void nav_touch_report(void)
{
    printf("\ntouch\n");
    printf("  presses reaching the deck : %u\n", (unsigned)s_press_seen);
    printf("  long presses recognised   : %u\n", (unsigned)s_longpress_seen);
    printf("  last one held for         : %lld ms (threshold %d)\n",
           (long long)s_longpress_last_ms, LONGPRESS_MS);
    printf("  overlay open right now    : %s\n", nav_overlay_open() ? "yes" : "no");
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
    s_overlay_create = create;
    if (create) create(s_overlay);
    ESP_LOGI(TAG, "overlay open: %s", name ? name : "?");
    s_last_touch_ms = now_ms();
}

void nav_close_overlay(void)
{
    if (s_overlay == NULL) return;
    lv_obj_delete(s_overlay);
    s_overlay        = NULL;
    s_overlay_create = NULL;
    s_last_touch_ms = now_ms();
    ESP_LOGI(TAG, "overlay closed");
}

bool nav_overlay_open(void) { return s_overlay != NULL; }

bool nav_overlay_is(void (*create)(lv_obj_t *parent))
{
    return s_overlay != NULL && create != NULL && s_overlay_create == create;
}

void nav_set_longpress_cb(void (*cb)(void)) { s_longpress_cb = cb; }
void nav_set_page_cb(void (*cb)(int page)) { s_page_cb = cb; }

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
