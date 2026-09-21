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

/* Prints what the touch layer has actually seen, to the serial console.
 * Exists because the long press cannot be triggered from the build host, so
 * "he did not press" and "the press never arrived" are otherwise the same
 * observation. */
void nav_touch_report(void);

int  nav_page(void);
void nav_go_to(int page, bool animate);

/* Overlays live above the deck and are not swipeable — you leave by the way you
 * came in, which is the only model that does not strand this user. */
void nav_open_overlay(void (*create)(lv_obj_t *parent), const char *name);
void nav_close_overlay(void);
bool nav_overlay_open(void);

/* True when an overlay is up AND it is the one `create` built — the same
 * function pointer passed to nav_open_overlay().
 *
 * Ask this rather than keeping a flag of your own. main.c kept a
 * `s_detail_open` bool, and nav_open_overlay() closes whatever is already
 * there before opening the next one — so tapping an aircraft and then
 * long-pressing into Einstellungen deleted the detail layer without anyone
 * telling main.c, and the flag stayed true for the rest of the session. Its
 * one reader is the branch that decides what to repaint, so from then on
 * neither Radar nor Liste was ever updated again: the panel simply stopped,
 * with nothing in the log. The state belongs to whoever owns the overlay, and
 * that is this file.
 *
 * Identified by the BUILDER, not by the `name` string beside it: the name is
 * for the log, and a caller comparing against a spelling of it can mistype
 * the spelling and get a silent false forever. A function pointer cannot be
 * mistyped — it either links or it does not. */
bool nav_overlay_is(void (*create)(lv_obj_t *parent));

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

/* The WLAN signal meter in the TOP-right corner — the badge above is its
 * counterpart in the bottom-right, and the two are the whole of this device's
 * chrome.
 *
 * It is here, on the root, for exactly the reason nav_set_badge() is: the
 * link belongs to the DEVICE, not to the Radar or the Liste, and a meter each
 * page drew for itself would be two meters that could disagree. Overlays are
 * created after it, so Einstellungen, WLAN and the detail layer cover it —
 * which is right, because the WLAN screen says all of this in words and the
 * other two are not about the network.
 *
 * `rssi_dbm` is what the radio reports for the association right now, in dBm,
 * and is ignored when `linked` is false. main/data/wifi_bars.h decides how
 * many bars that is worth, and says why the boundaries are where they are —
 * they are measured off this radio, not copied from a table.
 *
 * WHY THE CORNER AND NOT A SENTENCE. DESIGN.md §4 gives the corners to chrome
 * precisely because they are the lowest-attention zone: this is something to
 * be able to check, never something to be told. The screen already says "Kein
 * Netz" in words when there is no network at all (§5.3) — what it could not
 * say until now is the difference between a link that is fine and a link that
 * is about to stop working, which on this hardware is five decibels wide and
 * is the difference between the radar filling and the radar sitting empty.
 *
 * Cheap enough to call on every UI tick: nothing is touched unless the bar
 * count actually changed.
 */
void nav_set_signal(int rssi_dbm, bool linked);

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
