/* SNTP, with the timezone bound to the location rather than set by hand.
 *
 * He must never set a clock (AGENTS.md §6). The device travels between Austria
 * and Thailand twice a year and the offset changes by 5-6 hours depending on
 * the season, so the timezone follows the location preset — which is also why
 * this takes a POSIX TZ string rather than an offset.
 *
 * ESP-IDF's newlib has no locales, so strftime() cannot produce German names;
 * that is main/data/fmt_de.c's job. This module only gets the instant right.
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>

#define TZ_GLOGGNITZ "CET-1CEST,M3.5.0,M10.5.0/3"   /* Austria, with DST rules */
#define TZ_PATTAYA   "ICT-7"                         /* Thailand, no DST       */

esp_err_t timesync_start(const char *posix_tz);
void      timesync_set_tz(const char *posix_tz);
bool      timesync_is_set(void);
