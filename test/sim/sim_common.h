/* sim_common.h — what every screen simulator in test/sim shares: the
 * check macros, a 480x480 RGB565 framebuffer LVGL renders into in DIRECT
 * mode (the device's mode), a clock that only moves when the script says,
 * a scripted pointer, pixel helpers and a dependency-free PNG writer.
 * Header-only and all-static, because each simulator is one binary built
 * from one .c file (see Makefile). */
#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

/* Header-only and shared: not every simulator calls every helper. */
#define SIM_UNUSED __attribute__((unused))

#define W 480
#define H 480

/* ---- harness ----------------------------------------------------------- */

static int t_run, t_fail;
#define CHECK(cond, ...) do {                                              \
    t_run++;                                                               \
    if (!(cond)) {                                                         \
        t_fail++;                                                          \
        printf("    FAIL  %s:%d  %s — ", __FILE__, __LINE__, #cond);       \
        printf(__VA_ARGS__);                                               \
        printf("\n");                                                      \
    }                                                                      \
} while (0)
#define GROUP(name) printf("\n  %s\n", (name))

static uint16_t    s_fb[W * H];
static uint32_t    s_now_ms;
static int32_t     s_px, s_py;
static bool        s_down;
static const char *s_outdir = ".";

static SIM_UNUSED uint32_t tick_cb(void) { return s_now_ms; }

/* DIRECT mode, rendering straight into s_fb — the device's mode
 * (CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=y, two framebuffers kept in sync by
 * esp_lvgl_port). Only INVALIDATED areas are redrawn and everything else
 * keeps last frame's pixels, which is the whole point: a first version used
 * FULL mode, repainted every pixel every frame, and so could not see a
 * forgotten lv_obj_invalidate() — a mutant that removed the trail layer's
 * invalidate survived it. Two synced buffers behave as one for this purpose. */
static SIM_UNUSED void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    (void)a; (void)px;
    lv_display_flush_ready(d);
}

static SIM_UNUSED void read_cb(lv_indev_t *i, lv_indev_data_t *d)
{
    (void)i;
    d->point.x = s_px;
    d->point.y = s_py;
    d->state   = s_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* Advances the clock in 5 ms steps, running LVGL at each. */
static SIM_UNUSED void run_ms(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 5) {
        s_now_ms += 5;
        lv_timer_handler();
    }
}

static SIM_UNUSED void touch_down(int32_t x, int32_t y) { s_px = x; s_py = y; s_down = true;  run_ms(60); }
static SIM_UNUSED void touch_up(void)                    {                      s_down = false; run_ms(60); }
static SIM_UNUSED void tap(int32_t x, int32_t y)         { touch_down(x, y); run_ms(40); touch_up(); }
static SIM_UNUSED void render(void)                      { lv_refr_now(NULL); }

/* ---- pixels ------------------------------------------------------------ */

static SIM_UNUSED uint16_t to565(lv_color_t c)
{
    return (uint16_t)(((c.red >> 3) << 11) | ((c.green >> 2) << 5) | (c.blue >> 3));
}

typedef struct { int n; double cx, cy; } blob_t;

/* Every pixel inside the box that is EXACTLY `c`. */
static SIM_UNUSED blob_t find(lv_color_t c, int x0, int y0, int x1, int y1)
{
    uint16_t want = to565(c);
    blob_t   b    = { 0, 0, 0 };
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (s_fb[y * W + x] == want) {
                b.n++;
                b.cx += x;
                b.cy += y;
            }
        }
    }
    if (b.n > 0) {
        b.cx /= b.n;
        b.cy /= b.n;
    }
    return b;
}

/* Rendered width, unwrapped — the same call screen_radar.c's fit uses. */
static SIM_UNUSED int text_w(const char *txt, const lv_font_t *f)
{
    lv_point_t p;
    lv_text_get_size(&p, txt, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return (int)p.x;
}

static SIM_UNUSED void put32(FILE *f, uint32_t v)
{
    uint8_t q[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    fwrite(q, 1, 4, f);
}

static SIM_UNUSED void png_chunk(FILE *f, const uint32_t *crc_tab, const char *type,
                      const uint8_t *data, size_t len)
{
    put32(f, (uint32_t)len);
    fwrite(type, 1, 4, f);
    if (len > 0) fwrite(data, 1, len, f);
    uint32_t c = 0xFFFFFFFFu;
    for (int k = 0; k < 4; k++) c = crc_tab[(c ^ (uint8_t)type[k]) & 0xFF] ^ (c >> 8);
    for (size_t k = 0; k < len; k++) c = crc_tab[(c ^ data[k]) & 0xFF] ^ (c >> 8);
    put32(f, c ^ 0xFFFFFFFFu);
}

static SIM_UNUSED void png_write(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.png", s_outdir, name);
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        printf("    (could not write %s)\n", path);
        return;
    }
    /* Uncompressed PNG: zlib "stored" blocks. Big files, zero dependencies. */
    static uint32_t crc_tab[256];
    if (crc_tab[1] == 0) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_tab[n] = c;
        }
    }
    size_t   row = 1 + W * 3, raw_len = row * H;
    uint8_t *raw = malloc(raw_len);
    for (int y = 0; y < H; y++) {
        raw[y * row] = 0;
        for (int x = 0; x < W; x++) {
            uint16_t p = s_fb[y * W + x];
            uint8_t *o = &raw[y * row + 1 + x * 3];
            o[0] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
            o[1] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
            o[2] = (uint8_t)((p & 31) * 255 / 31);
        }
    }
    size_t   nblk = (raw_len + 65534) / 65535, z_len = 2 + raw_len + nblk * 5 + 4;
    uint8_t *z = malloc(z_len), *zp = z;
    *zp++ = 0x78; *zp++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw_len; off += 65535) {
        size_t n = raw_len - off < 65535 ? raw_len - off : 65535;
        *zp++ = (off + n == raw_len) ? 1 : 0;
        *zp++ = (uint8_t)n; *zp++ = (uint8_t)(n >> 8);
        *zp++ = (uint8_t)~n; *zp++ = (uint8_t)(~n >> 8);
        memcpy(zp, raw + off, n); zp += n;
    }
    for (size_t i = 0; i < raw_len; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    uint32_t ad = (b << 16) | a;
    *zp++ = (uint8_t)(ad >> 24); *zp++ = (uint8_t)(ad >> 16); *zp++ = (uint8_t)(ad >> 8); *zp++ = (uint8_t)ad;

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13] = { 0, 0, W >> 8, W & 255, 0, 0, H >> 8, H & 255, 8, 2, 0, 0, 0 };
    png_chunk(f, crc_tab, "IHDR", ihdr, 13);
    png_chunk(f, crc_tab, "IDAT", z, (size_t)(zp - z));
    png_chunk(f, crc_tab, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}
