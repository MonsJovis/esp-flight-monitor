/* Display and touch bring-up.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Portions Copyright Waveshare / Espressif Systems, from the
 * esp32_s3_touch_lcd_4b BSP, used under the Apache License 2.0.
 *
 * About sixty lines of this file reproduce the BSP's init path rather than
 * calling bsp_display_start(), because the BSP keeps the
 * esp_lcd_panel_handle_t in a file-static with no accessor and this project
 * needs it twice: to read the framebuffer back over USB (D4) and to vary the
 * framebuffer count for the M1 bandwidth measurement (D5). AGENTS.md §9 says
 * copying it is fine and the headers must be kept — this is that header,
 * added late. See THIRD-PARTY.md.
 *
 * The file as a whole therefore carries Apache-2.0 rather than the project's
 * MIT: it is the stricter of the two and the two are compatible, so the
 * combined file is governed by it. That is the honest label for a file with
 * somebody else's code in it, and it costs a reuser nothing.
 */
#include "display.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"
#include "bsp/touch.h"

static const char *TAG = "display";

static esp_lcd_panel_handle_t    s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io    = NULL;
static lv_display_t             *s_disp  = NULL;
static esp_lcd_touch_handle_t    s_touch = NULL;

esp_err_t display_init(void)
{
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    bsp_display_config_t disp_config = { 0 };
    ESP_ERROR_CHECK(bsp_display_new(&disp_config, &s_panel, &s_io));

#if CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR
    const int buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES;
#else
    const int buffer_size = BSP_LCD_H_RES * LVGL_BUFFER_HEIGHT;
#endif

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle    = s_io,
        .panel_handle = s_panel,
        .buffer_size  = buffer_size,
        .monochrome   = false,
        .hres         = BSP_LCD_H_RES,
        .vres         = BSP_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation     = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = {
            .sw_rotate   = true,
            .buff_dma    = false,
            .buff_spiram = false,
#if CONFIG_BSP_DISPLAY_LVGL_FULL_REFRESH
            .full_refresh = 1,
#elif CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE
            .direct_mode  = 1,
#endif
            .swap_bytes  = false,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = 0,
#if CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR
            .avoid_tearing = true,
#else
            .avoid_tearing = false,
#endif
        },
    };

    s_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (!s_disp) {
        ESP_LOGE(TAG, "lvgl_port_add_disp_rgb failed");
        return ESP_FAIL;
    }

    /* GT911 INT sits behind the TCA9554 expander, so the port polls it. */
    ESP_ERROR_CHECK(bsp_touch_new(NULL, &s_touch));
    const lvgl_port_touch_cfg_t touch_cfg = { .disp = s_disp, .handle = s_touch };
    if (!lvgl_port_add_touch(&touch_cfg)) {
        ESP_LOGE(TAG, "lvgl_port_add_touch failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "display up: %dx%d, %d framebuffer(s), avoid_tearing=%d",
             BSP_LCD_H_RES, BSP_LCD_V_RES, CONFIG_BSP_LCD_RGB_BUFFER_NUMS,
             rgb_cfg.flags.avoid_tearing);
    return ESP_OK;
}

esp_lcd_panel_handle_t display_panel(void) { return s_panel; }
lv_display_t          *display_lv(void)    { return s_disp;  }
bool display_lock(uint32_t t)              { return lvgl_port_lock(t); }
void display_unlock(void)                  { lvgl_port_unlock(); }
