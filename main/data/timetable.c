#include "timetable.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- shapes ---------------------------------------------------------------- */

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

/* An airline's ATC callsign as Eurocontrol's rules allow it: three letters of
 * ICAO code, one to four digits, up to two letters. "AUA25L" gives code "AUA"
 * and digits "25"; "WZZ839" gives "WZZ" and "839". `code` and `digits` may be
 * NULL when only the shape matters. */
static bool split_callsign(const char *cs, size_t len, char code[4], char digits[5])
{
    if (len < 4 || len > 8) {
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
    if (d == 3 || d - 3 > 4 || len - d > 2) {
        return false;
    }
    for (size_t i = d; i < len; i++) {
        if (!isupper((unsigned char)cs[i])) {
            return false;
        }
    }
    if (code != NULL) {
        memcpy(code, cs, 3);
        code[3] = '\0';
    }
    if (digits != NULL) {
        memcpy(digits, cs + 3, d - 3);
        digits[d - 3] = '\0';
    }
    return true;
}

/* ---- parsing ----------------------------------------------------------------- */

static bool parse_line(const char *p, const char *end, tt_pair_t *f)
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

    long long number, seen;
    if (!split_callsign(fld[0], flen[0], NULL, NULL) ||
        !is_carrier(fld[1], flen[1]) ||
        !parse_digits(fld[2], flen[2], &number) || number < 1 || number > 9999 ||
        !is_icao(fld[3], flen[3]) || !is_icao(fld[4], flen[4]) ||
        !parse_digits(fld[5], flen[5], &seen) || seen <= 0) {
        return false;
    }

    memset(f, 0, sizeof *f);
    memcpy(f->callsign, fld[0], flen[0]);
    memcpy(f->carrier, fld[1], 2);
    memcpy(f->from, fld[3], 4);
    memcpy(f->to, fld[4], 4);
    f->number = (uint16_t)number;
    f->seen   = seen;
    return true;
}

int timetable_parse(const char *text, size_t len, tt_pair_t *out, int max,
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

/* ---- lookup ------------------------------------------------------------------ */

size_t timetable_lookup(const tt_pair_t *tt, int n, const char *callsign,
                        const route_t *rt, int64_t now, char *out, size_t outsz)
{
    if (out == NULL || outsz == 0) {
        return 0;
    }
    out[0] = '\0';
    if (tt == NULL || n <= 0 || callsign == NULL || rt == NULL) {
        return 0;
    }
    if (!rt->resolved || !rt->plausible || rt->orig_icao[0] == '\0' ||
        rt->dest_icao[0] == '\0') {
        return 0;
    }

    char code[4], digits[5];
    if (!split_callsign(callsign, strlen(callsign), code, digits)) {
        return 0;
    }
    /* Austrian keeps the leading digits of the flight number in its
     * callsign; for everyone else the digits say nothing (timetable.h). */
    bool prefix_rule = (strcmp(code, "AUA") == 0);

    const tt_pair_t *hit = NULL;
    for (int i = 0; i < n; i++) {
        const tt_pair_t *f = &tt[i];
        if (strcmp(f->callsign, callsign) != 0 ||
            strcmp(f->from, rt->orig_icao) != 0 ||
            strcmp(f->to, rt->dest_icao) != 0) {
            continue;
        }
        if (now > 0 && now - f->seen > TT_MAX_AGE_S) {
            continue;
        }
        if (prefix_rule) {
            char num[8];
            snprintf(num, sizeof num, "%u", (unsigned)f->number);
            if (strncmp(num, digits, strlen(digits)) != 0) {
                continue;
            }
        }
        if (hit != NULL &&
            (hit->number != f->number || strcmp(hit->carrier, f->carrier) != 0)) {
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
