/* The two LVGL widgets whose built-in theme fights this one.
 *
 * Everything else on this device is drawn by code that picks its own colours
 * out of theme.h. `lv_keyboard` and `lv_textarea` are not: they arrive fully
 * styled by LVGL's default theme, which is light — a near-white slab across
 * the bottom of a panel whose ground is #0A0B0D precisely because pure black
 * maximises halation for aging eyes (DESIGN.md §2). On a device that dims
 * itself at 22:00 to avoid exactly that, an unstyled keyboard is the
 * brightest thing in a dark living room.
 *
 * SHARED, rather than copied into each screen, which is a deliberate break
 * from the make_label()/make_button() convention next door. Those are eight
 * lines and it does not matter if two screens differ by a pixel. This is
 * thirty lines of colour that must be IDENTICAL on the WLAN screen and the
 * Ortssuche screen, because they are the same keyboard on the same device
 * and a man who has learnt one has learnt the other. Two copies of this
 * would drift, and DESIGN.md exists to stop precisely that.
 *
 * Caller holds display_lock(), like everything else that touches LVGL here.
 */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Puts an lv_keyboard into DESIGN.md §2's colour system: dark keys, primary
 * text, cyan under a finger.
 *
 * THE FONT STAYS MONTSERRAT, at 24 px (CONFIG_LV_FONT_MONTSERRAT_24). Its
 * backspace, shift, enter and close keys are LV_SYMBOL_* codepoints in the
 * Unicode private use area, and DESIGN.md §3's Plex subset has no entry for
 * any of them — a Plex face here would render those keys as nothing at all,
 * with no error logged anywhere (AGENTS.md §7). The default 14 px is far
 * under §3's 24 px near-tier floor for something an elderly user has to hit
 * with a fingertip, which is why the larger face is built at all.
 *
 * POSITION IT WITH lv_obj_align(), NEVER lv_obj_set_pos(). lv_keyboard's
 * constructor calls lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, 0, 0) on itself
 * (lv_keyboard.c), and in LVGL 9 x/y become an OFFSET FROM THE ALIGNMENT
 * once one is set — so lv_obj_set_pos(kb, 0, 240) does not place the
 * keyboard 240 px down the screen, it places it 240 px BELOW THE BOTTOM
 * EDGE, where nothing is drawn and nothing can be tapped. The screen renders
 * perfectly and simply has no keyboard on it.
 */
void widget_style_keyboard(lv_obj_t *kb);

/* The same for a one-line lv_textarea: dark fill, a cyan border while it has
 * focus (the "this is the live value" colour everywhere else on this
 * device), and a dimmed placeholder. The caller still sets the font, the
 * size and whether it is a password field. */
void widget_style_field(lv_obj_t *ta);

/* LVGL's default theme puts a SHADOW under every lv_button, and against this
 * ground it renders as a 2 px band of #525152 all round — measured off the
 * panel's own framebuffer, because nothing in the source asks for a shadow
 * and so nothing in the source looks wrong. On a full-width list row it
 * becomes a grey line under every divider and a grey column down both edges
 * of the list. Call this on any lv_button that is meant to look like part of
 * this design.
 */
void widget_kill_button_chrome(lv_obj_t *btn);

#ifdef __cplusplus
}
#endif
