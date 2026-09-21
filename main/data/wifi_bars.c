/* wifi_bars.c — see wifi_bars.h for where the numbers came from. */
#include "wifi_bars.h"

/* The window a 2.4 GHz receiver can physically report.
 *
 * -100 dBm is roughly the noise floor of this radio; anything below it is not
 * a signal, it is a driver returning a default. The upper end is -10 rather
 * than 0 because a station sitting ON the antenna still reads about -20, so a
 * value above -10 is a bug somewhere upstream and not something to draw four
 * bars for. Both are treated as "no reading" rather than clamped: a meter
 * that quietly rounds nonsense into a plausible answer is how a wrong number
 * survives (AGENTS.md §11). */
#define RSSI_FLOOR   (-100)
#define RSSI_CEILING (-10)

int wifi_bars(int rssi_dbm)
{
    if (rssi_dbm == WIFI_RSSI_NONE ||
        rssi_dbm < RSSI_FLOOR || rssi_dbm > RSSI_CEILING) {
        return 0;
    }
    /* Descending, so the first match wins and the boundaries read in the same
     * order as the ladder they describe. */
    if (rssi_dbm >= -60) {
        return 4;   /* comfortable — the poll is never the bottleneck here */
    }
    if (rssi_dbm >= -70) {
        return 3;   /* fine */
    }
    if (rssi_dbm >= -79) {
        return 2;   /* measured at -74/-76: the 16 KB poll takes ~900 ms */
    }
    return 1;       /* measured at -80/-81: the same poll mostly never finishes */
}
