/* German words-and-numbers formatting for the panel (docs/PLAN.md M2.5).
 *
 * The APIs speak English, nautical miles and feet; the panel speaks German,
 * km and metres. This module is the translation layer between the two.
 *
 * All output is UTF-8. Every formatter writes into a caller-supplied buffer
 * with an explicit size and never overflows it; each returns the number of
 * bytes written, excluding the terminating NUL (i.e. the same convention as
 * snprintf's return value, but clamped to what actually fit). If the buffer
 * is too small the output is truncated but always NUL-terminated (as long as
 * n > 0).
 *
 * No dynamic allocation, no ESP-IDF headers — this compiles on the macOS
 * host test runner as well as under ESP-IDF. C11 only.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 1. Unit conversion --------------------------------------------- */

/* 1 nm = 1.852 km exactly. */
float nm_to_km(float nm);

/* 1 ft = 0.3048 m, rounded to nearest (round-half-away-from-zero). */
int32_t ft_to_m(int32_t ft);

/* ---- 2. German number formatting ------------------------------------ */

/* Thousands DOT, no decimals. 9100 -> "9.100", -1234567 -> "-1.234.567". */
size_t fmt_int_de(int32_t v, char *out, size_t n);

/* Thousands DOT, decimal COMMA, one decimal digit, rounded half-away-from-
 * zero. 12.35 -> "12,4", 1234.5 -> "1.234,5". */
size_t fmt_dec1_de(float v, char *out, size_t n);

/* ---- 3. Composite formatters used by the screens --------------------- */

/* Distance, converted nm -> km and rendered "12,4 km". */
size_t fmt_distance_km(float nm, char *out, size_t n);

/* Altitude, converted ft -> m and rendered "9.100 m". ALT_GROUND renders
 * as "am Boden", ALT_UNKNOWN as "—" (U+2014 EM DASH). */
size_t fmt_altitude_m(int32_t alt_ft, char *out, size_t n);

/* ---- 4. German compass bearing --------------------------------------- */

/* 16-point abbreviation ("N", "NNO", ... "NNW"). Normalises deg first, so
 * any float (negative, >360) is handled. German uses O for Ost, not E. */
const char *compass_de_abbr(float deg);

/* 8-point full word ("Norden", "Nordosten", ... "Nordwesten"). Same
 * normalisation rules as compass_de_abbr. */
const char *compass_de_word(float deg);

/* 8-point adverb ("nördlich", "nordöstlich", ... "nordwestlich"), the form
 * that reads naturally after a distance: "16,8 km nordöstlich". Same
 * normalisation rules as compass_de_abbr. */
const char *compass_de_adv(float deg);

/* ---- 5. German date and time ------------------------------------------ */

/* tm_wday: 0 = Sunday, per struct tm. */
const char *weekday_de(int tm_wday);

/* tm_mon: 0 = January, per struct tm. Austrian German: "Jänner", not
 * "Januar". */
const char *month_de(int tm_mon);

/* "Freitag, 18. September 2026" */
size_t fmt_date_de(const struct tm *t, char *out, size_t n);

/* "09:47" (24-hour, zero-padded) */
size_t fmt_time_de(const struct tm *t, char *out, size_t n);

/* ---- 6. UTF-8-safe truncation ------------------------------------------ */

/* Copies `src` into `out` (at most `n` bytes including the NUL) and NEVER
 * leaves half a character behind. Returns the number of bytes written,
 * excluding the NUL, like every other formatter here.
 *
 * EVERY GERMAN WORD ON THIS DEVICE IS MULTI-BYTE SOMEWHERE. strncpy() and a
 * memcpy-to-a-byte-count both cut at byte `n`, which lands inside a sequence
 * whenever the character straddling the limit is an umlaut, an ß, a · or a °
 * — and LVGL draws a stray lead byte as nothing at all, silently, the same
 * missing-glyph trap AGENTS.md §7 describes. Two places were doing it: the
 * geocoder's 72-byte display labels ("Sankt Johann im Pongau · Salzburg" is
 * 34 characters in 35 bytes and they get longer), which are then written to
 * NVS and redrawn for the life of the device; and the Ortssuche query buffer,
 * which counts 48 BYTES against a text area that limits 47 CODEPOINTS — so a
 * long umlauted search was cut mid-character and percent-encoded into the URL
 * as a stray %C3.
 *
 * Truncation still happens; it just happens at a character boundary. A short
 * `n` that cannot hold even the first character yields an empty string rather
 * than a fragment. */
size_t utf8_copy(char *out, size_t n, const char *src);

#ifdef __cplusplus
}
#endif
