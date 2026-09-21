/* How many bars a received-signal strength is worth — the one rule, for the
 * whole device.
 *
 * Two places draw a signal meter: the chrome corner of the deck (nav.c, the
 * link this device is using right now) and every row of the WLAN list
 * (screen_wifi.c, a network it could use). They must agree, or the same
 * router reads three bars on one screen and two on the other and the meter
 * stops being worth looking at. So the thresholds live here, once, and both
 * call in.
 *
 * It is in main/data/ rather than next to main/net/wifi.c for the same reason
 * battery_policy.c sits beside axp2101.c: this is a judgement, not a driver.
 * It touches no esp_* header, so the host suite compiles it and test_wifi_bars.c
 * pins the boundaries — which matters more here than it looks, because the
 * numbers below are NOT the textbook ones.
 *
 * THE THRESHOLDS ARE CALIBRATED TO THIS RADIO, and were measured on it
 * (DECISIONS.md D70). A phone's bars are a generous scale over two or three
 * antennas with diversity; this is one chip antenna on a 4 cm board, and the
 * gap between them is a real 5 to 10 dB. On 2026-09-21, in the apartment this
 * device lives in:
 *
 *     -74 to -76 dBm   the real 16 KB aircraft poll completes in ~900 ms
 *     -80 to -81 dBm   the same request takes 10 to 60 s and usually does not
 *                      finish at all
 *
 * Five decibels, and the device either works or does not. So the 2 -> 1
 * boundary is put at -79, which makes ONE BAR MEAN "measured unusable on this
 * hardware" rather than "weak but fine". That is the only fact the meter is
 * there to deliver: when the screen stops filling, the corner says why, and
 * the answer is to move the device — not to go and look at the router.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* The height of the ladder. Four, because that is what the hardware it draws
 * on can show legibly at 18 px wide, and what every phone he has ever held
 * uses. */
#define WIFI_BARS_MAX 4

/* No reading at all: not associated, or the driver handed back something that
 * cannot be a received power. A received signal is always negative dBm, so 0
 * is free to mean "nothing measured" without colliding with a real value. */
#define WIFI_RSSI_NONE 0

/* Returns 0..WIFI_BARS_MAX.
 *
 * Zero is reserved for "there is no reading", NOT for "the signal is very
 * weak" — a network that a scan can see is a network with some signal, and
 * showing it as empty would say the radio heard nothing, which is exactly
 * what it did not do. Anything a scan reports gets at least one bar; only
 * WIFI_RSSI_NONE and values outside what a 2.4 GHz receiver can physically
 * report come back as 0. */
int wifi_bars(int rssi_dbm);

#ifdef __cplusplus
}
#endif
