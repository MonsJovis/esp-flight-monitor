/* Display bring-up.
 *
 * This mirrors the BSP's own bsp_display_start_with_config(), with one deliberate
 * difference: it KEEPS the esp_lcd panel handle. The BSP stores it in a file-static
 * and exposes no accessor, but we need it for two things the plan requires —
 * reading the framebuffer back for on-device visual verification (debug/dbg_screen)
 * and varying the framebuffer count for the M1 bandwidth measurement.
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "lvgl.h"

esp_err_t               display_init(void);
esp_lcd_panel_handle_t  display_panel(void);
lv_display_t           *display_lv(void);

/* All LVGL calls happen behind this mutex, on the display task. Non-negotiable
 * on this hardware (AGENTS.md §10). */
bool display_lock(uint32_t timeout_ms);
void display_unlock(void);
