/* screen_settings.c — see screen_settings.h for the contract.
 *
 * Layout: one scrolling column, PAD-indented, sections stacked top to bottom
 * in the order the brief specifies (Ort, Umkreis, Helligkeit, Nachtabsenkung,
 * WLAN, Zurück). Unlike screen_overhead.c, nothing here auto-shrinks or
 * reflows at runtime — every font is fixed, so every position is computed
 * ONCE in screen_settings_create() from measured font line-heights, and
 * screen_settings_update() only ever changes text, colour and visibility on
 * the already-positioned tree. That keeps a state change (say, toggling
 * auto-dim) from ever moving anything else on screen.
 */
#include <stdint.h>
#include <stdio.h>
#include "screen_settings.h"
#include <math.h>
#include "theme.h"
/* The km conversion and German number grouping come from the same helpers the
 * rest of the device uses — this file must not reinvent either. */
#include "fmt_de.h"
#include "fonts/fonts.h"

/* ============================================================================
 * FIXED UI CHROME STRINGS — the only German (or other user-facing) literals
 * in this file. Location names are NOT here: they come from location_name()
 * (settings.h) so this file never hardcodes "Gloggnitz" or "Pattaya". Audit
 * THIS block, not the rest of the file, when checking for stray hard-coded
 * text (AGENTS.md §10).
 * ============================================================================
 */
#define HEADING_ORT             "Ort"
#define HEADING_UMKREIS         "Umkreis"
#define HEADING_HELLIGKEIT      "Helligkeit"
#define HEADING_NACHTABSENKUNG  "Nachtabsenkung"

/* The word that rides along with the active location card's magenta fill —
 * DO-257A §2.1.6, never colour alone. */
#define STR_ACTIVE_TAG          "Aktiv"

#define STR_WLAN                "WLAN"
#define STR_BACK                "Zurück"

/* U+2192 "→" — a generic "this row goes somewhere" affordance, not the
 * magenta route glyph screen_overhead.c uses; coloured THEME_TEXT_LABEL
 * here, deliberately not magenta, so the two meanings never look alike. */
#define STR_ROW_ARROW           "\xE2\x86\x92"

/* Units beside the two slider read-outs. Aviation convention, not a
 * translation gap: this product's own visual language is avionics symbology
 * (DESIGN.md, direction B "Cockpit"), and NM is the standard range unit in
 * that idiom in German-speaking aviation too — unlike the km the rest of the
 * panel speaks for distances actually flown. See this file's report note on
 * why this screen does not convert nm -> km itself. */
/* km, not NM. DESIGN.md is explicit that the panel speaks km everywhere, and
 * "30 NM" means nothing to the man this is built for — he has no reason to know
 * what a nautical mile is. The API speaks NM; that is the API's business. */
#define UNIT_KM                 "km"
#define UNIT_PERCENT            "%"

/* "22:00 — 07:00". U+2014 "—" (EM DASH), matching the codepoint DESIGN.md §3
 * lists for the font subset (en dash, U+2013, is NOT subsetted and would
 * render blank). Hours only: settings_t has no minutes field. */
#define FMT_DIM_WINDOW          "%02d:00 \xE2\x80\x94 %02d:00"

/* Read-only "Eigener Ort" coordinates. U+00B0 "°" (also subsetted). Decimal
 * POINT, not comma: this is a raw lat/lon pair, not a translated number, and
 * matching main/data/fmt_de.c's German-comma formatting would mean
 * reimplementing its rounding logic in a UI file for a rarely-seen advanced
 * field — not worth it. See this file's report note. */
#define FMT_CUSTOM_COORDS       "%.4f\xC2\xB0, %.4f\xC2\xB0"

/* ============================================================================
 * Layout constants — px, on the 8 px base unit (THEME_BASE_UNIT).
 * ============================================================================
 */
#define PAD       THEME_SIDE_PADDING                            /* 20 */
#define CONTENT_W (THEME_SCREEN_WIDTH - 2 * THEME_SIDE_PADDING) /* 440 */

#define GAP_LABEL   8   /* heading -> its content; value line -> its slider */
#define GAP_CARD    16  /* between the three location cards */
#define GAP_SECTION 32  /* between one section's content and the next heading */
#define GAP_INNER   4   /* between two stacked lines inside one item */

#define CARD_PAD_V    16
#define CARD_PAD_H    16
#define CARD_BORDER_W 2
#define CARD_RADIUS   12

/* Every tappable row (location cards, WLAN, Zurück) is at least this tall —
 * comfortably clears the 56 px floor the brief sets for an elderly user. */
#define TOUCH_ROW_H 64

#define SLIDER_TRACK_H  16
#define SLIDER_KNOB_PAD 16 /* knob diameter = track height + 2*pad = 48 px */
/* Reserve the knob's full visual/touch footprint when advancing past a
 * slider, not just the track's own height, so nothing below ever overlaps
 * the oversized knob. */
#define SLIDER_ROW_CLEARANCE 48

#define SWITCH_W 72
#define SWITCH_H 40

/* ============================================================================
 * Widget tree — built once by screen_settings_create(), single instance
 * (this device shows exactly one settings screen), so plain file-scope
 * statics rather than a heap-allocated context. Matches screen_overhead.c.
 * ============================================================================
 */
static lv_obj_t *s_cont; /* the scrollable column itself */

/* Ort */
static lv_obj_t *s_card[LOC_COUNT];
static lv_obj_t *s_card_name[LOC_COUNT];
static lv_obj_t *s_card_tag[LOC_COUNT]; /* STR_ACTIVE_TAG, shown only when active */
static lv_obj_t *s_card_coords;         /* LOC_CUSTOM only, always visible */

/* Umkreis */
static lv_obj_t *s_radius_value;
static lv_obj_t *s_radius_unit;
static lv_obj_t *s_radius_slider;

/* Helligkeit */
static lv_obj_t *s_bright_value;
static lv_obj_t *s_bright_unit;
static lv_obj_t *s_bright_slider;

/* Nachtabsenkung */
static lv_obj_t *s_dim_switch;
static lv_obj_t *s_dim_window;

/* This screen's own working copy of the settings — every card tap, slider
 * release and switch toggle mutates one field of this and hands it to the
 * callback; screen_settings_update() replaces the whole thing wholesale with
 * the integrator's authoritative copy. */
static settings_t s_current;

static settings_changed_cb s_changed_cb;
static settings_wifi_cb    s_wifi_cb;
static settings_exit_cb    s_exit_cb;

/* ----------------------------------------------------------------------
 * Small widget helpers
 * ---------------------------------------------------------------------- */

/* A label with fixed font/colour, empty text — position and text are set by
 * the caller. Mirrors screen_overhead.c's make_label(); each screen keeps
 * its own copy rather than sharing one across files. */
static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}

/* A plain rounded, bordered, non-scrollable container used for every
 * tappable row on this screen: the three location cards, the WLAN row and
 * the Zurück button. Colours are the caller's job (the location cards
 * recolour themselves per screen_settings_update()); this only builds the
 * shared shape, size and touch behaviour. A subtle press-state fill gives
 * immediate feedback that a tap registered, which matters more here than
 * usual — he is elderly and this screen has no other confirmation until
 * the state visibly changes. */
static lv_obj_t *make_row(lv_obj_t *parent, int32_t y, int32_t h)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, CONTENT_W, h);
    lv_obj_set_pos(r, PAD, y);
    lv_obj_set_style_radius(r, CARD_RADIUS, 0);
    lv_obj_set_style_border_width(r, CARD_BORDER_W, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, THEME_SURFACE_SEL, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_scrollable(r, false);
    return r;
}

/* Right-aligns `label` inside a `container_w`-wide parent, `inset` px from
 * its right edge, at a fixed `y`. Used once at create time for the two
 * glyphs/words whose text never changes afterwards (the "Aktiv" tag, the
 * WLAN row's arrow) — text-driven repositioning belongs in
 * screen_settings_update() instead; see align_beside() below. */
static void align_right(lv_obj_t *label, int32_t container_w, int32_t inset, int32_t y)
{
    lv_obj_update_layout(label);
    int32_t w = lv_obj_get_width(label);
    lv_obj_set_pos(label, container_w - inset - w, y);
}

/* Positions `unit` immediately beside `value` (to its right, vertically
 * centred on it), `gap` px apart. Called from screen_settings_update()
 * every time `value`'s text (and therefore width) changes — "10" and "100"
 * are not the same width, so the unit label cannot be placed once and
 * forgotten the way align_right()'s targets can. */
static void align_beside(lv_obj_t *value, lv_obj_t *unit, int32_t gap)
{
    lv_obj_update_layout(value);
    int32_t vx = lv_obj_get_x(value);
    int32_t vy = lv_obj_get_y(value);
    int32_t vw = lv_obj_get_width(value);
    int32_t vh = lv_obj_get_height(value);
    lv_obj_update_layout(unit);
    int32_t uh = lv_obj_get_height(unit);
    lv_obj_set_pos(unit, vx + vw + gap, vy + (vh - uh) / 2);
}

/* Builds and fully re-skins a slider used for a settings value: track is
 * THEME_BORDER_IDLE ("slider tracks", DESIGN.md §2's own words for that
 * token), the filled indicator and the knob are THEME_CYAN ("settings
 * values" in the same table). The knob is padded well past the track's own
 * thickness so it is a large, easy target (brief: "sliders get a large
 * knob"), not the thin 16 px track alone. */
static void style_value_slider(lv_obj_t *slider, int32_t y)
{
    lv_obj_remove_style_all(slider);
    lv_obj_set_size(slider, CONTENT_W, SLIDER_TRACK_H);
    lv_obj_set_pos(slider, PAD, y);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, THEME_BORDER_IDLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, THEME_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, THEME_CYAN, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, SLIDER_KNOB_PAD, LV_PART_KNOB);
}

/* Rewrites the radius/brightness read-outs and keeps their unit label glued
 * beside them. Shared by screen_settings_update() (full refresh) and the
 * sliders' own LV_EVENT_VALUE_CHANGED handlers (live text while dragging,
 * see the report note on why dragging does NOT fire settings_changed_cb). */
static void set_radius_value_text(int32_t nm)
{
    /* The slider works in NM because that is what the API takes, but nothing
     * on the panel says so. Converted and formatted through the same helpers
     * every other number on the device goes through, so "1.000" would group
     * the way he expects rather than the way C does. */
    char buf[16];
    fmt_int_de((int32_t)lroundf(nm_to_km((float)nm)), buf, sizeof buf);
    lv_label_set_text(s_radius_value, buf);
    align_beside(s_radius_value, s_radius_unit, GAP_LABEL);
}

static void set_bright_value_text(int32_t pct)
{
    char buf[8];
    snprintf(buf, sizeof buf, "%d", (int)pct);
    lv_label_set_text(s_bright_value, buf);
    align_beside(s_bright_value, s_bright_unit, GAP_LABEL);
}

/* Sanitises the working copy, hands it to the integrator, then redraws this
 * screen from it — so the screen is visually correct immediately, whether
 * or not (or how soon) the integrator calls screen_settings_update() back
 * with the persisted result. */
static void emit_change(void)
{
    settings_sanitise(&s_current);
    if (s_changed_cb) {
        s_changed_cb(&s_current);
    }
    screen_settings_update(&s_current);
}

/* ----------------------------------------------------------------------
 * Event handlers
 * ---------------------------------------------------------------------- */

static void card_event_cb(lv_event_t *e)
{
    location_preset_t p = (location_preset_t)(intptr_t)lv_event_get_user_data(e);
    s_current.preset = p;
    emit_change();
}

static void radius_value_changed_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    set_radius_value_text(lv_slider_get_value(slider));
}

/* Fires once per completed drag, not once per pixel — see screen_settings.h
 * and AGENTS.md §7 (NVS commits tear the display; this screen does not
 * invite a write storm mid-drag). */
static void radius_released_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    s_current.radius_nm = lv_slider_get_value(slider);
    emit_change();
}

static void bright_value_changed_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    set_bright_value_text(lv_slider_get_value(slider));
}

static void bright_released_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    s_current.brightness_pct = lv_slider_get_value(slider);
    emit_change();
}

static void dim_switch_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    s_current.auto_dim = lv_obj_has_state(sw, LV_STATE_CHECKED);
    emit_change();
}

static void wifi_row_event_cb(lv_event_t *e)
{
    (void)e;
    if (s_wifi_cb) {
        s_wifi_cb();
    }
}

static void back_row_event_cb(lv_event_t *e)
{
    (void)e;
    if (s_exit_cb) {
        s_exit_cb();
    }
}

/* ----------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void screen_settings_create(lv_obj_t *parent)
{
    settings_defaults(&s_current);

    s_cont = lv_obj_create(parent);
    lv_obj_remove_style_all(s_cont);
    lv_obj_set_size(s_cont, THEME_SCREEN_WIDTH, THEME_SCREEN_HEIGHT);
    lv_obj_set_pos(s_cont, 0, 0);
    lv_obj_set_style_bg_color(s_cont, THEME_GROUND, 0);
    lv_obj_set_style_bg_opa(s_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_cont, 0, 0);
    lv_obj_set_style_border_width(s_cont, 0, 0);
    lv_obj_set_scrollable(s_cont, true);
    /* SCROLLING: vertical only, with momentum — the content below is taller
     * than 480 px and he may not realise a settings screen scrolls at all,
     * so it also matters that §1 (Ort) is fully visible before any scroll
     * happens, which the top-down layout below gives for free. */
    lv_obj_set_scroll_dir(s_cont, LV_DIR_VER);
    lv_obj_set_scroll_momentum(s_cont, true);

    /* Line heights, measured once — every position below is derived from
     * these rather than guessed, matching screen_overhead.c's convention. */
    int32_t heading_lh = lv_font_get_line_height(&plex_sans_cond_34);
    int32_t body_lh     = lv_font_get_line_height(&plex_sans_cond_25);
    int32_t coord_lh    = lv_font_get_line_height(&plex_mono_17);
    int32_t value_lh    = lv_font_get_line_height(&plex_mono_32);

    int32_t y = PAD;

    /* ================= 1. Ort ================= */
    lv_obj_t *h_ort = make_label(s_cont, &plex_sans_cond_34, THEME_TEXT_LABEL);
    lv_label_set_text(h_ort, HEADING_ORT);
    lv_obj_set_pos(h_ort, PAD, y);
    y += heading_lh + GAP_LABEL;

    int32_t card_h_plain  = LV_MAX(TOUCH_ROW_H, 2 * CARD_PAD_V + body_lh);
    int32_t card_h_custom = LV_MAX(TOUCH_ROW_H, 2 * CARD_PAD_V + body_lh + GAP_INNER + coord_lh);

    for (int i = 0; i < LOC_COUNT; i++) {
        bool    is_custom = (i == LOC_CUSTOM);
        int32_t h         = is_custom ? card_h_custom : card_h_plain;

        lv_obj_t *card = make_row(s_cont, y, h);
        lv_obj_set_style_bg_color(card, THEME_GROUND, 0);
        lv_obj_set_style_border_color(card, THEME_BORDER_IDLE, 0);

        lv_obj_t *name = make_label(card, &plex_sans_cond_25, THEME_TEXT_PRIMARY);
        lv_label_set_text(name, location_name((location_preset_t)i));
        lv_obj_set_pos(name, CARD_PAD_H, CARD_PAD_V);

        /* Never colour alone (DO-257A §2.1.6): the active card is also
         * tagged with this word, not just its magenta fill. */
        lv_obj_t *tag = make_label(card, &plex_sans_cond_25, THEME_TEXT_LABEL);
        lv_label_set_text(tag, STR_ACTIVE_TAG);
        align_right(tag, CONTENT_W, CARD_PAD_H, CARD_PAD_V);
        lv_obj_set_hidden(tag, true); /* screen_settings_update() shows it on the active card */

        s_card[i]      = card;
        s_card_name[i] = name;
        s_card_tag[i]  = tag;

        if (is_custom) {
            /* Read-only, always shown regardless of whether this preset is
             * active — see screen_settings_update(). */
            s_card_coords = make_label(card, &plex_mono_17, THEME_TEXT_LABEL);
            lv_obj_set_pos(s_card_coords, CARD_PAD_H, CARD_PAD_V + body_lh + GAP_INNER);

            /* TODO(coordinate entry): this screen has no way to SET
             * custom_lat/custom_lon — a numeric keypad or map tap is exactly
             * the "coordinate entry form" this screen exists to avoid
             * (AGENTS.md §6). Tapping this card only selects LOC_CUSTOM as
             * the active preset; the figure above is read-only. A real entry
             * path (map tap, or a geocoded "type your city") belongs one tap
             * further away — an advanced, rarely-used screen — not competing
             * here with the one-tap preset switch that is the whole point of
             * this section. */
        }

        lv_obj_add_event_cb(card, card_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        y += h + ((i == LOC_COUNT - 1) ? GAP_SECTION : GAP_CARD);
    }

    /* ================= 2. Umkreis ================= */
    lv_obj_t *h_umkreis = make_label(s_cont, &plex_sans_cond_34, THEME_TEXT_LABEL);
    lv_label_set_text(h_umkreis, HEADING_UMKREIS);
    lv_obj_set_pos(h_umkreis, PAD, y);
    y += heading_lh + GAP_LABEL;

    s_radius_value = make_label(s_cont, &plex_mono_32, THEME_CYAN);
    lv_obj_set_pos(s_radius_value, PAD, y);
    s_radius_unit = make_label(s_cont, &plex_sans_cond_22, THEME_TEXT_TERTIARY);
    lv_label_set_text(s_radius_unit, UNIT_KM);
    lv_obj_set_pos(s_radius_unit, PAD, y); /* placed for real in screen_settings_update() */
    y += value_lh + GAP_LABEL;

    s_radius_slider = lv_slider_create(s_cont);
    lv_slider_set_range(s_radius_slider, 10, 100);
    style_value_slider(s_radius_slider, y);
    lv_obj_add_event_cb(s_radius_slider, radius_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_radius_slider, radius_released_cb, LV_EVENT_RELEASED, NULL);
    y += SLIDER_ROW_CLEARANCE + GAP_SECTION;

    /* ================= 3. Helligkeit ================= */
    lv_obj_t *h_hell = make_label(s_cont, &plex_sans_cond_34, THEME_TEXT_LABEL);
    lv_label_set_text(h_hell, HEADING_HELLIGKEIT);
    lv_obj_set_pos(h_hell, PAD, y);
    y += heading_lh + GAP_LABEL;

    s_bright_value = make_label(s_cont, &plex_mono_32, THEME_CYAN);
    lv_obj_set_pos(s_bright_value, PAD, y);
    s_bright_unit = make_label(s_cont, &plex_sans_cond_22, THEME_TEXT_TERTIARY);
    lv_label_set_text(s_bright_unit, UNIT_PERCENT);
    lv_obj_set_pos(s_bright_unit, PAD, y);
    y += value_lh + GAP_LABEL;

    s_bright_slider = lv_slider_create(s_cont);
    lv_slider_set_range(s_bright_slider, 10, 100);
    style_value_slider(s_bright_slider, y);
    lv_obj_add_event_cb(s_bright_slider, bright_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_bright_slider, bright_released_cb, LV_EVENT_RELEASED, NULL);
    y += SLIDER_ROW_CLEARANCE + GAP_SECTION;

    /* ================= 4. Nachtabsenkung ================= */
    lv_obj_t *h_dim = make_label(s_cont, &plex_sans_cond_34, THEME_TEXT_LABEL);
    lv_label_set_text(h_dim, HEADING_NACHTABSENKUNG);
    lv_obj_set_pos(h_dim, PAD, y);
    y += heading_lh + GAP_LABEL;

    int32_t dim_row_y = y;

    s_dim_switch = lv_switch_create(s_cont);
    lv_obj_remove_style_all(s_dim_switch);
    lv_obj_set_size(s_dim_switch, SWITCH_W, SWITCH_H);
    lv_obj_set_pos(s_dim_switch, PAD, dim_row_y + (TOUCH_ROW_H - SWITCH_H) / 2);
    lv_obj_set_style_radius(s_dim_switch, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(s_dim_switch, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_dim_switch, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_dim_switch, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_dim_switch, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_dim_switch, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(s_dim_switch, THEME_BORDER_IDLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_dim_switch, THEME_BORDER_IDLE, LV_PART_INDICATOR);
    /* AC 25-11A "engaged modes, normal conditions" fits an active protection
     * schedule exactly — the same reasoning DESIGN.md §2 gives for green. */
    lv_obj_set_style_bg_color(s_dim_switch, THEME_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s_dim_switch, THEME_WHITE, LV_PART_KNOB);
    lv_obj_add_event_cb(s_dim_switch, dim_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_dim_window = make_label(s_cont, &plex_sans_cond_25, THEME_TEXT_PRIMARY);
    lv_obj_set_pos(s_dim_window, PAD + SWITCH_W + GAP_LABEL * 2,
                    dim_row_y + (TOUCH_ROW_H - body_lh) / 2);

    y += TOUCH_ROW_H + GAP_SECTION;

    /* ================= 5. WLAN ================= */
    lv_obj_t *wifi_row = make_row(s_cont, y, TOUCH_ROW_H);
    lv_obj_set_style_bg_color(wifi_row, THEME_GROUND, 0);
    lv_obj_set_style_border_color(wifi_row, THEME_BORDER_IDLE, 0);

    lv_obj_t *wifi_label = make_label(wifi_row, &plex_sans_cond_25, THEME_TEXT_PRIMARY);
    lv_label_set_text(wifi_label, STR_WLAN);
    lv_obj_set_pos(wifi_label, CARD_PAD_H, (TOUCH_ROW_H - body_lh) / 2);

    lv_obj_t *wifi_arrow = make_label(wifi_row, &plex_sans_cond_25, THEME_TEXT_LABEL);
    lv_label_set_text(wifi_arrow, STR_ROW_ARROW);
    align_right(wifi_arrow, CONTENT_W, CARD_PAD_H, (TOUCH_ROW_H - body_lh) / 2);

    lv_obj_add_event_cb(wifi_row, wifi_row_event_cb, LV_EVENT_CLICKED, NULL);

    y += TOUCH_ROW_H + GAP_SECTION;

    /* ================= 6. Zurück ================= */
    lv_obj_t *back_row = make_row(s_cont, y, TOUCH_ROW_H);
    lv_obj_set_style_bg_color(back_row, THEME_GROUND, 0);
    lv_obj_set_style_border_color(back_row, THEME_BORDER_IDLE, 0);

    lv_obj_t *back_label = make_label(back_row, &plex_sans_cond_25, THEME_TEXT_PRIMARY);
    lv_label_set_text(back_label, STR_BACK);
    lv_obj_update_layout(back_label);
    int32_t back_w = lv_obj_get_width(back_label);
    lv_obj_set_pos(back_label, (CONTENT_W - back_w) / 2, (TOUCH_ROW_H - body_lh) / 2);

    lv_obj_add_event_cb(back_row, back_row_event_cb, LV_EVENT_CLICKED, NULL);
}

void screen_settings_update(const settings_t *s)
{
    if (s == NULL) {
        return;
    }
    s_current = *s;

    /* --- Ort: fill + border + word tag on the active card only --- */
    for (int i = 0; i < LOC_COUNT; i++) {
        bool active = (i == s_current.preset);
        lv_obj_set_style_bg_color(s_card[i], active ? THEME_SURFACE_MAGENTA : THEME_GROUND, 0);
        lv_obj_set_style_border_color(s_card[i], active ? THEME_MAGENTA : THEME_BORDER_IDLE, 0);
        lv_obj_set_style_text_color(s_card_name[i], active ? THEME_WHITE : THEME_TEXT_PRIMARY, 0);
        lv_obj_set_hidden(s_card_tag[i], !active);
    }

    char coords[40];
    snprintf(coords, sizeof coords, FMT_CUSTOM_COORDS, s_current.custom_lat, s_current.custom_lon);
    lv_label_set_text(s_card_coords, coords);

    /* --- Umkreis --- */
    lv_slider_set_value(s_radius_slider, s_current.radius_nm, LV_ANIM_OFF);
    set_radius_value_text(s_current.radius_nm);

    /* --- Helligkeit --- */
    lv_slider_set_value(s_bright_slider, s_current.brightness_pct, LV_ANIM_OFF);
    set_bright_value_text(s_current.brightness_pct);

    /* --- Nachtabsenkung --- */
    if (s_current.auto_dim) {
        lv_obj_add_state(s_dim_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(s_dim_switch, LV_STATE_CHECKED);
    }
    char window[32];
    snprintf(window, sizeof window, FMT_DIM_WINDOW, s_current.dim_from_hour, s_current.dim_to_hour);
    lv_label_set_text(s_dim_window, window);
}

void screen_settings_set_cb(settings_changed_cb cb)
{
    s_changed_cb = cb;
}

void screen_settings_set_wifi_cb(settings_wifi_cb cb)
{
    s_wifi_cb = cb;
}

void screen_settings_set_exit_cb(settings_exit_cb cb)
{
    s_exit_cb = cb;
}
