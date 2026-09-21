/* The signal meter — four bars, drawn in two sizes, meaning one thing.
 *
 * SHARED for the same reason widget_input.h is: it appears on two screens
 * that are nowhere near each other in the source — the chrome corner of the
 * deck and every row of the WLAN list — and a man who has learnt one has
 * learnt the other. Two copies would drift in the one way that matters,
 * which is the dBm the third bar starts at. The thresholds themselves are
 * not here at all; they are in main/data/wifi_bars.h, host-tested, and this
 * file only draws what that says.
 *
 * ONE OBJECT, NOT FIVE. It is a single lv_obj with an LV_EVENT_DRAW_MAIN
 * callback, the same idiom screen_radar.c uses for its aircraft marks, rather
 * than a container holding four little rectangles. The WLAN list has a pool
 * of twenty-four rows and every one of them gets a meter: four children each
 * would be ninety-six more objects on a device with ~24 KB of internal heap
 * to its name, for a graphic that is twelve rectangles of arithmetic.
 *
 * THE CALLER OWNS THE STATE. widget_signal_create() takes a pointer to a
 * widget_signal_t the caller keeps alive for as long as the object — a
 * file-scope static in nav.c, a field of the row struct in screen_wifi.c.
 * Same arrangement as radar_mark_state_t, and for the same reason: it means
 * there is nothing to malloc on the display task and nothing to free when
 * LVGL deletes the tree out from under it.
 *
 * Caller holds display_lock(), like everything else that touches LVGL here.
 */
#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "theme.h"
#include "wifi_bars.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Geometry. Both sizes are on the same 4-bar ladder; only the bar width, the
 * gap and the height differ.
 *
 * CHROME is the corner of the deck (DESIGN.md §4: "corners are the
 * lowest-attention zone — chrome only"), sized to sit in the same 13 px band
 * as the radar's clock without competing with it. Small on purpose: it is
 * there to be glanced at, never to be read.
 *
 * ROW is the WLAN list, which is the NEAR tier (~40 cm, DESIGN.md §3) and
 * where he is actually choosing between networks — so it is the size of the
 * thing it sits beside rather than the size of chrome. */
#define WIDGET_SIGNAL_CHROME_BAR_W 3
#define WIDGET_SIGNAL_CHROME_GAP   2
#define WIDGET_SIGNAL_CHROME_H     14

#define WIDGET_SIGNAL_ROW_BAR_W 4
#define WIDGET_SIGNAL_ROW_GAP   3
#define WIDGET_SIGNAL_ROW_H     20

/* Overall width of a meter with that bar width and gap. A macro rather than a
 * function because both call sites need it while laying out — before any of
 * this exists — to reserve the space it will occupy. */
#define WIDGET_SIGNAL_W(bar_w, gap) \
    (WIFI_BARS_MAX * (bar_w) + (WIFI_BARS_MAX - 1) * (gap))

/* What a deck page has to keep clear at its top right — the meter plus one
 * base unit of air beside it.
 *
 * nav.c draws the chrome meter on the ROOT, above every page, because the
 * link belongs to the device and not to any one screen (the same argument
 * nav_set_badge() makes for the battery). So a page that puts something of
 * its own in that corner has to know how much of it is already spoken for.
 * Exactly one does: the radar's clock, which was right-aligned to the panel
 * edge and would otherwise have been drawn underneath the meter. This
 * constant is the contract between them, and it is here rather than in
 * theme.h so that it cannot disagree with the geometry above it. */
#define WIDGET_SIGNAL_CHROME_SLOT                                              \
    (WIDGET_SIGNAL_W(WIDGET_SIGNAL_CHROME_BAR_W, WIDGET_SIGNAL_CHROME_GAP) +   \
     THEME_BASE_UNIT)

typedef struct {
    int32_t bar_w;
    int32_t gap;
    int32_t h;
    int     bars;    /* 0..WIFI_BARS_MAX, as wifi_bars() judged it */
    bool    linked;  /* false -> the amber stroke through it */
} widget_signal_t;

/* Builds one. `st` is zeroed and filled in here; it must outlive the object.
 * The meter starts in the "no link" state, because that is what is true
 * before anything has reported otherwise and AGENTS.md §1 does not allow a
 * widget to start by lying. Position it with lv_obj_set_pos() as usual. */
lv_obj_t *widget_signal_create(lv_obj_t *parent, widget_signal_t *st,
                               int32_t bar_w, int32_t gap, int32_t h);

/* Sets what it shows. `rssi_dbm` is ignored when `linked` is false.
 *
 * Cheap to call on every tick: it maps the dBm to a rung, compares, and
 * returns without touching LVGL when nothing moved. That is what lets the
 * deck push the live RSSI every two seconds without spending a redraw on a
 * number that wobbles by one decibel and changes no bars. */
void widget_signal_set(lv_obj_t *sig, int rssi_dbm, bool linked);

#ifdef __cplusplus
}
#endif
