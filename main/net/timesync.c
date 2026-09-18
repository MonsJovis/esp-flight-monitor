#include "timesync.h"
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

static const char *TAG = "timesync";
static bool s_synced;

static void on_sync(struct timeval *tv)
{
    (void)tv;
    s_synced = true;
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    /* Deliberately ASCII here: this is a log line, not a panel string. */
    ESP_LOGI(TAG, "clock set: %04d-%02d-%02d %02d:%02d local",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
}

void timesync_set_tz(const char *posix_tz)
{
    setenv("TZ", posix_tz, 1);
    tzset();
    ESP_LOGI(TAG, "timezone set to %s", posix_tz);
}

esp_err_t timesync_start(const char *posix_tz)
{
    timesync_set_tz(posix_tz);

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = on_sync;
    cfg.start = true;
    cfg.server_from_dhcp = true;      /* a hotel router often blocks the rest */
    cfg.renew_servers_after_new_IP = true;
    return esp_netif_sntp_init(&cfg);
}

bool timesync_is_set(void) { return s_synced; }
