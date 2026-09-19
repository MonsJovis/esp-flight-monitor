#include "settings.h"
#include <string.h>
#include "compat.h"

static const char *TAG = "settings";

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
    [LOC_CUSTOM]    = { "Eigener Ort", "UTC0",                     0.0,     0.0 },
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

#ifndef HOST_TEST
#include "nvs.h"
#include "nvs_flash.h"

#define SETTINGS_NS      "flight"
#define SETTINGS_KEY     "settings"
#define SETTINGS_VERSION 1u

typedef struct {
    uint32_t   version;
    settings_t s;
} settings_blob_t;

esp_err_t settings_load(settings_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    settings_defaults(out);

    nvs_handle_t h;
    if (nvs_open(SETTINGS_NS, NVS_READONLY, &h) != ESP_OK) {
        return ESP_OK;                       /* nothing saved yet — defaults */
    }
    settings_blob_t blob;
    size_t len = sizeof blob;
    esp_err_t err = nvs_get_blob(h, SETTINGS_KEY, &blob, &len);
    nvs_close(h);

    if (err == ESP_OK && len == sizeof blob && blob.version == SETTINGS_VERSION) {
        *out = blob.s;
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
    settings_blob_t blob = { .version = SETTINGS_VERSION, .s = *s };
    settings_sanitise(&blob.s);

    nvs_handle_t h;
    esp_err_t err = nvs_open(SETTINGS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, SETTINGS_KEY, &blob, sizeof blob);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
#endif /* HOST_TEST */
