/* timetable_fetch.h — keeps the CI's callsign pairs on the device (D94, D95).
 *
 * A GitHub Action reads Vienna's airport board from AeroDataBox twice a day,
 * keeps the callsign/flight-number pairs it saw in the last six days,
 * encrypts them and pushes the file to the `timetable` branch of this repo
 * (.github/workflows/timetable.yml, tools/build_timetable.py). This module
 * fetches that file once an hour over HTTPS, decrypts it with the key in NVS
 * (timetable_crypt.h), parses it into PSRAM, and lets the UI task annotate a
 * snapshot with it. The lookup itself is pure and lives in data/timetable.c.
 *
 * Without a key it asks for nothing. Without a published file it gets a 404.
 * Either way every aircraft keeps the name it had before (D89): nothing here
 * can make the panel show less than it did.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "flight_types.h"

/* Where the CI puts it. raw.githubusercontent.com answers 200 directly, with
 * headers that fit esp_http_client's default buffer (longest line 80 bytes,
 * measured 2026-10-19), so http_get() fetches it as is. */
#define TIMETABLE_URL \
    "https://raw.githubusercontent.com/MonsJovis/esp-flight-monitor/timetable/vie.tt"

/* The CI runs every twelve hours; asking hourly picks a new file up within an
 * hour of it landing, for one request. */
#define TIMETABLE_FETCH_EVERY_MS  (60LL * 60 * 1000)

/* Creates the lock and loads the key from NVS. Call once, before the tasks
 * that use this start. */
void timetable_init(void);

/* Fetches the file when an hour has passed since the last attempt, or there
 * has been none, or a key was just stored. Blocks for the length of one
 * request, so call it from the network task: flight_source's poll loop does.
 * Does nothing at all while no key is stored. */
void timetable_fetch_if_due(void);

/* Writes `flight_no` into every aircraft the pairs name with certainty, and
 * clears it on every other. `rt` is the snapshot's route array, searched by
 * callsign (route_find), never paired by index. `now_utc` is Unix seconds, or
 * 0 while the clock is not set, which only skips the age check. Returns how
 * many aircraft got a number. Safe before timetable_init(). */
int timetable_annotate(aircraft_t *ac, int n, const route_t *rt, int n_rt,
                       int64_t now_utc);

/* The key, from the console (`T`). Stored in NVS; never logged. Storing one
 * makes the next poll fetch. */
bool timetable_set_key(const uint8_t key[32]);
bool timetable_clear_key(void);
bool timetable_has_key(void);

/* For the console's `n`: pairs held, and when CI generated them (0 = nothing
 * loaded). */
void timetable_status(int *pairs, int64_t *generated);
