#include "arrival.h"

#include <math.h>

#define DEG2RAD 0.017453292519943295
#define EARTH_NM 3440.065

float arrival_gc_nm(float lat1, float lon1, float lat2, float lon2)
{
    double p1 = lat1 * DEG2RAD, p2 = lat2 * DEG2RAD;
    double dp = (lat2 - lat1) * DEG2RAD, dl = (lon2 - lon1) * DEG2RAD;
    double a  = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    return (float)(2.0 * EARTH_NM * atan2(sqrt(a), sqrt(1.0 - a)));
}

/* Initial great-circle bearing from 1 to 2, degrees 0..360. */
static float bearing_deg(float lat1, float lon1, float lat2, float lon2)
{
    double p1 = lat1 * DEG2RAD, p2 = lat2 * DEG2RAD, dl = (lon2 - lon1) * DEG2RAD;
    double y = sin(dl) * cos(p2);
    double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    double b = atan2(y, x) / DEG2RAD;
    return (float)fmod(b + 360.0, 360.0);
}

int departed_km(const aircraft_t *ac, const route_t *route)
{
    if (ac == NULL || route == NULL || !route->resolved || !route->plausible ||
        !route->has_coords) {
        return -1;
    }
    if (ac->lat == 0.0 && ac->lon == 0.0) {
        return -1;
    }
    float nm = arrival_gc_nm((float)ac->lat, (float)ac->lon, route->orig_lat, route->orig_lon);
    long  km = lroundf(nm * 1.852f);
    return (km < ARRIVAL_FROM_MIN_KM) ? -1 : (int)km;
}

int arrival_minutes(const aircraft_t *ac, const route_t *route)
{
    if (ac == NULL || route == NULL || !route->resolved || !route->plausible ||
        !route->has_coords) {
        return -1;
    }
    if (ac->alt_ft == ALT_GROUND || ac->alt_ft == ALT_UNKNOWN || ac->alt_ft < 0) {
        return -1;
    }
    if (ac->gs_kt < ARRIVAL_MIN_KT) {
        return -1;
    }
    if (ac->lat == 0.0 && ac->lon == 0.0) {
        return -1;                         /* the parser's "no position" */
    }

    float lat = (float)ac->lat, lon = (float)ac->lon;
    float rem  = arrival_gc_nm(lat, lon, route->dest_lat, route->dest_lon);
    float from = arrival_gc_nm(lat, lon, route->orig_lat, route->orig_lon);

    if (ac->alt_ft < ARRIVAL_CRUISE_FT && from < rem) {
        return -1;                         /* still climbing out */
    }
    if (rem > ARRIVAL_HEADING_NM && ac->has_track) {
        float want = bearing_deg(lat, lon, route->dest_lat, route->dest_lon);
        float off  = fabsf(fmodf(ac->track_deg - want + 540.0f, 360.0f) - 180.0f);
        if (off > 90.0f) {
            return -1;                     /* flying away: route probably wrong */
        }
    }
    float gs    = (float)ac->gs_kt;
    float v_app = (gs < ARRIVAL_APPROACH_KT) ? gs : ARRIVAL_APPROACH_KT;
    float cruise_nm   = (rem > ARRIVAL_APPROACH_NM) ? rem - ARRIVAL_APPROACH_NM : 0.0f;
    float approach_nm = (rem > ARRIVAL_APPROACH_NM) ? ARRIVAL_APPROACH_NM : rem;
    float hours = cruise_nm / gs + approach_nm / v_app;
    long  min   = lroundf(hours * 60.0f);
    return (min > ARRIVAL_MAX_MIN) ? -1 : (int)min;
}
