/* screen_wifi.c — see screen_wifi.h for the contract.
 *
 * DESIGN.md §5.7 / §3 NEAR tier (~40 cm, in hand or leaned into): every
 * label he needs to act on is >=24 px (plex_sans_cond_25), not the
 * 12-17 px the mockups used. This is also the one screen that is allowed
 * to interrupt him (DESIGN.md §6) and the reason on-device provisioning was
 * affordable over a captive portal at all (AGENTS.md §8) — see screen_wifi.h.
 *
 * Two full-bleed (480x480) sub-screens share `parent`, only one visible at
 * a time:
 *   s_main — title, status line, the scrollable network list, Suchen/Zurück.
 *   s_pw   — the password step: which network, a password field with a
 *            show/hide toggle, Verbinden/Abbrechen, and the keyboard.
 * Both are built once in screen_wifi_create() (screen_overhead.c's
 * create-once/update-many pattern). The ONE thing that is NOT create-once
 * is the network list itself: it is a fixed-size POOL of rows
 * (SCREEN_WIFI_MAX_ROWS), shown/hidden/re-texted by screen_wifi_set_networks(),
 * because the row count changes scan to scan and a pool means no widget is
 * ever created or destroyed on the display task after start-up.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "screen_wifi.h"
#include "theme.h"
#include "fonts/fonts.h"
#include "strings_de.h"
#include "widget_input.h"
#include "widget_busy.h"
#include "widget_signal.h"

/* ============================================================================
 * FIXED UI CHROME STRINGS — every German (or otherwise user-facing) literal
 * in this file lives here. Audit THIS block, not the rest of the file, when
 * checking for stray hard-coded text (screen_overhead.c's convention).
 *
 * NOTE on "..." below: DESIGN.md §3's lv_font_conv subsetting ranges for
 * the Plex faces cover ASCII, the umlauts, °, ·, → and — (screen_overhead.c
 * uses the arrow). They do NOT cover U+2026, the single "…" glyph. Using a
 * real ellipsis character here would hit exactly the "renders as nothing,
 * silently, with no error logged anywhere" trap AGENTS.md §7 calls out for
 * missing glyphs — so every "..." below is three ASCII periods on purpose.
 * ============================================================================
 */

/* ============================================================================
 * Layout constants — px, on the 8 px base unit (THEME_BASE_UNIT), matching
 * screen_overhead.c's convention.
 * ============================================================================
 */
#define PAD        THEME_SIDE_PADDING                            /* 20 */
#define CONTENT_W  (THEME_SCREEN_WIDTH - 2 * THEME_SIDE_PADDING) /* 440 */
#define GAP_SM     8
#define GAP_MD     16

/* Every tappable row/button on this NEAR-tier screen is >=56 px tall (task
 * brief); 64 = 8 * THEME_BASE_UNIT gives a comfortable margin above that
 * floor for an elderly user rather than sitting exactly on it. */
#define ROW_H        64
#define BTN_H        64
#define ROW_INSET    16   /* left/right inset for a row's own label(s) */
#define PW_TOGGLE_W  150  /* wide enough for "Verbergen", the longer of the two toggle words, at 25 px */

/* Pool size for the network list. wifi_scan()'s own `max` is the
 * integrator's call (main/net/wifi.h); this just needs to be at least as
 * large as a realistic scan result. Anything beyond this is silently
 * clamped in screen_wifi_set_networks() — the list still scrolls for
 * everything under the cap. */
#define SCREEN_WIFI_MAX_ROWS 24

/* Standard WPA2-PSK ASCII passphrase ceiling. wifi.h does not itself state
 * a password length limit (that lives in the NVS field size on the
 * integrator's side, and wifi_creds_set() already reports
 * ESP_ERR_INVALID_ARG if it is exceeded) — this is just a sane stop so the
 * on-screen keyboard does not let him type forever into a field that can
 * never be accepted. */
#define PW_MAX_PASS_LEN 63

/* ============================================================================
 * Widget tree — built once by screen_wifi_create(), single instance (one
 * WLAN screen per device), so file-scope statics rather than a heap context
 * (matches screen_overhead.c, main/debug/dbg_screen.c).
 * ============================================================================
 */

/* --- s_main: title, status, the busy bar, list, Suchen/Zurück --- */
static lv_obj_t *s_main;
static lv_obj_t *s_lbl_title;
static lv_obj_t *s_lbl_status;
static lv_obj_t *s_busy;
static lv_obj_t *s_list;
static lv_obj_t *s_lbl_empty;
static lv_obj_t *s_btn_rescan;
static lv_obj_t *s_btn_exit;

/* Ghost rows for the FIRST scan only — see set_waiting(). */
#define WIFI_SKEL_ROWS 4
static lv_obj_t *s_skel[WIFI_SKEL_ROWS];

/* How many networks are on the list right now. Kept because the skeleton
 * question is not "are we scanning" but "are we scanning with nothing to
 * show": a rescan over a list he can already read must not blank it. */
static int s_n_shown;

/* One entry per pool slot. `ssid[0] == '\0'` marks an unused slot. */
typedef struct {
    lv_obj_t *row;
    lv_obj_t *lbl_ssid;
    lv_obj_t *lbl_tick;   /* LV_SYMBOL_OK, LVGL default font (see create_row()) */
    lv_obj_t *lbl_saved;  /* STR_WIFI_SAVED */
    lv_obj_t *sig;        /* the four-bar meter at the right-hand end */
    /* The meter's own state, owned by the row rather than by the widget —
     * widget_signal.h's arrangement, which is what keeps twenty-four meters
     * from being twenty-four allocations on the display task. */
    widget_signal_t sig_state;
    char      ssid[SCREEN_WIFI_SSID_LEN];
    bool      saved;
} wifi_row_t;

static wifi_row_t s_rows[SCREEN_WIFI_MAX_ROWS];
/* Combined width of one row's tick + gap + STR_WIFI_SAVED label, in px.
 * Computed once from row 0 in create_row() — the text never changes, so
 * every row's badge is the same size. Used to size the SSID label so long
 * names don't run under the badge on a saved row. */
static int32_t s_badge_w;

/* What the meter takes out of a row, including the air to its left. A
 * constant rather than a measurement because, unlike the badge, its width is
 * arithmetic we already know (WIDGET_SIGNAL_W) and nothing about it depends
 * on a font. */
#define SIG_SLOT (WIDGET_SIGNAL_W(WIDGET_SIGNAL_ROW_BAR_W, WIDGET_SIGNAL_ROW_GAP) + GAP_MD)

/* --- s_pw: which network, password field, toggle, Verbinden/Abbrechen, keyboard --- */
static lv_obj_t *s_pw;
static lv_obj_t *s_pw_lbl_network;
static lv_obj_t *s_pw_ta;
static lv_obj_t *s_pw_toggle_lbl;
static lv_obj_t *s_pw_kb;

/* True only while this screen's widgets exist.
 *
 * The WiFi scan runs on its own task and calls screen_wifi_set_networks()
 * whenever it finishes — typically three to five seconds after the screen
 * opened. If the overlay was closed in the meantime, every pointer in this
 * file is dangling, and the scan task writes through all of them. That is
 * exactly what he does when he opens WLAN, reads "Suche Netzwerke...", and
 * taps Zurück without waiting: LoadProhibited in lv_obj_set_width(), from
 * wifi_scan_task, and the device reboots.
 *
 * Cleared by LVGL itself: nav_close_overlay() deletes the overlay, LVGL
 * deletes the children, and LV_EVENT_DELETE on s_main tells us. Nothing else
 * has to remember to call anything, which is the only version of this that
 * stays true. */
static bool s_alive;

static void on_main_deleted(lv_event_t *e)
{
    (void)e;
    s_alive = false;
    s_main  = NULL;
    s_pw    = NULL;
    /* And the waiting furniture, for the same reason the Ortssuche clears
     * its own: these are written through by set_waiting(), which is reached
     * from paths that do not all check s_alive first. s_n_shown goes with
     * them — a fresh screen that believed it still had rows would show the
     * bar alone on an empty list and never the ghosts. */
    s_busy    = NULL;
    s_n_shown = 0;
    for (int i = 0; i < WIFI_SKEL_ROWS; i++) {
        s_skel[i] = NULL;
    }
}

static char      s_pw_ssid[SCREEN_WIFI_SSID_LEN];

static wifi_join_cb   s_join_cb;
static wifi_rescan_cb s_rescan_cb;
static wifi_exit_cb   s_exit_cb;

/* ============================================================================
 * Forward declarations — the row/status/password-step callbacks reference
 * each other (e.g. tapping a saved row and tapping Verbinden both end up
 * setting the same "connecting" status), so this avoids having to sort the
 * whole file into one strict define-before-use order.
 * ============================================================================
 */
static void apply_status_text(const char *text, lv_color_t color);
static void set_waiting(bool waiting);
static void set_status_connecting(const char *ssid);
static void open_password_step(const char *ssid);
static void close_password_step(void);
static void do_connect(void);
static void do_cancel(void);
static void row_event_cb(lv_event_t *e);

/* ============================================================================
 * Small helpers
 * ============================================================================
 */

/* A label with fixed font + colour, empty text — position and text are set
 * per-caller (screen_overhead.c's helper of the same name and shape). */
static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}

/* A tappable button, sized explicitly (every button on this screen is
 * >=56 px tall per the task brief), with a centred 25 px label — the
 * near-tier floor applies to every tappable label, not just the list rows. */
static lv_obj_t *make_button(lv_obj_t *parent, int32_t w, int32_t h, const char *text,
                             lv_color_t bg, lv_color_t border, lv_color_t text_color)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    widget_kill_button_chrome(btn);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, border, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_FULL, 0);
    lv_obj_set_style_radius(btn, THEME_BASE_UNIT, 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &plex_sans_cond_25, 0);
    lv_obj_set_style_text_color(lbl, text_color, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

static void safe_copy_ssid(char dst[SCREEN_WIFI_SSID_LEN], const char *src)
{
    strncpy(dst, src, SCREEN_WIFI_SSID_LEN - 1);
    dst[SCREEN_WIFI_SSID_LEN - 1] = '\0';
}

/* ============================================================================
 * Status line
 * ============================================================================
 */
static void apply_status_text(const char *text, lv_color_t color)
{
    lv_label_set_text(s_lbl_status, text);
    lv_obj_set_style_text_color(s_lbl_status, color, 0);
}

/* The waiting state: the sweeping bar, and — only when there is nothing on
 * the list yet — the ghost rows.
 *
 * THE SKELETON IS FOR AN EMPTY LIST, NOT FOR EVERY SCAN. A rescan while he
 * can already see six networks must keep showing those six: replacing a list
 * he was reading with grey bars throws away information he has and tells him
 * nothing he does not. First scan, nothing to lose, ghosts say where the
 * answer will be; rescan, plenty to lose, the bar alone says it is working.
 * That is also the rule every phone in his pocket follows. */
static void set_waiting(bool waiting)
{
    if (!s_alive) {
        return;
    }
    widget_busy_set_active(s_busy, waiting);
    bool ghosts = waiting && s_n_shown == 0;
    for (int i = 0; i < WIFI_SKEL_ROWS; i++) {
        lv_obj_set_hidden(s_skel[i], !ghosts);
    }
    /* "Keine Netzwerke gefunden" and a set of ghost rows are two answers to
     * the same question, and one of them is wrong. While the ghosts are up
     * the scan has not answered yet, so the empty line must go. */
    if (ghosts) {
        lv_obj_set_hidden(s_lbl_empty, true);
    }
}

/* Local, optimistic status shown the instant he taps a saved row or
 * Verbinden — before the integrator's own screen_wifi_set_status() call for
 * the actual outcome has had time to arrive. Without this the status line
 * would sit on stale text (or "Nicht verbunden") for however long the
 * connect attempt takes, which reads as "did that tap even register?" to
 * this user (AGENTS.md §1: a sentence beats a stale or blank one). A later
 * screen_wifi_set_status() call always overwrites this. */
static void set_status_connecting(const char *ssid)
{
    char buf[64];
    snprintf(buf, sizeof buf, FMT_WIFI_CONNECTING, ssid);
    apply_status_text(buf, THEME_TEXT_LABEL);
    /* A join is the longer of the two waits on this screen — DHCP alone can
     * take several seconds — and it is the one where a device that looks
     * inert gets tapped again. The list stays exactly as it is: he has just
     * chosen a row from it and it has to stay there for him to see which. */
    set_waiting(true);
}

void screen_wifi_set_status(const char *ssid_or_null, bool connected, bool scanning)
{
    /* The screen may have been closed since whoever is calling last looked —
     * the scan task in particular finishes seconds later. */
    if (!s_alive) {
        return;
    }
    char buf[64];

    /* `scanning` wins over everything else: a rescan while still
     * technically associated to the old network must not show two
     * contradictory messages at once (screen_wifi.h). */
    if (scanning) {
        apply_status_text(STR_WIFI_SCANNING, THEME_TEXT_LABEL);
        set_waiting(true);
        return;
    }
    /* Everything below this line is an outcome, and an outcome ends the
     * wait — including the failure, which is the one a bar left sweeping
     * underneath would contradict most loudly. */
    set_waiting(false);
    if (connected) {
        if (ssid_or_null && ssid_or_null[0] != '\0') {
            snprintf(buf, sizeof buf, FMT_WIFI_CONNECTED, ssid_or_null);
            apply_status_text(buf, THEME_GREEN);
        } else {
            apply_status_text(STR_WIFI_CONNECTED_GEN, THEME_GREEN);
        }
        return;
    }
    if (ssid_or_null && ssid_or_null[0] != '\0') {
        snprintf(buf, sizeof buf, FMT_WIFI_FAILED, ssid_or_null);
        apply_status_text(buf, THEME_AMBER);
        return;
    }
    apply_status_text(STR_WIFI_IDLE, THEME_TEXT_LABEL);
}

/* ============================================================================
 * Network list — fixed pool, shown/hidden/re-texted per screen_wifi_set_networks()
 * ============================================================================
 */

static void row_event_cb(lv_event_t *e)
{
    wifi_row_t *row = (wifi_row_t *)lv_event_get_user_data(e);
    if (!row || row->ssid[0] == '\0') {
        return;
    }
    if (row->saved) {
        /* Behaviour #1 in the task brief: joins immediately, NO keyboard.
         * We never had the password — wifi_creds_list() deliberately never
         * returns one (wifi.h) — so NULL is the documented signal to the
         * join callback: "(re)connect `ssid` with whatever is already
         * stored for it", e.g. via wifi_reconnect_now() (see screen_wifi.h). */
        set_status_connecting(row->ssid);
        if (s_join_cb) {
            s_join_cb(row->ssid, NULL);
        }
    } else {
        open_password_step(row->ssid);
    }
}

/* Builds pool slot `idx` once. Row content (ssid/saved) is set later by
 * update_row(); this only creates widgets and fixes their static geometry. */
static void create_row(lv_obj_t *parent, int idx)
{
    wifi_row_t *row = &s_rows[idx];

    row->row = lv_button_create(parent);
    lv_obj_set_size(row->row, CONTENT_W, ROW_H);
    widget_kill_button_chrome(row->row);
    /* NO PADDING, so that ROW_INSET is the only inset there is.
     *
     * LVGL's default theme pads lv_button, and lv_obj_align() measures from
     * the CONTENT area — so every -ROW_INSET below was really -(ROW_INSET +
     * the theme's pad), on both sides at once, while the SSID label's width
     * was computed from CONTENT_W as though there were none. The label came
     * out about twenty pixels too wide and its ellipsis was drawn over the
     * green tick. Photographed; the arithmetic reads correct in isolation and
     * was measuring a box that was not the one on the glass. */
    lv_obj_set_style_pad_all(row->row, 0, 0);
    lv_obj_set_style_radius(row->row, THEME_BASE_UNIT, 0);
    lv_obj_set_hidden(row->row, true); /* pool starts empty; screen_wifi_set_networks() reveals what's in range */
    lv_obj_add_event_cb(row->row, row_event_cb, LV_EVENT_CLICKED, row);

    row->lbl_ssid = make_label(row->row, &plex_sans_cond_25, THEME_TEXT_PRIMARY);
    lv_label_set_long_mode(row->lbl_ssid, LV_LABEL_LONG_MODE_DOTS);
    /* ONE LINE HIGH, FIXED, and that is what makes DOT mode work at all.
     *
     * LVGL only ellipsises when the laid-out text is TALLER than the object
     * (lv_label.c), and update_row() sets a width but the height was left at
     * LV_SIZE_CONTENT — so a name too wide for its column simply grew a
     * second line and DOT mode never fired. "Apartamentos_Jose_Cruz", the
     * network this device is actually on, broke across two lines inside a
     * 64 px row, photographed on the panel. It had been doing that since M6:
     * at 240 px the name was already 90 px too wide, and adding the meter
     * only made it more obvious.
     *
     * The status line one band up has the identical pin for the identical
     * reason (D69, AGENTS.md §7). Same trap, second place, found the same
     * way — by looking at the glass rather than at the code. */
    lv_obj_set_height(row->lbl_ssid, lv_font_get_line_height(&plex_sans_cond_25));
    lv_obj_align(row->lbl_ssid, LV_ALIGN_LEFT_MID, ROW_INSET, 0);

    /* The tick deliberately does NOT get a Plex font. DESIGN.md §3's
     * lv_font_conv subsetting list has no entry for LV_SYMBOL_OK's
     * private-use-area codepoint, so applying a Plex face here would render
     * nothing at all — the exact "no error logged anywhere" glyph trap
     * AGENTS.md §7 warns about, just for a checkmark instead of an umlaut.
     * Left at the LVGL default font (Montserrat 14, built with the symbol
     * range), which does have it — see sdkconfig's
     * CONFIG_LV_FONT_DEFAULT_MONTSERRAT_14. */
    row->lbl_tick = lv_label_create(row->row);
    lv_label_set_text(row->lbl_tick, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(row->lbl_tick, THEME_GREEN, 0);

    row->lbl_saved = make_label(row->row, &plex_sans_cond_25, THEME_GREEN);
    lv_label_set_text(row->lbl_saved, STR_WIFI_SAVED);

    /* The meter, hard against the right-hand inset and vertically centred —
     * the SAME x on every row, saved or not, so the four ladders line down
     * the edge of the list as one column. That is the whole reason it is at
     * the outside end rather than tucked in beside the name: a column can be
     * compared at a glance, and comparing is what he is doing when he reads
     * this list. The "gespeichert" badge moves aside for it instead, since
     * that one is absent on most rows anyway. */
    row->sig = widget_signal_create(row->row, &row->sig_state,
                                    WIDGET_SIGNAL_ROW_BAR_W,
                                    WIDGET_SIGNAL_ROW_GAP,
                                    WIDGET_SIGNAL_ROW_H);
    lv_obj_align(row->sig, LV_ALIGN_RIGHT_MID, -ROW_INSET, 0);

    /* Position the badge once: its text is constant, so its size is
     * constant, so this does not need to re-run on every list rebuild. */
    lv_obj_update_layout(row->lbl_saved);
    lv_obj_align(row->lbl_saved, LV_ALIGN_RIGHT_MID, -ROW_INSET - SIG_SLOT, 0);
    lv_obj_update_layout(row->lbl_tick);
    int32_t saved_w = lv_obj_get_width(row->lbl_saved);
    int32_t tick_w  = lv_obj_get_width(row->lbl_tick);
    lv_obj_align(row->lbl_tick, LV_ALIGN_RIGHT_MID,
                 -ROW_INSET - SIG_SLOT - saved_w - GAP_SM, 0);
    if (idx == 0) {
        s_badge_w = tick_w + GAP_SM + saved_w;
    }

    lv_obj_set_hidden(row->lbl_tick, true);
    lv_obj_set_hidden(row->lbl_saved, true);
}

/* Fills pool slot `idx` with one scan result and shows it. Saved rows get
 * the surface-green "card" treatment theme.h already names for exactly this
 * (DESIGN.md §5.7); unsaved rows stay a plain divided list row instead of
 * looking like their own button, matching how §5.4's list uses dividers
 * between rows rather than a card per row. */
static void update_row(int idx, const char *ssid, bool saved, int rssi_dbm)
{
    wifi_row_t *row = &s_rows[idx];
    safe_copy_ssid(row->ssid, ssid);
    row->saved = saved;

    int32_t ssid_w = CONTENT_W - 2 * ROW_INSET - SIG_SLOT;
    if (saved) {
        ssid_w -= s_badge_w + GAP_MD;
    }
    lv_obj_set_width(row->lbl_ssid, ssid_w);
    lv_label_set_text(row->lbl_ssid, row->ssid);

    /* `linked` is true for every row here, and that is not a shrug: a network
     * a scan reported IS one the radio heard, so an empty meter beside its
     * name would be a claim the device never made. The crossed-out state is
     * for the chrome meter on the deck, where "no link at all" is a real
     * thing to say. A missing rssi array arrives as WIFI_RSSI_NONE and comes
     * out as an empty ladder with no stroke — nothing measured, nothing
     * claimed. */
    widget_signal_set(row->sig, rssi_dbm, true);

    lv_obj_set_hidden(row->lbl_tick, !saved);
    lv_obj_set_hidden(row->lbl_saved, !saved);

    if (saved) {
        lv_obj_set_style_bg_opa(row->row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row->row, THEME_SURFACE_GREEN, 0);
        lv_obj_set_style_border_width(row->row, 1, 0);
        lv_obj_set_style_border_side(row->row, LV_BORDER_SIDE_FULL, 0);
        lv_obj_set_style_border_color(row->row, THEME_BORDER_GREEN, 0);
    } else {
        lv_obj_set_style_bg_opa(row->row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row->row, 1, 0);
        lv_obj_set_style_border_side(row->row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row->row, THEME_DIVIDER, 0);
    }

    lv_obj_set_hidden(row->row, false);
}

void screen_wifi_set_networks(const char ssids[][SCREEN_WIFI_SSID_LEN],
                              const int8_t rssi[], int n,
                              const char saved[][SCREEN_WIFI_SSID_LEN], int n_saved)
{
    /* The screen may have been closed since whoever is calling last looked —
     * the scan task in particular finishes seconds later. */
    if (!s_alive) {
        return;
    }
    /* A NEGATIVE COUNT IS NOT AN EMPTY ONE. It used to be clamped to zero two
     * lines down, which turned "the radio could not look" into "there is
     * nothing out there" — a wrong answer delivered as a fact, to a man who
     * can see his own router from where he is sitting.
     *
     * Nothing about the list changes on a failure, because nothing was
     * learned: whatever he could already read stays readable, and only the
     * sentence under it says the scan did not work. */
    if (n < 0) {
        set_waiting(false);
        lv_label_set_text(s_lbl_empty, STR_WIFI_SCAN_FAILED);
        lv_obj_set_style_text_color(s_lbl_empty, THEME_AMBER, 0);
        lv_obj_set_hidden(s_lbl_empty, s_n_shown != 0);
        return;
    }
    if (n > SCREEN_WIFI_MAX_ROWS) {
        n = SCREEN_WIFI_MAX_ROWS; /* pool cap — see its #define */
    }
    if (n_saved < 0) {
        n_saved = 0;
    }

    for (int i = 0; i < n; i++) {
        bool is_saved = false;
        for (int j = 0; j < n_saved; j++) {
            if (saved[j][0] != '\0' && strncmp(ssids[i], saved[j], SCREEN_WIFI_SSID_LEN) == 0) {
                is_saved = true;
                break;
            }
        }
        update_row(i, ssids[i], is_saved, rssi ? rssi[i] : WIFI_RSSI_NONE);
    }
    for (int i = n; i < SCREEN_WIFI_MAX_ROWS; i++) {
        lv_obj_set_hidden(s_rows[i].row, true);
        s_rows[i].ssid[0] = '\0';
    }

    s_n_shown = n;

    /* A list has arrived, so whatever it says, the scan is over. This also
     * clears the ghost rows — which matters more than the bar does, because a
     * ghost row left standing under a real one is indistinguishable from a
     * network whose name failed to render. */
    set_waiting(false);

    /* AGENTS.md §1: never a blank panel. An empty scan result is not a
     * hypothetical here — see DESIGN.md §5.3's own empty-state precedent.
     * Text and colour are set rather than assumed: a previous scan may have
     * failed and left the amber sentence in this label. */
    lv_label_set_text(s_lbl_empty, STR_WIFI_LIST_EMPTY);
    lv_obj_set_style_text_color(s_lbl_empty, THEME_TEXT_LABEL, 0);
    lv_obj_set_hidden(s_lbl_empty, n != 0);
}

/* ============================================================================
 * Password step
 * ============================================================================
 */

static void update_toggle_label(void)
{
    bool hidden = lv_textarea_get_password_mode(s_pw_ta);
    lv_label_set_text(s_pw_toggle_lbl, hidden ? STR_WIFI_PW_SHOW : STR_WIFI_PW_HIDE);
}

static void pw_toggle_event_cb(lv_event_t *e)
{
    (void)e;
    bool hidden = lv_textarea_get_password_mode(s_pw_ta);
    lv_textarea_set_password_mode(s_pw_ta, !hidden);
    update_toggle_label();
}

static void open_password_step(const char *ssid)
{
    safe_copy_ssid(s_pw_ssid, ssid);

    char line[16 + SCREEN_WIFI_SSID_LEN];
    snprintf(line, sizeof line, FMT_WIFI_PW_NETWORK, s_pw_ssid);
    lv_label_set_text(s_pw_lbl_network, line);

    lv_textarea_set_text(s_pw_ta, "");
    lv_textarea_set_password_mode(s_pw_ta, true); /* always re-hide for a fresh network (task brief) */
    update_toggle_label();

    lv_keyboard_set_textarea(s_pw_kb, s_pw_ta);
    lv_keyboard_set_mode(s_pw_kb, LV_KEYBOARD_MODE_TEXT_LOWER);

    lv_obj_set_hidden(s_main, true);
    lv_obj_set_hidden(s_pw, false);
}

static void close_password_step(void)
{
    /* Never leave a typed password sitting in the widget after we're done
     * with it (AGENTS.md §10 in spirit — this isn't NVS or a log line, but
     * there's no reason for it to linger either). */
    lv_textarea_set_text(s_pw_ta, "");
    lv_obj_set_hidden(s_pw, true);
    lv_obj_set_hidden(s_main, false);
}

static void do_connect(void)
{
    const char *pass = lv_textarea_get_text(s_pw_ta); /* read once, before close_password_step() clears it */
    char ssid_copy[SCREEN_WIFI_SSID_LEN];
    safe_copy_ssid(ssid_copy, s_pw_ssid);

    /* Never stored or logged here — wifi_creds_set() is the integrator's
     * call, invoked from inside this callback (screen_wifi.h). */
    if (s_join_cb) {
        s_join_cb(ssid_copy, pass);
    }

    close_password_step();
    set_status_connecting(ssid_copy);
}

static void do_cancel(void)
{
    close_password_step();
}

static void pw_connect_clicked_cb(lv_event_t *e) { (void)e; do_connect(); }
static void pw_cancel_clicked_cb(lv_event_t *e)  { (void)e; do_cancel(); }

/* lv_keyboard's own "OK"/checkmark key sends LV_EVENT_READY to its assigned
 * text area, and — because s_pw_ta is one_line — so does its "Enter"/return
 * key (lv_keyboard.c). Wiring both of those to do_connect(), and the
 * keyboard's "close" glyph (LV_EVENT_CANCEL) to do_cancel(), means the
 * on-screen keyboard behaves the way every other on-screen keyboard he has
 * ever used behaves, on top of the explicit Verbinden/Abbrechen buttons the
 * task brief asks for. */
static void pw_ta_ready_cb(lv_event_t *e)  { (void)e; do_connect(); }
static void pw_ta_cancel_cb(lv_event_t *e) { (void)e; do_cancel(); }

/* ============================================================================
 * Suchen / exit
 * ============================================================================
 */
static void rescan_clicked_cb(lv_event_t *e)
{
    (void)e;
    /* Immediate local feedback so the tap never looks like it didn't
     * register while the integrator's scan (which this screen never runs
     * itself — it blocks for seconds) is still in flight. */
    screen_wifi_set_status(NULL, false, true);
    if (s_rescan_cb) {
        s_rescan_cb();
    }
}

static void exit_clicked_cb(lv_event_t *e)
{
    (void)e;
    if (s_exit_cb) {
        s_exit_cb();
    }
}

/* ============================================================================
 * Public API
 * ============================================================================
 */

void screen_wifi_create(lv_obj_t *parent)
{
    /* ---------- s_main ---------- */
    s_main = lv_obj_create(parent);
    lv_obj_add_event_cb(s_main, on_main_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_remove_style_all(s_main);
    lv_obj_set_size(s_main, THEME_SCREEN_WIDTH, THEME_SCREEN_HEIGHT);
    lv_obj_set_pos(s_main, 0, 0);
    lv_obj_set_style_bg_color(s_main, THEME_GROUND, 0);
    lv_obj_set_style_bg_opa(s_main, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_main, 0, 0);
    lv_obj_set_style_border_width(s_main, 0, 0);
    lv_obj_set_scrollable(s_main, false);

    s_lbl_title = make_label(s_main, &plex_sans_cond_34, THEME_TEXT_PRIMARY);
    lv_label_set_text(s_lbl_title, STR_WLAN);
    lv_obj_set_pos(s_lbl_title, PAD, PAD);
    lv_obj_update_layout(s_lbl_title);
    int32_t y = PAD + lv_obj_get_height(s_lbl_title) + GAP_SM;

    s_lbl_status = make_label(s_main, &plex_sans_cond_25, THEME_TEXT_LABEL);
    /* A FIXED ONE-LINE BOX, not just a fixed width.
     *
     * Every sentence here has a %s in it and an SSID runs to 32 characters,
     * so "Verbindung fehlgeschlagen: Apartamentos_Jose_Cruz" is far wider
     * than the 440 px column. Setting the WIDTH and asking for dots was not
     * enough and looked like it was: LVGL's DOT mode keeps the text inside
     * the object's SIZE, and the height was still LV_SIZE_CONTENT, so the
     * label simply grew a second line instead of ellipsising. The band below
     * is laid out once, from this label's height at build time, so the second
     * line appeared UNDERNEATH the network list — the failure message and the
     * card drawn on top of each other. Photographed on the panel; the code
     * reads as if it had been fixed.
     *
     * Pinning the height to one line is what makes DOT mode do its job, and
     * it also means nothing below this can ever move again, whatever is
     * written here later. */
    lv_obj_set_size(s_lbl_status, CONTENT_W, lv_font_get_line_height(&plex_sans_cond_25));
    lv_label_set_long_mode(s_lbl_status, LV_LABEL_LONG_MODE_DOTS);
    apply_status_text(STR_WIFI_IDLE, THEME_TEXT_LABEL); /* AGENTS.md §1: never blank, even before the first scan */
    lv_obj_set_pos(s_lbl_status, PAD, y);

    /* Inside the existing status-to-list gap, not below it, so the list keeps
     * every pixel it had — same placement and same reasoning as the Ortssuche
     * screen next door (screen_geo.c). */
    int32_t status_h = lv_font_get_line_height(&plex_sans_cond_25);
    s_busy = widget_busy_create(s_main, CONTENT_W);
    lv_obj_set_pos(s_busy, PAD, y + status_h + (GAP_MD - WIDGET_BUSY_H) / 2);

    y += status_h + GAP_MD;

    int32_t list_top = y;

    /* Bottom bar: Suchen + Zurück, side by side, both >=56 px tall. */
    int32_t btn_w = (CONTENT_W - GAP_MD) / 2;
    int32_t btn_y = THEME_SCREEN_HEIGHT - PAD - BTN_H;

    s_btn_rescan = make_button(s_main, btn_w, BTN_H, STR_WIFI_BTN_RESCAN,
                               THEME_SURFACE_SEL, THEME_BORDER_IDLE, THEME_TEXT_PRIMARY);
    lv_obj_set_pos(s_btn_rescan, PAD, btn_y);
    lv_obj_add_event_cb(s_btn_rescan, rescan_clicked_cb, LV_EVENT_CLICKED, NULL);

    s_btn_exit = make_button(s_main, btn_w, BTN_H, STR_BACK,
                             THEME_SURFACE_SEL, THEME_BORDER_IDLE, THEME_TEXT_PRIMARY);
    lv_obj_set_pos(s_btn_exit, PAD + btn_w + GAP_MD, btn_y);
    lv_obj_add_event_cb(s_btn_exit, exit_clicked_cb, LV_EVENT_CLICKED, NULL);

    int32_t list_bottom = btn_y - GAP_MD;

    /* Scrollable network list: vertical only, momentum on (task brief). */
    s_list = lv_obj_create(s_main);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_pos(s_list, PAD, list_top);
    lv_obj_set_size(s_list, CONTENT_W, list_bottom - list_top);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(s_list, GAP_SM, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(s_list, true);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scroll_momentum(s_list, true);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    /* A hidden pool row does not take flex layout space, so the first
     * visible row always lands at the top of the list — "first row fully
     * visible without scrolling" (task brief) needs no extra handling. */
    /* Ghost rows first: flex order is creation order, and a ghost below the
     * real list would promise a network that is not coming.
     *
     * THE SHAPE IS AN UNSAVED ROW'S, which is what almost every answer is
     * made of — update_row() gives a saved network a filled, fully bordered
     * card and an unsaved one a transparent row with a hairline under it, and
     * a scan he is waiting on is a scan for something new. The first version
     * drew a rounded box outlined on all four sides, which is neither: the
     * ghosts read as empty cards and then the answer arrived as hairline
     * rows, so the list visibly changed construction at the one moment he was
     * looking at it. Exactly what create_skeleton() in screen_geo.c takes
     * care to avoid, missed here because that screen's rows are all one
     * shape and this screen's are two. */
    for (int i = 0; i < WIFI_SKEL_ROWS; i++) {
        static const int32_t ssid_pct[WIFI_SKEL_ROWS] = { 58, 42, 66, 36 };

        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, CONTENT_W, ROW_H);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, THEME_DIVIDER, 0);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_hidden(row, true);

        int32_t gh = 16;
        widget_busy_ghost(row, ROW_INSET, (ROW_H - gh) / 2,
                          (CONTENT_W - 2 * ROW_INSET - SIG_SLOT) * ssid_pct[i] / 100,
                          gh, false);
        /* And a ghost where the meter will be, dimmer than the name's: the
         * ghosts exist to say where the answer will land, and a column that
         * appears out of nowhere once the scan returns is the same "the list
         * changed construction while he was looking at it" the row shape was
         * fixed for. */
        int32_t sig_w = WIDGET_SIGNAL_W(WIDGET_SIGNAL_ROW_BAR_W, WIDGET_SIGNAL_ROW_GAP);
        widget_busy_ghost(row, CONTENT_W - ROW_INSET - sig_w,
                          (ROW_H - gh) / 2, sig_w, gh, true);
        s_skel[i] = row;
    }

    s_lbl_empty = make_label(s_list, &plex_sans_cond_25, THEME_TEXT_LABEL);
    lv_label_set_text(s_lbl_empty, STR_WIFI_LIST_EMPTY);
    lv_obj_set_width(s_lbl_empty, CONTENT_W);

    for (int i = 0; i < SCREEN_WIFI_MAX_ROWS; i++) {
        create_row(s_list, i);
    }

    /* ---------- s_pw ---------- */
    s_pw = lv_obj_create(parent);
    lv_obj_remove_style_all(s_pw);
    lv_obj_set_size(s_pw, THEME_SCREEN_WIDTH, THEME_SCREEN_HEIGHT);
    lv_obj_set_pos(s_pw, 0, 0);
    lv_obj_set_style_bg_color(s_pw, THEME_GROUND, 0);
    lv_obj_set_style_bg_opa(s_pw, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_pw, 0, 0);
    lv_obj_set_style_border_width(s_pw, 0, 0);
    lv_obj_set_scrollable(s_pw, false);
    lv_obj_set_hidden(s_pw, true);

    /* Which network — wrapped rather than a fixed one-line height, since a
     * 32-char SSID plus the German prefix can run past 440 px at 25 px; the
     * band below is positioned from its REAL rendered height, the same
     * "measure, don't assume" approach screen_overhead.c uses for its hero. */
    s_pw_lbl_network = make_label(s_pw, &plex_sans_cond_25, THEME_TEXT_PRIMARY);
    lv_obj_set_width(s_pw_lbl_network, CONTENT_W);
    lv_label_set_long_mode(s_pw_lbl_network, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(s_pw_lbl_network, STR_WIFI_IDLE); /* placeholder for this layout pass; open_password_step() overwrites it per network */
    lv_obj_set_pos(s_pw_lbl_network, PAD, PAD);
    lv_obj_update_layout(s_pw_lbl_network);
    int32_t py = PAD + lv_obj_get_height(s_pw_lbl_network) + GAP_MD;

    /* Password field + show/hide toggle, side by side so neither has to
     * cover the other, both >=56 px tall. */
    int32_t toggle_w = PW_TOGGLE_W;
    int32_t ta_w     = CONTENT_W - toggle_w - GAP_MD;

    s_pw_ta = lv_textarea_create(s_pw);
    lv_obj_set_size(s_pw_ta, ta_w, BTN_H);
    lv_obj_set_pos(s_pw_ta, PAD, py);
    lv_textarea_set_one_line(s_pw_ta, true);
    lv_textarea_set_password_mode(s_pw_ta, true);
    lv_textarea_set_placeholder_text(s_pw_ta, STR_WIFI_PW_PLACEHOLDER);
    lv_textarea_set_max_length(s_pw_ta, PW_MAX_PASS_LEN);
    lv_obj_set_style_text_font(s_pw_ta, &plex_sans_cond_25, 0);
    widget_style_field(s_pw_ta);
    lv_obj_add_event_cb(s_pw_ta, pw_ta_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_pw_ta, pw_ta_cancel_cb, LV_EVENT_CANCEL, NULL);

    lv_obj_t *toggle_btn = make_button(s_pw, toggle_w, BTN_H, STR_WIFI_PW_SHOW,
                                       THEME_SURFACE_SEL, THEME_BORDER_IDLE, THEME_TEXT_PRIMARY);
    lv_obj_set_pos(toggle_btn, PAD + ta_w + GAP_MD, py);
    lv_obj_add_event_cb(toggle_btn, pw_toggle_event_cb, LV_EVENT_CLICKED, NULL);
    s_pw_toggle_lbl = lv_obj_get_child(toggle_btn, 0);

    py += BTN_H + GAP_MD;

    /* Verbinden / Abbrechen. Verbinden reuses the surface-green/border-green
     * pairing theme.h already names for "the good path" (§5.7's saved-card
     * tokens); Abbrechen stays neutral. */
    int32_t pw_btn_w = (CONTENT_W - GAP_MD) / 2;

    lv_obj_t *btn_connect = make_button(s_pw, pw_btn_w, BTN_H, STR_WIFI_PW_CONNECT,
                                        THEME_SURFACE_GREEN, THEME_BORDER_GREEN, THEME_TEXT_PRIMARY);
    lv_obj_set_pos(btn_connect, PAD, py);
    lv_obj_add_event_cb(btn_connect, pw_connect_clicked_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_cancel = make_button(s_pw, pw_btn_w, BTN_H, STR_WIFI_PW_CANCEL,
                                       THEME_SURFACE_SEL, THEME_BORDER_IDLE, THEME_TEXT_PRIMARY);
    lv_obj_set_pos(btn_cancel, PAD + pw_btn_w + GAP_MD, py);
    lv_obj_add_event_cb(btn_cancel, pw_cancel_clicked_cb, LV_EVENT_CLICKED, NULL);

    py += BTN_H + GAP_MD;

    /* Keyboard: full screen width (0, not PAD) for the largest possible
     * touch targets, filling everything below the buttons so it never has
     * to overlap the text area above it (task brief).
     *
     * THIS KEYBOARD WAS OFF-SCREEN FROM M6 UNTIL 2026-09-20. The line below
     * used to be lv_obj_set_pos(s_pw_kb, 0, py), which every other widget on
     * this screen is positioned with and which is wrong for exactly one
     * widget class: lv_keyboard's constructor aligns itself BOTTOM_MID
     * (lv_keyboard.c), and in LVGL 9 x/y are an offset FROM the alignment
     * once one is set — so this asked for a keyboard `py` pixels below the
     * bottom edge of the panel. The password step rendered perfectly, with
     * no keyboard on it and no way to type a password into it, and nothing
     * in the code or the logs said so.
     *
     * It survived because the password step cannot be reached from the build
     * host: it needs a finger on an unknown network, so every check of this
     * screen in this repo stopped at the network list. Found while building
     * the Ortssuche keyboard next door, which hit the same wall and was
     * caught by tools/grab_screen.py (D4, D41). AGENTS.md §11's cousin: code
     * that reads correctly and was never once executed on the glass.
     *
     * The styling is shared with that screen rather than copied — see
     * widget_input.h. */
    s_pw_kb = lv_keyboard_create(s_pw);
    lv_obj_set_size(s_pw_kb, THEME_SCREEN_WIDTH, THEME_SCREEN_HEIGHT - py);
    lv_obj_align(s_pw_kb, LV_ALIGN_TOP_LEFT, 0, py);
    widget_style_keyboard(s_pw_kb);
    lv_keyboard_set_mode(s_pw_kb, LV_KEYBOARD_MODE_TEXT_LOWER);

    /* Last, deliberately: until every widget exists there is nothing safe
     * for the scan task to write into. */
    s_alive = true;
}

void screen_wifi_set_join_cb(wifi_join_cb cb)
{
    s_join_cb = cb;
}

void screen_wifi_set_rescan_cb(wifi_rescan_cb cb)
{
    s_rescan_cb = cb;
}

void screen_wifi_set_exit_cb(wifi_exit_cb cb)
{
    s_exit_cb = cb;
}

bool screen_wifi_debug_tap_saved(void)
{
    if (!s_alive) {
        return false;
    }
    for (int i = 0; i < SCREEN_WIFI_MAX_ROWS; i++) {
        if (s_rows[i].ssid[0] == '\0' || !s_rows[i].saved) {
            continue;
        }
        lv_obj_send_event(s_rows[i].row, LV_EVENT_CLICKED, NULL);
        return true;
    }
    return false;
}

int screen_wifi_debug_password_step(void)
{
    if (!s_alive) {
        return SCREEN_WIFI_PW_NONE;
    }
    for (int i = 0; i < SCREEN_WIFI_MAX_ROWS; i++) {
        if (s_rows[i].ssid[0] == '\0' || s_rows[i].saved) {
            continue;
        }
        /* Through the row's own click event rather than by calling
         * open_password_step() directly: what is being checked is the path a
         * finger takes, and a direct call would skip the event dispatch that
         * the path actually runs through. */
        lv_obj_send_event(s_rows[i].row, LV_EVENT_CLICKED, NULL);
        return SCREEN_WIFI_PW_TAPPED;
    }
    /* Nothing unsaved in range. That is a property of where the device is
     * standing, not of the code — and it is exactly the circumstance that
     * kept this screen unverified for four milestones, so it gets a way
     * through rather than a shrug. Open the step on the first row there,
     * WITHOUT clicking it. */
    for (int i = 0; i < SCREEN_WIFI_MAX_ROWS; i++) {
        if (s_rows[i].ssid[0] != '\0') {
            open_password_step(s_rows[i].ssid);
            return SCREEN_WIFI_PW_FORCED;
        }
    }
    return SCREEN_WIFI_PW_NONE;
}
