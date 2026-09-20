/* widget_input.c — see widget_input.h for why this is shared rather than
 * copied into each screen. */
#include "widget_input.h"
#include "theme.h"

void widget_kill_button_chrome(lv_obj_t *btn)
{
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
}

void widget_style_keyboard(lv_obj_t *kb)
{
    lv_obj_set_style_bg_color(kb, THEME_GROUND, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb, 2, LV_PART_MAIN);

    /* The letters. Montserrat, not Plex — see widget_input.h. */
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_24, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, THEME_TEXT_PRIMARY, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, THEME_SURFACE_SEL, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, THEME_BORDER_IDLE, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, THEME_BASE_UNIT / 2, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_opa(kb, LV_OPA_TRANSP, LV_PART_ITEMS);

    /* THE CONTROL KEYS NEED THEIR OWN PASS. lv_keyboard marks shift, 1#,
     * ABC, backspace, enter, close and the two cursor keys with
     * LV_BUTTONMATRIX_CTRL_CHECKED, and LVGL's default theme gives
     * LV_STATE_CHECKED a style of its own — so styling LV_PART_ITEMS alone
     * darkens the letters and leaves nine near-white keys sitting in the
     * middle of them. Measured on the panel; it is not something the code
     * reads as wrong. They stay a step quieter than the letters, because a
     * letter is what he is looking for and backspace is not. */
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
