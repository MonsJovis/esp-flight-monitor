/* Which remembered network to join, and which slot a new one goes into.
 *
 * Pure — no ESP-IDF, no NVS — so both rules run on the host (test_wifi_pick.c).
 * wifi.c holds the radio and the storage and asks these two functions.
 *
 * THE OWNER, D91: "we connected to a wifi, it worked, then we connected to
 * another wifi, and wanted to change back to the old wifi and had to reenter
 * the password." Two separate things did that, one per function below.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* rssi[] value for a remembered network that the scan did not see. */
#define WIFI_PICK_UNSEEN (-1000)

/* The slot to join, or -1 when no remembered network is in range.
 *
 * `ssid[i]` is slot i's SSID ("" for an empty slot), `rssi[i]` the strongest
 * signal the scan saw for it, or WIFI_PICK_UNSEEN.
 *
 * The network he last CHOSE (`prefer`) wins whenever it is in range, however
 * weak. Before D91 the strongest remembered network always won, so tapping
 * the old network to go back to it re-scanned and landed on the new one
 * again, because the new one was louder. The strongest is the fallback: for a
 * device that has travelled to where the chosen one does not exist, and for
 * `honour_prefer` false, which wifi.c passes for one attempt after the chosen
 * network failed, so a changed password on it cannot strand the device off a
 * network that works. Ties go to the lower slot.
 */
int wifi_pick_join(const char *const ssid[], const int rssi[], int n,
                   const char *prefer, bool honour_prefer);

/* The slot to store `new_ssid` in: its own slot if it is already remembered,
 * else the first empty one, else the one chosen longest ago (smallest
 * `seq[i]`), never `protect` (the network the device is on right now).
 *
 * Before D91 a full list always gave up slot 3, whatever was in it — the
 * network he had used an hour ago as readily as one from last year — and that
 * network then showed as unsaved and asked for its password again.
 *
 * Returns -1 only when n <= 0 or everything is protected.
 */
int wifi_pick_store(const char *const ssid[], const uint32_t seq[], int n,
                    const char *new_ssid, const char *protect);

#ifdef __cplusplus
}
#endif
