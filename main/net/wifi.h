/* Station-mode WiFi: event-driven connect state, auto-reconnect with
 * exponential backoff, and a list of remembered networks in NVS.
 *
 * The device travels between Austria and Thailand twice a year (AGENTS.md
 * §6), so credentials are a LIST, not a single SSID: on every (re)connect
 * attempt this module scans and picks whichever remembered network is
 * actually in range, rather than assuming the last-used one is still
 * reachable. Credentials live in NVS namespace "wifi" (keys "ssidN"/"passN",
 * N = 0..WIFI_MAX_NETWORKS-1) — never hard-coded, never written anywhere
 * else in the repo, and never logged (AGENTS.md §10).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MAX_NETWORKS 4
#define WIFI_SSID_LEN     33  /* 32 + NUL — IEEE 802.11 max SSID length */

/* Brings up NVS + netif + the default event loop + the WiFi driver in
 * station mode, loads the remembered-network list, and starts a background
 * task that connects to whichever known SSID is in range and reconnects
 * with exponential backoff on drop (a router reboot must recover
 * unattended). Safe to call once; a second call logs a warning and returns
 * ESP_ERR_INVALID_STATE. */
esp_err_t wifi_start(void);

/* True once an IP address has been obtained on the current association;
 * false immediately on disconnect, before any reconnect attempt starts. */
bool wifi_is_connected(void);

/* Stores network `slot` (0..WIFI_MAX_NETWORKS-1) in NVS. Never logs `pass`.
 * Picked up by the connect task's next scan pass; does not force an
 * immediate reconnect or drop an existing connection. `pass` may be NULL or
 * "" for an open network. Returns ESP_ERR_INVALID_ARG for an out-of-range
 * slot or an SSID/password too long to fit its NVS field. */
esp_err_t wifi_creds_set(int slot, const char *ssid, const char *pass);

/* Writes exactly min(max, WIFI_MAX_NETWORKS) entries into
 * out[][WIFI_SSID_LEN] — SSIDs only, NEVER passwords (AGENTS.md §10). An
 * unconfigured slot comes back as "". Any remaining out[] entries beyond
 * WIFI_MAX_NETWORKS (if max > WIFI_MAX_NETWORKS) are left untouched by this
 * call. */
esp_err_t wifi_creds_list(char out[][WIFI_SSID_LEN], int max);

/* The received signal strength of the association right now, in dBm, or
 * WIFI_RSSI_NONE (main/data/wifi_bars.h) when there is no association to
 * measure.
 *
 * Always negative when it is real. Cheap — the driver keeps the figure from
 * the beacons it is already receiving, so this is a read, not a measurement,
 * and calling it on every UI tick costs nothing.
 *
 * It exists because "my phone has two bars, why does this thing not work?" is
 * a real question with a measurable answer, and for most of this build there
 * was no way to ask it from anywhere but a log line. main/data/wifi_bars.h
 * turns the number into the four bars the panel draws, and records what was
 * measured off this radio to put the boundaries where they are. */
int wifi_rssi(void);

/* Blocking scan; copies up to `max` visible, de-duplicated SSIDs (hidden
 * networks omitted) into out[][WIFI_SSID_LEN]. Returns the count found, or
 * -1 on error. For the M6 on-device provisioning screen (AGENTS.md §8) —
 * shows what's actually in range rather than asking for a typed SSID.
 *
 * `rssi_out`, when not NULL, receives the signal strength in dBm for each
 * SSID written, in the same order. A separate array rather than a struct
 * because the UI side deliberately does not link against this header
 * (screen_wifi.h) and moves plain arrays around.
 *
 * SORTED STRONGEST FIRST, which also decides which duplicate survives. The
 * same SSID routinely appears more than once — this device lives in an
 * apartment whose network is on channel 1 and channel 11, i.e. a router and a
 * repeater — and the de-duplication keeps the first one it sees. Unsorted,
 * that was whichever the radio happened to report first, so the list could
 * show the far end of the flat while the near one was the stronger by 15 dB.
 * Sorting first makes "the first one" mean "the best one", and puts the list
 * in the order he is choosing from anyway. */
int wifi_scan(char out[][WIFI_SSID_LEN], int8_t rssi_out[], int max);

/* Abandon the current backoff and try to associate right now. Called after new
 * credentials are stored: the reconnect loop may be 60 s into a wait, and
 * nobody should have to stand there after typing a password. */
void wifi_reconnect_now(void);

#ifdef __cplusplus
}
#endif
