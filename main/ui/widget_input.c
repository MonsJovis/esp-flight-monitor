/* widget_input.c — see widget_input.h for why this is shared rather than
 * copied into each screen. */
#include "widget_input.h"
#include "theme.h"
#include "fonts/fonts.h"
#include "esp_log.h"

static const char *TAG = "kbd";

/* ============================================================================
 * The German layout (widget_input.h explains what it replaces and why it is
 * process-wide rather than per-keyboard).
 *
 * FOUR ROWS, EVERY ROW ADDING UP TO 11 UNITS, so the columns line up down the
 * whole keyboard instead of each row setting its own rhythm. LVGL's stock
 * layout does not do this — its rows come to 52, 40, 12 and 14 units — and on
 * a 480 px panel the ragged grid is visible. The widths are the numbers in
 * the ctrl tables below; a bare `1` is one unit.
 *
 *   q w e r t z u i o p ü          11 x 1
 *   a s d f g h j k l ö ä          11 x 1
 *   [ABC]2  y x c v b n m  [<-]2    2 + 7 + 2
 *   [1#]2  ß  [ space ]5  -  [OK]2  2 + 1 + 5 + 1 + 2
 *
 * THE KEY CAPS ARE NOT IN main/strings_de.h AND MUST NOT BE. They are the
 * alphabet, not prose: tools/check_strings.py gathers German so a native
 * speaker can read every sentence the device can say, and twenty-six single
 * letters in that lexicon would bury the sentences without adding one word to
 * judge. The gate has a rule for this — see its `is_single_letter`.
 *
 * THE THREE LAYER KEYS ARE A CONTRACT WITH LVGL, SPELLED BY HAND BECAUSE IT
 * GIVES US NO CHOICE. lv_keyboard_def_event_cb() decides whether a key
 * switches layer by comparing its cap TEXT — lv_strcmp(txt, "abc") and the
 * other two — against LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_LOWER / _UPPER /
 * _SPECIAL. Those macros are #ifndef-guarded inside lv_keyboard.c itself and
 * are not exported by lv_keyboard.h, so a custom map cannot reference them
 * and has to carry the same three strings. Get one wrong and nothing warns:
 * the key stops switching layers and starts typing its own cap into the
 * field. Pinned here against LVGL 9.6's lv_keyboard.c, lines 31-39.
 * ============================================================================
 */

/* See above — not German, not ours, and not free to change. */
#define KB_MODE_LOWER   "abc"
#define KB_MODE_UPPER   "ABC"
#define KB_MODE_SPECIAL "1#"

#define KB_DE_BUTTONS 36   /* 11 + 11 + 9 + 5, checked at install time */

static const char * const kb_de_map_lc[] = {
    "q", "w", "e", "r", "t", "z", "u", "i", "o", "p", "\xC3\xBC", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\xC3\xB6", "\xC3\xA4", "\n",
    KB_MODE_UPPER,
    "y", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
    KB_MODE_SPECIAL,
    "\xC3\x9F", " ", "-", LV_SYMBOL_OK, ""
};

static const char * const kb_de_map_uc[] = {
    "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "\xC3\x9C", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\xC3\x96", "\xC3\x84", "\n",
    KB_MODE_LOWER,
    "Y", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
    KB_MODE_SPECIAL,
    /* ß has no uppercase anyone types. Capital ẞ (U+1E9E) exists, is not in
     * the font subset, and is not what he would reach for. */
    "\xC3\x9F", " ", "-", LV_SYMBOL_OK, ""
};

/* One ctrl table for both maps: the two layouts are the same shape, key for
 * key, so the widths and flags are the same too. Two copies would be two
 * things to keep in step for no gain.
 *
 * LV_KEYBOARD_CTRL_BUTTON_FLAGS is NO_REPEAT|CLICK_TRIG|CHECKED — a layer key
 * must not auto-repeat while a finger rests on it. Backspace gets bare
 * CHECKED, so holding it DOES repeat, which is what makes clearing a wrong
 * word bearable. CHECKED is also what puts a key into Montserrat and the
 * quieter tone (widget_style_keyboard, below): control keys and letter keys
 * are told apart exactly once, here, and everything else follows from it. */
static const lv_buttonmatrix_ctrl_t kb_de_ctrl[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
    1, 1, 1, 1, 1, 1, 1, LV_BUTTONMATRIX_CTRL_CHECKED | 2,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
    1, 5, 1, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
};

/* Buttons in a map: everything that is not a row break or the terminator.
 * Counted rather than asserted by hand — the last time a length in this
 * codebase was worked out by eye it was two JSON fixtures and both were
 * wrong. */
static int map_button_count(const char * const *map)
{
    int n = 0;
    for (int i = 0; map[i][0] != '\0'; i++) {
        if (map[i][0] != '\n') {
            n++;
        }
    }
    return n;
}

void widget_keyboard_install_de(lv_obj_t *kb)
{
    /* lv_keyboard copies exactly as many ctrl entries as the map has buttons.
     * A table one entry short is therefore a read off the end of a const
     * array — no crash, no log, just a key somewhere with the wrong width or
     * silently marked hidden. Cheap to check, and checked every time rather
     * than in a comment, because a comment does not notice an edit. */
    int lc = map_button_count(kb_de_map_lc);
    int uc = map_button_count(kb_de_map_uc);
    if (lc != KB_DE_BUTTONS || uc != KB_DE_BUTTONS ||
        (int)(sizeof kb_de_ctrl / sizeof kb_de_ctrl[0]) != KB_DE_BUTTONS) {
        /* Leave LVGL's own layout in place. A keyboard missing its umlauts is
         * a nuisance; a keyboard reading past the end of its control table is
         * a bug that will be found somewhere else entirely. */
        ESP_LOGE(TAG, "German layout not installed: %d/%d keys, %d ctrl entries, expected %d",
                 lc, uc, (int)(sizeof kb_de_ctrl / sizeof kb_de_ctrl[0]), KB_DE_BUTTONS);
        return;
    }

    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, kb_de_map_lc, kb_de_ctrl);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, kb_de_map_uc, kb_de_ctrl);
}

bool widget_keyboard_debug_layer(lv_obj_t *kb)
{
    if (kb == NULL) {
        return false;
    }
    const char *want;
    switch (lv_keyboard_get_mode(kb)) {
    case LV_KEYBOARD_MODE_TEXT_UPPER: want = KB_MODE_SPECIAL; break;
    case LV_KEYBOARD_MODE_SPECIAL:    want = KB_MODE_LOWER;   break;
    default:                          want = KB_MODE_UPPER;   break;
    }

    /* The 1# layer is LVGL's own map and has more buttons than ours, so the
     * loop is bounded by what the widget says it has rather than by
     * KB_DE_BUTTONS — lv_buttonmatrix_get_button_text() returns NULL past the
     * end, which is the only count either map agrees to give. */
    for (uint32_t i = 0; i < 64; i++) {
        const char *txt = lv_buttonmatrix_get_button_text(kb, i);
        if (txt == NULL) {
            break;                    /* past the end of this map */
        }
        if (lv_strcmp(txt, want) != 0) {
            continue;
        }
        /* The two steps lv_buttonmatrix does itself on a release: mark which
         * button was hit, then tell the widget its value changed. Going
         * through lv_keyboard_def_event_cb() is the whole point — calling
         * lv_keyboard_set_mode() directly would switch the layer and prove
         * nothing about the text comparison that is being checked. */
        lv_buttonmatrix_set_selected_button(kb, i);
        lv_obj_send_event(kb, LV_EVENT_VALUE_CHANGED, NULL);
        return true;
    }
    return false;
}

void widget_kill_button_chrome(lv_obj_t *btn)
{
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
}

void widget_style_keyboard(lv_obj_t *kb)
{
    /* Before the styling, so that a keyboard is never briefly on the glass
     * with the wrong layout on it. */
    widget_keyboard_install_de(kb);

    lv_obj_set_style_bg_color(kb, THEME_GROUND, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb, 2, LV_PART_MAIN);

    /* The letters. Plex Sans Condensed 34 — this device's own face, and the
     * only one of the two that has ä ö ü ß. Condensed is what makes 34 px fit
     * an 11-column row at all, and 34 against DESIGN.md §3's 24 px near-tier
     * floor is deliberate headroom: this is the one control on the device he
     * has to hit repeatedly and accurately with a fingertip. */
    lv_obj_set_style_text_font(kb, &plex_sans_cond_34, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, THEME_TEXT_PRIMARY, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, THEME_SURFACE_SEL, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, THEME_BORDER_IDLE, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, THEME_BASE_UNIT / 2, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_opa(kb, LV_OPA_TRANSP, LV_PART_ITEMS);

    /* THE CONTROL KEYS NEED THEIR OWN PASS, and it carries the other font.
     *
     * Every layer key, backspace and OK carries LV_BUTTONMATRIX_CTRL_CHECKED
     * (kb_de_ctrl above), and LVGL's default theme gives LV_STATE_CHECKED a
     * style of its own — so styling LV_PART_ITEMS alone darkens the letters
     * and leaves the control keys sitting near-white in the middle of them.
     * Measured on the panel; it is not something the code reads as wrong.
     *
     * The font is here for a harder reason: backspace and OK are FontAwesome
     * codepoints that exist in Montserrat and not in Plex, so they have to be
     * drawn by Montserrat or they are drawn by nothing. lv_buttonmatrix
     * re-reads this part's label style per button with that button's own
     * state (lv_buttonmatrix.c, draw_main), which is what makes a per-state
     * font work at all — it is not a documented feature, it is a read of the
     * draw loop, and it is verified on the panel rather than assumed. */
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_24, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(kb, THEME_GROUND, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb, THEME_TEXT_LABEL, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(kb, THEME_BORDER_IDLE, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS | LV_STATE_CHECKED);

    /* A pressed key has to be unmistakable: he is looking at his finger, not
     * at the key, and this is the only feedback that a tap registered. Both
     * states, or a pressed control key would stay dark while a pressed
     * letter lit up. */
    lv_obj_set_style_bg_color(kb, THEME_CYAN, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, THEME_GROUND, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(kb, THEME_CYAN,
                              LV_PART_ITEMS | LV_STATE_PRESSED | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb, THEME_GROUND,
                                LV_PART_ITEMS | LV_STATE_PRESSED | LV_STATE_CHECKED);
}

void widget_style_field(lv_obj_t *ta)
{
    lv_obj_set_style_bg_color(ta, THEME_SURFACE_SEL, 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ta, THEME_BORDER_IDLE, 0);
    lv_obj_set_style_border_width(ta, 2, 0);
    lv_obj_set_style_radius(ta, THEME_BASE_UNIT, 0);
    lv_obj_set_style_text_color(ta, THEME_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_color(ta, THEME_TEXT_TERTIARY, LV_PART_TEXTAREA_PLACEHOLDER);
    /* Cyan while focused, like every other "this is the live value" on this
     * device (DESIGN.md §2). */
    lv_obj_set_style_border_color(ta, THEME_CYAN, LV_STATE_FOCUSED);
}
