/* M1 bring-up: the panel, the touch controller, the memory budget and the font
 * gate. The numbers this logs go into the table at the bottom of docs/PLAN.md.
 *
 * Debug console (over USB serial):
 *   s  screenshot the live framebuffer    0  give the screen back to the UI task
 *   g  swipe to the next deck page        e  open Einstellungen
 *   k  open WLAN                          o  cycle the location preset
 *   d  scroll the current screen to its end
 *   i  open/close the detail layer   v  LVGL heap
 *
 * 'i' exists for the same reason g/e/k do (D41): the detail layer is only
 * reachable by tapping an aircraft, and a screen that can only be reached
 * by touching the glass is a screen nobody checks.
 *   f  draw the font card — the M1 "German renders at 100 px" gate
 *   b  run the render benchmark suite     t  tearing bench
 *   m  measure every place name against the hero shrink ladder
 *   w  provision WiFi (typed in over serial, stored in NVS — never in the repo)
 *   n  network status and a scan of what is in range
 *   p  probe the link (DNS, then a raw GET by IP)
 *   y  battery: the judged status and the PMIC registers under it
 *   x  what the touch layer has seen (presses, long presses, the last hold)
 *   Y  cycle a PRETENDED battery (60 %, 18 %, 5 %, off) so the badge,
 *      the amber caution and the backlight cap can be seen without one
 *   1  replay §5.1 from the real capture   2  §5.2 Ohne Route
 *   3  §5.3 Himmel frei                    4  longest destination (shrink ladder)
 *
 * g/e/k exist so every screen can be reached from the build host and read
 * back as a PNG (tools/grab_screen.py). A screen that can only be reached by
 * tapping is a screen nobody checks.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "ui/display.h"
#include "ui/theme.h"
#include "ui/fonts/fonts.h"
#include "debug/dbg_screen.h"
#include "debug/dbg_bench.h"
#include "debug/dbg_fontcard.h"
#include "debug/dbg_metrics.h"
#include "nvs_flash.h"
#include "net/wifi.h"
#include "net/flight_source.h"
#include "net/timesync.h"
#include "net/ota.h"
#include "net/adsb_parse.h"
#include "data/extrapolate.h"
#include "data/settings.h"
#include "net/http_get.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "ui/screen_overhead.h"
#include "ui/nav.h"
#include "ui/screen_settings.h"
#include "ui/screen_wifi.h"
#include "ui/screen_geo.h"
#include "net/geocode.h"
#include "ui/screen_list.h"
#include "ui/screen_radar.h"
#include "data/view_build.h"
#include "net/route_parse.h"
#include "power/axp2101.h"
#include "power/battery_policy.h"
#include "debug/dbg_fixture.h"
#include <time.h>

static const char *TAG = "flight";

/* Set while a debug command owns the screen.
 *
 * It is not merely "do not overwrite my screen": every debug view calls
 * lv_obj_clean(), which DELETES screen_overhead's labels. If the UI task then
 * runs screen_overhead_update() it writes through dangling pointers and the
 * device panics with LoadProhibited. The fixture commands happened to set this
 * flag; bench, fontcard and the tearing test did not, so each of them was a
 * crash waiting for the next 2 s tick. Resuming rebuilds the widget tree. */
static volatile bool s_ui_suspended = false;

/* Tapping a row in the list means "tell me about THAT one", and the screen that
 * answers that question is §5.1 — so the selection becomes the subject of the
 * hero screen and the deck slides back to it. No separate detail card: a fourth
 * layout to learn, for information the first page already shows, is exactly the
 * kind of thing this user does not need. */
/* Deck page order, and the state of the layer beneath it. Up here because
 * ui_task reads them long before the overlay code that owns them. */
#define PAGE_RADAR 0
#define PAGE_LISTE 1

static bool s_detail_open;
static int  s_detail_from = PAGE_RADAR;

static void open_detail(void);   /* defined with the overlays, below */
static void close_detail(void);

static aircraft_t s_selected;
static bool       s_has_selection;

static void on_list_select(const aircraft_t *ac)
{
    if (ac == NULL) return;
    s_selected = *ac;
    s_has_selection = true;
    open_detail();
}

/* The radar hands over an ICAO hex rather than an aircraft_t, because by the
 * time this runs the array has usually been re-sorted — every fix is carried
 * forward between polls, which can change who is nearest. The hex is the only
 * identifier that survives that.
 *
 * Nothing needs looking up here: ui_task matches the selection by hex on its
 * next tick and falls back to the nearest if it has gone. Setting the hex IS
 * the selection. */
static void on_radar_select(const char *hex)
{
    if (hex == NULL || hex[0] == '\0') return;
    memset(&s_selected, 0, sizeof s_selected);
    snprintf(s_selected.hex, sizeof s_selected.hex, "%s", hex);
    s_has_selection = true;
    open_detail();
}


static void log_memory_budget(const char *when)
{
    ESP_LOGW(TAG, "MEM %-36s internal %7u (blk %6u)  psram %8u (blk %8u)",
             when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}


static void bench_suite(void)
{
    ESP_LOGW(TAG, "=== render benchmark: full-screen invalidate every frame ===");
    dbg_bench_run(&lv_font_montserrat_48, "montserrat_48 (flash)", "Muenchen", 8);
    dbg_bench_run(&plex_sans_cond_56,     "plex_cond_56  (psram)", "München",  8);
    dbg_bench_run(&plex_sans_cond_100,    "plex_cond_100 (psram)", "München",  8);
    ESP_LOGW(TAG, "=== benchmark done ===");
    dbg_font_card();
}

/* Where the device thinks it is, and how it should look. Loaded from NVS at
 * boot; the coordinates, the timezone and the backlight all follow from it,
 * because he must never have to set a clock or type a coordinate. */
static settings_t g_settings;

/* ---- battery ------------------------------------------------------------
 *
 * The cell is optional and always will be: this device spends 23-and-a-half
 * hours of every day on USB, and the half hour it does not is the whole
 * feature. Everything below therefore has to be a no-op on a device with no
 * battery in it, which is every device until one is fitted.
 *
 * NOT ZERO-INITIALISED, and that is not style. A zeroed battery_status_t has
 * brightness_cap_pct == 0, and apply_brightness() takes the MINIMUM of the
 * schedule and the cap — so a plain `static battery_status_t s_battery;`
 * turns the backlight off at the first tick of a device that has no battery
 * at all. */
static battery_status_t s_battery = {
    .state              = BAT_ABSENT,
    .percent            = -1,
    .brightness_cap_pct = BAT_CAP_NORMAL_PCT,
};

/* Every reason the panel has to change brightness, in one place: the night
 * schedule he set, and the ceiling a low battery imposes on it. Both are
 * recomputed from scratch on every call, so whoever calls it last is right
 * and nothing has to remember to undo anything.
 *
 * The memo is the brightness itself rather than the hour, because the hour
 * is no longer the only input. Re-setting the LEDC duty every two seconds
 * would be pointless traffic; re-setting it when the answer changes is the
 * whole job. */
static int s_brightness_applied = -1;

static void apply_brightness(void)
{
    time_t raw = time(NULL);
    struct tm lt;
    localtime_r(&raw, &lt);

    int want = settings_brightness_for_hour(&g_settings, lt.tm_hour);
    if (s_battery.brightness_cap_pct < want) {
        want = s_battery.brightness_cap_pct;
    }
    if (want == s_brightness_applied) {
        return;
    }
    s_brightness_applied = want;
    bsp_display_brightness_set(want);
}

/* What the panel is currently showing about the battery, so that a poll which
 * changes nothing costs no LVGL work at all. File scope rather than function
 * statics because ui_resume() has to be able to forget it: it rebuilds the
 * whole deck, which destroys the badge, and a memo that still believes the
 * badge is on screen would leave him with no badge at all. */
static char s_badge_shown[24];
static char s_line_shown[64];
static bool s_badge_caution;

static void power_forget_ui(void)
{
    s_badge_shown[0] = '\0';
    s_line_shown[0]  = '\0';
    s_badge_caution  = false;
}

/* A pretended battery, for the build host.
 *
 * The badge, the amber caution and the backlight cap can otherwise only be
 * seen by flattening a real cell, which takes hours and cannot be done
 * before one exists. That is the same argument D41 makes for 'g', 'e' and
 * 'k': a screen that can only be reached by an hours-long physical event is
 * a screen nobody checks, and this one is a warning — the single element on
 * the device that has to be right the first time it ever appears.
 *
 * -1 is off, and off is the only state a real device is ever in: nothing
 * persists it, nothing sets it but the 'Y' key on the serial console, and a
 * reboot clears it. */
static int s_battery_sim = -1;

static void battery_sim_cycle(void)
{
    static const int k_steps[] = { 60, 18, 5, -1 };
    static int next;
    s_battery_sim = k_steps[next];
    next = (next + 1) % (int)(sizeof k_steps / sizeof k_steps[0]);
    printf("\nsimulated battery: %s\n",
           s_battery_sim < 0 ? "off (real PMIC again)" : "on");
    if (s_battery_sim >= 0) {
        printf("  pretending: on battery, %d %%\n", s_battery_sim);
    }
}

/* One PMIC poll and everything that follows from it.
 *
 * Every early return leaves s_battery exactly as it was, which is the right
 * answer for a transient I2C fault: the bus is shared with the touch
 * controller, esp_lvgl_port_touch.c panics the device on a single fault of
 * its own (D47), and a battery percentage is not worth adding to that. */
static void power_tick(void)
{
    battery_raw_t raw;
    if (s_battery_sim >= 0) {
        /* Everything downstream of here is the real code path — the same
         * eval, the same German, the same badge, the same backlight cap. */
        raw = (battery_raw_t){
            .present    = true,
            .vbus_good  = false,
            .chg_status = BAT_CHG_STOP,
            .mv         = 3700,
            .gauge_pct  = s_battery_sim,
        };
    } else if (!axp2101_present()) {
        /* No PMIC, and there never will be one on this boot — so this is not
         * the transient case the early return below is for. It has to go
         * through the same path as everything else, because it is what takes
         * the simulated badge and the simulated backlight cap back off again
         * when 'Y' cycles round to "off" on exactly the bench device the
         * simulation exists for. */
        raw = (battery_raw_t){ .present = false };
    } else if (axp2101_read(&raw) != ESP_OK) {
        return;
    }

    battery_status_t st;
    battery_eval(&raw, &s_battery, &st);
    s_battery = st;

    char badge[sizeof s_badge_shown];
    char line[sizeof s_line_shown];
    battery_badge_text(&s_battery, badge, sizeof badge);
    battery_line_text(&s_battery, line, sizeof line);

    bool badge_changed = strcmp(badge, s_badge_shown) != 0 ||
                         s_battery.low != s_badge_caution;
    bool line_changed  = strcmp(line, s_line_shown) != 0;

    /* Re-checked here, not only by the caller, and re-checked INSIDE the
     * lock rather than before it.
     *
     * ui_suspend() sets the flag and waits 120 ms for whatever the UI task is
     * doing to finish. The PMIC read above can outlast that on a sick bus —
     * 200 ms for the driver mutex plus four transactions at a 100 ms timeout
     * each — so by the time this code runs a debug view may already have
     * called lv_obj_clean() and freed the badge nav.c points at.
     *
     * Testing the flag before taking the lock narrows that window but does
     * not close it: the test could pass, the debug view could then take the
     * lock and clean the screen, and this task would block, acquire the lock
     * afterwards and write into freed heap anyway. Every lv_obj_clean() in
     * main/debug/ happens under this same lock, so asking the question after
     * acquiring it is the version that cannot lose the race.
     *
     * The reading itself is kept either way; only the widgets are skipped,
     * and the memo is left stale on purpose so the next poll repaints —
     * ui_resume() clears it as well. */
    if (badge_changed || line_changed) {
        display_lock(0);
        bool painted = !s_ui_suspended;
        if (painted) {
            if (badge_changed) {
                nav_set_badge(badge, s_battery.low);
            }
            if (line_changed) {
                screen_settings_set_battery(line);
            }
        }
        display_unlock();

        if (painted) {
            snprintf(s_badge_shown, sizeof s_badge_shown, "%s", badge);
            snprintf(s_line_shown,  sizeof s_line_shown,  "%s", line);
            s_badge_caution = s_battery.low;
        }
    }

    /* The discharge curve, for free, the first time he ever unplugs it.
     *
     * AGENTS.md §2 carries a CALCULATED figure for how long this device runs
     * on a cell — 1.2 to 1.9 W, so four hours from 2000 mAh — and this
     * project does not leave calculated numbers standing when the hardware
     * can be asked. One line a minute while discharging is the measurement,
     * and it costs nothing on a device that is plugged in. */
    static int64_t last_log_ms;
    int64_t now = esp_timer_get_time() / 1000;
    if (s_battery.state != BAT_ON_BATTERY) {
        last_log_ms = 0;
    } else if (last_log_ms == 0 || now - last_log_ms >= 60000) {
        last_log_ms = now;
        ESP_LOGW(TAG, "battery: %d %% %d mV on battery, backlight capped at %d %%",
                 s_battery.percent, s_battery.mv, s_battery.brightness_cap_pct);
    }
}

/* Applies the whole of g_settings to the running system: the poll location, the
 * timezone that follows it, and the backlight. Safe to call whenever something
 * changed. */
static void apply_settings(void)
{
    double lat, lon;
    settings_coords(&g_settings, &lat, &lon);
    flight_source_set_location(lat, lon, g_settings.radius_nm);
    /* settings_tz(), not location_tz(): a place he found with the search
     * carries its own POSIX rule, and AGENTS.md §6 binds the clock to the
     * location rather than making him set one. */
    timesync_set_tz(settings_tz(&g_settings));

    apply_brightness();
    /* The OTA task installs only inside the night window, so it needs to hear
     * about a change to that window at the same moment the dimmer does. */
    ota_settings_update(&g_settings);

    ESP_LOGW(TAG, "settings applied: %s (%.4f/%.4f) r=%d nm tz=%s",
             location_name(g_settings.preset), lat, lon,
             g_settings.radius_nm, settings_tz(&g_settings));
}

/* Credentials are typed in here and stored in NVS. They must never appear in a
 * source file: AGENTS.md §10, and "temporarily hard-coded" is exactly how they
 * end up committed. This is also the seed of the M6 provisioning screen, which
 * reads from the same place. */
static void provision_wifi(void)
{
    char line[160];
    printf("\nSSID then TAB then password, one line, ENTER to finish:\n> ");
    fflush(stdout);
    if (dbg_read_line(line, sizeof line, 120000) < 0) {
        ESP_LOGW(TAG, "provisioning timed out — nothing stored");
        return;
    }
    char *tab = strchr(line, '\t');
    if (tab == NULL) {
        ESP_LOGE(TAG, "expected SSID<TAB>password; nothing stored");
        return;
    }
    *tab = '\0';
    const char *ssid = line, *pass = tab + 1;

    /* Pick the slot rather than always writing 0. The device is meant to
     * remember several networks and join whichever is in range (AGENTS.md §6) —
     * it spends half the year in Thailand — so provisioning a new one must not
     * quietly destroy the one that gets him home. Same SSID overwrites itself;
     * otherwise take the first free slot. */
    char known[WIFI_MAX_NETWORKS][WIFI_SSID_LEN];
    int slot = -1;
    if (wifi_creds_list(known, WIFI_MAX_NETWORKS) == ESP_OK) {
        for (int i = 0; i < WIFI_MAX_NETWORKS; i++) {
            if (strcmp(known[i], ssid) == 0) { slot = i; break; }
        }
        if (slot < 0) {
            for (int i = 0; i < WIFI_MAX_NETWORKS; i++) {
                if (known[i][0] == '\0') { slot = i; break; }
            }
        }
    }
    if (slot < 0) {
        slot = WIFI_MAX_NETWORKS - 1;   /* all full: replace the last */
        ESP_LOGW(TAG, "all %d slots full — replacing slot %d",
                 WIFI_MAX_NETWORKS, slot);
    }

    if (wifi_creds_set(slot, ssid, pass) == ESP_OK) {
        /* SSID only. Never log the password. */
        /* The reconnect loop may be deep in a backoff, so nudge it instead of
         * making him wait up to a minute after typing his password. */
        ESP_LOGW(TAG, "stored \"%s\" in slot %d; reconnecting now", ssid, slot);
        wifi_reconnect_now();
    } else {
        ESP_LOGE(TAG, "could not store credentials");
    }
}

static void network_status(void)
{
    char ssids[8][WIFI_SSID_LEN];
    /* The "never succeeded" sentinel is INT64_MAX; printing it raw gives
     * "last_ok=9223372036854775807 ms ago", which reads as a bug. */
    int64_t age = flight_source_last_success_age_ms();
    char age_str[32];
    if (age == INT64_MAX) {
        snprintf(age_str, sizeof age_str, "never");
    } else {
        snprintf(age_str, sizeof age_str, "%lld ms ago", (long long)age);
    }
    ESP_LOGW(TAG, "connected=%d  source=%s  stale=%d  failures=%d  last_ok=%s",
             wifi_is_connected(), flight_source_current_source_name(),
             flight_source_is_stale(), flight_source_consecutive_failures(),
             age_str);

    if (wifi_creds_list(ssids, 8) == ESP_OK) {
        for (int i = 0; i < 8; i++) {
            if (ssids[i][0]) ESP_LOGW(TAG, "  stored slot %d: %s", i, ssids[i]);
        }
    }
    int n = wifi_scan(ssids, 8);
    for (int i = 0; i < n; i++) {
        ESP_LOGW(TAG, "  in range: %s", ssids[i]);
    }

    /* Which DNS servers did DHCP actually give us? A poll that dies in
     * getaddrinfo() looks identical to one that dies in connect(), and the
     * difference decides whether the problem is name resolution or routing. */
    /* Signal strength, because intermittent connect() timeouts on a link that
     * reports "connected" are usually a radio problem rather than a routing one.
     * Rule of thumb: > -60 dBm is comfortable, < -75 dBm is where TCP starts
     * failing in ways that look like an unreachable host. */
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        ESP_LOGW(TAG, "  ap: %s  rssi=%d dBm  ch=%d", (char *)ap.ssid, ap.rssi, ap.primary);
    }

    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    for (int i = 0; i < 2 && sta; i++) {
        esp_netif_dns_info_t dns;
        if (esp_netif_get_dns_info(sta, i, &dns) == ESP_OK) {
            ESP_LOGW(TAG, "  dns%d: " IPSTR, i, IP2STR(&dns.ip.u_addr.ip4));
        }
    }

    /* Resolve, then connect by raw IP. If the name fails but the IP works, it
     * is DNS; if both fail it is routing. */
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res = NULL;
    int rc = getaddrinfo("api.adsb.lol", "80", &hints, &res);
    if (rc != 0 || res == NULL) {
        ESP_LOGE(TAG, "  getaddrinfo(api.adsb.lol) failed: %d", rc);
    } else {
        struct in_addr a = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
        ESP_LOGW(TAG, "  api.adsb.lol -> " IPSTR, IP2STR((esp_ip4_addr_t *)&a));
        freeaddrinfo(res);
    }

    char buf[512];
    int status = 0;
    bool trunc = false;
    int got = http_get("http://89.58.11.153/v2/point/47.6691/15.9303/30",
                       buf, sizeof buf, 8000, &status, &trunc);
    ESP_LOGW(TAG, "  raw-IP GET: bytes=%d http=%d (bypasses DNS entirely)", got, status);
}

/* Reads the poller's snapshot and repaints. Owns no network state and does no
 * formatting — view_build turns raw API data into final German, this just moves
 * it onto the panel. All LVGL work happens here, behind the display lock. */
static void ui_task(void *arg)
{
    /* The heartbeat for the whole device. If rendering, the display mutex or the
     * snapshot copy ever wedges, this stops feeding and the watchdog reboots us
     * — which is strictly better than a panel frozen on a stale aircraft in a
     * room where nobody can tell the difference. */
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    static aircraft_t ac[MAX_AIRCRAFT];
    static route_t    rt[MAX_AIRCRAFT];
    static aircraft_t last_seen;
    static bool       have_last_seen = false;

    bool sntp_started = false;
    int  power_ticks  = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_task_wdt_reset();

        /* Start SNTP the first time we actually have a network, whenever that
         * happens — at boot on a known network, or minutes later when someone
         * types a password in a holiday apartment. */
        /* Auto-dim. DESIGN.md §7: a glowing dark panel in a dim living room at
         * 22:00 is glare, and the clock is already right, so a schedule is
         * enough. Cheap to call every tick — it only touches the backlight
         * when the answer it computes actually changes, and since the battery
         * cap joined the night schedule as an input, "the hour changed" is no
         * longer the only reason that answer moves. */
        apply_brightness();

        if (!sntp_started && wifi_is_connected()) {
            if (timesync_start(settings_tz(&g_settings)) == ESP_OK) {
                sntp_started = true;
            }
        }
        if (s_ui_suspended) {
            continue;   /* a replayed screen stays up until dismissed */
        }

        /* The PMIC, every fifth tick. Ten seconds is already faster than
         * anything about a battery changes, and each poll is four register
         * reads on the I2C bus the touch controller is also using.
         *
         * BELOW the suspend check, not above it, and that is the difference
         * between a feature and a panic: every debug view calls
         * lv_obj_clean(), which deletes the badge nav.c holds a pointer to.
         * A poll that ran while a fixture was on screen would write through
         * it. apply_brightness() above is deliberately on the other side of
         * the check — it touches the backlight, never a widget, and the
         * night schedule has no business stopping because someone pressed
         * '1'. */
        if (++power_ticks >= 5) {
            power_ticks = 0;
            power_tick();
        }

        int n = flight_source_snapshot(ac, MAX_AIRCRAFT, rt, MAX_AIRCRAFT);

        /* Carry every fix forward to NOW before anything draws it.
         *
         * The source is polled every 12 s, so without this the marks sit
         * perfectly still and then jump — and the jump is small enough to
         * miss: 7 px for an airliner on a 55 km scope, 1.6 px for a Cessna.
         * The panel read as frozen, which is exactly how it was reported.
         * Done here, once, rather than in each screen, so the radar, the list
         * and the hero cannot disagree about where an aircraft is.
         *
         * Re-sorting afterwards is not optional: the ordering (nearest first)
         * is structural — screen_list.c treats row 0 as "the nearest" and
         * main.c reads ac[0] as "the plane overhead" — and moving everything
         * is exactly what can change who is nearest. */
        int64_t last_ok_ms = flight_source_last_success_ms();
        if (last_ok_ms > 0 && n > 1) {
            float age_s = (float)((esp_timer_get_time() / 1000) - last_ok_ms) / 1000.0f;
            for (int i = 0; i < n; i++) {
                aircraft_extrapolate(&ac[i], age_s, &ac[i]);
            }
            adsb_sort_by_distance(ac, n);
        }
        time_t raw = time(NULL);
        struct tm now;
        localtime_r(&raw, &now);

        /* What the amber "KEIN NETZ" caution actually means.
         *
         * It used to be !flight_source_is_stale(), which is true after a SINGLE
         * failed poll — so one dropped request on a weak link told him the
         * network was down while it was demonstrably fine, and the only thing he
         * can do about "KEIN NETZ" is go and look at the router. A caution he
         * cannot act on correctly is worse than none.
         *
         * No WiFi is the case he can actually fix, so that shows immediately.
         * With WiFi up, tolerate a couple of lost polls before crying wolf —
         * a weak link drops one now and then and the screen keeps showing the
         * last aircraft, which is the designed behaviour anyway.
         *
         * Split into two labels in M8. They are different problems with
         * different fixes — one he can walk over and solve, one he cannot —
         * and sharing a label sent him to check a router that was working. */
        net_state_t net;
        if (!wifi_is_connected()) {
            net = NET_NO_WIFI;
        } else if (flight_source_consecutive_failures() >= 3) {
            net = NET_NO_DATA;
        } else {
            net = NET_OK;
        }

        /* Leaving the radar drops whatever mark he had tapped there. A
         * selection made five minutes ago is not what he means by a glance,
         * and the caption going back to the nearest aircraft is the same rule
         * the deck already follows when it auto-returns from an empty sky. */
        {
            static int prev_page = -1;
            int page_now = nav_page();
            if (page_now != prev_page) {
                if (prev_page == 2) {
                    screen_radar_clear_selection();
                }
                prev_page = page_now;
            }
        }

        /* A row he tapped stays the subject while it is still up there. Once it
         * leaves the ring the device goes back to answering "what is overhead
         * now", which is the question the screen is for. */
        int subject = 0;
        if (s_has_selection) {
            subject = -1;
            for (int i = 0; i < n; i++) {
                if (strcmp(ac[i].hex, s_selected.hex) == 0) { subject = i; break; }
            }
            if (subject < 0) {
                s_has_selection = false;
                subject = 0;
                /* It left the ring while he was reading about it. Closing the
                 * layer is the honest move: silently swapping in a different
                 * aircraft under the same heading is how a panel teaches him
                 * not to trust it. */
                if (s_detail_open) {
                    close_detail();
                }
            }
        }

        view_model_t vm;
        if (n > 0) {
            last_seen = ac[subject];
            have_last_seen = true;
            /* "Still looking" and "has no flight plan" are different answers
             * and must not share a screen. flight_source knows which it is. */
            bool searching =
                flight_source_route_status(ac[subject].flight) == ROUTE_STATUS_RESOLVING;
            view_build_ex(&ac[subject], route_find(rt, n, ac[subject].flight),
                          searching, &now, n, net, &vm);
        } else {
            view_build_empty(&now, have_last_seen ? &last_seen : NULL,
                             net, &vm);
        }

        display_lock(0);
        /* Only what is actually on screen is repainted. The other page is
         * behind the tileview and repainting it costs PSRAM bandwidth for
         * nothing; when the detail layer is up it covers both. */
        if (s_detail_open) {
            screen_overhead_update(&vm);
        } else if (nav_page() == PAGE_LISTE) {
            screen_list_update(ac, n, rt, n);
        } else {
            screen_radar_update(ac, n, rt, n, g_settings.radius_nm);
            screen_radar_set_clock(vm.clock_valid ? vm.clock : "");
        }
        nav_tick(vm.state == VIEW_EMPTY_SKY);
        display_unlock();
    }
}

/* Fires the same request repeatedly and reports how many got through. The point
 * is to tell a firmware fault apart from a bad radio environment: a logic bug
 * fails every time, RF interference fails a fraction of the time. Those two look
 * identical in a log of one failure. */
static void probe_link(void)
{
    static char buf[1024];
    const int tries = 15;
    int ok = 0;
    int64_t best = INT64_MAX, worst = 0, total = 0;

    ESP_LOGW(TAG, "probing the link, %d attempts...", tries);
    for (int i = 0; i < tries; i++) {
        int status = 0;
        bool trunc = false;
        int64_t t0 = esp_timer_get_time();
        int n = http_get("http://api.adsb.lol/v2/point/47.6691/15.9303/5",
                         buf, sizeof buf, 10000, &status, &trunc);
        int64_t dt = (esp_timer_get_time() - t0) / 1000;
        if (n >= 0 && status == 200) {
            ok++;
            total += dt;
            if (dt < best)  best = dt;
            if (dt > worst) worst = dt;
        }
        ESP_LOGW(TAG, "  %2d/%d  %-4s  %5lld ms  http=%d",
                 i + 1, tries, (n >= 0 && status == 200) ? "ok" : "FAIL",
                 (long long)dt, status);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    wifi_ap_record_t ap;
    int rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
    ESP_LOGW(TAG, "link probe: %d/%d succeeded (%d%%), rssi=%d dBm", ok, tries,
             ok * 100 / tries, rssi);
    if (ok > 0) {
        ESP_LOGW(TAG, "  latency  best=%lld ms  worst=%lld ms  mean=%lld ms",
                 (long long)best, (long long)worst, (long long)(total / ok));
    }
}

/* Radar first, Liste beside it. The hero screen is no longer in the deck at
 * all — it is the layer you reach by tapping an aircraft, and you leave it the
 * way you came in.
 *
 * Worth being honest in writing about what that trades away: AGENTS.md §1 asks
 * for the answer "in under two seconds with NO interaction", and the hero was
 * the default page precisely because of that sentence. The Radar answers a
 * different question first — what is up there — and names only the nearest
 * aircraft, in its caption. Destination and distance are still there without a
 * tap; airline, type and altitude are a tap away. Chosen by the man who uses
 * it, after using it. */
static const nav_page_t k_pages[] = {
    { "radar", screen_radar_create },
    { "liste", screen_list_create },
};


/* ---- Einstellungen and WLAN, reached from the long-press ----------------
 *
 * Both are overlays, not deck pages (DESIGN.md §6): you leave them the way you
 * came in. WLAN can also present itself, which is the one case the device is
 * allowed to interrupt him.
 */
static void open_settings(void);

/* Scanning blocks for seconds, so it cannot happen on the LVGL task. This runs
 * it once on its own stack and hands the result back under the display lock. */
static void wifi_scan_task(void *arg)
{
    (void)arg;
    static char found[8][WIFI_SSID_LEN];
    static char saved[WIFI_MAX_NETWORKS][WIFI_SSID_LEN];

    int n = wifi_scan(found, 8);
    int n_saved = (wifi_creds_list(saved, WIFI_MAX_NETWORKS) == ESP_OK)
                      ? WIFI_MAX_NETWORKS : 0;

    display_lock(0);
    if (nav_overlay_open()) {
        screen_wifi_set_networks((const char (*)[WIFI_SSID_LEN])found, n,
                                 (const char (*)[WIFI_SSID_LEN])saved, n_saved);
        screen_wifi_set_status(NULL, wifi_is_connected(), false);
    }
    display_unlock();
    vTaskDelete(NULL);
}

static void start_wifi_scan(void)
{
    display_lock(0);
    if (nav_overlay_open()) {
        screen_wifi_set_status(NULL, wifi_is_connected(), true);
    }
    display_unlock();
    xTaskCreate(wifi_scan_task, "wifiscan", 4096, NULL, 4, NULL);
}

static void on_wifi_join(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') return;

    if (password != NULL) {
        /* A new network. Slot choice is the same rule as the serial path: reuse
         * the slot this SSID already occupies, else the first free one, so
         * setting up a holiday network never destroys the one that gets him
         * home (AGENTS.md §6). */
        char known[WIFI_MAX_NETWORKS][WIFI_SSID_LEN];
        int slot = -1;
        if (wifi_creds_list(known, WIFI_MAX_NETWORKS) == ESP_OK) {
            for (int i = 0; i < WIFI_MAX_NETWORKS; i++) {
                if (strcmp(known[i], ssid) == 0) { slot = i; break; }
            }
            if (slot < 0) {
                for (int i = 0; i < WIFI_MAX_NETWORKS; i++) {
                    if (known[i][0] == '\0') { slot = i; break; }
                }
            }
        }
        if (slot < 0) slot = WIFI_MAX_NETWORKS - 1;
        wifi_creds_set(slot, ssid, password);   /* never logs the password */
        ESP_LOGW(TAG, "stored \"%s\" in slot %d from the panel", ssid, slot);
    } else {
        /* A saved network: the screen never had the password and must not ask
         * for one again. Just reconnect with what NVS already holds. */
        ESP_LOGW(TAG, "reconnecting to saved network \"%s\"", ssid);
    }
    wifi_reconnect_now();
}

static void close_overlay(void)
{
    display_lock(0);
    nav_close_overlay();
    display_unlock();
}

/* ---- The detail layer ---------------------------------------------------
 *
 * §5.1/§5.2 used to be page 0 of the deck. It is now a layer underneath the
 * Radar and the Liste, opened by tapping an aircraft on either, and left by
 * tapping anywhere. `s_detail_from` remembers which page he came from so
 * "back" means back, and not "back to wherever the deck happens to be". */

static void close_detail(void)
{
    display_lock(0);
    nav_close_overlay();
    s_detail_open = false;
    s_has_selection = false;      /* the subject dies with the layer */
    nav_go_to(s_detail_from, true);
    display_unlock();
}

static void build_detail_screen(lv_obj_t *parent)
{
    screen_overhead_create(parent);
    screen_overhead_set_back_cb(close_detail);
}

static void open_detail(void)
{
    s_detail_from = nav_page();
    display_lock(0);
    nav_open_overlay(build_detail_screen, "detail");
    display_unlock();
    s_detail_open = true;
}

static void build_wifi_screen(lv_obj_t *parent)
{
    screen_wifi_create(parent);
    screen_wifi_set_join_cb(on_wifi_join);
    screen_wifi_set_rescan_cb(start_wifi_scan);
    screen_wifi_set_exit_cb(open_settings);   /* back to where he came from */
}

static void open_wifi(void)
{
    display_lock(0);
    nav_open_overlay(build_wifi_screen, "wlan");
    display_unlock();
    start_wifi_scan();
}

/* ---- The place search (§5.8) --------------------------------------------
 *
 * Same shape as the WiFi scan above, for the same reason: geocode_lookup()
 * opens a connection and waits on a free community service, which is seconds
 * on the display task and therefore not on the display task. One task per
 * search, created on tap and gone again when the answer has been handed back
 * under display_lock().
 *
 * The query is copied into a file-scope buffer rather than passed as the
 * task argument: screen_geo.c hands out a pointer into its own text area and
 * says so, and that text area belongs to an overlay that may be deleted
 * before the task ever runs. One search can be in flight at a time, which is
 * also all he can start — the screen is showing "Suche Orte..." and its
 * Suchen button is on the other sub-screen. */
static char s_geo_query[SCREEN_GEO_QUERY_MAX];

/* Set by the 'z' console command: take the first hit automatically instead of
 * waiting for a finger. Tapping a result row is the one step of this feature
 * that cannot be driven from the build host, and it is the step where the
 * settings are written, the clock is re-based and the poll location moves —
 * so without this, "does picking a place actually move the device" would be
 * a question only Markus could answer, by opening a screen he has no reason
 * to open. D4/D41: drive it from here and read the framebuffer back. */
static bool s_geo_autopick;

static void on_geo_pick(const geo_place_t *place);

static void geo_search_task(void *arg)
{
    (void)arg;
    static geo_place_t found[GEOCODE_MAX_RESULTS];

    int n = geocode_lookup(s_geo_query, found, GEOCODE_MAX_RESULTS);

    display_lock(0);
    if (nav_overlay_open()) {
        /* Negative codes all mean the same thing to him — the search did not
         * answer — and screen_geo.c turns any of them into that one
         * sentence. The distinction stays in geocode.c's log line, which is
         * where it is useful. */
        screen_geo_set_results(found, n);
    }
    display_unlock();

    if (s_geo_autopick && n > 0) {
        s_geo_autopick = false;
        ESP_LOGW(TAG, "autopick: %s", found[0].label);
        /* Through the row's own click event, not straight to on_geo_pick():
         * the interesting part of this path is that picking a hit deletes
         * this screen from inside one of its own event callbacks, and
         * calling the callback directly would skip exactly that. */
        display_lock(0);
        screen_geo_debug_tap(0);
        display_unlock();
    }
    vTaskDelete(NULL);
}

static void on_geo_search(const char *query)
{
    if (query == NULL) return;
    snprintf(s_geo_query, sizeof s_geo_query, "%s", query);
    /* 4 KB matches the WiFi scan task beside it. cJSON parses the response on
     * this stack, but into a heap document, and the 8 KB response buffer is a
     * PSRAM allocation inside geocode_lookup() — nothing large lives here. */
    xTaskCreate(geo_search_task, "geosearch", 4096, NULL, 4, NULL);
}

/* The two fixed-length string pairs that this function copies between are
 * declared in different headers on purpose — main/data does not include
 * main/net anywhere else in this tree — so nothing but this stops them
 * drifting apart into a silent truncation. test/host/test_geo.c checks the
 * same pair, which covers the host build; this covers the firmware. */
_Static_assert(SETTINGS_LABEL_LEN == GEO_LABEL_LEN,
               "settings_t.custom_label and geo_place_t.label must match");
_Static_assert(SETTINGS_TZ_LEN == GEO_TZ_LEN,
               "settings_t.custom_tz and geo_place_t.tz must match");

/* He tapped one of the hits. This is the whole point of the screen: the
 * device moves, the clock follows it, and both are remembered. */
static void on_geo_pick(const geo_place_t *place)
{
    if (place == NULL) return;

    g_settings.preset     = LOC_CUSTOM;
    g_settings.custom_lat = place->lat;
    g_settings.custom_lon = place->lon;
    snprintf(g_settings.custom_label, sizeof g_settings.custom_label, "%s", place->label);
    snprintf(g_settings.custom_tz, sizeof g_settings.custom_tz, "%s", place->tz);

    settings_sanitise(&g_settings);
    settings_save(&g_settings);
    apply_settings();
    ESP_LOGW(TAG, "moved to a searched place: %.4f/%.4f tz=%s",
             g_settings.custom_lat, g_settings.custom_lon, g_settings.custom_tz);

    /* Back to Einstellungen, where the "Eigener Ort" card now says where he
     * has just put the device. Landing anywhere else would leave him looking
     * at a result list with no sign that the tap did anything. */
    open_settings();
}

static void build_geo_screen(lv_obj_t *parent)
{
    screen_geo_create(parent);
    screen_geo_set_search_cb(on_geo_search);
    screen_geo_set_pick_cb(on_geo_pick);
    screen_geo_set_exit_cb(open_settings);   /* back to where he came from */
}

static void open_geo(void)
{
    display_lock(0);
    nav_open_overlay(build_geo_screen, "ortsuche");
    display_unlock();
}

/* Pretends the search came back empty, then that it did not come back at
 * all. The same idea as 'Y' for the battery: these two states are the ones a
 * hotel network produces and the ones nobody can produce on demand at a
 * desk, so the device is asked to draw them rather than waited on.
 *
 * Both say something in words — AGENTS.md §1, never a blank panel — and they
 * deliberately say DIFFERENT things: one is his typo to fix, the other is
 * the device's problem and nothing he types will help. */
static void geo_demo_states(void)
{
    open_geo();
    display_lock(0);
    screen_geo_set_results(NULL, 0);          /* nothing by that name */
    display_unlock();
    vTaskDelay(pdMS_TO_TICKS(4000));
    display_lock(0);
    screen_geo_set_results(NULL, -1);         /* no answer at all */
    display_unlock();
}

static void on_settings_changed(const settings_t *s)
{
    if (s == NULL) return;
    g_settings = *s;
    settings_sanitise(&g_settings);
    settings_save(&g_settings);
    apply_settings();
    display_lock(0);
    screen_settings_update(&g_settings);
    display_unlock();
}

static void build_settings_screen(lv_obj_t *parent)
{
    screen_settings_create(parent);
    screen_settings_set_cb(on_settings_changed);
    screen_settings_set_wifi_cb(open_wifi);
    screen_settings_set_geo_cb(open_geo);
    screen_settings_set_exit_cb(close_overlay);
    screen_settings_update(&g_settings);
}

static void open_settings(void)
{
    display_lock(0);
    nav_open_overlay(build_settings_screen, "einstellungen");
    display_unlock();
}

/* Opens the place search and runs one, from the serial console.
 *
 * The search screen cannot be exercised from the build host otherwise: the
 * only way to a result list is to type a word on the on-screen keyboard,
 * and there is no keyboard on the build host. That made the RESULTS state
 * unreachable for tools/grab_screen.py, which is how every other screen in
 * this repo was checked (D4, D41) — so "does the hit list actually look
 * right" would have been a question only Markus could answer, about a screen
 * he has no reason to open.
 *
 * Deliberately a real request against the real endpoint, not a fixture: what
 * is being checked here is the whole path, and a canned list would have
 * proved the layout while hiding a URL that 404s. */
static void geo_demo_search(void)
{
    /* Unconditionally, not "only if no overlay is open" — which is what this
     * said first and which quietly made the command a no-op whenever
     * Einstellungen happened to be up. A stress run then reported forty
     * cycles and had performed twenty, which is the kind of test harness bug
     * that reads as a passing test. */
    open_geo();
    /* The query comes from location_name(), not from a literal here. Partly
     * because tools/check_strings.py is right that a German place name
     * written into main.c is indistinguishable from a label that leaked —
     * and partly because this is the better demo anyway: it searches for a
     * place the device already knows, so the hit list can be read against
     * the card two screens away. LOC_WIEN in particular returns four places
     * across two countries, which is exactly the case the second line on
     * each row exists for. */
    on_geo_search(location_name(LOC_WIEN));
}

/* Hand the screen to a debug view: stop the UI task touching it, and wait out
 * any update already in progress. */
static void ui_suspend(void)
{
    s_ui_suspended = true;
    vTaskDelay(pdMS_TO_TICKS(120));   /* longer than one update takes */
}

/* Give it back. The widget tree was destroyed by lv_obj_clean(), so it has to
 * be rebuilt before the UI task is allowed near it again. */
static void ui_resume(void)
{
    display_lock(0);
    lv_obj_clean(lv_screen_active());
    nav_create(k_pages, (int)(sizeof k_pages / sizeof k_pages[0]));
    nav_set_longpress_cb(open_settings);
    screen_list_set_select_cb(on_list_select);
    screen_radar_set_select_cb(on_radar_select);
    display_unlock();
    /* The deck was rebuilt, so the badge that was on it is gone. Forget what
     * we believed was showing or the next poll will decide nothing changed. */
    power_forget_ui();
    s_ui_suspended = false;
    ESP_LOGW(TAG, "live view restored");
}

/* Cycles Gloggnitz -> Pattaya -> Eigener Ort. Stands in for the §5.6 screen
 * until it is wired into the navigation graph. */
static void cycle_location(void)
{
    g_settings.preset = (location_preset_t)((g_settings.preset + 1) % LOC_COUNT);
    settings_sanitise(&g_settings);
    settings_save(&g_settings);
    apply_settings();
}

/* Scroll whatever is scrollable on the current screen to its end.
 *
 * Einstellungen is taller than 480 px, so its foot — including the data
 * attribution line the ODbL requires — cannot be photographed from the build
 * host without this. Generic rather than a screen_settings_* call: it finds
 * the scrollable by flag, so it keeps working for any screen that grows past
 * one panel height, and it adds no debug-only API to a product header. */
static void scroll_to_end_rec(lv_obj_t *obj)
{
    /* Recursive because the scrollable column is not a child of the screen:
     * nav_open_overlay() puts a full-screen overlay root in between, so a
     * one-level scan finds nothing and silently does nothing — which is
     * exactly what the first version of this did. */
    if (lv_obj_is_scrollable(obj)) {
        int32_t remaining = lv_obj_get_scroll_bottom(obj);
        if (remaining > 0) {
            lv_obj_scroll_by(obj, 0, -remaining, LV_ANIM_OFF);
        }
    }
    uint32_t n = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < n; i++) {
        scroll_to_end_rec(lv_obj_get_child(obj, i));
    }
}

static void scroll_to_end(void)
{
    display_lock(0);
    scroll_to_end_rec(lv_screen_active());
    display_unlock();
}

/* Update source, typed in over serial. Deliberately the same shape as
 * provision_wifi(): a URL that decides what firmware this device will run is
 * not something to leave in a config file in a repository either. */
static void update_console(void)
{
    char cur[192];
    ota_get_url(cur, sizeof cur);
    printf("\nrunning version : %s\n", ota_running_version());
    printf("update source   : %s\n", cur[0] ? cur : "(none — updates are off)");
    printf("\nPaste an https:// manifest URL and press ENTER.\n"
           "ENTER alone leaves it as it is; \"-\" turns updates off.\n> ");
    fflush(stdout);

    /* dbg_read_line(), not fgets(): stdin is non-blocking on this build (D22 —
     * the usb_serial_jtag driver's blocking write wedged the device, and "no
     * host attached" is its normal state), so fgets() returns NULL instantly
     * and every character typed afterwards arrives at on_cmd() as a COMMAND.
     * Pasting an https:// URL that way runs 't' — the tearing benchmark —
     * among others. Found exactly that way. */
    char line[256];
    if (dbg_read_line(line, sizeof line, 120000) < 0) {
        printf("\ntimed out — nothing stored\n");
        return;
    }
    if (line[0] == '\0') {
        printf("unchanged\n");
    } else if (strcmp(line, "-") == 0) {
        printf(ota_set_url(NULL) ? "updates off\n" : "could not clear\n");
    } else if (!ota_set_url(line)) {
        printf("rejected (must be https:// and under 192 bytes)\n");
    } else {
        /* Handed to the OTA task rather than run here: this console task has a
         * 4 KB stack and a TLS handshake needs about 8 KB. Doing it inline
         * silently corrupted the touch driver and aborted the device. */
        ota_request_check();
        printf("stored; the update task is checking now — watch the log.\n"
               "anything newer installs inside the night window (%02d:00-%02d:00)\n",
               g_settings.dim_from_hour, g_settings.dim_to_hour);
    }
}

/* The battery, on demand, with the registers behind it.
 *
 * Reads the PMIC live rather than printing the cached status: on the morning
 * a cell is first plugged in, the question is not "what does the device
 * think" but "what does the chip say", and those are only the same thing
 * when everything already works. */
static void battery_console(void)
{
    printf("\nbattery\n");
    if (!axp2101_present()) {
        printf("  no AXP2101 configured — this device is USB-only\n");
        return;
    }
    battery_raw_t raw;
    esp_err_t err = axp2101_read(&raw);
    if (err != ESP_OK) {
        printf("  PMIC read failed: %s\n", esp_err_to_name(err));
        return;
    }
    battery_status_t st;
    battery_eval(&raw, &s_battery, &st);
    char line[64];
    battery_line_text(&st, line, sizeof line);

    printf("  cell present  : %s\n", raw.present ? "yes" : "no");
    printf("  vbus          : %s, %d mV\n",
           raw.vbus_good ? "good" : "absent", axp2101_vbus_mv());
    printf("  charger state : %u (2=CC 3=CV 4=done 5=not charging)\n",
           (unsigned)raw.chg_status);
    printf("  cell          : %d mV, gauge %d %%, shown %d %%\n",
           raw.mv, raw.gauge_pct, st.percent);
    printf("  backlight cap : %d %%\n", st.brightness_cap_pct);
    printf("  panel shows   : %s\n", line);
    axp2101_dump();
}

/* Where LVGL's widgets actually come from.
 *
 * It used to print lv_mem_monitor(), which was the right thing while LVGL had
 * a fixed pool of its own. Since D58 it allocates through the system heap, so
 * that monitor reports zeros — a diagnostic that answers every question with
 * "0" is worse than none, because it looks like an answer. What matters now
 * is the largest contiguous internal block: LVGL asks for many small blocks
 * and one big one for a keyboard, and it is the big one that fails first.
 */
static void lvgl_mem_report(const char *when)
{
    ESP_LOGW(TAG, "widget memory %-14s internal free %6u (largest %6u)  psram free %8u",
             when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void on_cmd(char c)
{
    if (c >= '1' && c <= '4') {
        ui_suspend();
        dbg_fixture_show(c - '0');
        return;
    }
    if (c == '0') {
        ui_resume();
        return;
    }
    /* Everything below replaces the screen wholesale, so it must take
     * ownership first. Press '0' to get the live view back. */
    if (c == 'b') { ui_suspend(); bench_suite(); }
    else if (c == 'm') dbg_metrics_hero();
    else if (c == 'w') provision_wifi();
    else if (c == 'n') network_status();
    else if (c == 'p') probe_link();
    else if (c == 't') { ui_suspend(); dbg_bench_tearing(); }
    else if (c == 'o') cycle_location();
    else if (c == 'g') { display_lock(0); nav_go_to((nav_page() + 1) % (int)(sizeof k_pages / sizeof k_pages[0]), true); display_unlock(); }
    else if (c == 'e') open_settings();
    else if (c == 'k') open_wifi();
    else if (c == 'q') open_geo();
    else if (c == 'Q') geo_demo_search();
    else if (c == 'z') { s_geo_autopick = true; geo_demo_search(); }
    else if (c == 'Z') geo_demo_states();
    else if (c == 'u') update_console();
    else if (c == 'd') scroll_to_end();
    else if (c == 'v') lvgl_mem_report("on demand");
    else if (c == 'i') { if (s_detail_open) close_detail(); else open_detail(); }
    else if (c == 'f') { ui_suspend(); dbg_font_card(); }
    else if (c == 'y') battery_console();
    else if (c == 'Y') battery_sim_cycle();
    else if (c == 'x') nav_touch_report();
}

void app_main(void)
{
    log_memory_budget("boot, before display init");

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    ESP_ERROR_CHECK(display_init());
    /* bsp_display_new() already ran brightness init; calling it again just
     * re-configures GPIO4 and logs an LEDC conflict. The BSP drives this
     * backlight active-low, so "flipped 0%" in the log means FULL brightness. */
    bsp_display_backlight_on();

    /* The PMIC, once the BSP has brought the I2C bus up with the display.
     * Never fatal, and a no-op on a device with no cell fitted — but the one
     * thing it does that nothing else can is stop the TS pin gating the
     * charger, so a battery plugged in later charges without a reflash. */
    (void)axp2101_init();

    log_memory_budget("after display init (framebuffer up)");

    dbg_bench_init();
    dbg_screen_start(on_cmd);

    display_lock(0);
    nav_create(k_pages, (int)(sizeof k_pages / sizeof k_pages[0]));
    nav_set_longpress_cb(open_settings);
    screen_list_set_select_cb(on_list_select);
    screen_radar_set_select_cb(on_radar_select);
    display_unlock();
    log_memory_budget("after screen built");

    /* Network last, and never fatal: the panel must come up and show something
     * even with no credentials stored, which is the state every new device is
     * in and the state he will be in when he lands in Thailand. */
    /* Timezone follows the location preset — he never sets a clock. The preset
     * picker is M6; for now it is compiled in with the coordinates. The TZ can
     * be set immediately, but SNTP itself must wait for an actual network (see
     * ui_task): starting it here only worked on a device that already had
     * credentials at boot, which is never true of a device arriving somewhere
     * new — exactly the case this product is built around. */
    settings_load(&g_settings);
    timesync_set_tz(settings_tz(&g_settings));

    double lat, lon;
    settings_coords(&g_settings, &lat, &lon);
    bsp_display_brightness_set(g_settings.brightness_pct);

    if (wifi_start() == ESP_OK) {
        ESP_ERROR_CHECK(flight_source_start(lat, lon, g_settings.radius_nm));
        log_memory_budget("after wifi + poller started");
    } else {
        ESP_LOGW(TAG, "no WiFi credentials stored — press 'w' to provision");
    }

    /* Go through apply_settings() rather than trusting the open-coded
     * bootstrap above to stay in step with it. It did not: apply_settings()
     * grew a call to ota_settings_update(), the boot path did not, and so
     * s_settings stayed all-zero — auto_dim false — for the entire life of
     * the device unless he happened to change a setting by hand. The OTA task
     * would find an update, log it, and then decline to install it every five
     * minutes forever, while the console cheerfully printed the night window
     * it believed was in force. Safe to call here now that the flight source
     * tolerates not having been started. */
    apply_settings();

    xTaskCreate(ui_task, "ui", 4096, NULL, 4, NULL);
    ota_start();

    ESP_LOGW(TAG, "ready: s=shot f=fontcard b=bench m=metrics w=wifi n=net u=update y=akku x=touch o=ort g=seite e=einst k=wlan d=scroll 1-4=fixture 0=live");

    /* Rollback confirmation. With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE a
     * freshly written image is on probation until it says otherwise, and the
     * thing worth proving before it says so is not that the image runs — the
     * bootloader already checked its hash — but that it can still get ONLINE.
     * An image that boots happily and cannot reach WiFi is exactly the brick
     * nobody can fix from 9,000 km away, and it is the one a rollback saves.
     * Two minutes of steady-state polling is the evidence; if it never comes,
     * the next reboot goes back to the build that worked. */
    int healthy_ticks = 0;
    bool confirmed = false;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        log_memory_budget("steady state");

        if (!confirmed) {
            healthy_ticks = wifi_is_connected() ? healthy_ticks + 1 : 0;
            if (healthy_ticks >= 4) {
                ota_confirm_running_image();
                confirmed = true;
            }
        }
    }
}
