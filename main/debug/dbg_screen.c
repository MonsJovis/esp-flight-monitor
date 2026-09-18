#include "dbg_screen.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "ui/display.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "dbg";

static dbg_cmd_fn s_on_cmd;

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* 57 input bytes -> 76 output chars, the classic line width. */
#define RAW_PER_LINE 57

static void emit(const char *s, size_t n)
{
    size_t sent = 0;
    while (sent < n) {
        int w = usb_serial_jtag_write_bytes(s + sent, n - sent, pdMS_TO_TICKS(1000));
        if (w <= 0) break;
        sent += (size_t)w;
    }
}

static void emit_str(const char *s) { emit(s, strlen(s)); }

static void b64_line(const uint8_t *in, size_t n, char *out)
{
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n) v |= (uint32_t)in[i + 2];
        out[o++] = B64[(v >> 18) & 0x3F];
        out[o++] = B64[(v >> 12) & 0x3F];
        out[o++] = (i + 1 < n) ? B64[(v >> 6) & 0x3F] : '=';
        out[o++] = (i + 2 < n) ? B64[v & 0x3F]        : '=';
    }
    out[o++] = '\n';
    out[o]   = '\0';
}

static void screenshot(void)
{
    esp_lcd_panel_handle_t panel = display_panel();
    if (!panel) { ESP_LOGE(TAG, "no panel handle"); return; }

    void *fb = NULL;
    if (esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb) != ESP_OK || !fb) {
        ESP_LOGE(TAG, "framebuffer unavailable");
        return;
    }

    /* Hold the LVGL lock for the whole capture. Otherwise anything that
     * redraws — the perf monitor's FPS label alone is enough — mutates the
     * framebuffer between the CRC and the transfer, and every grab fails. */
    if (!display_lock(2000)) { ESP_LOGE(TAG, "could not lock display"); return; }

    const size_t n = (size_t)BSP_LCD_H_RES * BSP_LCD_V_RES * 2;
    const uint8_t *p = (const uint8_t *)fb;
    uint32_t crc = esp_rom_crc32_le(0, p, n);

    char hdr[128];
    snprintf(hdr, sizeof hdr,
             "\n<<<SHOT w=%d h=%d fmt=rgb565 bytes=%u crc=%08x>>>\n",
             BSP_LCD_H_RES, BSP_LCD_V_RES, (unsigned)n, (unsigned)crc);
    emit_str(hdr);

    char line[80];
    for (size_t off = 0; off < n; off += RAW_PER_LINE) {
        size_t chunk = (n - off) < RAW_PER_LINE ? (n - off) : RAW_PER_LINE;
        b64_line(p + off, chunk, line);
        emit(line, strlen(line));
    }
    emit_str("<<<ENDSHOT>>>\n");
    display_unlock();
}

int dbg_read_line(char *out, size_t out_sz, int timeout_ms)
{
    size_t n = 0;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (n + 1 < out_sz && xTaskGetTickCount() < deadline) {
        uint8_t c;
        if (usb_serial_jtag_read_bytes(&c, 1, pdMS_TO_TICKS(200)) != 1) {
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (n == 0) continue;      /* tolerate CRLF and stray newlines */
            break;
        }
        out[n++] = (char)c;
    }
    out[n] = '\0';
    return (n == 0) ? -1 : (int)n;
}

static void dbg_task(void *arg)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.tx_buffer_size = 2048;
    cfg.rx_buffer_size = 1024;
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "usb_serial_jtag driver install failed");
        vTaskDelete(NULL);
        return;
    }
    usb_serial_jtag_vfs_use_driver();   /* console shares the driver; no peripheral fight */
    ESP_LOGI(TAG, "debug console ready: s=screenshot, b=bench, f=fontcard");

    uint8_t c;
    for (;;) {
        int n = usb_serial_jtag_read_bytes(&c, 1, pdMS_TO_TICKS(500));
        if (n != 1) continue;
        if (c == 's' || c == 'S') screenshot();
        else if (s_on_cmd && c != '\r' && c != '\n') s_on_cmd((char)c);
    }
}

void dbg_screen_start(dbg_cmd_fn on_cmd)
{
    s_on_cmd = on_cmd;
    xTaskCreate(dbg_task, "dbg", 4096, NULL, 3, NULL);
}
