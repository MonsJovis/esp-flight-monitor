/* The screen graph — DESIGN.md §6.
 *
 *              ←   Radar   ·   Liste   →
 *                    ●           ○
 *                    |           |
 *                    +-----+-----+
 *                          |
 *                   Über dir jetzt        (tap an aircraft; tap to go back)
 *
 * TWO swipe pages, and three things that are deliberately NOT in the deck:
 *
 *  - §5.1/§5.2/§5.3 is the layer BELOW the deck, not a page in it. It is
 *    reached by tapping an aircraft on either page and left by tapping
 *    anywhere, returning to the page it was opened from. It is one screen in
 *    three states; the device picks the state, he never navigates between them.
 *  - Einstellungen is behind a long-press, because putting it in the swipe path
 *    means finding it by accident, and for this user being somewhere he did not
 *    mean to go is the same as being lost.
 *  - WLAN opens from Einstellungen, and presents itself when no known network is
 *    in range — the one case where the device is allowed to interrupt him.
 *
 * nav.c does not know what any page contains: pages are registered as
 * descriptors, so a screen that does not exist yet simply is not registered.
 */
#pragma once
#include <stdbool.h>
#include "lvgl.h"

typedef struct {
    const char *name;                  /* for logs only, never shown */
    void      (*create)(lv_obj_t *parent);
} nav_page_t;

/* Builds the deck. Caller holds display_lock(). `pages` must outlive the call. */
void nav_create(const nav_page_t *pages, int n_pages);

int  nav_page(void);
void nav_go_to(int page, bool animate);

/* Overlays live above the deck and are not swipeable — you leave by the way you
 * came in, which is the only model that does not strand this user. */
void nav_open_overlay(void (*create)(lv_obj_t *parent), const char *name);
void nav_close_overlay(void);
bool nav_overlay_open(void);

/* Long-press on the chrome strip opens Einstellungen (DESIGN.md §6). */
void nav_set_longpress_cb(void (*cb)(void));

/* Auto-return to page 0. Called once per UI tick.
 *
 * It fires ONLY from the empty-sky state and only after 30 s untouched: if he is
 * reading the list, traffic appearing must not yank the screen out from under
 * him. `empty_sky` is the only state the device is allowed to leave by itself. */
void nav_tick(bool empty_sky);
