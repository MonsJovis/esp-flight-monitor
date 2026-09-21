/* main/data/wifi_bars.c — the dBm -> bars ladder both signal meters read.
 *
 * Worth a suite of its own for a reason that is easy to miss: the boundaries
 * are NOT round numbers and NOT the textbook ones. They were measured on this
 * device's own radio (wifi_bars.h, DECISIONS.md D70), and the interesting one
 * is -79/-80 — the five decibels where the aircraft poll goes from 900 ms to
 * not finishing. A future tidy-up that "rounds" that to -80 would move the
 * meaning of one bar off the measurement it was built on, silently, and
 * nothing on the panel would look wrong.
 */
#include "test_util.h"
#include "wifi_bars.h"

static void test_ladder(void)
{
    GROUP("the four rungs");

    /* Four: comfortable. */
    CHECK_INT(wifi_bars(-10), 4);
    CHECK_INT(wifi_bars(-35), 4);
    CHECK_INT(wifi_bars(-60), 4);

    /* Three. */
    CHECK_INT(wifi_bars(-61), 3);
    CHECK_INT(wifi_bars(-65), 3);
    CHECK_INT(wifi_bars(-70), 3);

    /* Two — and this is the band the device was measured working in. */
    CHECK_INT(wifi_bars(-71), 2);
    CHECK_INT(wifi_bars(-74), 2);
    CHECK_INT(wifi_bars(-76), 2);
    CHECK_INT(wifi_bars(-79), 2);

    /* One — measured unusable on this hardware. */
    CHECK_INT(wifi_bars(-80), 1);
    CHECK_INT(wifi_bars(-81), 1);
    CHECK_INT(wifi_bars(-95), 1);
    CHECK_INT(wifi_bars(-100), 1);
    /* A beacon scan across a building really does report these, and they are
     * readings, not noise. The floor was -100 and drew them as an empty
     * meter — "nothing measured" — which is the one thing zero is reserved
     * for. Caught by review, not by the panel. */
    CHECK_INT(wifi_bars(-101), 1);
    CHECK_INT(wifi_bars(-105), 1);
    CHECK_INT(wifi_bars(-120), 1);
}

static void test_measured_boundary(void)
{
    GROUP("the boundary that carries the meaning");

    /* The whole point of the ladder, stated as a test rather than a comment:
     * the two readings taken off this device on 2026-09-21 must land on
     * DIFFERENT rungs, or the meter cannot tell him the one thing it exists
     * to tell him. -76 dBm was a working link (~900 ms for the real poll);
     * -81 dBm was a link that mostly did not complete a request at all. */
    CHECK(wifi_bars(-76) > wifi_bars(-81));
    CHECK_INT(wifi_bars(-76), 2);
    CHECK_INT(wifi_bars(-81), 1);
}

static void test_no_reading(void)
{
    GROUP("no reading is not a weak reading");

    /* Zero is the sentinel, not a very strong signal. */
    CHECK_INT(wifi_bars(WIFI_RSSI_NONE), 0);
    CHECK_INT(wifi_bars(0), 0);

    /* Above what a receiver can report, and below its noise floor: both are
     * "nothing measured". Deliberately NOT clamped into the ladder — a meter
     * that turns nonsense into a plausible answer is how a wrong number
     * survives. */
    CHECK_INT(wifi_bars(5), 0);
    CHECK_INT(wifi_bars(-9), 0);
    CHECK_INT(wifi_bars(-121), 0);
    CHECK_INT(wifi_bars(-128), 0);   /* an int8_t sentinel, not a measurement */

    /* And the two values immediately inside the window still read. */
    CHECK_INT(wifi_bars(-10), 4);
    CHECK_INT(wifi_bars(-120), 1);
}

static void test_monotonic(void)
{
    GROUP("monotonic, and never out of range");

    /* Stronger is never fewer bars, across the whole plausible window. A
     * threshold typed in the wrong order would pass every spot check above
     * and fail here. */
    int prev = 0;
    for (int dbm = -120; dbm <= -10; dbm++) {
        int b = wifi_bars(dbm);
        CHECK(b >= 1 && b <= WIFI_BARS_MAX);
        CHECK(b >= prev);
        prev = b;
    }
    CHECK_INT(prev, WIFI_BARS_MAX);
}

int main(void)
{
    printf("\n== wifi signal bars\n");
    test_ladder();
    test_measured_boundary();
    test_no_reading();
    test_monotonic();
    return test_summary();
}
