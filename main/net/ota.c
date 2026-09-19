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
#include "nvs.h"

#include "data/settings.h"
#include "net/http_get.h"   /* HTTP_USER_AGENT */
#include "net/timesync.h"
#include "net/wifi.h"

static const char *TAG = "ota";

/* Same namespace the settings live in — one namespace, one thing to erase. */
#define OTA_NS      "flight"
#define OTA_KEY_URL "ota_url"

/* A real manifest is ~120 bytes. 4 KB is headroom for a hand-written one with
 * release notes in it, and small enough that a wrong URL pointing at a web
 * page is refused rather than parsed. */
#define MANIFEST_MAX     4096
#define MANIFEST_TIMEOUT 10000  /* ms */

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
    esp_err_t err = esp_http_client_open(c, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "manifest: connect failed: %s", esp_err_to_name(err));
        goto done;
    }
    if (esp_http_client_fetch_headers(c) < 0) {
        ESP_LOGW(TAG, "manifest: no headers");
        goto done;
    }
    int status = esp_http_client_get_status_code(c);
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

    /* On the stack would be 1 KB of a 4 KB task. Static: this runs at most
     * once a day and never re-entrantly. */
    static char body[MANIFEST_MAX];
    int n = https_get(url, body, sizeof body);
    s_last_check_ms = esp_timer_get_time() / 1000;
    if (n <= 0) {
        return false;
    }

    ota_manifest_t m;
    if (!ota_manifest_parse(body, (size_t)n, &m)) {
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
         * than sleeping lets ota_request_check() cut it short. */
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
    if (s_task != NULL) {
        return;
    }
    if (s_wake == NULL) {
        s_wake = xSemaphoreCreateBinary();
    }
    /* 10 KB. A TLS handshake verified against the full Mozilla root bundle is
     * the deepest thing this firmware does; 6 KB was not enough and 4 KB
     * corrupted the touch driver rather than reporting itself (D45). A real
     * handshake leaves 6,172 B of this unused. */
    if (xTaskCreate(ota_task, "ota", 10240, NULL, 3, &s_task) != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "could not start the update task");
    }
}

void ota_start(void)
{
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
