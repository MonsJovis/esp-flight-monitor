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

/* The three LOC_CUSTOM fields below are sized here rather than included from
 * main/net/geo_parse.h on purpose: the data layer does not depend on the
 * network layer anywhere else in this tree, and a settings struct that has to
 * pull in a parser to know how long a string is would be the first exception.
 * They are the same numbers as GEO_LABEL_LEN/GEO_TZ_LEN, and two things keep
 * them that way: a _Static_assert in main.c, where the two are copied between,
 * and a check in test/host/test_geo.c for the host build. */
#define SETTINGS_LABEL_LEN 72   /* "Innsbruck · Tirol"           */
#define SETTINGS_TZ_LEN    40   /* a POSIX TZ string             */

typedef struct {
    location_preset_t preset;
    double            custom_lat, custom_lon;  /* used only by LOC_CUSTOM */
    /* What he chose in the search (§5.8), so the card can say where the
     * device thinks it is standing instead of reciting four decimal places
     * at him. Empty until he has searched once — that state is not a bug and
     * the card has a line for it. */
    char              custom_label[SETTINGS_LABEL_LEN];
    /* The POSIX timezone of that place, because AGENTS.md §6 binds the clock
     * to the location and a custom location has to keep that promise too.
     * Empty falls back to Gloggnitz's rule rather than to UTC — see
     * settings_tz(). */
    char              custom_tz[SETTINGS_TZ_LEN];
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

/* POSIX TZ string for a PRESET. See main/net/timesync.h.
 *
 * LOC_CUSTOM has no fixed answer here — its timezone lives in the settings,
 * not in the preset table — so this returns Gloggnitz's rule for it. Call
 * settings_tz() instead wherever a settings_t is in hand, which is
 * everywhere that matters; this stays public only because the preset table
 * is what the location cards are built from. */
const char *location_tz(location_preset_t p);

/* The POSIX TZ string to actually run the clock on: the preset's, or the one
 * the search stored for LOC_CUSTOM.
 *
 * AGENTS.md §6 — "Timezone changes with the location ... do not make him set
 * a clock" — is why this exists rather than a separate timezone setting. A
 * custom location whose stored rule is empty (a blob written before the
 * search existed, or a place the geocoder had no zone for) falls back to
 * GLOGGNITZ, not to UTC. UTC was the old behaviour and it was wrong in the
 * one way that is guaranteed to be noticed: the panel's clock was two hours
 * out, in Austria, with no explanation on screen. */
const char *settings_tz(const settings_t *s);

/* Where to poll. Falls back to Gloggnitz for a nonsense preset rather than
 * polling 0,0 in the Atlantic. */
void settings_coords(const settings_t *s, double *lat, double *lon);

/* Brightness to apply right now, honouring the dim schedule. `hour` is local
 * 0..23. Returns brightness_pct when the schedule is off or out of window. */
int settings_brightness_for_hour(const settings_t *s, int hour);

void settings_defaults(settings_t *out);

/* ---- The NVS blob ------------------------------------------------------
 *
 * Exposed so the host suite can reach it. This is the code that decides
 * whether a device that has just been updated still knows where it is, and
 * it used to sit behind #ifndef HOST_TEST where nothing could test it.
 * settings_load()/settings_save() are thin wrappers over these.
 */

/* Bytes settings_save() writes — the CURRENT layout. */
size_t settings_blob_size(void);

/* Bytes a reader must have room for: the largest layout this firmware still
 * understands, so one read can fetch whichever version is stored. */
size_t settings_blob_read_size(void);

/* Writes a current-layout blob (settings_blob_size() bytes) into `out`,
 * sanitising on the way — nothing reaches flash unclamped. */
void settings_encode_blob(const settings_t *s, void *out);

/* Reads `len` bytes back. Recognises the current layout and every older one
 * still supported, migrating as needed. Returns false when no layout claims
 * those bytes, in which case `out` holds the defaults — never half a
 * migration. `out` is always sanitised. */
bool settings_decode_blob(const void *blob, size_t len, settings_t *out);

/* Clamps everything into range. Anything read from NVS goes through this, so a
 * corrupt or downgraded blob cannot produce a black screen or a 500 nm poll. */
void settings_sanitise(settings_t *s);

#ifndef HOST_TEST
#include "esp_err.h"
esp_err_t settings_load(settings_t *out);   /* defaults when absent */
esp_err_t settings_save(const settings_t *s);
#endif
