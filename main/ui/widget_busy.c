/* widget_busy.c — see widget_busy.h for why this is shared rather than
 * copied into each screen. */
#include "widget_busy.h"
#include "theme.h"

/* How much of the track the moving segment covers, and how long one sweep
 * takes. 32% and 1100 ms were chosen against the two waits this actually
 * covers: the geocoder answers in ~250 ms (PLAN.md M10) and a WiFi scan takes
 * three to five seconds. A sweep much faster than this reads as agitated on
 * the short wait; much slower and the long one looks stuck. */
#define IND_NUM      32
#define IND_DEN      100
#define SWEEP_MS     700

/* The animation moves the indicator's x. Its `var` is the INDICATOR, not the
 * track — which is what makes this safe: lv_obj's destructor calls
 * lv_anim_delete(obj, NULL) on itself, so when a screen is torn down while
 * the bar is still sweeping, LVGL removes the animation as it frees the
 * object. An animation whose var outlived its object would be a timer writing
 * into freed memory every frame, which is D58 wearing a different hat. */
static void set_x_cb(void *var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, v);
}

lv_obj_t *widget_busy_create(lv_obj_t *parent, int32_t w)
{
    lv_obj_t *track = lv_obj_create(parent);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, w, WIDGET_BUSY_H);
    lv_obj_set_style_bg_color(track, THEME_BORDER_IDLE, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(track, WIDGET_BUSY_H / 2, 0);
    lv_obj_set_style_pad_all(track, 0, 0);
    lv_obj_set_style_border_width(track, 0, 0);
    lv_obj_set_scrollable(track, false);

    /* The segment is a child, so LVGL clips it to the track for free — it
     * slides in from off the left edge and out past the right one without
     * anything else on the screen having to know. */
    lv_obj_t *ind = lv_obj_create(track);
    lv_obj_remove_style_all(ind);
    lv_obj_set_size(ind, w * IND_NUM / IND_DEN, WIDGET_BUSY_H);
    lv_obj_set_style_bg_color(ind, THEME_CYAN, 0);
    lv_obj_set_style_bg_opa(ind, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ind, WIDGET_BUSY_H / 2, 0);
    lv_obj_set_style_pad_all(ind, 0, 0);
    lv_obj_set_style_border_width(ind, 0, 0);
    lv_obj_set_scrollable(ind, false);
    lv_obj_set_pos(ind, 0, 0);

    lv_obj_set_hidden(track, true);
    return track;
}

void widget_busy_set_active(lv_obj_t *busy, bool active)
{
    if (busy == NULL) {
        return;
    }
    lv_obj_t *ind = lv_obj_get_child(busy, 0);
    if (ind == NULL) {
        return;
    }

    /* Unconditionally, in both directions. Starting a second animation on the
     * same var would leave two of them fighting over one x (lv_anim_start
     * replaces an animation with the same var AND exec_cb, but only that
     * pair), and stopping one that was never running is free. */
    lv_anim_delete(ind, set_x_cb);

    if (!active) {
        lv_obj_set_hidden(busy, true);
        return;
    }

    lv_obj_set_hidden(busy, false);

    /* The sweep's endpoints are the real widths, so they have to exist. A
     * screen that switches this on during construction — before LVGL has laid
     * anything out — would otherwise animate from 0 to 0 and sit still while
     * claiming to be alive. */
    lv_obj_update_layout(busy);
    int32_t track_w = lv_obj_get_width(busy);
    if (track_w <= 0) {
        return;
    }

    /* The segment is re-derived from the track's CURRENT width every time,
     * not fixed at construction. One caller resizes its track per update —
     * the route bar on the hero screen is only as wide as the sentence above
     * it, and that sentence changes — and a segment left at its original
     * fraction would creep towards covering the whole of a narrower bar,
     * which is a full bar, which is the one thing an indeterminate indicator
     * must never look like. */
    int32_t ind_w = track_w * IND_NUM / IND_DEN;
    lv_obj_set_width(ind, ind_w);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ind);
    lv_anim_set_exec_cb(&a, set_x_cb);
    /* IT SWEEPS BACK AND FORTH INSIDE THE TRACK, AND IS NEVER OFF IT.
     *
     * The first version did what a phone does: one-way from off the left edge
     * to past the right one, eased, repeating. Measured on the panel across
     * three frames, two of them caught the segment at x=439 of 440 with ONE
     * pixel of it still showing. That is not a fluke of sampling — an
     * ease-in-out spends its slowest time at the ends of its travel, and at
     * this end most of the segment is outside the track. So for roughly a
     * quarter of every cycle the bar is a blank line, which is the one thing
     * a "still working" indicator must never look like, and the exact
     * impression it exists to prevent.
     *
     * Travelling 0 -> track_w - ind_w keeps the whole segment on the track at
     * every instant, and the reverse leg means there is no jump back to the
     * start either. Slow at each turn, quick through the middle: alive
     * everywhere, and never once blank. */
    lv_anim_set_values(&a, 0, track_w - ind_w);
    lv_anim_set_duration(&a, SWEEP_MS);
    lv_anim_set_reverse_duration(&a, SWEEP_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

lv_obj_t *widget_busy_ghost(lv_obj_t *parent, int32_t x, int32_t y,
                            int32_t w, int32_t h, bool dim)
{
    lv_obj_t *g = lv_obj_create(parent);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, w, h);
    lv_obj_set_pos(g, x, y);
    /* Two tones, both already in DESIGN.md §2's surface set: a ghost is
     * structure, not text, so it does not get a text colour and does not need
     * to clear a contrast floor — the opposite, in fact. It has to stay
     * clearly BELOW the text tones or it reads as content that failed to
     * render rather than content that has not arrived. */
    lv_obj_set_style_bg_color(g, dim ? THEME_DIVIDER : THEME_BORDER_IDLE, 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g, h / 2, 0);
    lv_obj_set_style_pad_all(g, 0, 0);
    lv_obj_set_style_border_width(g, 0, 0);
    lv_obj_set_scrollable(g, false);
    return g;
}
