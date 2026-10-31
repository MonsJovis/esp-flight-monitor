/* sim_keyboard.c — the password step of the WLAN screen on the host (D96).
 *
 * The same harness as sim_radar.c (sim_common.h): real LVGL in the device's
 * DIRECT mode, the real screen_wifi.c, a scripted finger.
 *
 * What it guards: the key at the bottom left of the 1# layer used to be
 * LVGL's close-keyboard glyph, and one touch of it threw the typed password
 * away. Found on the panel, typing a phone hotspot's password. So every key
 * on every layer is pressed here, not only the ones that look dangerous.
 *
 * The other half is the promise widget_input.h makes: a WPA passphrase is
 * arbitrary ASCII. Eight characters of it were on no layer at all, and a
 * password that needs one of them could not be typed on this device.
 */
#include <string.h>

#include "lvgl.h"
#include "screen_wifi.h"
#include "theme.h"
#include "sim_common.h"

static lv_obj_t *find_class(lv_obj_t *o, const lv_obj_class_t *cls)
{
    if (lv_obj_check_type(o, cls)) {
        return o;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *hit = find_class(lv_obj_get_child(o, (int32_t)i), cls);
        if (hit != NULL) {
            return hit;
        }
    }
    return NULL;
}

static int join_calls;
static void on_join(const char *ssid, const char *pass)
{
    (void)ssid; (void)pass;
    join_calls++;
}

/* What the widget does on a release: mark the button, say its value changed.
 * The same two steps as widget_keyboard_debug_layer(). */
static void press_key(lv_obj_t *kb, uint32_t i)
{
    lv_buttonmatrix_set_selected_button(kb, i);
    lv_obj_send_event(kb, LV_EVENT_VALUE_CHANGED, NULL);
    run_ms(20);
}

static uint32_t key_count(lv_obj_t *kb)
{
    uint32_t n = 0;
    while (lv_buttonmatrix_get_button_text(kb, n) != NULL) {
        n++;
    }
    return n;
}

static void switch_layer(lv_obj_t *kb, const char *cap)
{
    for (uint32_t i = 0; i < key_count(kb); i++) {
        if (strcmp(lv_buttonmatrix_get_button_text(kb, i), cap) == 0) {
            press_key(kb, i);
            return;
        }
    }
    CHECK(false, "no \"%s\" key on this layer", cap);
}

/* Every key but the ones that are meant to leave the layer or the step.
 * Backspace is pressed on purpose: it is the other control key on the row
 * and it must take one character, not the field. */
static void press_all(lv_obj_t *kb, lv_obj_t *ta, const char *layer)
{
    for (uint32_t i = 0; i < key_count(kb); i++) {
        const char *cap = lv_buttonmatrix_get_button_text(kb, i);
        CHECK(strcmp(cap, LV_SYMBOL_KEYBOARD) != 0 && strcmp(cap, LV_SYMBOL_CLOSE) != 0,
              "%s layer, key %u: a close glyph, one touch ends the step", layer, (unsigned)i);
        if (strcmp(cap, LV_SYMBOL_OK) == 0 || strcmp(cap, "abc") == 0 ||
            strcmp(cap, "ABC") == 0 || strcmp(cap, "1#") == 0) {
            continue;
        }
        press_key(kb, i);
        CHECK(lv_obj_is_visible(ta), "%s layer, key \"%s\" closed the password step", layer, cap);
        CHECK(lv_textarea_get_text(ta)[0] != '\0', "%s layer, key \"%s\" emptied the field", layer, cap);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1) s_outdir = argv[1];

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_fb, NULL, sizeof s_fb, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, THEME_GROUND, 0);
    screen_wifi_create(scr);
    screen_wifi_set_join_cb(on_join);

    static const char ssids[1][33] = { "Testnetz" };
    static const int8_t rssi[1] = { -50 };
    screen_wifi_set_networks(ssids, rssi, 1, NULL, 0);
    run_ms(50);

    GROUP("the password step opens on the abc layer");
    CHECK(screen_wifi_debug_password_step() == SCREEN_WIFI_PW_TAPPED, "no unsaved row to tap");
    run_ms(50);
    lv_obj_t *kb = find_class(scr, &lv_keyboard_class);
    lv_obj_t *ta = find_class(scr, &lv_textarea_class);
    CHECK(kb != NULL && ta != NULL, "no keyboard or no field");
    if (kb == NULL || ta == NULL) {
        return 1;
    }
    CHECK(lv_obj_is_visible(ta), "the step is not on the glass");
    CHECK(lv_keyboard_get_mode(kb) == LV_KEYBOARD_MODE_TEXT_LOWER, "wrong first layer");

    GROUP("no key on any layer ends the step or empties the field");
    lv_textarea_set_text(ta, "seed");
    press_all(kb, ta, "abc");
    switch_layer(kb, "ABC");
    press_all(kb, ta, "ABC");
    switch_layer(kb, "1#");
    press_all(kb, ta, "1#");
    render();
    png_write("keyboard_symbols");

    GROUP("a real finger on the bottom-left key of the 1# layer");
    CHECK(lv_keyboard_get_mode(kb) == LV_KEYBOARD_MODE_SPECIAL, "not on the 1# layer");
    lv_textarea_set_text(ta, "seed");
    tap(24, H - 20);
    CHECK(lv_obj_is_visible(ta), "the step closed");
    CHECK(strlen(lv_textarea_get_text(ta)) == 5, "wanted seed + one key, got %u chars",
          (unsigned)strlen(lv_textarea_get_text(ta)));

    GROUP("every printable ASCII character is on a layer");
    bool have[128] = { false };
    static const lv_keyboard_mode_t modes[] = {
        LV_KEYBOARD_MODE_TEXT_LOWER, LV_KEYBOARD_MODE_TEXT_UPPER, LV_KEYBOARD_MODE_SPECIAL,
    };
    for (int l = 0; l < 3; l++) {
        lv_keyboard_set_mode(kb, modes[l]);
        for (uint32_t i = 0; i < key_count(kb); i++) {
            const char *cap = lv_buttonmatrix_get_button_text(kb, i);
            if (cap[0] > 0 && cap[1] == '\0') {
                have[(int)cap[0]] = true;
            }
        }
    }
    for (int c = 0x20; c < 0x7F; c++) {
        CHECK(have[c], "'%c' (0x%02X) is on no layer", c, c);
    }

    GROUP("the tick still connects");
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_SPECIAL);
    for (uint32_t i = 0; i < key_count(kb); i++) {
        if (strcmp(lv_buttonmatrix_get_button_text(kb, i), LV_SYMBOL_OK) == 0) {
            press_key(kb, i);
            break;
        }
    }
    CHECK(join_calls == 1, "wanted one join, got %d", join_calls);
    CHECK(!lv_obj_is_visible(ta), "the step should have closed behind the join");

    printf("\n  %d checks, %d failed\n", t_run, t_fail);
    return t_fail != 0;
}
