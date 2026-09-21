/* widget_signal.c — see widget_signal.h for why this is one object and why
 * the caller owns its state. */
#include "widget_signal.h"
#include "theme.h"

/* A 1 px radius on a 3 px bar: enough to take the hard pixel off each corner,
 * not enough to turn a bar into a lozenge. LV_RADIUS_CIRCLE would do the
 * latter and the ladder would stop reading as a ladder. */
#define BAR_RADIUS 1

/* The stroke through an unlit meter. Two pixels so it survives the panel's
 * own subpixel geometry at this size — one pixel of amber on a dark ground,
 * diagonally, comes out as a dotted line. */
#define SLASH_W 2

static void draw_cb(lv_event_t *e)
{
    lv_obj_t              *obj   = lv_event_get_target_obj(e);
    lv_layer_t            *layer = lv_event_get_layer(e);
    const widget_signal_t *st    = (const widget_signal_t *)lv_event_get_user_data(e);
    if (st == NULL) {
        return;
    }

    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa       = LV_OPA_COVER;
    dsc.border_width = 0;
    dsc.radius       = BAR_RADIUS;

    for (int i = 0; i < WIFI_BARS_MAX; i++) {
        /* Ascending heights, the shortest on the left, all of them standing
         * on the same baseline — which is the bottom edge of the object, so
         * a caller positions the meter by where its FEET go and the tall end
         * grows upward into the space above. */
        int32_t bh = st->h * (i + 1) / WIFI_BARS_MAX;
        int32_t x  = a.x1 + i * (st->bar_w + st->gap);

        /* LVGL areas are inclusive at both ends, so a bar `bar_w` wide runs
         * from x to x + bar_w - 1. Off by one here draws the gaps a pixel
         * narrower than they were laid out for and the whole meter creeps
         * right. */
        lv_area_t bar = {
            .x1 = x,
            .x2 = x + st->bar_w - 1,
            .y1 = a.y2 - bh + 1,
            .y2 = a.y2,
        };

        /* Lit bars are the chrome grey every other corner element on this
         * device uses (the clock, the range read-out, the battery badge);
         * unlit ones drop to the surface tone, so an empty meter is visibly
         * still a meter rather than an absence. Neither is a semantic colour
         * and neither needs to be: WHICH bars are lit is the carrier, and a
         * count is not a colour (DO-257A §2.1.6). */
        dsc.bg_color = (i < st->bars) ? THEME_TEXT_LABEL : THEME_BORDER_IDLE;
        lv_draw_rect(layer, &dsc, &bar);
    }

    if (st->linked) {
        return;
    }

    /* NO LINK GETS A STROKE, not merely an empty ladder.
     *
     * Zero lit bars on its own is four dark bars on a near-black ground, at
     * 18 px, in the corner — which from across the room is indistinguishable
     * from the meter not being there, and "the icon is missing" and "the
     * device has no network" must not look the same. The stroke is a SHAPE,
     * so it carries on its own; amber only agrees with it, and amber is
     * already this device's colour for "no network" (DESIGN.md §2, the same
     * token §5.3's Kein Netz uses).
     *
     * Bottom-left to top-right, inside the object's own box so LVGL's clip
     * keeps it there. */
    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color       = THEME_AMBER;
    ldsc.width       = SLASH_W;
    ldsc.opa         = LV_OPA_COVER;
    ldsc.round_start = 1;
    ldsc.round_end   = 1;
    ldsc.p1.x = a.x1;
    ldsc.p1.y = a.y2;
    ldsc.p2.x = a.x2;
    ldsc.p2.y = a.y1;
    lv_draw_line(layer, &ldsc);
}

lv_obj_t *widget_signal_create(lv_obj_t *parent, widget_signal_t *st,
                               int32_t bar_w, int32_t gap, int32_t h)
{
    if (st == NULL) {
        return NULL;
    }
    st->bar_w  = bar_w;
    st->gap    = gap;
    st->h      = h;
    st->bars   = 0;
    st->linked = false;

    lv_obj_t *sig = lv_obj_create(parent);
    lv_obj_remove_style_all(sig);
    lv_obj_set_size(sig, WIDGET_SIGNAL_W(bar_w, gap), h);
    lv_obj_set_style_pad_all(sig, 0, 0);
    lv_obj_set_style_border_width(sig, 0, 0);
    lv_obj_set_style_bg_opa(sig, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollable(sig, false);
    /* SCENERY DOES NOT EAT A TOUCH. LVGL 9's lv_obj constructor sets
     * clickable, so without this the meter is a hit target in its own right —
     * and the chrome one sits on the root, above the deck, in the corner. A
     * press landing on it would stop there instead of reaching nav.c's
     * tileview, so the long press to Einstellungen would quietly not work in
     * that part of the screen. That exact failure cost this codebase the
     * whole long-press once already (nav.c, bubble_decorative), on objects
     * that looked just as decorative as this one. */
    lv_obj_set_clickable(sig, false);
    /* The state is reachable two ways on purpose: the draw callback gets it
     * as its user data (it has no other route to it), and the object carries
     * it so that widget_signal_set() needs nothing but the object — which is
     * what lets the WLAN list update row 7's meter without also holding row
     * 7's struct. */
    lv_obj_set_user_data(sig, st);
    lv_obj_add_event_cb(sig, draw_cb, LV_EVENT_DRAW_MAIN, st);
    return sig;
}

void widget_signal_set(lv_obj_t *sig, int rssi_dbm, bool linked)
{
    if (sig == NULL) {
        return;
    }
    widget_signal_t *st = (widget_signal_t *)lv_obj_get_user_data(sig);
    if (st == NULL) {
        return;
    }

    int bars = linked ? wifi_bars(rssi_dbm) : 0;
    if (bars == st->bars && linked == st->linked) {
        return;   /* nothing moved — do not spend a redraw on it */
    }
    st->bars   = bars;
    st->linked = linked;
    lv_obj_invalidate(sig);
}
