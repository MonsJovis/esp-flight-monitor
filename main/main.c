/* M0/M1 bring-up: prove the panel, the touch controller and the memory budget.
 * The numbers this logs go straight into the table at the bottom of docs/PLAN.md. */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "ui/display.h"
#include "debug/dbg_screen.h"

static const char *TAG = "flight";

static void log_memory_budget(const char *when)
{
    ESP_LOGI(TAG, "MEM %-38s internal %7u (max blk %6u)  psram %8u (max blk %8u)",
             when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

void app_main(void)
{
    log_memory_budget("boot, before display init");

    ESP_ERROR_CHECK(display_init());
    bsp_display_brightness_init();
    bsp_display_backlight_on();

    log_memory_budget("after display init (framebuffer up)");
    dbg_screen_start();

    display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0B0D), 0);

    /* Deliberately exercises the umlauts and the arrow: if the font subset is
     * wrong these render as blanks, and that is the whole M1 font gate. */
    lv_obj_t *l = lv_label_create(scr);
    lv_label_set_text(l, "Wien -> London\n"LV_SYMBOL_OK" Bereit");
    lv_obj_set_style_text_color(l, lv_color_hex(0x00E676), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    display_unlock();

    ESP_LOGI(TAG, "up; send 's' over serial for a screenshot");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        log_memory_budget("steady state");
    }
}
