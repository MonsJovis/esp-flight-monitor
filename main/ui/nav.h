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

/* One line of chrome in the bottom strip, right-aligned beside the page dots,
 * for a condition that belongs to the DEVICE rather than to any one screen —
 * today that means the battery, and nothing else.
 *
 * `text` NULL or empty hides it again. `caution` paints it amber instead of
 * grey; the words carry the meaning either way, so colour only reinforces
 * (DO-257A §2.1.6, the same rule the "Aktiv" word follows).
 *
 * It sits in the 24 px band the dots already own — screen_list.c keeps its
 * rows out of it (DOTS_CLEARANCE) and the radar caption stops above it — so
 * nothing on either page has to move to make room. Overlays are created on
 * the root AFTER this label, so Einstellungen and WLAN cover it, which is
 * correct: Einstellungen has a battery row of its own.
 */
void nav_set_badge(const char *text, bool caution);

/* Long-press opens Einstellungen (DESIGN.md §6).
 *
 * ANYWHERE on the deck page, not just the chrome strip: the handler is bound
 * to the tileview, which is the full 480 x 480, and there is no coordinate
 * test. This comment used to say "on the chrome strip" and was wrong for the
 * whole build. It is the behaviour that is right, not the old comment — the
 * chrome strip is 24 px of a 480 px panel and a settings screen nobody can
 * find is a settings screen he cannot use.
 *
 * A child that handles its own clicks keeps them: long-pressing an aircraft
 * caption or a list row does nothing, because LVGL does not bubble events to
 * a parent unless the child asks it to. Empty space always works, and so
 * does the chrome strip itself.
 *
 * The threshold is LONGPRESS_MS in nav.c (1.2 s), enforced there rather than
 * taken from LVGL's 400 ms default. */
void nav_set_longpress_cb(void (*cb)(void));

/* Auto-return to page 0. Called once per UI tick.
 *
 * It fires ONLY from the empty-sky state and only after 30 s untouched: if he is
 * reading the list, traffic appearing must not yank the screen out from under
 * him. `empty_sky` is the only state the device is allowed to leave by itself. */
void nav_tick(bool empty_sky);
