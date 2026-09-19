/* widget_compass.c — see widget_compass.h for the contract. */
#include <stdio.h>
#include "widget_compass.h"
#include "theme.h"
#include "fonts/fonts.h"
#include "strings_de.h"

/* Internal geometry, in px, all inside the widget's own (0,0)-(width,HEIGHT)
 * box. Kept together so create() and set_bearing() can't drift apart. */
#define READOUT_Y     0
#define BASELINE_Y    20
#define BASELINE_H    2
#define MARKER_Y      14
#define MARKER_SIZE   12
#define TICK_ROW_Y    36
#define READOUT_GAP   4   /* px between the abbreviation and the degree figure */

/* Both are chrome-tier (12-13 px) per the task brief: this tape reinforces
 * the bearing that the data band already states in full words, it does not
 * carry it alone. */
static const lv_font_t *const TICK_FONT    = &plex_mono_13;
static const lv_font_t *const READOUT_FONT = &plex_mono_13;

/* Single-instance widget — see the "no heap" note in widget_compass.h. */
static lv_obj_t *s_marker;
static lv_obj_t *s_lbl_abbr;
static lv_obj_t *s_lbl_deg;
static int32_t   s_width;

/* Centres `obj`'s already-set text under `center_x`, clamped so it never
 * runs past [0, bound_w] — the tape's own edges. */
static void position_centered(lv_obj_t *obj, int32_t center_x, int32_t y, int32_t bound_w)
{
    lv_obj_update_layout(obj);
    int32_t w = lv_obj_get_width(obj);
    int32_t x = center_x - w / 2;
    if (x + w > bound_w) {
        x = bound_w - w;
    }
    if (x < 0) {
        x = 0;
    }
    lv_obj_set_pos(obj, x, y);
}

lv_obj_t *widget_compass_create(lv_obj_t *parent, int32_t width,
                                 const char *const cardinal_labels[WIDGET_COMPASS_NUM_CARDINALS])
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, width, WIDGET_COMPASS_HEIGHT);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_scrollable(cont, false);

    /* The tape's baseline. */
    lv_obj_t *baseline = lv_obj_create(cont);
    lv_obj_remove_style_all(baseline);
    lv_obj_set_size(baseline, width, BASELINE_H);
    lv_obj_set_pos(baseline, 0, BASELINE_Y);
    lv_obj_set_style_bg_color(baseline, THEME_HAIRLINE, 0);
    lv_obj_set_style_bg_opa(baseline, LV_OPA_COVER, 0);

    /* Fixed cardinal ticks — built once, never touched again. Index
     * WIDGET_COMPASS_NUM_CARDINALS wraps back to the first label (north),
     * one tape-width later, so the strip reads like an HSI tape rather than
     * a wrapped dial. */
    for (int i = 0; i <= WIDGET_COMPASS_NUM_CARDINALS; i++) {
        const char *txt = cardinal_labels[i % WIDGET_COMPASS_NUM_CARDINALS];
        lv_obj_t *lbl = lv_label_create(cont);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_font(lbl, TICK_FONT, 0);
        lv_obj_set_style_text_color(lbl, THEME_TEXT_LABEL, 0);
        lv_label_set_text(lbl, txt);
        int32_t center_x = (width * i) / WIDGET_COMPASS_NUM_CARDINALS;
        position_centered(lbl, center_x, TICK_ROW_Y, width);
    }

    /* The moving bearing marker — magenta, "the thing you are heading
     * toward" (DESIGN.md §2). A shape, not just a colour: it is the only
     * round object on the tape. */
    s_marker = lv_obj_create(cont);
    lv_obj_remove_style_all(s_marker);
    lv_obj_set_size(s_marker, MARKER_SIZE, MARKER_SIZE);
    lv_obj_set_style_radius(s_marker, MARKER_SIZE / 2, 0);
    lv_obj_set_style_bg_color(s_marker, THEME_MAGENTA, 0);
    lv_obj_set_style_bg_opa(s_marker, LV_OPA_COVER, 0);
    lv_obj_set_pos(s_marker, 0, MARKER_Y);

    /* The read-out beside the marker — white abbreviation, cyan degrees, so
     * the marker's colour never carries the meaning alone (DO-257A §2.1.6). */
    s_lbl_abbr = lv_label_create(cont);
    lv_label_set_long_mode(s_lbl_abbr, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(s_lbl_abbr, READOUT_FONT, 0);
    lv_obj_set_style_text_color(s_lbl_abbr, THEME_WHITE, 0);
    lv_label_set_text(s_lbl_abbr, "");
    lv_obj_set_pos(s_lbl_abbr, 0, READOUT_Y);

    s_lbl_deg = lv_label_create(cont);
    lv_label_set_long_mode(s_lbl_deg, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(s_lbl_deg, READOUT_FONT, 0);
    lv_obj_set_style_text_color(s_lbl_deg, THEME_CYAN, 0);
    lv_label_set_text(s_lbl_deg, "");
    lv_obj_set_pos(s_lbl_deg, 0, READOUT_Y);

    s_width = width;
    return cont;
}

void widget_compass_set_bearing(lv_obj_t *compass, float bearing_deg, const char *bearing_abbr)
{
    (void)compass; /* single-instance widget — see widget_compass.h */

    float b = bearing_deg;
    while (b < 0.0f) {
        b += 360.0f;
    }
    while (b >= 360.0f) {
        b -= 360.0f;
    }

    int32_t x = (int32_t)((float)s_width * b / 360.0f);
    if (x > s_width - MARKER_SIZE) {
        x = s_width - MARKER_SIZE;
    }
    if (x < 0) {
        x = 0;
    }
    lv_obj_set_x(s_marker, x);

    lv_label_set_text(s_lbl_abbr, bearing_abbr ? bearing_abbr : "");

    /* The only formatting this widget does: a raw float in, a short
     * degree figure out. No lookup, no German — see widget_compass.h. */
    char deg_buf[16]; /* generous: GCC's format-truncation check sizes "%d" against the
                        * full int range, not the 0-359 this code actually produces */
    int  deg = (int)(b + 0.5f);
    if (deg >= 360) {
        deg = 0;
    }
    snprintf(deg_buf, sizeof deg_buf, FMT_DEGREES, deg);
    lv_label_set_text(s_lbl_deg, deg_buf);

    /* The abbreviation and the degree figure are two label objects, but they
     * read as one thing and must be placed as one thing.
     *
     * They used to be positioned and edge-clamped INDEPENDENTLY, which is fine
     * until the bearing approaches 0/360: both got pushed against the right
     * edge and landed on top of each other, printing "NNW" and "352°" as one
     * unreadable smear. Seen on a live Pattaya poll at 352°.
     *
     * So: measure both, centre the PAIR on the marker, clamp the pair once,
     * then lay them out inside it. */
    lv_obj_update_layout(s_lbl_abbr);
    lv_obj_update_layout(s_lbl_deg);
    int32_t abbr_w = lv_obj_get_width(s_lbl_abbr);
    int32_t deg_w  = lv_obj_get_width(s_lbl_deg);
    int32_t group_w = abbr_w + READOUT_GAP + deg_w;

    int32_t marker_center = x + MARKER_SIZE / 2;
    int32_t group_x = marker_center - group_w / 2;
    if (group_x + group_w > s_width) {
        group_x = s_width - group_w;
    }
    if (group_x < 0) {
        group_x = 0;
    }

    lv_obj_set_pos(s_lbl_abbr, group_x, READOUT_Y);
    lv_obj_set_pos(s_lbl_deg,  group_x + abbr_w + READOUT_GAP, READOUT_Y);
}
