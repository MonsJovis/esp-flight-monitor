/* sim_list.c — the Liste page (screen_list.c) on the host (D82).
 *
 * The same harness as sim_radar.c (sim_common.h): real LVGL in the device's
 * DIRECT mode, the real screen, a 480x480 framebuffer, checks from pixels.
 *
 * What it guards: an EMPTY list is three different answers, and before D82
 * it gave one of them for all three. At boot and right after the device is
 * moved there is no answer yet for this place, and the list said "Der Himmel
 * ist frei." — the one false sentence on the screen, and after a move it was
 * preceded by a poll's worth of the OLD place's aircraft. Now:
 *   no answer, network up    -> "Suche Flugzeuge..." + the bar + ghost rows
 *   no answer, network down  -> "Noch keine Flugdaten.", nothing moving
 *   an answer, and it is 0   -> "Der Himmel ist frei."
 * The wait never replaces rows he can read.
 */
#include <string.h>

#include "lvgl.h"
#include "screen_list.h"
#include "theme.h"
#include "flight_types.h"
#include "sim_common.h"

static aircraft_t mk(const char *hex, const char *flight, const char *type, float dst, float dir)
{
    aircraft_t a;
    memset(&a, 0, sizeof a);
    snprintf(a.hex, sizeof a.hex, "%s", hex);
    snprintf(a.flight, sizeof a.flight, "%s", flight);
    snprintf(a.type, sizeof a.type, "%s", type);
    a.dst_nm = dst;
    a.dir_deg = dir;
    a.alt_ft = 9000;
    a.gs_kt = 300;
    return a;
}

/* The bar is the only cyan on an empty list; ghost rows are the only
 * border-idle fill in the rows' band. */
static int cyan_top(void)   { return find(THEME_CYAN, 0, 30, W - 1, 60).n; }
static int ghosts(void)     { return find(THEME_BORDER_IDLE, 0, 60, W - 1, 380).n; }
static int sentence(void)   { return find(THEME_TEXT_PRIMARY, 0, 180, W - 1, 300).n; }

static void show(const aircraft_t *ac, int n, bool has_data, net_state_t net, const char *png)
{
    screen_list_set_source(has_data, net);
    screen_list_update(ac, n, NULL, 0);
    run_ms(400);
    render();
    png_write(png);
}

int main(int argc, char **argv)
{
    if (argc > 1) s_outdir = argv[1];

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(W, H);
    lv_display_set_buffers(d, s_fb, NULL, sizeof s_fb, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flush_cb);
    lv_obj_set_style_bg_color(lv_screen_active(), THEME_GROUND, 0);
    screen_list_create(lv_screen_active());

    aircraft_t sky[3] = {
        mk("440823", "AUA110", "E195", 9.6f, 120.0f),
        mk("4ca7b5", "RYR12AB", "B738", 14.2f, 250.0f),
        mk("440123", "", "C172", 18.0f, 10.0f),
    };

    GROUP("no answer yet, network up: the wait, with ghosts where the rows go");
    {
        show(NULL, 0, false, NET_OK, "list_01_waiting");
        CHECK(cyan_top() > 0, "no bar under the header");
        CHECK(ghosts() > 200, "no ghost rows: %d px", ghosts());
        CHECK(sentence() == 0, "a centred sentence under the ghosts: %d px", sentence());
        CHECK(find(THEME_TEXT_LABEL, 0, 0, W - 1, 30).n > 20, "no \"Suche Flugzeuge...\" header");
    }

    GROUP("no answer yet, network down: one sentence, nothing moving");
    {
        show(NULL, 0, false, NET_NO_WIFI, "list_02_no_answer");
        CHECK(cyan_top() == 0, "a bar with nothing in flight");
        CHECK(ghosts() == 0, "ghost rows with nothing in flight: %d px", ghosts());
        CHECK(sentence() > 50, "no sentence: %d px", sentence());
    }

    GROUP("an answer, and it is empty: the empty sky, and the wait is gone");
    {
        int no_answer = sentence();
        show(NULL, 0, true, NET_OK, "list_03_empty_sky");
        CHECK(cyan_top() == 0, "the bar stayed");
        CHECK(ghosts() == 0, "the ghosts stayed: %d px", ghosts());
        CHECK(sentence() > 50 && sentence() != no_answer,
              "empty-sky sentence %d px vs no-answer %d px — expected a different one",
              sentence(), no_answer);
    }

    GROUP("rows he can read: no bar, no ghosts, whatever has_data says");
    {
        show(sky, 3, true, NET_OK, "list_04_rows");
        CHECK(cyan_top() == 0, "a bar over real rows");
        CHECK(ghosts() == 0, "ghost fill over real rows: %d px", ghosts());
        CHECK(find(THEME_TEXT_PRIMARY, 0, 60, W - 1, 380).n > 200, "no rows drawn");
    }

    printf("\n  %d checks, %d failed\n", t_run, t_fail);
    return t_fail ? 1 : 0;
}
