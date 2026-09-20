/* When to update, and whether an update is even newer. No ESP-IDF, no network
 * — pure decisions, so the rules can be tested on the host in milliseconds
 * instead of by waiting for a night to pass on real hardware.
 *
 * The device-side plumbing (TLS, flash writes, rollback) is main/net/ota.c.
 * This file is the part with the judgement in it.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_VERSION_LEN 32
#define OTA_URL_LEN     192

typedef struct {
    char     version[OTA_VERSION_LEN];  /* "0.4.2" */
    char     url[OTA_URL_LEN];          /* where the .bin is */
    uint32_t size;                      /* bytes; 0 when the manifest omits it */
} ota_manifest_t;

/* Parses the update manifest:
 *
 *   { "version": "0.4.2",
 *     "url": "https://example.org/esp-flight-monitor-0.4.2.bin",
 *     "size": 2209600 }
 *
 * Returns false — and leaves *out zeroed — on anything malformed, missing or
 * too long to hold. A manifest that does not parse must never be treated as
 * "no update"; it is an error, and the caller has to be able to tell the two
 * apart. `size` is optional; everything else is required.
 */
bool ota_manifest_parse(const char *json, size_t len, ota_manifest_t *out);

/* Compares two dotted version strings. Returns <0, 0 or >0 like strcmp.
 *
 * Tolerates a leading "v" and anything after the numeric part, so the strings
 * esp_app_desc_t actually carries — "0.4.2", "v0.4.2", "0.4.2-3-gdeadbee",
 * "0.4.2-dirty" — all compare as 0.4.2. Missing components count as zero, so
 * "1.2" == "1.2.0". Components compare NUMERICALLY: "0.10.0" is newer than
 * "0.9.0", which a strcmp would get backwards, and getting that backwards
 * means the device refuses the fix you sent it.
 */
int ota_version_cmp(const char *a, const char *b);

/* Everything the two decisions below are allowed to look at. Passed in rather
 * than read from globals so a test can put the device in any state without a
 * clock, a radio or a night. */
typedef struct {
    bool    have_url;       /* an update source is configured at all */
    bool    online;
    bool    clock_valid;    /* SNTP has answered */
    bool    auto_dim;       /* the night window is enabled */
    int     hour;           /* local hour, 0..23 */
    int     dim_from_hour;  /* inclusive */
    int     dim_to_hour;    /* exclusive */
    int64_t now_ms;
    int64_t last_check_ms;  /* 0 = never checked */
} ota_ctx_t;

/* 24 hours. He is not waiting for a feature; this only has to be faster than
 * a flight to Bangkok. */
#define OTA_CHECK_INTERVAL_MS (24 * 60 * 60 * 1000LL)

/* True when it is time to fetch the manifest. Needs a URL, a network and a
 * clock.
 *
 * The caller's `last_check_ms` is not persisted anywhere, so this is an
 * interval per UPTIME, not per day: a device that reboots checks again
 * straight away. That is deliberate — a manifest fetch is one small GET, and
 * persisting the timestamp would cost an NVS write a day to save it — but it
 * is not what this comment used to claim, which was that the clock gate
 * stopped a re-check on every reboot. It does not. */
bool ota_should_check(const ota_ctx_t *ctx);

/* True when an update that is known to be available may be downloaded and
 * written NOW.
 *
 * The gate is the night dim window, and the reason is not politeness. Writing
 * 2 MB to flash may tear this panel — espressif/esp-bsp#570 on this exact
 * silicon, which docs/PLAN.md lists as one of the three risks the whole build
 * order exists to retire.
 *
 * Note the "may", because D29 measured the same bug and did NOT reproduce it:
 * an NVS commit costs 3 us against a 49.7 ms worst-case frame gap, and
 * sustained commits under a moving high-contrast pattern produced no visible
 * tearing. But that is the SMALL-WRITE case. A 2 MB continuous write to
 * another partition is a different workload, it has never been measured here
 * (the download path is one of the two things this project has not
 * exercised), and the cost of being wrong is the screen garbling for a minute
 * in front of the one person who must never see this thing look broken. At
 * 3 a.m. the precaution costs nothing, so it stays until someone measures it.
 *
 * With auto_dim off there is no window, so there is no safe hour, and the
 * answer is never. That is deliberate: a device whose owner has turned off the
 * night schedule has told us it may be looked at at any hour.
 */
bool ota_should_install(const ota_ctx_t *ctx);

/* Is `hour` inside [from, to)? Exposed because it wraps midnight and that is
 * exactly the kind of arithmetic that looks right and is not — the same
 * wrap settings_brightness_for_hour() already gets right, kept in one place
 * conceptually even though the two cannot share a translation unit. */
bool ota_hour_in_window(int hour, int from_hour, int to_hour);

#ifdef __cplusplus
}
#endif
