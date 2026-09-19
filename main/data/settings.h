/* Everything the device remembers about where it is and how it should look.
 *
 * The device travels between Austria and Thailand twice a year, carried by
 * someone who will not read a manual (AGENTS.md §6), so this is deliberately
 * small: named places, one tap each. Coordinates are an advanced escape
 * hatch, not the path.
 *
 * The timezone is bound to the location rather than set separately — he never
 * sets a clock. That is a product rule, not an implementation convenience, and
 * it is why location_tz() lives here next to the coordinates.
 *
 * The pure parts of this build on the host; persistence is device-only.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

/* These values are WRITTEN TO NVS. Append new places at the end and never
 * renumber: inserting LOC_WIEN in the middle would turn a device that had
 * LOC_CUSTOM stored as 2 into one sitting in Vienna after a firmware update,
 * silently and with no way for him to know why the distances stopped making
 * sense. The order he SEES is location_display_order(), which is separate
 * precisely so this list can stay append-only. */
typedef enum {
    LOC_GLOGGNITZ = 0,
    LOC_PATTAYA   = 1,
    LOC_CUSTOM    = 2,
    LOC_WIEN      = 3,
    LOC_COUNT
} location_preset_t;

typedef struct {
    location_preset_t preset;
    double            custom_lat, custom_lon;  /* used only by LOC_CUSTOM */
    int               radius_nm;               /* 10..100                 */
    int               brightness_pct;          /* 10..100                 */
    bool              auto_dim;
    int               dim_from_hour;           /* inclusive, local time    */
    int               dim_to_hour;             /* exclusive, local time    */
    int               dim_brightness_pct;
} settings_t;

/* Display name, as it appears on the settings screen. */
const char *location_name(location_preset_t p);

/* The presets in the order the settings screen should show them: the real
 * places first, grouped sensibly, with the coordinate escape hatch last.
 * Decoupled from the enum because the enum is persisted (see above).
 * `i` is 0..LOC_COUNT-1; out of range returns LOC_GLOGGNITZ. */
location_preset_t location_display_order(int i);

/* POSIX TZ string for the preset. See main/net/timesync.h. */
const char *location_tz(location_preset_t p);

/* Where to poll. Falls back to Gloggnitz for a nonsense preset rather than
 * polling 0,0 in the Atlantic. */
void settings_coords(const settings_t *s, double *lat, double *lon);

/* Brightness to apply right now, honouring the dim schedule. `hour` is local
 * 0..23. Returns brightness_pct when the schedule is off or out of window. */
int settings_brightness_for_hour(const settings_t *s, int hour);

void settings_defaults(settings_t *out);

/* Clamps everything into range. Anything read from NVS goes through this, so a
 * corrupt or downgraded blob cannot produce a black screen or a 500 nm poll. */
void settings_sanitise(settings_t *s);

#ifndef HOST_TEST
#include "esp_err.h"
esp_err_t settings_load(settings_t *out);   /* defaults when absent */
esp_err_t settings_save(const settings_t *s);
#endif
