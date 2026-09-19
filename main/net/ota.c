#include "ota.h"
#include "ota_policy.h"

#include <string.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "nvs.h"

#include "data/settings.h"
#include "net/http_get.h"   /* HTTP_USER_AGENT */
#include "net/timesync.h"
#include "net/wifi.h"

static const char *TAG = "ota";

/* Same namespace the settings live in — one namespace, one thing to erase. */
#define OTA_NS      "flight"
#define OTA_KEY_URL "ota_url"
/* The version of an image that was written, booted, failed to confirm itself
 * and was rolled back. Persisted so the device does not spend 2 MB of flash
 * writes a night re-installing the same broken build forever. */
#define OTA_KEY_BADVER "ota_badver"

/* A real manifest is ~120 bytes. 4 KB is headroom for a hand-written one with
 * release notes in it, and small enough that a wrong URL pointing at a web
 * page is refused rather than parsed. */
#define MANIFEST_MAX     4096
#define MANIFEST_TIMEOUT 10000  /* ms */
#define MANIFEST_MAX_REDIRECTS 5

/* Result of the last successful check, so install does not re-fetch. */
static ota_manifest_t s_pending;
static bool           s_have_pending;
static int64_t        s_last_check_ms;

/* The settings the policy needs. main.c owns the live copy; this is a
 * snapshot taken whenever it changes, because the OTA task must not reach
 * into another module's globals. */
static settings_t s_settings;

/* Poked by ota_request_check() so the task wakes instead of sleeping out its
 * five-minute tick. */
static SemaphoreHandle_t s_wake;
static TaskHandle_t      s_task;

/* A version that was written, booted, and failed to confirm itself. Loaded
 * from NVS at start, so it survives the reboot that the rollback caused. */
static char s_bad_version[OTA_VERSION_LEN];
static bool s_starting;   /* start_task_once() is in flight */

static void start_task_once(void);   /* defined with ota_start(), below */

/* ---- version ----------------------------------------------------------- */

const char *ota_running_version(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    return (d != NULL && d->version[0] != '\0') ? d->version : "0.0.0";
}

/* ---- url in NVS -------------------------------------------------------- */

void ota_get_url(char *out, size_t outsz)
{
    if (out == NULL || outsz == 0) {
        return;
    }
    out[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(OTA_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t n = outsz;
    if (nvs_get_str(h, OTA_KEY_URL, out, &n) != ESP_OK) {
        out[0] = '\0';
    }
    nvs_close(h);
}

bool ota_set_url(const char *url)
{
    nvs_handle_t h;
    if (nvs_open(OTA_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    /* Whatever was pending came from the OLD source and must not survive the
     * change. Without this, replacing a source (because it was wrong, stale,
     * or compromised) and then having the NEW one fail to answer leaves
     * s_have_pending set from the old fetch — and the night's install pulls
     * the image from the source that was just removed. Only replacement was
     * affected; clearing already refused via have_url. */
    s_have_pending = false;
    memset(&s_pending, 0, sizeof s_pending);

    esp_err_t err;
    if (url == NULL || url[0] == '\0') {
        err = nvs_erase_key(h, OTA_KEY_URL);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;                      /* already absent */
        }
        ESP_LOGI(TAG, "update source cleared");
    } else if (strncmp(url, "https://", 8) != 0) {
        /* An unauthenticated firmware source is a remote root shell with
         * extra steps. Refused here as well as in Kconfig, because a config
         * flag is one edit away and this is not. */
        ESP_LOGE(TAG, "refusing a non-https update source");
        nvs_close(h);
        return false;
    } else if (strlen(url) >= OTA_URL_LEN) {
        ESP_LOGE(TAG, "update source is longer than %d bytes", OTA_URL_LEN - 1);
        nvs_close(h);
        return false;
    } else {
        err = nvs_set_str(h, OTA_KEY_URL, url);
        ESP_LOGI(TAG, "update source set");
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

/* ---- manifest fetch ---------------------------------------------------- */

static int https_get(const char *url, char *buf, size_t bufsz)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = MANIFEST_TIMEOUT,
        .user_agent = HTTP_USER_AGENT,   /* the same one the flight data uses;
                                          * some hosts 403 a client with none */
        .crt_bundle_attach = esp_crt_bundle_attach,
        /* The manifest may sit behind a redirect (a GitHub release asset
         * always does). Unlike the flight data path, which must see a 3xx as
         * itself, here a redirect is ordinary and following it is correct. */
        .disable_auto_redirect = false,
        .max_redirection_count = 5,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (c == NULL) {
        return -1;
    }
    int out = -1;

    /* Follow redirects BY HAND.
     *
     * .disable_auto_redirect and .max_redirection_count are inert on this code
     * path, and the comment above used to claim otherwise. esp_http_client
     * only acts on them inside esp_http_client_perform(); the
     * open/fetch_headers/read sequence used here never reaches the function
     * that calls esp_http_client_set_redirection(). That is exactly why
     * esp_https_ota rolls its own loop over the same three calls.
     *
     * It matters because the obvious place to put a manifest is a GitHub
     * release asset, and that answers 302 every single time. The failure was
     * also asymmetric and therefore nasty to diagnose: the .bin named INSIDE
     * the manifest would have downloaded fine, because esp_https_ota handles
     * its own redirects — so only the first hop was broken. */
    int status = 0;
    esp_err_t err = ESP_FAIL;
    for (int hop = 0; hop <= MANIFEST_MAX_REDIRECTS; hop++) {
        err = esp_http_client_open(c, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "manifest: connect failed: %s", esp_err_to_name(err));
            goto done;
        }
        if (esp_http_client_fetch_headers(c) < 0) {
            ESP_LOGW(TAG, "manifest: no headers");
            goto done;
        }
        status = esp_http_client_get_status_code(c);
        if (status != 301 && status != 302 && status != 303 &&
            status != 307 && status != 308) {
            break;
        }
        if (hop == MANIFEST_MAX_REDIRECTS) {
            ESP_LOGW(TAG, "manifest: more than %d redirects; giving up",
                     MANIFEST_MAX_REDIRECTS);
            goto done;
        }
        esp_http_client_set_redirection(c);
        esp_http_client_close(c);
    }
    if (status != 200) {
        ESP_LOGW(TAG, "manifest: HTTP %d", status);
        goto done;
    }
    /* Read in a LOOP. One esp_http_client_read() returns whatever happens to
     * have arrived — one TLS record, one chunk — not the whole body, and a
     * body cut off mid-object parses as "not JSON" while looking exactly like
     * a server problem. That is how this failed the first time it worked:
     * api.github.com's 2.3 KB answer arrived in pieces and only the first was
     * ever read. */
    int total = 0;
    for (;;) {
        int n = esp_http_client_read(c, buf + total, (int)bufsz - 1 - total);
        if (n < 0) {
            ESP_LOGW(TAG, "manifest: read failed after %d B", total);
            goto done;
        }
        if (n == 0) {
            break;                        /* end of body */
        }
        total += n;
        if (total >= (int)bufsz - 1) {
            /* Never parse a truncated manifest: half a JSON object can still
             * be valid JSON, and then the device believes a version number
             * that was never sent. */
            ESP_LOGW(TAG, "manifest: larger than %u B; refusing it",
                     (unsigned)bufsz - 1);
            goto done;
        }
    }
    if (!esp_http_client_is_complete_data_received(c)) {
        ESP_LOGW(TAG, "manifest: connection ended early at %d B", total);
        goto done;
    }
    buf[total] = '\0';
    ESP_LOGI(TAG, "manifest: %d B received", total);
    out = total;
done:
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return out;
}

bool ota_check(char *newer_version, size_t vsz)
{
    if (newer_version != NULL && vsz > 0) {
        newer_version[0] = '\0';
    }
    char url[OTA_URL_LEN];
    ota_get_url(url, sizeof url);
    if (url[0] == '\0') {
        return false;
    }

    /* PSRAM, on demand, freed before returning.
     *
     * This was `static char body[MANIFEST_MAX]` — 4 KB of .bss, and .bss is
     * INTERNAL DRAM here (CONFIG_SPIRAM_RODATA relocates .rodata, not .bss,
     * and ALLOW_BSS_SEG_EXTERNAL_MEMORY is off). So a feature that ships
     * switched off was spending a quarter of the ~16 KB of internal heap this
     * board has in steady state — on every device, forever, whether or not an
     * update source is configured. That flatly contradicts start_task_once(),
     * which goes to real lengths to avoid a 10 KB stack for the same reason.
     * There are 4.7 MB of PSRAM and this runs once a day. */
    char *body = heap_caps_malloc(MANIFEST_MAX, MALLOC_CAP_SPIRAM);
    if (body == NULL) {
        ESP_LOGW(TAG, "no PSRAM for the manifest buffer");
        return false;
    }
    int n = https_get(url, body, MANIFEST_MAX);
    s_last_check_ms = esp_timer_get_time() / 1000;
    if (n <= 0) {
        free(body);
        return false;
    }

    ota_manifest_t m;
    bool parsed = ota_manifest_parse(body, (size_t)n, &m);
    free(body);
    if (!parsed) {
        return false;
    }

    const char *running = ota_running_version();
    int cmp = ota_version_cmp(m.version, running);
    if (cmp <= 0) {
        ESP_LOGI(TAG, "up to date (running %s, offered %s)", running, m.version);
        s_have_pending = false;
        return false;
    }
    if (strncmp(m.url, "https://", 8) != 0) {
        ESP_LOGE(TAG, "manifest offers a non-https image; ignoring");
        s_have_pending = false;
        return false;
    }

    /* The size check the manifest field was always documented as providing
     * and never did. esp_https_ota would discover this the expensive way —
     * after erasing the target slot. */
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (m.size != 0 && target != NULL && m.size > target->size) {
        ESP_LOGE(TAG, "manifest offers %u B but the slot is %u B; ignoring",
                 (unsigned)m.size, (unsigned)target->size);
        s_have_pending = false;
        return false;
    }

    /* Refuse a version that already rolled back once (see ota_start()).
     * Otherwise a build that boots but cannot get online is re-downloaded and
     * re-written every night, 2 MB at a time, forever. */
    if (s_bad_version[0] != '\0' && ota_version_cmp(m.version, s_bad_version) == 0) {
        ESP_LOGW(TAG, "%s already failed to confirm itself once; not retrying",
                 m.version);
        s_have_pending = false;
        return false;
    }

    ESP_LOGW(TAG, "update available: %s -> %s", running, m.version);
    s_pending = m;
    s_have_pending = true;
    if (newer_version != NULL && vsz > 0) {
        strlcpy(newer_version, m.version, vsz);
    }
    return true;
}

/* ---- install ----------------------------------------------------------- */

void ota_install_and_reboot(void)
{
    if (!s_have_pending) {
        ESP_LOGW(TAG, "install requested with nothing pending");
        return;
    }
    ESP_LOGW(TAG, "installing %s from %s", s_pending.version, s_pending.url);

    esp_http_client_config_t http = {
        .url = s_pending.url,
        .timeout_ms = 30000,
        .user_agent = HTTP_USER_AGENT,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t cfg = { .http_config = &http };

    esp_err_t err = esp_https_ota(&cfg);
    if (err != ESP_OK) {
        /* Nothing was switched: esp_https_ota only sets the boot partition
         * after the whole image has been written and its hash checked. A
         * failure here leaves the running image exactly as it was. */
        ESP_LOGE(TAG, "update failed: %s — staying on %s",
                 esp_err_to_name(err), ota_running_version());
        s_have_pending = false;
        return;
    }
    ESP_LOGW(TAG, "update written; rebooting into %s", s_pending.version);
    vTaskDelay(pdMS_TO_TICKS(500));   /* let the log drain */
    esp_restart();
}

/* ---- rollback ---------------------------------------------------------- */

void ota_confirm_running_image(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (run == NULL || esp_ota_get_state_partition(run, &state) != ESP_OK) {
        return;
    }
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;                      /* not on probation; nothing to confirm */
    }
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGW(TAG, "new image %s confirmed: %s",
             ota_running_version(), esp_err_to_name(err));
}

/* ---- the task ---------------------------------------------------------- */

void ota_request_check(void)
{
    s_last_check_ms = 0;               /* make ota_should_check() say yes */
    start_task_once();                 /* may be the first URL ever stored */
    if (s_wake != NULL) {
        xSemaphoreGive(s_wake);
    }
}

void ota_settings_update(const settings_t *s)
{
    if (s != NULL) {
        s_settings = *s;
    }
}

static ota_ctx_t build_ctx(void)
{
    char url[OTA_URL_LEN];
    ota_get_url(url, sizeof url);

    int hour = -1;
    bool clock_ok = timesync_is_set();
    if (clock_ok) {
        time_t now = time(NULL);
        struct tm lt;
        localtime_r(&now, &lt);
        hour = lt.tm_hour;
    }
    ota_ctx_t c = {
        .have_url      = (url[0] != '\0'),
        .online        = wifi_is_connected(),
        .clock_valid   = clock_ok,
        .auto_dim      = s_settings.auto_dim,
        .hour          = hour,
        .dim_from_hour = s_settings.dim_from_hour,
        .dim_to_hour   = s_settings.dim_to_hour,
        .now_ms        = esp_timer_get_time() / 1000,
        .last_check_ms = s_last_check_ms,
    };
    return c;
}

static void ota_task(void *arg)
{
    /* Let the rest of the system come up first. Nothing here is urgent and
     * everything here competes for the same radio and the same PSRAM bus as
     * the thing he is actually looking at.
     *
     * A WAIT, not a sleep. The task is now created on demand as well as at
     * boot, so this delay can also be the first minute after someone has just
     * typed in an update URL and is watching the console for an answer —
     * where a settling delay is not settling anything, it is only confusing.
     * ota_request_check() has already given the semaphore by then, so the
     * wait returns at once. */
    xSemaphoreTake(s_wake, pdMS_TO_TICKS(60 * 1000));

    for (;;) {
        ota_ctx_t c = build_ctx();
        if (ota_should_check(&c)) {
            ota_check(NULL, 0);
            /* After a check, not every tick: this is the only thing the task
             * does that goes deep, and 4 KB of it was enough to corrupt the
             * touch driver once already. Worth knowing the margin. */
            ESP_LOGI(TAG, "stack headroom after check: %u B",
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }
        if (s_have_pending) {
            c = build_ctx();
            if (ota_should_install(&c)) {
                ota_install_and_reboot();   /* does not return on success */
            }
        }
        /* Five minutes. The 24 h interval lives in the policy; this is just
         * how often the policy gets asked, and it has to be fine enough to
         * catch an hour-wide night window. Waiting on the semaphore rather
         * than sleeping lets ota_request_check() cut it short.
         *
         * Note s_last_check_ms is per-UPTIME, not per-day: it is not
         * persisted, so a device that reboots re-checks immediately. That is
         * the intended trade (a check is one small GET; persisting it would
         * mean an NVS write a day), but ota_policy.h used to describe it as
         * preventing exactly that, which was wrong. */
        xSemaphoreTake(s_wake, pdMS_TO_TICKS(5 * 60 * 1000));
    }
}

/* Creates the task, once, the first time there is anything for it to do.
 *
 * A feature that ships switched off should cost nothing while it is off. The
 * stack is 10 KB of INTERNAL heap, and this board runs at about 16 KB free
 * internal in steady state once the framebuffer, WiFi and the whole deck are
 * up — so an always-created task would have spent well over half the
 * remaining headroom sleeping, on a device that is expected to run for months
 * between power cycles and that has no update source configured. 10 KB is
 * cheap when it is doing something and indefensible when it is not. */
static void start_task_once(void)
{
    /* Two tasks reach this: the main task via ota_start(), and the debug
     * console task via ota_request_check(). The console task exists well
     * before ota_start() runs — nav, settings, wifi and the poller all come up
     * in between — so the window is seconds, not microseconds. Unguarded, a
     * 'u' landing in it made both callers see NULL: either two 10 KB tasks, or
     * (likelier on a board with 16 KB free) a failed second xTaskCreate that
     * NULLs the handle of the task that DID start, permanently breaking the
     * "once". A tiny critical section is the whole fix. */
    static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    taskENTER_CRITICAL(&lock);
    bool mine = (s_task == NULL && !s_starting);
    if (mine) {
        s_starting = true;
    }
    taskEXIT_CRITICAL(&lock);
    if (!mine) {
        return;
    }

    if (s_wake == NULL) {
        s_wake = xSemaphoreCreateBinary();
    }
    if (s_wake == NULL) {
        ESP_LOGE(TAG, "could not create the update task's semaphore");
        s_starting = false;
        return;
    }
    /* 10 KB. A TLS handshake verified against the full Mozilla root bundle is
     * the deepest thing this firmware does; 6 KB was not enough and 4 KB
     * corrupted the touch driver rather than reporting itself (D45). A real
     * handshake leaves 6,172 B of this unused. */
    if (xTaskCreate(ota_task, "ota", 10240, NULL, 3, &s_task) != pdPASS) {
        s_task = NULL;
        s_starting = false;          /* let a later request try again */
        ESP_LOGE(TAG, "could not start the update task");
    }
}

/* Did the bootloader just put us back on the previous image?
 *
 * The signal is the OTHER slot: a rollback leaves it ESP_OTA_IMG_INVALID or
 * ESP_OTA_IMG_ABORTED. Its app descriptor still carries the version that
 * failed, so that version can be remembered and never offered again.
 *
 * Without this the device grinds: the update installs at 03:00, boots, cannot
 * get online, never confirms itself, is rolled back — and because nothing
 * survives the reboot, the next check compares the manifest against the OLD
 * running version, finds the same build newer again, and re-downloads and
 * re-writes the same 2 MB every night indefinitely. */
static void note_rollback_if_any(void)
{
    nvs_handle_t h;
    if (nvs_open(OTA_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof s_bad_version;
        if (nvs_get_str(h, OTA_KEY_BADVER, s_bad_version, &n) != ESP_OK) {
            s_bad_version[0] = '\0';
        }
        nvs_close(h);
    }

    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    esp_ota_img_states_t st;
    if (other == NULL || esp_ota_get_state_partition(other, &st) != ESP_OK) {
        return;
    }
    if (st != ESP_OTA_IMG_INVALID && st != ESP_OTA_IMG_ABORTED) {
        return;
    }
    esp_app_desc_t d;
    if (esp_ota_get_partition_description(other, &d) != ESP_OK || d.version[0] == '\0') {
        return;
    }
    if (strncmp(s_bad_version, d.version, sizeof s_bad_version) == 0) {
        return;                                   /* already recorded */
    }
    ESP_LOGE(TAG, "%s was rolled back; it will not be offered again", d.version);
    strlcpy(s_bad_version, d.version, sizeof s_bad_version);
    if (nvs_open(OTA_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, OTA_KEY_BADVER, s_bad_version);
        nvs_commit(h);
        nvs_close(h);
    }
}

void ota_start(void)
{
    note_rollback_if_any();

    char url[OTA_URL_LEN];
    ota_get_url(url, sizeof url);
    if (url[0] == '\0') {
        ESP_LOGI(TAG, "running %s; updates disabled (no source stored)",
                 ota_running_version());
        return;                        /* no task, no stack, no cost */
    }
    ESP_LOGI(TAG, "running %s; updates enabled", ota_running_version());
    start_task_once();
}
