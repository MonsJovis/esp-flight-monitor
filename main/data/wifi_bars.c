/* wifi_bars.c — see wifi_bars.h for where the numbers came from. */
#include "wifi_bars.h"

/* The window a 2.4 GHz receiver can physically report.
 *
 * THE FLOOR IS -120, NOT -100, and the difference is a real reading. It was
 * -100 on the reasoning that the radio's noise floor is about there — true of
 * a link, false of a SCAN. An ESP32 beacon scan routinely reports -101 to
 * -105 for something at the far end of a building, and those came back as
 * zero bars, which this header promises means "nothing measured". An empty
 * meter beside a network the radio demonstrably heard is the exact confusion
 * the zero is reserved to avoid. -120 is below anything a receiver reports
 * and above the int8_t sentinels a driver falls back to.
 *
 * The upper end is -10 rather than 0 because a station sitting ON the antenna
 * still reads about -20, so anything above -10 is a bug upstream and not
 * something to draw four bars for. Both ends are treated as "no reading"
 * rather than clamped: a meter that quietly rounds nonsense into a plausible
 * answer is how a wrong number survives (AGENTS.md §11). */
#define RSSI_FLOOR   (-120)
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
