#include "extrapolate.h"

#include <math.h>
#include <string.h>

/* M_PI is not ISO C — it is a POSIX/X-Open extension, and glibc hides it when
 * __STRICT_ANSI__ is set, which -std=c11 does. Apple's libc exposes it either
 * way, so this file compiled on the machine it was written on and nowhere
 * else; the host suite only ever ran on macOS, so nothing said so until CI
 * built it on Ubuntu. Defining it here keeps the translation unit
 * self-contained instead of making every build depend on which libc it
 * happens to meet. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool aircraft_extrapolate(const aircraft_t *ac, float age_s, aircraft_t *out)
{
    if (ac == NULL || out == NULL) {
        return false;
    }
    if (out != ac) {
        *out = *ac;
    }

    if (!isfinite(age_s) || age_s <= 0.0f || age_s > EXTRAPOLATE_MAX_AGE_S) {
        return false;
    }
    if (!ac->has_track || ac->gs_kt <= 0 || ac->dst_nm == DST_UNKNOWN) {
        return false;
    }
    if (!isfinite(ac->dst_nm) || !isfinite(ac->dir_deg) || !isfinite(ac->track_deg)) {
        return false;
    }

    /* A local tangent plane centred on the house, in nautical miles, north up.
     * Over the 55 km this screen covers, treating it as flat costs far less
     * than the twelve seconds of staleness it removes. */
    const float to_rad = (float)(M_PI / 180.0);
    float bearing = ac->dir_deg * to_rad;
    float x = ac->dst_nm * sinf(bearing);   /* east  */
    float y = ac->dst_nm * cosf(bearing);   /* north */

    /* Groundspeed is knots — nautical miles per hour — so the conversion is
     * the one thing here that cannot be got wrong quietly. */
    float travelled_nm = (float)ac->gs_kt * (age_s / 3600.0f);
    float track = ac->track_deg * to_rad;
    x += travelled_nm * sinf(track);
    y += travelled_nm * cosf(track);

    out->dst_nm = sqrtf(x * x + y * y);

    /* atan2(east, north) — arguments in that order, because this is a compass
     * bearing from north, not a mathematical angle from the x axis. */
    float deg = atan2f(x, y) * (float)(180.0 / M_PI);
    if (deg < 0.0f) {
        deg += 360.0f;
        /* An aircraft ending up exactly due north gives atan2f a vanishingly
         * small NEGATIVE east component, and -1e-7 + 360.0f rounds to exactly
         * 360.0f in single precision. That is outside [0, 360) and it would
         * walk straight into compass_de_abbr(), which is entitled to assume a
         * normalised bearing. Caught by a test asserting due-north stays due
         * north; it would have been all but invisible on the panel. */
        if (deg >= 360.0f) {
            deg = 0.0f;
        }
    }
    out->dir_deg = deg;
    return true;
}
