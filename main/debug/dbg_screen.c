#include "dbg_screen.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_lcd_panel_rgb.h"
#include <unistd.h>
#include <fcntl.h>
#include "ui/display.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "dbg";

static dbg_cmd_fn s_on_cmd;

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* 57 input bytes -> 76 output chars, the classic line width. */
#define RAW_PER_LINE 57

/* Writes through the ordinary console. Deliberately NOT through the
 * usb_serial_jtag driver: that driver's write blocks when its TX ring fills and
 * no host is draining, which is the normal state of this device — it sits on a
 * desk with nothing plugged into it. See docs/DECISIONS.md D22. */
static void emit(const char *s, size_t n)
{
    fwrite(s, 1, n, stdout);
    fflush(stdout);
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

    /* FRAME BUFFER 0 IS NOT NECESSARILY WHAT IS ON THE GLASS.
     *
     * esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb) hands back the FIRST
     * buffer, and this build has two (CONFIG_BSP_LCD_RGB_BUFFER_NUMS=2):
     * LVGL renders into them alternately, so after a single redraw buffer 0
     * still holds the frame BEFORE the change. Every screenshot of a screen
     * that has just been changed and then gone still was therefore one state
     * out of date — and said nothing about it, because the image was a
     * perfectly valid picture of the wrong moment.
     *
     * It went unnoticed for as long as it did because it only bites a STATIC
     * screen. Anything with an animation on it redraws continuously, both
     * buffers converge within a frame or two, and the grab is correct; the
     * loading states added in M11 photographed correctly for exactly that
     * reason, while the no-route fixture beside them came back twice showing
     * the state before it. Two readings, both wrong, neither complaining —
     * the fourth harness bug in this feature with that shape (PLAN.md M10).
     *
     * Redrawing the whole screen once per buffer leaves buffer 0 holding the
     * current frame whatever the rotation, for about a quarter of a second
     * on a command that already takes seconds to transfer. */
    for (int i = 0; i < CONFIG_BSP_LCD_RGB_BUFFER_NUMS; i++) {
        if (display_lock(2000)) {
            lv_obj_invalidate(lv_screen_active());
            display_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(120));
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
        int ci = fgetc(stdin);
        if (ci == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        char c = (char)ci;
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
    /* Non-blocking stdin: poll for a command byte instead of parking a task on
     * a read. Nothing here may ever block waiting for a host that is not there. */
    int fl = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, fl | O_NONBLOCK);

    ESP_LOGI(TAG, "debug console ready: s=shot f=fontcard b=bench m=metrics w=wifi n=net");

    for (;;) {
        int c = fgetc(stdin);
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (c == 's' || c == 'S') screenshot();
        else if (s_on_cmd && c != '\r' && c != '\n') s_on_cmd((char)c);
    }
}

void dbg_screen_start(dbg_cmd_fn on_cmd)
{
    s_on_cmd = on_cmd;
    xTaskCreate(dbg_task, "dbg", 4096, NULL, 3, NULL);
}
