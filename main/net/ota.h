/* Over-the-air update.
 *
 * The device spends half the year in Pattaya. "Plug it into a computer" is
 * not a repair plan at 9,000 km, which is why partitions.csv has carried two
 * 5 MB app slots since before there was any code to put in the second one.
 *
 * THIS IS OFF UNLESS AN UPDATE URL IS STORED IN NVS. A device that leaves
 * with no URL never contacts anything, never writes flash, and cannot be
 * updated — which is the correct behaviour for a device with nowhere
 * trustworthy to update from. Store one with ota_set_url(), or over serial
 * with the 'u' debug command.
 *
 * Two safety properties, both of which cost nothing until they are needed:
 *
 *   HTTPS only. CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP stays off, so a plain-HTTP
 *   URL is refused rather than quietly trusted. An unauthenticated firmware
 *   source is a remote root shell with extra steps.
 *
 *   Rollback. CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is on: a freshly written
 *   image boots once on probation, and unless it calls
 *   esp_ota_mark_app_valid_cancel_rollback() the bootloader reverts to the
 *   image that was working. ota_confirm_running_image() is what makes that
 *   call, and it makes it only after the new build has proved it can still
 *   reach the network — the failure mode worth guarding against is not a
 *   corrupt image (the bootloader checks the hash) but a working image that
 *   cannot get online, which is exactly the state you cannot fix remotely.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#include "data/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The running build's version string, from esp_app_desc_t. Never NULL. */
const char *ota_running_version(void);

/* Update source. `out` gets "" when none is stored. */
void ota_get_url(char *out, size_t outsz);

/* Stores (or, with NULL/"", clears) the manifest URL in NVS. Returns false if
 * the URL is not https:// or does not fit. */
bool ota_set_url(const char *url);

/* Fetches the manifest and reports whether it offers something newer than the
 * running build. `newer_version` (may be NULL) gets the offered version.
 *
 * Returns false both when nothing is newer and when the check failed; the two
 * are distinguished in the log, not in the return value, because no caller
 * behaves differently. */
/* STACK: a TLS handshake against the Mozilla root bundle needs roughly 8 KB
 * of stack. Call this only from a task that has it. It was first called from
 * the 4 KB debug-console task, and the overflow did not report itself as an
 * overflow — it corrupted the touch driver's context, and the device aborted
 * inside esp_lcd_touch_read_data() with ESP_ERR_INVALID_ARG a few hundred
 * milliseconds later. Use ota_request_check() from anywhere else. */
bool ota_check(char *newer_version, size_t vsz);

/* Asks the OTA task to check now, and returns immediately. Safe from any
 * task, including the 4 KB console one. */
void ota_request_check(void);

/* Downloads and writes the image named by the last successful ota_check(),
 * then reboots into it. Does not return on success.
 *
 * Call only when ota_should_install() says so: this writes ~2 MB to flash,
 * and flash writes tear this panel (espressif/esp-bsp#570). */
void ota_install_and_reboot(void);

/* Hands the OTA task a snapshot of the settings it needs — the night window,
 * which is the only thing it is allowed to install during. Call on boot and
 * whenever the settings change; the task must not reach into another module's
 * globals to read them. */
void ota_settings_update(const settings_t *s);

/* Starts the background task that checks daily and installs at night. Safe to
 * call with no URL stored — it simply never does anything. */
void ota_start(void);

/* Cancels the rollback for the running image. Call once, after the device has
 * proved it can reach the network. Harmless when rollback is not pending. */
void ota_confirm_running_image(void);

#ifdef __cplusplus
}
#endif
