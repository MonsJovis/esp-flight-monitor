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

#include "data/fmt_de.h"    /* update_state_t */
#include "data/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The running build's version string, from esp_app_desc_t. Never NULL. */
const char *ota_running_version(void);

/* Update source. `out` gets "" when none is stored. */
void ota_get_url(char *out, size_t outsz);

/* True when an update source is stored at all. The UI asks before it offers
 * a "Nach Updates suchen" row, because on a device that left with no URL the
 * only answer that row could ever give is "Keine Verbindung" (D74). */
bool ota_has_url(void);

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

/* ---- the manual path (D74) ---------------------------------------------
 *
 * Everything above is the unattended one: check daily, install in the night
 * window. This is the other half — he is standing in front of the panel and
 * wants to know now.
 *
 * The callback is invoked FROM THE OTA TASK as the flow moves through
 * update_state_t, with the offered version for UPD_AVAILABLE and NULL
 * otherwise. The implementation takes display_lock() and touches LVGL, which
 * is the same thing geo_search_task() does and is safe for the same reason;
 * what it must not do is anything deep, because the task has about 6 KB of
 * stack left after a TLS handshake.
 *
 * It is also called for the DAILY check, not just a manual one. That is
 * deliberate: if an update installs itself at 03:00 the row should already
 * say so the next time he opens the screen, rather than claiming the state
 * from whenever he last looked. */
void ota_set_status_cb(void (*cb)(update_state_t state, const char *version));

/* Install the pending update NOW, without waiting for the night window.
 *
 * The window exists because a 2 MB flash write may tear this panel and
 * nobody should meet that while glancing at a clock. A tap on "Jetzt
 * installieren" is the one case where that reasoning does not apply: he is
 * looking at the panel on purpose, he asked for this, and the takeover tells
 * him what is happening. Every other gate still stands — https only, the
 * signature, the size, and the version that already rolled back once.
 *
 * Does nothing if no update is pending. Safe from any task. */
void ota_request_install_now(void);

/* Starts the background task that checks daily and installs at night. Safe to
 * call with no URL stored — it simply never does anything. */
void ota_start(void);

/* Cancels the rollback for the running image. Call once, after the device has
 * proved it can reach the network. Harmless when rollback is not pending. */
void ota_confirm_running_image(void);

#ifdef __cplusplus
}
#endif
