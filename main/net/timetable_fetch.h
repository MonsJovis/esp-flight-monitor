/* timetable_fetch.h — keeps the CI's timetable on the device (D94).
 *
 * A GitHub Action builds a CSV of every flight at Vienna from the Lufthansa
 * Open API and pushes it to the `timetable` branch of this repo
 * (.github/workflows/timetable.yml, tools/build_timetable.py). This module
 * fetches that file once an hour over HTTPS, parses it into PSRAM, and lets
 * the UI task annotate a snapshot with it. The matching itself is pure and
 * lives in data/timetable.c.
 *
 * Until the workflow has published anything the file is a 404, and every
 * aircraft keeps the name it had before (D89). Nothing here can make the
 * panel show less than it did.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "flight_types.h"

/* Where the CI puts it. raw.githubusercontent.com answers 200 directly, with
 * headers that fit esp_http_client's default buffer (longest line 80 bytes,
 * measured 2026-10-19), so http_get() fetches it as is. A release asset
 * would need OTA's redirect loop and 8 KB buffers. */
#define TIMETABLE_URL \
    "https://raw.githubusercontent.com/MonsJovis/esp-flight-monitor/timetable/vie.csv"

/* The CI runs every two hours; asking hourly picks up a new file within an
 * hour of it landing, for one small request. */
#define TIMETABLE_FETCH_EVERY_MS  (60LL * 60 * 1000)

/* Creates the lock. Call once, before the tasks that use this start. */
void timetable_init(void);

/* Fetches the file when an hour has passed since the last attempt (or there
 * has been none). Blocks for the length of one request, so call it from the
 * network task — flight_source's poll loop does. */
void timetable_fetch_if_due(void);

/* Writes `flight_no` into every aircraft the timetable matches to exactly one
 * flight, and clears it on every other. `rt` is the snapshot's route array,
 * searched by callsign (route_find), never paired by index. `now_utc` is Unix
 * seconds, or 0 while the clock is not set, which matches nothing. Returns how
 * many aircraft got a number. Safe before timetable_init(): it then clears
 * and returns 0. */
int timetable_annotate(aircraft_t *ac, int n, const route_t *rt, int n_rt,
                       int64_t now_utc);

/* For the console's `n`: flights held, and when the CI generated them
 * (0 = nothing loaded). */
void timetable_status(int *flights, int64_t *generated);
