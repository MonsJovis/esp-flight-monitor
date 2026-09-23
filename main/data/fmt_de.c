/* German words-and-numbers formatting for the panel (docs/PLAN.md M2.5).
 *
 * The German LEXICON lives here: the three compass tables, the weekday and
 * month tables. They are arrays addressed by index, which is why they are
 * not in main/strings_de.h with everything else he reads — naming sixteen
 * compass points as sixteen macros and then rebuilding an array out of them
 * would be strictly worse than the array. tools/check_strings.py parses them
 * out of this file so `--list` still shows a reviewer the whole vocabulary.
 *
 * Everything else user-facing that this file used to spell inline — the unit
 * words, "am Boden", the date order — IS in main/strings_de.h, because each
 * of those is one string and there is no array to keep it honest.
 */
#include "fmt_de.h"
#include "strings_de.h"
#include "flight_types.h"   /* ALT_GROUND, ALT_UNKNOWN */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ---- shared helpers --------------------------------------------------- */

/* Copies the NUL-terminated `src` into `out` (capacity `n`), truncating
 * safely if it does not fit. Always NUL-terminates when n > 0. Returns the
 * number of bytes actually written to `out`, excluding the NUL. */
static size_t safe_copy(char *out, size_t n, const char *src)
{
    if (n == 0) return 0;
    size_t len = strlen(src);
    size_t to_copy = (len < n - 1) ? len : n - 1;
    memcpy(out, src, to_copy);
    out[to_copy] = '\0';
    return to_copy;
}

/* Writes the decimal digits of `mag` into `out` (capacity `outsz`), with a
 * '.' inserted every three digits from the right — German thousands
 * grouping, no sign. Always NUL-terminates when outsz > 0. */
static void format_grouped_u64(uint64_t mag, char *out, size_t outsz)
{
    char digits[24];  /* UINT64_MAX has 20 digits; 24 is slack */
    int  nd = 0;

    if (mag == 0) {
        digits[nd++] = '0';
    } else {
        while (mag > 0 && nd < (int)sizeof digits) {
            digits[nd++] = (char)('0' + (mag % 10));
            mag /= 10;
        }
    }

    size_t oi = 0;
    for (int i = nd - 1; i >= 0; i--) {
        if (oi + 1 < outsz) out[oi++] = digits[i];
        if (i > 0 && (i % 3) == 0) {
            if (oi + 1 < outsz) out[oi++] = '.';
        }
    }
    if (outsz > 0) out[oi < outsz ? oi : outsz - 1] = '\0';
}

/* ---- 1. Unit conversion ------------------------------------------------ */

float nm_to_km(float nm)
{
    return nm * 1.852f;
}

int32_t ft_to_m(int32_t ft)
{
    double m = (double)ft * 0.3048;
    return (int32_t)llround(m);
}

/* ---- 2. German number formatting --------------------------------------- */

size_t fmt_int_de(int32_t v, char *out, size_t n)
{
    bool neg = v < 0;
    uint64_t mag = neg ? (uint64_t)(-(int64_t)v) : (uint64_t)v;

    char grouped[32];
    format_grouped_u64(mag, grouped, sizeof grouped);

    char full[40];
    snprintf(full, sizeof full, "%s%s", neg ? "-" : "", grouped);
    return safe_copy(out, n, full);
}

size_t fmt_dec1_de(float v, char *out, size_t n)
{
    /* Round to the nearest tenth, half away from zero. `v` is a float, so a
     * decimal value like 12.35 may already be stored as e.g. 12.350000381
     * or 12.349999905 — a tiny nudge (well above float32's ~1e-5 relative
     * error, well below the 0.05 that would flip a genuine non-boundary
     * value) restores the rounding the source decimal literal intended. */
    double scaled = (double)v * 10.0;
    const double EPS = 1e-4;
    double nudged = scaled + copysign(EPS, scaled);
    long long scaled_i = llround(nudged);

    bool neg = scaled_i < 0;
    uint64_t mag = neg ? (uint64_t)(-scaled_i) : (uint64_t)scaled_i;
    uint64_t int_part = mag / 10;
    unsigned  frac     = (unsigned)(mag % 10);

    char grouped[32];
    format_grouped_u64(int_part, grouped, sizeof grouped);

    char full[48];
    snprintf(full, sizeof full, "%s%s,%u", neg ? "-" : "", grouped, frac);
    return safe_copy(out, n, full);
}

/* ---- 3. Composite formatters used by the screens ------------------------ */

size_t fmt_distance_km(float nm, char *out, size_t n)
{
    char num[32];
    fmt_dec1_de(nm_to_km(nm), num, sizeof num);

    char full[48];
    snprintf(full, sizeof full, FMT_KM, num);
    return safe_copy(out, n, full);
}

size_t fmt_altitude_m(int32_t alt_ft, char *out, size_t n)
{
    if (alt_ft == ALT_GROUND)   return safe_copy(out, n, STR_ON_GROUND);
    if (alt_ft == ALT_UNKNOWN)  return safe_copy(out, n, STR_EM_DASH);

    char num[32];
    fmt_int_de(ft_to_m(alt_ft), num, sizeof num);

    char full[48];
    snprintf(full, sizeof full, FMT_METRES, num);
    return safe_copy(out, n, full);
}

/* ---- 4. German compass bearing ------------------------------------------ */

/* German, not English: O for Ost, not E for East. */
static const char *const COMPASS_ABBR[16] = {
    "N", "NNO", "NO", "ONO", "O", "OSO", "SO", "SSO",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
};

static const char *const COMPASS_WORD[8] = {
    "Norden", "Nordosten", "Osten", "Südosten",
    "Süden", "Südwesten", "Westen", "Nordwesten",
};

/* The adverbial form of the same eight points, same index order. A bare noun
 * stranded after a number ("16,8 km Nordosten") reads as machine translation;
 * "16,8 km nordöstlich" is what is actually said (docs/PLAN.md M7). Lower
 * case throughout — it is an adverb mid-sentence, never sentence-initial here. */
static const char *const COMPASS_ADV[8] = {
    "nördlich", "nordöstlich", "östlich", "südöstlich",
    "südlich", "südwestlich", "westlich", "nordwestlich",
};

/* Normalises any float (negative, >360, ...) into [0, 360). */
static double normalize_deg(float deg)
{
    double d = fmod((double)deg, 360.0);
    if (d < 0.0) d += 360.0;
    return d;
}

const char *compass_de_abbr(float deg)
{
    double d = normalize_deg(deg);
    /* Sectors are 22.5 wide, centred on multiples of 22.5, closed at the
     * low edge and open at the high edge: N is [348.75,360) u [0,11.25). */
    int idx = (int)floor((d + 11.25) / 22.5);
    idx %= 16;
    if (idx < 0) idx += 16;
    return COMPASS_ABBR[idx];
}

/* Index into an 8-point table. Same convention as compass_de_abbr, 45 wide
 * sectors: N is [337.5,360) u [0,22.5). Deliberately one copy — two tables
 * indexed by two separately-written rounding rules is exactly the kind of
 * drift this project has already been bitten by. */
static int compass_idx8(float deg)
{
    double d = normalize_deg(deg);
    int idx = (int)floor((d + 22.5) / 45.0);
    idx %= 8;
    if (idx < 0) idx += 8;
    return idx;
}

const char *compass_de_word(float deg)
{
    return COMPASS_WORD[compass_idx8(deg)];
}

const char *compass_de_adv(float deg)
{
    return COMPASS_ADV[compass_idx8(deg)];
}

/* ---- 5. German date and time --------------------------------------------- */

/* tm_wday: 0 = Sunday. */
static const char *const WEEKDAY_DE[7] = {
    "Sonntag", "Montag", "Dienstag", "Mittwoch",
    "Donnerstag", "Freitag", "Samstag",
};

/* tm_mon: 0 = January. Austrian German: "Jänner", not "Januar". */
static const char *const MONTH_DE[12] = {
    "Jänner", "Februar", "März", "April", "Mai", "Juni",
    "Juli", "August", "September", "Oktober", "November", "Dezember",
};

const char *weekday_de(int tm_wday)
{
    int idx = tm_wday % 7;
    if (idx < 0) idx += 7;
    return WEEKDAY_DE[idx];
}

const char *month_de(int tm_mon)
{
    int idx = tm_mon % 12;
    if (idx < 0) idx += 12;
    return MONTH_DE[idx];
}

size_t fmt_date_de(const struct tm *t, char *out, size_t n)
{
    char full[64];
    snprintf(full, sizeof full, FMT_DATE_DE,
              weekday_de(t->tm_wday), t->tm_mday, month_de(t->tm_mon),
              t->tm_year + 1900);
    return safe_copy(out, n, full);
}

size_t fmt_time_de(const struct tm *t, char *out, size_t n)
{
    char full[16];
    snprintf(full, sizeof full, "%02d:%02d", t->tm_hour, t->tm_min);
    return safe_copy(out, n, full);
}

/* ---- 6. UTF-8-safe truncation ------------------------------------------ */

size_t utf8_copy(char *out, size_t n, const char *src)
{
    if (out == NULL || n == 0) {
        return 0;
    }
    if (src == NULL) {
        out[0] = '\0';
        return 0;
    }

    size_t len = strlen(src);
    if (len <= n - 1) {
        memcpy(out, src, len);
        out[len] = '\0';
        return len;
    }

    /* Too long, so it has to be cut — walk back off any continuation byte.
     *
     * UTF-8 continuation bytes are 10xxxxxx (0x80..0xBF) and no lead byte
     * ever is, so stepping back while the byte at the cut is a continuation
     * lands exactly on the start of the character that straddled the limit,
     * and dropping that character is what leaves the string whole. At most
     * three steps — a UTF-8 sequence is four bytes — so the loop cannot run
     * away even on malformed input. */
    size_t cut = n - 1;
    while (cut > 0 && ((unsigned char)src[cut] & 0xC0) == 0x80) {
        cut--;
    }
    memcpy(out, src, cut);
    out[cut] = '\0';
    return cut;
}

/* ---- the update row (D74) ---------------------------------------------- */

size_t fmt_update_status(update_state_t state, const char *version,
                         char *out, size_t n)
{
    if (out == NULL || n == 0) {
        return 0;
    }
    if (state == UPD_AVAILABLE) {
        if (version == NULL || version[0] == '\0') {
            /* Checked, something is newer, and we cannot say what. Fall back
             * to the neutral prompt rather than print a sentence with a hole
             * in it. */
            return utf8_copy(out, n, STR_UPDATE_CHECK);
        }
        int w = snprintf(out, n, FMT_UPDATE_AVAILABLE, version);
        if (w < 0) {
            out[0] = '\0';
            return 0;
        }
        return (size_t)w < n - 1 ? (size_t)w : n - 1;
    }

    const char *s;
    switch (state) {
        case UPD_CHECKING:     s = STR_UPDATE_CHECKING;     break;
        case UPD_CURRENT:      s = STR_UPDATE_CURRENT;      break;
        case UPD_CHECK_FAILED: s = STR_UPDATE_CHECK_FAILED; break;
        case UPD_INSTALLING:   s = STR_UPDATE_INSTALLING;   break;
        case UPD_FAILED:       s = STR_UPDATE_FAILED;       break;
        case UPD_IDLE:
        default:               s = STR_UPDATE_CHECK;        break;
    }
    return utf8_copy(out, n, s);
}

const char *update_action_label(update_state_t state)
{
    switch (state) {
        case UPD_AVAILABLE:  return STR_UPDATE_INSTALL;
        /* Nothing a second tap can do while one is already running. */
        case UPD_CHECKING:
        case UPD_INSTALLING: return NULL;
        default:             return STR_UPDATE_CHECK;
    }
}
