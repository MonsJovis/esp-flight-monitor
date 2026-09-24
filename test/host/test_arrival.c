/* The arrival estimate (arrival.h, D79).
 *
 * Every rule here exists because the simple version is wrong in a way that
 * looks right on the panel: a clean "Landung in etwa 34 Min." for a flight
 * that lands in 45, or a confident time for an aircraft still climbing away
 * from its runway. The Flightradar24 example the owner photographed is the
 * one real cross-check in here.
 */
#include <string.h>

#include "test_util.h"
#include "arrival.h"
#include "fmt_de.h"

static route_t rt(float olat, float olon, float dlat, float dlon)
{
    route_t r;
    memset(&r, 0, sizeof r);
    r.resolved = r.plausible = r.has_coords = true;
    r.orig_lat = olat; r.orig_lon = olon; r.dest_lat = dlat; r.dest_lon = dlon;
    return r;
}

static aircraft_t ac(double lat, double lon, int32_t alt_ft, int gs_kt, float track)
{
    aircraft_t a;
    memset(&a, 0, sizeof a);
    a.lat = lat; a.lon = lon; a.alt_ft = alt_ft; a.gs_kt = gs_kt;
    a.track_deg = track; a.has_track = true;
    return a;
}

/* Ordu (OGU) -> Stuttgart (STR), the owner's screenshot. */
#define OGU_LAT 40.9661f
#define OGU_LON 38.0800f
#define STR_LAT 48.6899f
#define STR_LON 9.2220f

int main(void)
{
    GROUP("great-circle distance: nautical miles, and the right ones");
    {
        /* Vienna -> Frankfurt, the routeset's own coordinates: ~335 nm. */
        CHECK_NEAR(arrival_gc_nm(48.110298f, 16.5697f, 50.026402f, 8.54313f), 335.0, 5.0);
        CHECK_NEAR(arrival_gc_nm(48.0f, 16.0f, 48.0f, 16.0f), 0.0, 1e-3);
        /* One degree of latitude is 60 nm. */
        CHECK_NEAR(arrival_gc_nm(48.0f, 16.0f, 49.0f, 16.0f), 60.0, 0.2);
    }

    GROUP("the owner's example: 483 km out at cruise, FR24 said 45 min");
    {
        /* 483 km = 260.8 nm east of STR on its parallel, westbound at 460 kt. */
        double lon = STR_LON + 260.8 / (60.0 * cos(STR_LAT * 0.017453292519943295));
        route_t r  = rt(OGU_LAT, OGU_LON, STR_LAT, STR_LON);
        aircraft_t a = ac(STR_LAT, lon, 37000, 460, 270.0f);
        int m = arrival_minutes(&a, &r);
        /* 220.8 nm at 460 kt + 40 nm at 200 kt = 28.8 + 12.0 = 40.8 min.
         * Straight distance over cruise speed would have said 34. */
        CHECK_INT(m, 41);
        char buf[80];
        fmt_arrival_de(m, buf, sizeof buf);
        CHECK_STR(buf, "Landung in etwa 40 Min.");
    }

    GROUP("on approach: slower than cruise, so the approach speed governs");
    {
        route_t r = rt(OGU_LAT, OGU_LON, STR_LAT, STR_LON);
        /* 20 nm south of STR, 220 kt, 6 000 ft: 20 nm at 200 kt = 6 min. */
        aircraft_t a = ac(STR_LAT - 20.0 / 60.0, STR_LON, 6000, 220, 0.0f);
        CHECK_INT(arrival_minutes(&a, &r), 6);
        /* A turboprop at 160 kt keeps its own, lower speed: 7.5 min. */
        a.gs_kt = 160;
        CHECK_INT(arrival_minutes(&a, &r), 8);
    }

    GROUP("inside 2 nm it is landing now");
    {
        route_t r = rt(OGU_LAT, OGU_LON, STR_LAT, STR_LON);
        aircraft_t a = ac(STR_LAT + 1.0 / 60.0, STR_LON, 400, 140, 180.0f);
        CHECK_INT(arrival_minutes(&a, &r), 0);
        char buf[80];
        fmt_arrival_de(0, buf, sizeof buf);
        CHECK_STR(buf, "Landung in wenigen Minuten");
    }

    GROUP("climbing out: nothing, until it is high or past halfway");
    {
        /* 10 nm out of Vienna for Frankfurt, 8 000 ft, 280 kt. */
        route_t r = rt(48.110298f, 16.5697f, 50.026402f, 8.54313f);
        aircraft_t a = ac(48.110298, 16.5697 - 10.0 / 40.0, 8000, 280, 290.0f);
        CHECK_INT(arrival_minutes(&a, &r), -1);
        /* The same spot at cruise height is fine. */
        a.alt_ft = 24000;
        CHECK(arrival_minutes(&a, &r) > 0);
    }

    GROUP("flying away from its destination: the route is probably wrong");
    {
        route_t r = rt(OGU_LAT, OGU_LON, STR_LAT, STR_LON);
        double lon = STR_LON + 200.0 / (60.0 * cos(STR_LAT * 0.017453292519943295));
        aircraft_t a = ac(STR_LAT, lon, 37000, 460, 90.0f);   /* eastbound, STR is west */
        CHECK_INT(arrival_minutes(&a, &r), -1);
        /* ...but on a downwind leg 15 nm out, pointing away is normal. */
        aircraft_t d = ac(STR_LAT, STR_LON + 15.0 / 40.0, 4000, 180, 90.0f);
        CHECK(arrival_minutes(&d, &r) > 0);
    }

    GROUP("nothing without what the estimate stands on");
    {
        route_t r = rt(OGU_LAT, OGU_LON, STR_LAT, STR_LON);
        aircraft_t a = ac(STR_LAT, STR_LON + 3.0, 37000, 460, 270.0f);
        CHECK(arrival_minutes(&a, &r) > 0);                      /* baseline */
        route_t x = r; x.has_coords = false;   CHECK_INT(arrival_minutes(&a, &x), -1);
        x = r; x.resolved = false;              CHECK_INT(arrival_minutes(&a, &x), -1);
        x = r; x.plausible = false;             CHECK_INT(arrival_minutes(&a, &x), -1);
        CHECK_INT(arrival_minutes(&a, NULL), -1);
        aircraft_t b = a; b.alt_ft = ALT_GROUND;  CHECK_INT(arrival_minutes(&b, &r), -1);
        b = a; b.alt_ft = ALT_UNKNOWN;             CHECK_INT(arrival_minutes(&b, &r), -1);
        b = a; b.gs_kt = 40;                       CHECK_INT(arrival_minutes(&b, &r), -1);
        b = a; b.gs_kt = -1;                       CHECK_INT(arrival_minutes(&b, &r), -1);
        b = a; b.lat = 0.0; b.lon = 0.0;           CHECK_INT(arrival_minutes(&b, &r), -1);
    }

    GROUP("more than 18 hours is not an estimate, it is a bug somewhere");
    {
        route_t r = rt(OGU_LAT, OGU_LON, STR_LAT, STR_LON);
        aircraft_t a = ac(STR_LAT, STR_LON + 30.0, 37000, 60, 270.0f);  /* ~1190 nm at 60 kt */
        CHECK_INT(arrival_minutes(&a, &r), -1);
    }

    GROUP("the words: abbreviated units (the owner's call), and precision falls as the number grows");
    {
        char b[80];
        fmt_arrival_de(-1, b, sizeof b);  CHECK_STR(b, "");
        fmt_arrival_de(2, b, sizeof b);   CHECK_STR(b, "Landung in wenigen Minuten");
        fmt_arrival_de(3, b, sizeof b);   CHECK_STR(b, "Landung in etwa 3 Min.");
        fmt_arrival_de(14, b, sizeof b);  CHECK_STR(b, "Landung in etwa 14 Min.");
        fmt_arrival_de(17, b, sizeof b);  CHECK_STR(b, "Landung in etwa 15 Min.");
        fmt_arrival_de(18, b, sizeof b);  CHECK_STR(b, "Landung in etwa 20 Min.");
        fmt_arrival_de(58, b, sizeof b);  CHECK_STR(b, "Landung in etwa 1 Std.");
        fmt_arrival_de(62, b, sizeof b);  CHECK_STR(b, "Landung in etwa 1 Std.");
        fmt_arrival_de(100, b, sizeof b); CHECK_STR(b, "Landung in etwa 1 Std. 40 Min.");
        fmt_arrival_de(124, b, sizeof b); CHECK_STR(b, "Landung in etwa 2 Std. 5 Min.");
        fmt_arrival_de(180, b, sizeof b); CHECK_STR(b, "Landung in etwa 3 Std.");
    }

    GROUP("distance from the origin: straight line, whole km, and not at the airport");
    {
        route_t r = rt(OGU_LAT, OGU_LON, STR_LAT, STR_LON);
        /* The stand-in for the owner's example (483 km east of STR on its
         * parallel — not on the real track, so it cannot show that a straight
         * line is shorter than the 1,938 km FR24 said were flown). Its
         * straight-line distance from Ordu, computed independently of this
         * code with Python's haversine: 1,946 km. */
        double lon = STR_LON + 260.8 / (60.0 * cos(STR_LAT * 0.017453292519943295));
        aircraft_t a = ac(STR_LAT, lon, 37000, 460, 270.0f);
        int km = departed_km(&a, &r);
        CHECK(km >= 1945 && km <= 1947);
        char b[96];
        fmt_from_origin_de(1938, "Ordu", b, sizeof b);
        CHECK_STR(b, "1.938 km von Ordu entfernt");
        /* Still at (or just off) the origin: nothing worth saying. */
        aircraft_t g = ac(OGU_LAT, OGU_LON + 0.05, 0, 10, 0.0f);
        CHECK_INT(departed_km(&g, &r), -1);
        /* No coordinates, no position, no route: nothing. */
        route_t x = r; x.has_coords = false; CHECK_INT(departed_km(&a, &x), -1);
        aircraft_t z = a; z.lat = 0.0; z.lon = 0.0; CHECK_INT(departed_km(&z, &r), -1);
        CHECK_INT(departed_km(&a, NULL), -1);
        fmt_from_origin_de(-1, "Ordu", b, sizeof b); CHECK_STR(b, "");
        fmt_from_origin_de(120, "", b, sizeof b);    CHECK_STR(b, "");
    }

    GROUP("ground speed: km/h, to 10, German grouping, nothing when absent");
    {
        char b[32];
        fmt_speed_kmh(460, b, sizeof b); CHECK_STR(b, "850 km/h");     /* 851.9 */
        fmt_speed_kmh(551, b, sizeof b); CHECK_STR(b, "1.020 km/h");   /* 1020.5 */
        fmt_speed_kmh(7, b, sizeof b);   CHECK_STR(b, "10 km/h");      /* taxiing: 13 */
        fmt_speed_kmh(0, b, sizeof b);   CHECK_STR(b, "0 km/h");
        fmt_speed_kmh(-1, b, sizeof b);  CHECK_STR(b, "");
    }

    return test_summary();
}
