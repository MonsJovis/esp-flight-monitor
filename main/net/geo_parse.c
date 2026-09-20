#include "geo_parse.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "compat.h"
#include "strings_de.h"
#include "tz_table.h"

static const char *TAG = "geo_parse";

/* The endpoint, whole, as a format string. Plain HTTP on purpose and verified
 * to answer on it — see the measurement in geo_parse.h.
 *
 * `language=de` is not a nicety: it is what makes the second line read
 * "Niederösterreich · Österreich" instead of "Lower Austria · Austria" on a
 * German panel, and what turns Brno into "Brünn" and Poznań into "Posen"
 * (AGENTS.md §1 — the route API's English city names are the same problem,
 * solved the same way).
 *
 * One literal rather than a base plus a query fragment, because a fragment
 * like "?name=%s&count=%d" carries no scheme and so reads to
 * tools/check_strings.py as prose rather than as a URL. Keeping the whole
 * thing together is also how anyone reading this file sees the request that
 * actually goes out. */
#define GEO_SEARCH_URL \
    "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=%d&language=de&format=json"

/* ============================================================================
 * URL building
 * ============================================================================
 */

static char hex_digit(unsigned v)
{
    return (char)(v < 10 ? '0' + v : 'A' + (v - 10));
}

/* RFC 3986 §2.3 unreserved. Everything else is percent-encoded, including the
 * space and including any UTF-8 byte — the on-screen keyboard is ASCII today
 * (screen_geo.c), but an encoder that only handles what today's keyboard can
 * produce is a bug waiting for the day someone adds an umlaut key. */
static bool is_unreserved(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
}

int geo_build_url(const char *query, int count, char *out, size_t out_sz)
{
    if (query == NULL || out == NULL || out_sz == 0) {
        return -1;
    }
    /* The endpoint returns an empty result set for a single character, so a
     * one-letter query is a request we know the answer to. Spending it costs
     * a round trip and shows him an empty list that means nothing. */
    if (strlen(query) < 2) {
        return -1;
    }
    if (count < 1) {
        count = 1;
    }

    char enc[3 * 64 + 1];   /* worst case: every byte of a 64-byte query encoded */
    size_t e = 0;
    for (const unsigned char *p = (const unsigned char *)query; *p != '\0'; p++) {
        if (is_unreserved(*p)) {
            if (e + 1 >= sizeof enc) {
                return -1;
            }
            enc[e++] = (char)*p;
        } else {
            if (e + 3 >= sizeof enc) {
                return -1;
            }
            /* Upper-case hex digits, as RFC 3986 §2.1 prefers. Built with
             * arithmetic rather than a lookup string so that the one table of
             * characters in this file is not something a reader has to check
             * for German. */
            enc[e++] = '%';
            enc[e++] = hex_digit(*p >> 4);
            enc[e++] = hex_digit(*p & 0x0F);
        }
    }
    enc[e] = '\0';

    int n = snprintf(out, out_sz, GEO_SEARCH_URL, enc, count);
    if (n < 0 || (size_t)n >= out_sz) {
        return -1;
    }
    return n;
}

/* ============================================================================
 * Timezone
 * ============================================================================
 */

const char *geo_tz_posix(const char *iana, double lon, char *buf, size_t buf_sz)
{
    if (buf == NULL || buf_sz == 0) {
        return buf;
    }

    if (iana != NULL && iana[0] != '\0') {
        /* tz_zones[] is emitted sorted by name, which is what makes this a
         * binary search rather than 114 strcmp()s on a task that also has a
         * display to feed. */
        int lo = 0, hi = (int)(sizeof tz_zones / sizeof tz_zones[0]) - 1;
        while (lo <= hi) {
            int mid = lo + (hi - lo) / 2;
            int cmp = strcmp(iana, tz_zones[mid].iana);
            if (cmp == 0) {
                snprintf(buf, buf_sz, "%s", tz_rules[tz_zones[mid].rule]);
                return buf;
            }
            if (cmp < 0) {
                hi = mid - 1;
            } else {
                lo = mid + 1;
            }
        }
        ESP_LOGW(TAG, "no POSIX rule for %s; falling back to the longitude", iana);
    }

    /* Whole-hour offset from the longitude. Note the sign: POSIX TZ counts
     * the offset you ADD to local time to get UTC, so it is the negative of
     * the offset people say out loud. Bangkok is UTC+7 and is written "-7".
     * This inversion is the single most common way to get a TZ string wrong,
     * and test/host/test_geo.c pins both directions. */
    int hours = (int)((lon < 0 ? lon - 7.5 : lon + 7.5) / 15.0);
    if (hours > 14) {
        hours = 14;
    }
    if (hours < -12) {
        hours = -12;
    }
    snprintf(buf, buf_sz, "<%+03d>%d", hours, -hours);
    return buf;
}

/* ============================================================================
 * What the panel can actually draw
 * ============================================================================
 */

/* LVGL renders a glyph that is not in the font subset as NOTHING AT ALL — no
 * box, no error, no log line (AGENTS.md §7). Every other string on this
 * device is a literal that tools/check_font_coverage.py has already checked;
 * these come off the network, so they are the one class of text on the panel
 * that no gate can see in advance.
 *
 * THE RANGES BELOW ARE A COPY. The source of truth is CORE_RANGES/FULL_RANGES
 * in tools/build_fonts.sh, and a copy can go stale — but note which way it
 * goes stale: if the subset is ever WIDENED and this list is not, the only
 * consequence is that a hit we could have drawn is dropped. Being too strict
 * costs a search result; being too loose puts a blank row on the screen for
 * him to tap. So the copy errs in the safe direction by construction.
 *
 * In practice `language=de` makes this rare: the endpoint answers with German
 * exonyms, so Brno comes back "Brünn", Poznań "Posen" and Timişoara
 * "Temeswar" — measured, 2026-09-20. The realistic casualties are Ž and ž,
 * which sit just past the Latin Extended-A range the fonts were built with. */
static bool cp_renderable(unsigned cp)
{
    return (cp >= 0x20 && cp <= 0x7F) ||        /* ASCII                    */
           (cp >= 0xC0 && cp <= 0xFF) ||        /* Latin-1 letters          */
           cp == 0xB0 || cp == 0xB7 ||          /* ° ·                      */
           cp == 0x2014 || cp == 0x2192 ||      /* — →                      */
           (cp >= 0x0104 && cp <= 0x017C);      /* Latin Extended-A, partial */
}

/* True when every codepoint in `s` has a glyph. Decodes UTF-8 by hand rather
 * than pulling in a library for eight lines; malformed input fails closed. */
static bool text_renderable(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p != '\0') {
        unsigned cp;
        int extra;
        if (*p < 0x80)             { cp = *p;        extra = 0; }
        else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F;  extra = 1; }
        else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F;  extra = 2; }
        else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07;  extra = 3; }
        else {
            return false;                       /* stray continuation byte */
        }
        p++;
        for (int i = 0; i < extra; i++, p++) {
            if ((*p & 0xC0) != 0x80) {
                return false;                   /* truncated sequence      */
            }
            cp = (cp << 6) | (*p & 0x3F);
        }
        if (!cp_renderable(cp)) {
            return false;
        }
    }
    return true;
}

/* ============================================================================
 * Response parsing
 * ============================================================================
 */

static const char *str_field(const cJSON *obj, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static void copy_str(char *dst, size_t dst_sz, const char *src)
{
    if (dst_sz == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n > dst_sz - 1) {
        n = dst_sz - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Joins two parts with the middle dot this device uses for "and also"
 * everywhere else, dropping the separator when either side is missing. Both
 * display lines are one call to this.
 *
 * `admin1` is the state or province ("Niederösterreich", "Chonburi"), and it
 * is the field that does the work here: a search for "Pattaya" returns the
 * Thai one and a Burmese one, which the country alone separates — but "Wien"
 * returns three and only the region tells them apart. */
static void join_de(char *dst, size_t dst_sz, const char *a, const char *b)
{
    bool has_a = (a != NULL && a[0] != '\0');
    bool has_b = (b != NULL && b[0] != '\0');

    if (has_a && has_b) {
        snprintf(dst, dst_sz, FMT_GEO_JOIN, a, b);
    } else if (has_a) {
        copy_str(dst, dst_sz, a);
    } else if (has_b) {
        copy_str(dst, dst_sz, b);
    } else {
        dst[0] = '\0';
    }
}

int geo_parse(const char *json, size_t len, geo_place_t *out, int max)
{
    if (json == NULL || out == NULL || max <= 0) {
        return -1;
    }

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (root == NULL) {
        ESP_LOGW(TAG, "response is not JSON");
        return -1;
    }

    /* No `results` key at all is what this endpoint returns for a query that
     * matched nothing — the body is just {"generationtime_ms":0.16}. That is
     * a NORMAL outcome and the screen has a sentence for it; it is not the
     * same event as a body that would not parse, and conflating the two would
     * show him "keine Verbindung" when he simply mistyped a town. */
    cJSON *results = cJSON_GetObjectItemCaseSensitive(root, "results");
    if (!cJSON_IsArray(results)) {
        cJSON_Delete(root);
        return 0;
    }

    int n = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, results) {
        if (n >= max) {
            break;
        }
        if (!cJSON_IsObject(item)) {
            continue;
        }

        cJSON *lat = cJSON_GetObjectItemCaseSensitive(item, "latitude");
        cJSON *lon = cJSON_GetObjectItemCaseSensitive(item, "longitude");
        const char *name = str_field(item, "name");

        /* A hit with no coordinates cannot be selected and a hit with no name
         * cannot be shown, so neither is worth a row he might tap. */
        if (!cJSON_IsNumber(lat) || !cJSON_IsNumber(lon) || name == NULL || name[0] == '\0') {
            continue;
        }
        /* And neither is a name the panel would draw as an empty row. */
        if (!text_renderable(name)) {
            ESP_LOGW(TAG, "dropping a hit whose name has no glyphs in the font subset");
            continue;
        }

        /* The region parts are decoration, not identity: a hit whose province
         * has no glyphs is still a place he can pick, so the PART goes rather
         * than the row. Filtered before they are joined, so one unrenderable
         * half cannot take the other half's line down with it. */
        const char *admin1  = str_field(item, "admin1");
        const char *country = str_field(item, "country");
        if (admin1 != NULL && !text_renderable(admin1)) {
            admin1 = NULL;
        }
        if (country != NULL && !text_renderable(country)) {
            country = NULL;
        }

        geo_place_t *p = &out[n];
        copy_str(p->name, sizeof p->name, name);
        join_de(p->region, sizeof p->region, admin1, country);
        join_de(p->label,  sizeof p->label,  p->name, admin1);
        p->lat = lat->valuedouble;
        p->lon = lon->valuedouble;
        geo_tz_posix(str_field(item, "timezone"), p->lon, p->tz, sizeof p->tz);
        n++;
    }

    cJSON_Delete(root);
    return n;
}
