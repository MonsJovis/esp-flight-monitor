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
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Puts an lv_keyboard into DESIGN.md §2's colour system: dark keys, primary
 * text, cyan under a finger — and installs the German layout below.
 *
 * TWO FACES, ON PURPOSE, and the split is not cosmetic.
 *
 * The LETTERS are Plex Sans Condensed 34, this device's own body face, which
 * DESIGN.md §3 subsets with the Latin-1 supplement — so it has ä ö ü ß Ä Ö Ü.
 * The CONTROL KEYS are Montserrat 24 (CONFIG_LV_FONT_MONTSERRAT_24), because
 * backspace, shift and OK are LV_SYMBOL_* codepoints in the Unicode private
 * use area and the Plex subset has no entry for any of them.
 *
 * Neither face can draw the whole keyboard. LVGL's built-in Montserrat is
 * subset "-r 0x20-0x7F,0xB0,0x2022" plus FontAwesome — READ OFF THE GENERATED
 * FILE, not assumed — so it has no umlauts, and an "ü" key drawn in it is a
 * key with nothing on it. Plex has the umlauts and none of the symbols, so a
 * backspace drawn in it is a key with nothing on it either. The same trap
 * from both directions (AGENTS.md §7: LVGL draws a missing glyph as nothing,
 * logs nothing, and the key still works when you press it).
 *
 * The split works because lv_buttonmatrix re-reads the label style per button
 * with that button's own state (lv_buttonmatrix.c, draw_main), and every
 * control key carries LV_BUTTONMATRIX_CTRL_CHECKED. So LV_PART_ITEMS gets
 * Plex and LV_PART_ITEMS|LV_STATE_CHECKED gets Montserrat, and each key is
 * drawn by the face that has its glyph. Mixing faces is invisible here: the
 * control keys are symbols, not type, and they are a tone quieter anyway.
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

/* Installs the German QWERTZ layout — the umlauts in the places a German
 * keyboard has always had them, ü right of p and ö ä right of l, and ß on the
 * bottom row.
 *
 * Called for you by widget_style_keyboard(); it is here separately only
 * because it is worth reading about.
 *
 * WHAT IT REPLACES. LVGL's stock layout is US QWERTY with `_ - . , :` filling
 * the bottom letter row. For a man searching for the town he lives near,
 * every one of those five keys is a key he will never press, and the two he
 * needs — ö and ä — are not there at all. The device is German, is used by
 * exactly one German speaker, and has never once needed to type English.
 *
 * WHAT IT DROPS, and why that is not a loss: the cursor keys and the
 * close-keyboard glyph. Both screens that carry a keyboard already have a
 * 64 px Zurück/Abbrechen button in plain words above it, which is a far
 * easier thing to hit than a keyboard key and cannot be confused with the
 * tick beside it; and lv_textarea moves the cursor when he taps INTO the
 * text, which is how he expects to fix a typo anyway. That buys the width
 * back for a bottom row of three big targets.
 *
 * IT IS PROCESS-WIDE, NOT PER-KEYBOARD, and that is a real gotcha worth
 * knowing before you touch it: lv_keyboard_set_map() writes into a file-scope
 * table inside lv_keyboard.c (`kb_map[mode] = map`), which every keyboard
 * reads at redraw. Two keyboards cannot have two layouts in this LVGL, so
 * installing one from either screen changes both.
 *
 * Here that is exactly what is wanted — widget_input.h's whole argument is
 * that the WLAN keyboard and the Ortssuche keyboard must be the same
 * keyboard — so this leans on it rather than fighting it, and calls the
 * installer from the styling function so that the two can never come apart.
 * It is idempotent: it writes the same two pointers every time.
 *
 * The 1# layer (digits and punctuation) is deliberately left as LVGL's own.
 * A WPA passphrase is arbitrary ASCII and has to stay typeable, that layer is
 * what makes it typeable, and re-spelling a table he reaches roughly once in
 * the life of the device would be risk spent on nothing.
 *
 * Takes a keyboard although what it installs is global, because that is
 * lv_keyboard_set_map()'s signature: the table is shared, the refresh is per
 * object. Pass the one you are building.
 */
void widget_keyboard_install_de(lv_obj_t *kb);

/* Presses whichever layer key moves the keyboard to the NEXT layer — abc ->
 * ABC -> 1# -> abc — exactly as a finger would, and returns true if that key
 * was found and pressed.
 *
 * A debug entry point for the single most fragile line in this file. The
 * layout above has to spell "abc" and "ABC" by hand, because lv_keyboard.c
 * keeps those three tokens to itself and decides what a key DOES by comparing
 * its cap text against them (see the note over the tables). Get one wrong and
 * there is no error anywhere: the key simply stops switching layers and
 * starts typing its own cap into the field. That is a contract with a
 * third-party library, written out by hand, which is precisely the kind of
 * thing this codebase is not willing to take on trust.
 *
 * So it is pressed from the console and the panel is photographed: the next
 * layer on the glass means the contract holds, the cap's own letters sitting
 * in the search field means it does not. Cycling rather than toggling because
 * there are THREE tokens and all three have to be right — 1# in particular
 * is the layer a WPA passphrase needs, and it is reachable on this device
 * from exactly one place, the WLAN password step, which is the screen that
 * spent four milestones unverified. One flash settles all three, and they
 * stay settled across an LVGL bump, which is when this would actually break.
 *
 * Caller holds display_lock().
 */
bool widget_keyboard_debug_layer(lv_obj_t *kb);

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
