#include "settings.h"
#include <stdint.h>
#include <string.h>
#include "compat.h"

/* The two places this device lives. Coordinates verified in AGENTS.md §6;
 * both were measured for traffic density before being chosen. */
static const struct {
    const char *name;
    const char *tz;
    double      lat, lon;
} k_presets[] = {
    /* Semmeringstraße 11, 2640 Gloggnitz */
    [LOC_GLOGGNITZ] = { "Gloggnitz", "CET-1CEST,M3.5.0,M10.5.0/3", 47.6691, 15.9303 },
    /* 154 Thappraya Rd, Pattaya City. No DST in Thailand. */
    [LOC_PATTAYA]   = { "Pattaya",   "ICT-7",                      12.9211, 100.8721 },
    /* The escape hatch. Its real coordinates and timezone live in the
     * settings (settings_t.custom_*), written by the place search in §5.8 —
     * the row's lat/lon are never read and its TZ is only the fallback
     * settings_tz() uses when nothing has been searched yet. It used to say
     * "UTC0" here, which meant that tapping this card moved the clock two
     * hours without moving the device an inch. */
    [LOC_CUSTOM]    = { "Eigener Ort", "CET-1CEST,M3.5.0,M10.5.0/3", 0.0,     0.0 },
    /* Meiselstraße 79, 1140 Wien (Penzing). Same timezone as Gloggnitz —
     * bound to the place, not set separately, because he never sets a clock.
     * Appended rather than slotted in beside Gloggnitz: the enum value goes
     * into NVS (settings.h). */
    [LOC_WIEN]      = { "Wien",        "CET-1CEST,M3.5.0,M10.5.0/3", 48.1984, 16.3074 },
};

/* What he sees, top to bottom. The two Austrian places together, then the
 * one he flies to, then the escape hatch. */
static const location_preset_t k_display_order[LOC_COUNT] = {
    LOC_GLOGGNITZ, LOC_WIEN, LOC_PATTAYA, LOC_CUSTOM,
};

location_preset_t location_display_order(int i)
{
    if (i < 0 || i >= LOC_COUNT) return LOC_GLOGGNITZ;
    return k_display_order[i];
}

const char *location_name(location_preset_t p)
{
    if (p < 0 || p >= LOC_COUNT) return k_presets[LOC_GLOGGNITZ].name;
    return k_presets[p].name;
}

const char *location_tz(location_preset_t p)
{
    if (p < 0 || p >= LOC_COUNT) return k_presets[LOC_GLOGGNITZ].tz;
    return k_presets[p].tz;
}

const char *settings_tz(const settings_t *s)
{
    if (s == NULL) return k_presets[LOC_GLOGGNITZ].tz;
    if (s->preset == LOC_CUSTOM && s->custom_tz[0] != '\0') {
        return s->custom_tz;
    }
    return location_tz(s->preset);
}

void settings_coords(const settings_t *s, double *lat, double *lon)
{
    if (s == NULL || lat == NULL || lon == NULL) return;

    if (s->preset == LOC_CUSTOM) {
        *lat = s->custom_lat;
        *lon = s->custom_lon;
        return;
    }
    if (s->preset < 0 || s->preset >= LOC_COUNT) {
        /* Never poll 0,0 — that is the Atlantic, and it would look like the
         * device is simply broken rather than misconfigured. */
        *lat = k_presets[LOC_GLOGGNITZ].lat;
        *lon = k_presets[LOC_GLOGGNITZ].lon;
        return;
    }
    *lat = k_presets[s->preset].lat;
    *lon = k_presets[s->preset].lon;
}

int settings_brightness_for_hour(const settings_t *s, int hour)
{
    if (s == NULL) return 100;
    if (!s->auto_dim || hour < 0 || hour > 23) return s->brightness_pct;

    bool dim;
    if (s->dim_from_hour == s->dim_to_hour) {
        dim = false;                                   /* empty window */
    } else if (s->dim_from_hour < s->dim_to_hour) {
        dim = (hour >= s->dim_from_hour && hour < s->dim_to_hour);
    } else {
        /* Wraps midnight, which is the normal case: 22:00 to 07:00. */
        dim = (hour >= s->dim_from_hour || hour < s->dim_to_hour);
    }
    return dim ? s->dim_brightness_pct : s->brightness_pct;
}

void settings_defaults(settings_t *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof *out);
    out->preset             = LOC_GLOGGNITZ;
    out->custom_lat         = k_presets[LOC_GLOGGNITZ].lat;
    out->custom_lon         = k_presets[LOC_GLOGGNITZ].lon;
    /* Deliberately left empty by the memset above: "he has never searched"
     * is a real state with a line of its own on the settings card (the
     * coordinates), not something to paper over with a made-up name. */
    out->radius_nm          = 30;   /* ~55 km: what "overhead" means, and it
                                     * keeps the payload near 4 KB (AGENTS.md §6) */
    out->brightness_pct     = 100;
    /* Auto-dim is on by DEFAULT, not opt-in: DESIGN.md §7 calls a glowing dark
     * panel in a dim living room at 22:00 glare, and the clock is already
     * correct, so a schedule is enough. Someone who never opens the settings
     * screen is exactly who this protects. */
    out->auto_dim           = true;
    out->dim_from_hour      = 22;
    out->dim_to_hour        = 7;
    out->dim_brightness_pct = 25;
}

static int clamp_int(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void settings_sanitise(settings_t *s)
{
    if (s == NULL) return;

    if (s->preset < 0 || s->preset >= LOC_COUNT) {
        s->preset = LOC_GLOGGNITZ;
    }
    if (s->custom_lat < -90.0  || s->custom_lat > 90.0 ||
        s->custom_lon < -180.0 || s->custom_lon > 180.0) {
        s->custom_lat = k_presets[LOC_GLOGGNITZ].lat;
        s->custom_lon = k_presets[LOC_GLOGGNITZ].lon;
    }
    /* NUL-terminate whatever NVS handed back before anything treats these as
     * strings. A blob truncated or corrupted mid-field is otherwise a label
     * that runs off the end of the struct and a TZ string that setenv() reads
     * past — the two places where a bad byte stops being cosmetic. */
    s->custom_label[sizeof s->custom_label - 1] = '\0';
    s->custom_tz[sizeof s->custom_tz - 1]       = '\0';

    s->radius_nm = clamp_int(s->radius_nm, 10, 100);
    /* Floor the brightness well above zero: a device that can be configured
     * into a black screen looks broken and gives him no way back. */
    s->brightness_pct     = clamp_int(s->brightness_pct, 10, 100);
    s->dim_brightness_pct = clamp_int(s->dim_brightness_pct, 5, 100);
    s->dim_from_hour      = clamp_int(s->dim_from_hour, 0, 23);
    s->dim_to_hour        = clamp_int(s->dim_to_hour, 0, 23);
    if (s->dim_brightness_pct > s->brightness_pct) {
        s->dim_brightness_pct = s->brightness_pct;   /* "dim" must not brighten */
    }
}

/* ============================================================================
 * The NVS blob, and what to do with an older one
 *
 * PURE, and therefore ABOVE the #ifndef HOST_TEST line, which is the whole
 * point: this is the most dangerous code in the file — it is the only thing
 * standing between a firmware update and a device that silently forgets
 * where it is — and it would otherwise be the only part of settings.c that
 * no test can reach. AGENTS.md §10: anything host-testable is host-tested.
 * settings_load()/settings_save() below are now thin: open NVS, move bytes,
 * close, and hand the bytes to these.
 *
 * WHAT THE HOST SUITE CAN AND CANNOT PROVE. It compiles settings_v1_t with
 * the same compiler as everything else, so it proves the field mapping and
 * the size discrimination. It does NOT prove that settings_v1_t matches the
 * bytes a device actually wrote in M6 — only the device can settle that, by
 * being updated and still knowing where it is.
 * ============================================================================
 */
#define SETTINGS_VERSION 2u

typedef struct {
    uint32_t   version;
    settings_t s;
} settings_blob_t;

/* ---- Version 1, kept so a device in the field survives this change ------
 *
 * Version 2 added custom_label/custom_tz for the place search (§5.8), which
 * moved every field after them and changed the blob's size. The load path
 * tests `len == sizeof blob`, so without this a unit that has been running
 * since M6 would come up on DEFAULTS after the update: back to Gloggnitz,
 * back to 100% brightness, and no indication that anything had happened
 * beyond the location quietly being wrong.
 *
 * That is not hypothetical — there IS such a unit, it is the only one, and
 * it is 9,000 km from anyone who could fix it for half the year. So the old
 * layout is transcribed here EXACTLY (this is a copy of the struct as it
 * stood, not a reference to the current one, which is the entire point) and
 * migrated field by field.
 *
 * When version 3 comes: leave this alone, add settings_v2_t beside it. */
typedef struct {
    location_preset_t preset;
    double            custom_lat, custom_lon;
    int               radius_nm;
    int               brightness_pct;
    bool              auto_dim;
    int               dim_from_hour;
    int               dim_to_hour;
    int               dim_brightness_pct;
} settings_v1_t;

typedef struct {
    uint32_t      version;
    settings_v1_t s;
} settings_blob_v1_t;

/* The read buffer: large enough for any layout this firmware understands, so
 * that one nvs_get_blob() can fetch whichever one is actually stored. */
typedef union {
    settings_blob_t    v2;
    settings_blob_v1_t v1;
} settings_blob_any_t;

static void migrate_v1(settings_t *out, const settings_v1_t *v1)
{
    settings_defaults(out);               /* the two new fields, and only those */
    out->preset             = v1->preset;
    out->custom_lat         = v1->custom_lat;
    out->custom_lon         = v1->custom_lon;
    out->radius_nm          = v1->radius_nm;
    out->brightness_pct     = v1->brightness_pct;
    out->auto_dim           = v1->auto_dim;
    out->dim_from_hour      = v1->dim_from_hour;
    out->dim_to_hour        = v1->dim_to_hour;
    out->dim_brightness_pct = v1->dim_brightness_pct;
    /* custom_label and custom_tz stay empty: he has never searched, because
     * the screen that searches did not exist when this blob was written. The
     * settings card says so in coordinates, exactly as it did before. */
}

size_t settings_blob_size(void)
{
    return sizeof(settings_blob_t);
}

size_t settings_blob_read_size(void)
{
    return sizeof(settings_blob_any_t);
}

void settings_encode_blob(const settings_t *s, void *out)
{
    if (s == NULL || out == NULL) return;
    settings_blob_t blob = { .version = SETTINGS_VERSION, .s = *s };
    settings_sanitise(&blob.s);
    memcpy(out, &blob, sizeof blob);
}

bool settings_decode_blob(const void *blob, size_t len, settings_t *out)
{
    if (out == NULL) return false;
    settings_defaults(out);
    if (blob == NULL) return false;

    /* Size FIRST, then the version word. Reading blob->version out of a
     * buffer that is too short to hold one is undefined behaviour long
     * before it is a wrong answer, and a corrupt NVS entry is exactly the
     * case this function exists for. */
    if (len == sizeof(settings_blob_t)) {
        settings_blob_t v2;
        memcpy(&v2, blob, sizeof v2);
        if (v2.version == SETTINGS_VERSION) {
            *out = v2.s;
            settings_sanitise(out);
            return true;
        }
        return false;
    }
    if (len == sizeof(settings_blob_v1_t)) {
        settings_blob_v1_t v1;
        memcpy(&v1, blob, sizeof v1);
        if (v1.version == 1u) {
            migrate_v1(out, &v1.s);
            settings_sanitise(out);
            return true;
        }
    }
    return false;
}

#ifndef HOST_TEST

/* Declared inside the device-only block because its one use is: on the host
 * build there is no persistence and therefore no log line, and an unused TAG
 * at file scope is a warning in every host suite. Warning noise is not
 * cosmetic — a real -Wincompatible-pointer-types in test_view.c hid behind
 * exactly this kind of chatter for a whole session. */
static const char *TAG = "settings";
#include "nvs.h"
#include "nvs_flash.h"

#define SETTINGS_NS      "flight"
#define SETTINGS_KEY     "settings"

esp_err_t settings_load(settings_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    settings_defaults(out);

    nvs_handle_t h;
    if (nvs_open(SETTINGS_NS, NVS_READONLY, &h) != ESP_OK) {
        return ESP_OK;                       /* nothing saved yet — defaults */
    }
    /* Read into the LARGEST layout, then let settings_decode_blob() decide
     * what it was from its size and its version word. Reading into the
     * current layout and hoping is how the v1 blob would have been silently
     * dropped. */
    settings_blob_any_t blob;
    size_t len = sizeof blob;
    esp_err_t err = nvs_get_blob(h, SETTINGS_KEY, &blob, &len);
    nvs_close(h);

    if (err == ESP_OK && !settings_decode_blob(&blob, len, out)) {
        ESP_LOGW(TAG, "stored settings are %u bytes and no layout claims them; "
                      "starting from the defaults", (unsigned)len);
    }
    /* Always sanitise, including the defaults path: this is the single choke
     * point where a bad value can be caught before it reaches the panel. */
    settings_sanitise(out);
    ESP_LOGI(TAG, "location=%s radius=%d nm brightness=%d%% auto_dim=%d",
             location_name(out->preset), out->radius_nm,
             out->brightness_pct, (int)out->auto_dim);
    return ESP_OK;
}

esp_err_t settings_save(const settings_t *s)
{
    if (s == NULL) return ESP_ERR_INVALID_ARG;
    settings_blob_t blob;
    settings_encode_blob(s, &blob);

    nvs_handle_t h;
    esp_err_t err = nvs_open(SETTINGS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, SETTINGS_KEY, &blob, sizeof blob);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
#endif /* HOST_TEST */
