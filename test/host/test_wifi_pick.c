/* main/net/wifi_pick.c — which remembered network to join, and which slot a
 * new one may take (D91).
 *
 * The owner's report, as a sequence: on network A, join B with its password,
 * then tap A to go back. Before D91 that tap landed on B again whenever B was
 * the louder of the two, and a full list had already given A's slot to B.
 */
#include <string.h>

#include "test_util.h"
#include "wifi_pick.h"

#define U WIFI_PICK_UNSEEN

int main(void)
{
    GROUP("the chosen network wins while it is in range, even when weaker");
    {
        const char *ssid[] = { "Heimnetz", "Nachbar-Gast", "", "" };
        int rssi[]         = { -78, -45, U, U };
        CHECK_INT(wifi_pick_join(ssid, rssi, 4, "Heimnetz", true), 0);
        CHECK_INT(wifi_pick_join(ssid, rssi, 4, "Nachbar-Gast", true), 1);
    }

    GROUP("the owner's sequence: A, then B, then back to A");
    {
        const char *ssid[] = { "A", "B", "", "" };
        int rssi[]         = { -70, -50, U, U };      /* B is the louder */
        CHECK_INT(wifi_pick_join(ssid, rssi, 4, "B", true), 1);   /* joined B */
        CHECK_INT(wifi_pick_join(ssid, rssi, 4, "A", true), 0);   /* tapped A: A */
    }

    GROUP("the chosen network out of range: the strongest remembered one");
    {
        /* The device has travelled; the chosen network is on another continent. */
        const char *ssid[] = { "Wien", "Pattaya", "Hotspot", "" };
        int rssi[]         = { U, -60, -72, U };
        CHECK_INT(wifi_pick_join(ssid, rssi, 4, "Wien", true), 1);
    }

    GROUP("after the chosen network failed: one attempt on the strongest");
    {
        const char *ssid[] = { "A", "B" };
        int rssi[]         = { -70, -50 };
        CHECK_INT(wifi_pick_join(ssid, rssi, 2, "A", false), 1);
    }

    GROUP("nothing chosen yet: the strongest, ties to the lower slot");
    {
        const char *ssid[] = { "A", "B", "C" };
        int rssi[]         = { -60, -50, -50 };
        CHECK_INT(wifi_pick_join(ssid, rssi, 3, "", true), 1);
        CHECK_INT(wifi_pick_join(ssid, rssi, 3, NULL, true), 1);
    }

    GROUP("no remembered network in range, and empty slots never picked");
    {
        const char *ssid[] = { "A", "", "C" };
        int rssi[]         = { U, -30, U };      /* an empty slot "seen" is not a network */
        CHECK_INT(wifi_pick_join(ssid, rssi, 3, "A", true), -1);
        CHECK_INT(wifi_pick_join(NULL, rssi, 3, "A", true), -1);
        CHECK_INT(wifi_pick_join(ssid, NULL, 3, "A", true), -1);
    }

    GROUP("storing: the SSID's own slot, then a free one");
    {
        const char *ssid[]  = { "A", "", "C", "" };
        uint32_t    seq[]   = { 5, 0, 7, 0 };
        CHECK_INT(wifi_pick_store(ssid, seq, 4, "C", "A"), 2);    /* a new password for C */
        CHECK_INT(wifi_pick_store(ssid, seq, 4, "D", "A"), 1);
    }

    GROUP("storing into a full list: the one chosen longest ago goes");
    {
        /* Before D91 this was always slot 3. */
        const char *ssid[] = { "Wien", "Pattaya", "Hotspot", "A" };
        uint32_t    seq[]  = { 9, 8, 2, 10 };
        CHECK_INT(wifi_pick_store(ssid, seq, 4, "B", "A"), 2);
    }

    GROUP("storing into a full list: never the network in use");
    {
        const char *ssid[] = { "A", "B", "C", "D" };
        uint32_t    seq[]  = { 1, 5, 6, 7 };
        CHECK_INT(wifi_pick_store(ssid, seq, 4, "E", "A"), 1);
        CHECK_INT(wifi_pick_store(ssid, seq, 4, "E", ""), 0);
        CHECK_INT(wifi_pick_store(ssid, seq, 4, "E", NULL), 0);
    }

    GROUP("networks stored before D91 have no record: they go first, lowest slot");
    {
        const char *ssid[] = { "A", "B", "C", "D" };
        uint32_t    seq[]  = { 0, 0, 3, 0 };
        CHECK_INT(wifi_pick_store(ssid, seq, 4, "E", "A"), 1);
    }

    GROUP("defensive");
    {
        const char *ssid[] = { "A" };
        uint32_t    seq[]  = { 1 };
        CHECK_INT(wifi_pick_store(ssid, seq, 1, "B", "A"), -1);   /* all protected */
        CHECK_INT(wifi_pick_store(NULL, seq, 1, "B", ""), -1);
        CHECK_INT(wifi_pick_store(ssid, NULL, 1, "B", ""), -1);
        CHECK_INT(wifi_pick_store(ssid, seq, 0, "B", ""), -1);
    }

    return test_summary();
}
