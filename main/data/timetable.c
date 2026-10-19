#include "timetable.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arrival.h"
#include "tables.h"

/* ---- parsing ------------------------------------------------------------- */

static bool is_icao(const char *s, size_t n)
{
    if (n != 4) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (!isupper((unsigned char)s[i])) {
            return false;
        }
    }
    return true;
}

/* "OS", "4Y": two characters, letters or digits. */
static bool is_carrier(const char *s, size_t n)
{
    if (n != 2) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (!isupper((unsigned char)s[i]) && !isdigit((unsigned char)s[i])) {
            return false;
        }
    }
    return true;
}

/* Digits only, nothing else, at most 12 of them. strtoll would accept a sign,
 * spaces and trailing garbage, and a line with any of those is not one the CI
 * wrote. */
static bool parse_digits(const char *s, size_t n, long long *out)
{
    if (n == 0 || n > 12) {
        return false;
    }
    long long v = 0;
    for (size_t i = 0; i < n; i++) {
        if (!isdigit((unsigned char)s[i])) {
            return false;
        }
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return true;
}

static bool parse_line(const char *p, const char *end, tt_flight_t *f)
{
    const char *fld[6];
    size_t      flen[6];
    int         k = 0;
    const char *start = p;
    for (const char *q = p;; q++) {
        if (q == end || *q == ',') {
            if (k == 6) {
                return false;              /* a seventh field */
            }
            fld[k]  = start;
            flen[k] = (size_t)(q - start);
            k++;
            if (q == end) {
                break;
            }
            start = q + 1;
        }
    }
    if (k != 6) {
        return false;
    }

    long long number, dep, arr;
    if (!is_carrier(fld[0], flen[0]) ||
        !parse_digits(fld[1], flen[1], &number) || number < 1 || number > 9999 ||
        !is_icao(fld[2], flen[2]) || !is_icao(fld[3], flen[3]) ||
        !parse_digits(fld[4], flen[4], &dep) ||
        !parse_digits(fld[5], flen[5], &arr)) {
        return false;
    }
    /* A flight lands after it leaves, and none of them flies for 20 hours. */
    if (dep <= 0 || arr <= dep || arr - dep > 20 * 3600) {
        return false;
    }

    memset(f, 0, sizeof *f);
    memcpy(f->carrier, fld[0], 2);
    memcpy(f->from, fld[2], 4);
    memcpy(f->to, fld[3], 4);
    f->number = (uint16_t)number;
    f->dep    = dep;
    f->arr    = arr;
    return true;
}

int timetable_parse(const char *text, size_t len, tt_flight_t *out, int max,
                    int64_t *generated)
{
    if (generated != NULL) {
        *generated = 0;
    }
    if (text == NULL || out == NULL || max <= 0) {
        return -1;
    }

    static const char gen_prefix[] = "# generated ";
    const char *p   = text;
    const char *end = text + len;
    int  n          = 0;
    bool first      = true;

    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        const char *nl  = (eol != NULL) ? eol : end;
        const char *le  = nl;
        if (le > p && le[-1] == '\r') {
            le--;
        }
        size_t ll = (size_t)(le - p);

        if (first) {
            if (ll != sizeof TT_HEADER - 1 || memcmp(p, TT_HEADER, ll) != 0) {
                return -1;
            }
            first = false;
        } else if (ll > 0 && p[0] == '#') {
            size_t gl = sizeof gen_prefix - 1;
            long long g;
            if (generated != NULL && ll > gl && memcmp(p, gen_prefix, gl) == 0 &&
                parse_digits(p + gl, ll - gl, &g)) {
                *generated = g;
            }
        } else if (ll > 0 && n < max && parse_line(p, le, &out[n])) {
            n++;
        }
        p = nl + 1;
    }
    return first ? -1 : n;   /* an empty body has no header either */
}

/* ---- matching ------------------------------------------------------------ */

/* An airline's alphanumeric callsign: three letters, a digit, and at least
 * one letter somewhere after it. "AUA25L" gives code "AUA" and digits "25".
 * A callsign of digits only is not one of these: it already is its flight
 * number, and flight_number() says so (D89). */
static bool split_callsign(const char *cs, char code[4], char digits[5])
{
    size_t len = strlen(cs);
    if (len < 5 || len > 7) {
        return false;
    }
    for (size_t i = 0; i < 3; i++) {
        if (!isupper((unsigned char)cs[i])) {
            return false;
        }
    }
    size_t d = 3;
    while (d < len && isdigit((unsigned char)cs[d])) {
        d++;
    }
    if (d == 3 || d == len || d - 3 > 4) {
        return false;
    }
    for (size_t i = d; i < len; i++) {
        if (!isupper((unsigned char)cs[i]) && !isdigit((unsigned char)cs[i])) {
            return false;
        }
    }
    memcpy(code, cs, 3);
    code[3] = '\0';
    memcpy(digits, cs + 3, d - 3);
    digits[d - 3] = '\0';
    return true;
}

size_t timetable_match(const tt_flight_t *tt, int n, const aircraft_t *ac,
                       const route_t *rt, int64_t now, char *out, size_t outsz)
{
    if (out == NULL || outsz == 0) {
        return 0;
    }
    out[0] = '\0';
    if (tt == NULL || n <= 0 || ac == NULL || rt == NULL || now <= 0) {
        return 0;
    }
    if (!rt->resolved || !rt->plausible || rt->orig_icao[0] == '\0' ||
        rt->dest_icao[0] == '\0') {
        return 0;
    }

    char code[4], digits[5];
    if (!split_callsign(ac->flight, code, digits)) {
        return 0;
    }
    const char *carrier = airline_iata(code);
    if (carrier == NULL) {
        return 0;
    }
    /* Austrian keeps the leading digits of the flight number in its
     * callsign; for everyone else the digits say nothing (timetable.h). */
    bool prefix_rule = (strcmp(code, "AUA") == 0);

    int eta_min = arrival_minutes(ac, rt);
    int64_t predicted_arr = (eta_min >= 0) ? now + (int64_t)eta_min * 60 : 0;

    const tt_flight_t *hit = NULL;
    for (int i = 0; i < n; i++) {
        const tt_flight_t *f = &tt[i];
        if (strcmp(f->carrier, carrier) != 0 ||
            strcmp(f->from, rt->orig_icao) != 0 ||
            strcmp(f->to, rt->dest_icao) != 0) {
            continue;
        }
        if (now < f->dep - TT_SLACK_BEFORE_S || now > f->arr + TT_SLACK_AFTER_S) {
            continue;
        }
        if (predicted_arr != 0) {
            int64_t off = predicted_arr - f->arr;
            if (off < -TT_ETA_TOLERANCE_S || off > TT_ETA_TOLERANCE_S) {
                continue;
            }
        }
        if (prefix_rule) {
            char num[8];
            snprintf(num, sizeof num, "%u", (unsigned)f->number);
            if (strncmp(num, digits, strlen(digits)) != 0) {
                continue;
            }
        }
        if (hit != NULL && hit->number != f->number) {
            return 0;      /* two flights fit: not certain, so the callsign stays */
        }
        hit = f;
    }
    if (hit == NULL) {
        return 0;
    }
    int w = snprintf(out, outsz, "%s %u", hit->carrier, (unsigned)hit->number);
    if (w < 0) {
        out[0] = '\0';
        return 0;
    }
    return ((size_t)w < outsz) ? (size_t)w : outsz - 1;
}
