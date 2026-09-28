#include "wifi_pick.h"

#include <stddef.h>
#include <string.h>

static bool filled(const char *s)
{
    return s != NULL && s[0] != '\0';
}

int wifi_pick_join(const char *const ssid[], const int rssi[], int n,
                   const char *prefer, bool honour_prefer)
{
    if (ssid == NULL || rssi == NULL) {
        return -1;
    }
    if (honour_prefer && filled(prefer)) {
        for (int i = 0; i < n; i++) {
            if (filled(ssid[i]) && rssi[i] != WIFI_PICK_UNSEEN &&
                strcmp(ssid[i], prefer) == 0) {
                return i;
            }
        }
    }
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (filled(ssid[i]) && rssi[i] != WIFI_PICK_UNSEEN &&
            (best < 0 || rssi[i] > rssi[best])) {
            best = i;
        }
    }
    return best;
}

int wifi_pick_store(const char *const ssid[], const uint32_t seq[], int n,
                    const char *new_ssid, const char *protect)
{
    if (ssid == NULL || seq == NULL || n <= 0) {
        return -1;
    }
    if (filled(new_ssid)) {
        for (int i = 0; i < n; i++) {
            if (filled(ssid[i]) && strcmp(ssid[i], new_ssid) == 0) {
                return i;
            }
        }
    }
    for (int i = 0; i < n; i++) {
        if (!filled(ssid[i])) {
            return i;
        }
    }
    int oldest = -1;
    for (int i = 0; i < n; i++) {
        if (filled(protect) && strcmp(ssid[i], protect) == 0) {
            continue;
        }
        if (oldest < 0 || seq[i] < seq[oldest]) {
            oldest = i;
        }
    }
    return oldest;
}
