#include "wifi.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "wifi";

/* Reconnect backoff: doubling from a short base, capped well under a
 * minute, since a transient drop (a router reboot, a brief AP restart)
 * should recover fast -- unlike the ADS-B poll backoff in source_logic.h,
 * which is tuned to a rate-limited HTTP API, not a local radio link. */
#define WIFI_RECONNECT_BASE_MS  2000
#define WIFI_RECONNECT_CAP_MS   60000
#define WIFI_CONNECT_TIMEOUT_MS 10000
#define WIFI_IDLE_RECHECK_MS    2000
#define WIFI_PASS_LEN           64 /* WPA2 PSK passphrase, max 63 chars + NUL */

typedef struct {
    char ssid[WIFI_SSID_LEN];
    char pass[WIFI_PASS_LEN];
} wifi_cred_t;

static wifi_cred_t        g_creds[WIFI_MAX_NETWORKS];
static SemaphoreHandle_t  g_cred_mutex;
static SemaphoreHandle_t  g_scan_mutex; /* serialises esp_wifi_scan_start() callers */
static EventGroupHandle_t g_evt;
static volatile bool      g_connected;
static bool               g_started;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int64_t wifi_backoff_ms(int attempt)
{
    int64_t delay = WIFI_RECONNECT_BASE_MS;
    for (int i = 0; i < attempt; i++) {
        if (delay >= WIFI_RECONNECT_CAP_MS) {
            return WIFI_RECONNECT_CAP_MS;
        }
        delay *= 2;
    }
    return (delay > WIFI_RECONNECT_CAP_MS) ? WIFI_RECONNECT_CAP_MS : delay;
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        g_connected = false;
        xEventGroupSetBits(g_evt, WIFI_FAIL_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        g_connected = true;
        xEventGroupSetBits(g_evt, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t nvs_init_once(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static void load_creds_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no stored networks yet");
        return; /* g_creds stays zeroed -> every slot reads as unconfigured */
    }

    for (int slot = 0; slot < WIFI_MAX_NETWORKS; slot++) {
        char key[8];
        size_t len;

        snprintf(key, sizeof key, "ssid%d", slot);
        len = sizeof g_creds[slot].ssid;
        if (nvs_get_str(h, key, g_creds[slot].ssid, &len) != ESP_OK) {
            g_creds[slot].ssid[0] = '\0';
        }

        snprintf(key, sizeof key, "pass%d", slot);
        len = sizeof g_creds[slot].pass;
        if (nvs_get_str(h, key, g_creds[slot].pass, &len) != ESP_OK) {
            g_creds[slot].pass[0] = '\0';
        }
    }
    nvs_close(h);
}

/* Blocking scan + connect attempt to whichever remembered SSID is in range
 * and strongest. Returns true iff an IP was obtained before
 * WIFI_CONNECT_TIMEOUT_MS elapsed. */
static bool attempt_connect(void)
{
    xSemaphoreTake(g_scan_mutex, portMAX_DELAY);

    wifi_scan_config_t scan_cfg = { 0 };
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        xSemaphoreGive(g_scan_mutex);
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        return false;
    }

    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    if (num == 0) {
        xSemaphoreGive(g_scan_mutex);
        ESP_LOGW(TAG, "scan found no networks at all");
        return false;
    }

    wifi_ap_record_t *recs = malloc(sizeof(wifi_ap_record_t) * num);
    if (recs == NULL) {
        xSemaphoreGive(g_scan_mutex);
        ESP_LOGE(TAG, "out of memory for %u scan results", (unsigned)num);
        return false;
    }
    err = esp_wifi_scan_get_ap_records(&num, recs);
    xSemaphoreGive(g_scan_mutex);
    if (err != ESP_OK) {
        free(recs);
        ESP_LOGW(TAG, "could not read scan results: %s", esp_err_to_name(err));
        return false;
    }

    xSemaphoreTake(g_cred_mutex, portMAX_DELAY);
    int best_slot = -1;
    int8_t best_rssi = INT8_MIN;
    for (int i = 0; i < (int)num; i++) {
        for (int slot = 0; slot < WIFI_MAX_NETWORKS; slot++) {
            if (g_creds[slot].ssid[0] == '\0') {
                continue;
            }
            if (strcmp((const char *)recs[i].ssid, g_creds[slot].ssid) == 0 &&
                recs[i].rssi > best_rssi) {
                best_rssi = recs[i].rssi;
                best_slot = slot;
            }
        }
    }
    free(recs);

    if (best_slot < 0) {
        xSemaphoreGive(g_cred_mutex);
        ESP_LOGW(TAG, "no known network in range (%u seen)", (unsigned)num);
        return false;
    }

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, g_creds[best_slot].ssid, sizeof wifi_config.sta.ssid - 1);
    strncpy((char *)wifi_config.sta.password, g_creds[best_slot].pass, sizeof wifi_config.sta.password - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN; /* accept anything from open up to WPA2/3 */
    char connecting_ssid[WIFI_SSID_LEN];
    strncpy(connecting_ssid, g_creds[best_slot].ssid, sizeof connecting_ssid - 1);
    connecting_ssid[sizeof connecting_ssid - 1] = '\0';
    xSemaphoreGive(g_cred_mutex);

    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    xEventGroupClearBits(g_evt, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect() failed: %s", esp_err_to_name(err));
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(g_evt, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdTRUE, pdFALSE,
                                            pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    if (bits & WIFI_CONNECTED_BIT) {
        /* SSID only -- never the password (AGENTS.md §10). */
        ESP_LOGI(TAG, "connected: ssid=%s", connecting_ssid);
        return true;
    }
    return false;
}

static void wifi_task(void *arg)
{
    (void)arg;
    int attempt = 0;

    for (;;) {
        if (!g_connected) {
            if (attempt_connect()) {
                attempt = 0;
            } else {
                int64_t delay = wifi_backoff_ms(attempt);
                if (attempt < 100) { /* keep the counter from ever wrapping */
                    attempt++;
                }
                ESP_LOGW(TAG, "reconnect attempt failed, retrying in %lld ms", (long long)delay);
                vTaskDelay(pdMS_TO_TICKS(delay));
                continue;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(WIFI_IDLE_RECHECK_MS));
    }
}

esp_err_t wifi_start(void)
{
    if (g_started) {
        ESP_LOGW(TAG, "wifi_start called twice; ignoring");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_ERROR_CHECK(nvs_init_once());

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    g_evt = xEventGroupCreate();
    g_cred_mutex = xSemaphoreCreateMutex();
    g_scan_mutex = xSemaphoreCreateMutex();
    if (g_evt == NULL || g_cred_mutex == NULL || g_scan_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    load_creds_from_nvs();

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (xTaskCreate(wifi_task, "wifi_reconnect", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    g_started = true;
    return ESP_OK;
}

bool wifi_is_connected(void)
{
    return g_connected;
}

esp_err_t wifi_creds_set(int slot, const char *ssid, const char *pass)
{
    if (slot < 0 || slot >= WIFI_MAX_NETWORKS || ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(ssid) >= WIFI_SSID_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pass != NULL && strlen(pass) >= WIFI_PASS_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open("wifi", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    char key[8];
    snprintf(key, sizeof key, "ssid%d", slot);
    err = nvs_set_str(h, key, ssid);
    if (err == ESP_OK) {
        snprintf(key, sizeof key, "pass%d", slot);
        err = nvs_set_str(h, key, pass != NULL ? pass : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        return err;
    }

    /* NOTE for the integrator (AGENTS.md §7): NVS commits are known to tear
     * this panel (espressif/esp-bsp#570) -- pause LVGL around this call from
     * whichever M6 provisioning screen ends up invoking it. This module
     * stays UI-agnostic on purpose and cannot call display_lock() itself. */

    xSemaphoreTake(g_cred_mutex, portMAX_DELAY);
    strncpy(g_creds[slot].ssid, ssid, sizeof g_creds[slot].ssid - 1);
    g_creds[slot].ssid[sizeof g_creds[slot].ssid - 1] = '\0';
    strncpy(g_creds[slot].pass, pass != NULL ? pass : "", sizeof g_creds[slot].pass - 1);
    g_creds[slot].pass[sizeof g_creds[slot].pass - 1] = '\0';
    xSemaphoreGive(g_cred_mutex);

    ESP_LOGI(TAG, "stored network in slot %d: %s", slot, ssid); /* never logs pass */
    return ESP_OK;
}

esp_err_t wifi_creds_list(char out[][WIFI_SSID_LEN], int max)
{
    if (out == NULL || max <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(g_cred_mutex, portMAX_DELAY);
    int n = (max < WIFI_MAX_NETWORKS) ? max : WIFI_MAX_NETWORKS;
    for (int i = 0; i < n; i++) {
        strncpy(out[i], g_creds[i].ssid, WIFI_SSID_LEN - 1);
        out[i][WIFI_SSID_LEN - 1] = '\0';
    }
    xSemaphoreGive(g_cred_mutex);

    for (int i = n; i < max; i++) {
        out[i][0] = '\0';
    }
    return ESP_OK;
}

int wifi_scan(char out[][WIFI_SSID_LEN], int max)
{
    if (out == NULL || max <= 0) {
        return -1;
    }

    xSemaphoreTake(g_scan_mutex, portMAX_DELAY);
    wifi_scan_config_t cfg = { 0 };
    esp_err_t err = esp_wifi_scan_start(&cfg, true);
    if (err != ESP_OK) {
        xSemaphoreGive(g_scan_mutex);
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        return -1;
    }

    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    if (num == 0) {
        xSemaphoreGive(g_scan_mutex);
        return 0;
    }

    wifi_ap_record_t *recs = malloc(sizeof(wifi_ap_record_t) * num);
    if (recs == NULL) {
        xSemaphoreGive(g_scan_mutex);
        ESP_LOGE(TAG, "out of memory for %u scan results", (unsigned)num);
        return -1;
    }
    err = esp_wifi_scan_get_ap_records(&num, recs);
    xSemaphoreGive(g_scan_mutex);
    if (err != ESP_OK) {
        free(recs);
        return -1;
    }

    int count = 0;
    for (int i = 0; i < (int)num && count < max; i++) {
        const char *ssid = (const char *)recs[i].ssid;
        if (ssid[0] == '\0') {
            continue; /* hidden network -- nothing to show */
        }
        bool dup = false;
        for (int j = 0; j < count; j++) {
            if (strcmp(out[j], ssid) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        strncpy(out[count], ssid, WIFI_SSID_LEN - 1);
        out[count][WIFI_SSID_LEN - 1] = '\0';
        count++;
    }
    free(recs);
    return count;
}
