#include "axp2101.h"

#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "axp";

#define AXP_ADDR        0x34
#define AXP_SCL_HZ      400000      /* the bus speed AGENTS.md §2 records */
#define AXP_TIMEOUT_MS  100

/* Registers, numbered as the AXP2101 datasheet numbers them. Only these. */
#define REG_STATUS1     0x00    /* [5] vbus good, [3] battery present */
#define REG_STATUS2     0x01    /* [6:5] current direction, [2:0] charge status */
#define REG_CHIP_ID     0x03    /* undocumented in the datasheet; logged, never gated on */
#define REG_IIN_LIMIT   0x16
#define REG_PWRON_STS   0x20    /* why it last powered on */
#define REG_PWROFF_STS  0x21    /* why it last powered off */
#define REG_PWROFF_EN   0x22    /* [1] the side PWRKEY may power the device off, [0] off vs restart */
#define REG_KEY_LEVELS  0x27    /* [3:2] how long a press has to be to count as off */
#define REG_MODULE_EN   0x18    /* [3] gauge, [1] cell charger */
#define REG_ADC_EN      0x30
#define REG_VBAT_H      0x34
#define REG_VBUS_H      0x38
#define REG_TS_CTRL     0x50
#define REG_IPRECHG     0x61
#define REG_ICC         0x62
#define REG_ITERM       0x63
#define REG_CV          0x64
#define REG_BAT_DETECT  0x68
#define REG_GAUGE_PCT   0xA4

/* ---- the four numbers that are actually decisions ----------------------
 *
 * CHARGE CURRENT — 500 mA. The cell fitted is a 2000 mAh 103450, so this is
 * 0.25C against a datasheet recommendation of 0.5C. Slower on purpose: the
 * device sits on USB roughly 23.5 hours out of 24, so charge time is the one
 * thing about this that genuinely does not matter, while heat in a sealed
 * 86 x 86 x 14 mm box in a Thai living room does. Raise to 0x0E (800 mA) if
 * a fast turnaround ever matters more than that.
 *
 * TARGET VOLTAGE — 4.1 V, not the 4.2 V default. Keep BAT_CHARGE_TARGET_MV
 * in battery_policy.h in step with this: it is the top of the fallback
 * percentage curve, and a curve that disagrees with the charger prints
 * "95 % voll geladen". This costs about 15 % of
 * the cell's capacity and buys back most of its calendar life. A cell that
 * is held at full charge and kept warm is the one that swells, and this one
 * will be held at full charge permanently, warm, behind glass, 20 cm from
 * his face. Four hours of runtime minus 15 % is still six times the half
 * hour that was asked for.
 *
 * PRECHARGE — 50 mA, the gentle end, for the case where a protected cell has
 * been sitting flat in a drawer.
 *
 * TERMINATION — 100 mA, 0.05C, the textbook figure for this chemistry.
 */
#define ICC_500MA       0x0B
#define CV_4V1          0x02
#define IPRECHG_50MA    0x02
#define ITERM_EN_100MA  0x14    /* bit 4 enables termination, [3:0] = 100 mA */
#define IIN_LIMIT_1A5   0x04    /* the POR default, written out so it lives here and not in an eFuse */

static i2c_master_dev_handle_t s_dev;
static SemaphoreHandle_t       s_lock;
static bool                    s_ready;

/* ---- bus --------------------------------------------------------------- */

static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n,
                                       AXP_TIMEOUT_MS);
}

static esp_err_t reg_read8(uint8_t reg, uint8_t *val)
{
    return reg_read(reg, val, 1);
}

static esp_err_t reg_write8(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { reg, val };
    return i2c_master_transmit(s_dev, tx, sizeof tx, AXP_TIMEOUT_MS);
}

/* Read, change only the bits named, write back. Used for every register that
 * shares a byte with a bit this file has no business touching — the watchdog
 * enable sits next to the charger enable, and the whole point of this driver
 * is that it stays out of everything it did not come for. */
static esp_err_t reg_update(uint8_t reg, uint8_t clear_mask, uint8_t set_mask)
{
    uint8_t v;
    esp_err_t err = reg_read8(reg, &v);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t want = (uint8_t)((v & (uint8_t)~clear_mask) | set_mask);
    if (want == v) {
        return ESP_OK;
    }
    return reg_write8(reg, want);
}

/* The 14-bit ADC channels are read high byte first, then low — the datasheet
 * is explicit about the order (§6.10, "TWSI must read the high 6 bits firstly
 * and then the low 8 bits"), and a two-byte auto-incrementing read does
 * exactly that. The top two bits of the high byte are a debug-channel
 * selector, not data. LSB is 1 mV for vbat, vbus and vsys alike. */
static esp_err_t adc_read_mv(uint8_t reg_h, int *mv)
{
    uint8_t b[2];
    esp_err_t err = reg_read(reg_h, b, sizeof b);
    if (err != ESP_OK) {
        return err;
    }
    *mv = (int)(((uint16_t)(b[0] & 0x3F) << 8) | b[1]);
    return ESP_OK;
}

static bool lock(void)
{
    /* The console reads this chip from one task and the UI tick from another.
     * The IDF i2c_master driver serialises the bus itself, but a register
     * pair read as two transactions is not one transaction, and vbat is a
     * register pair. */
    return s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) == pdTRUE;
}

static void unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

/* ---- init -------------------------------------------------------------- */

esp_err_t axp2101_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        ESP_LOGW(TAG, "no I2C bus — PMIC left at its power-on defaults");
        return ESP_ERR_NOT_FOUND;
    }

    if (i2c_master_probe(bus, AXP_ADDR, AXP_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "nothing answers at 0x%02X — PMIC left alone", AXP_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = AXP_ADDR,
        .scl_speed_hz    = AXP_SCL_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bus add failed: %s", esp_err_to_name(err));
        return err;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        (void)i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ESP_ERR_NO_MEM;
    }

    uint8_t id = 0;
    (void)reg_read8(REG_CHIP_ID, &id);   /* logged for the record, never a gate */

    /* THE ONE THAT MATTERS. REG50[4] takes the TS pin out of the charger's
     * decision.
     *
     * On this board the TS pin sits on a plain resistor to ground, not on a
     * battery thermistor, and the reset value of this bit comes from the
     * chip's eFuse — so whether a cell charges out of the box is decided by
     * a fuse nobody here can read. Waveshare's own ESP-IDF example calls
     * disableTSPinMeasure() with the comment "otherwise it will cause
     * abnormal charging", which is as close to an admission as a vendor
     * example gets.
     *
     * Bits 3:2 are the TS current source; there is nothing to sense, so it
     * is switched off with it. */
    err = reg_update(REG_TS_CTRL, 0x0C, 0x10);

    /* ADC: vbat, vbus and vsys on, TS off. vbat is the reading everything
     * else in this feature is built on. */
    if (err == ESP_OK) err = reg_update(REG_ADC_EN, 0x02, 0x0D);

    /* Fuel gauge on (bit 3), cell charger on (bit 1). Both are POR defaults;
     * both are written anyway, because "it was already like that" is how the
     * TS bit above would have been described too. Bit 0 is the watchdog and
     * is deliberately not in either mask. */
    if (err == ESP_OK) err = reg_update(REG_MODULE_EN, 0x00, 0x0A);

    /* Battery detection on, so REG00[3] means what this driver reads it to
     * mean. */
    if (err == ESP_OK) err = reg_update(REG_BAT_DETECT, 0x00, 0x01);

    if (err == ESP_OK) err = reg_write8(REG_IIN_LIMIT, IIN_LIMIT_1A5);
    if (err == ESP_OK) err = reg_write8(REG_IPRECHG,   IPRECHG_50MA);
    if (err == ESP_OK) err = reg_write8(REG_ICC,       ICC_500MA);
    if (err == ESP_OK) err = reg_write8(REG_ITERM,     ITERM_EN_100MA);
    if (err == ESP_OK) err = reg_write8(REG_CV,        CV_4V1);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "configuration failed: %s — charging is whatever the "
                      "chip defaults say", esp_err_to_name(err));
        /* Hand the bus back. s_ready stays false, so a second call would probe
         * and add the device all over again; without this it would leak a
         * device handle and overwrite the mutex it just created. */
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        (void)i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return err;
    }

    s_ready = true;

    uint8_t st1 = 0;
    (void)reg_read8(REG_STATUS1, &st1);
    ESP_LOGW(TAG, "AXP2101 at 0x%02X (id 0x%02X): charger 500 mA to 4.1 V, "
                  "TS ignored, battery %s",
             AXP_ADDR, id, (st1 & 0x08) ? "PRESENT" : "absent");
    return ESP_OK;
}

bool axp2101_present(void)
{
    return s_ready;
}

esp_err_t axp2101_read(battery_raw_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!lock()) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t st1 = 0, st2 = 0, pct = 0;
    int mv = 0;
    esp_err_t err = reg_read8(REG_STATUS1, &st1);
    if (err == ESP_OK) err = reg_read8(REG_STATUS2, &st2);
    if (err == ESP_OK) err = adc_read_mv(REG_VBAT_H, &mv);
    if (err == ESP_OK) err = reg_read8(REG_GAUGE_PCT, &pct);
    unlock();

    if (err != ESP_OK) {
        /* Deliberately not ESP_ERROR_CHECK, and deliberately not a log line
         * per failure. esp_lvgl_port_touch.c panics the whole device on one
         * transient fault on this same bus (D47); this is the same bus, and
         * a battery indicator is not worth a reboot. */
        return err;
    }

    out->present   = (st1 & 0x08) != 0;
    out->vbus_good = (st1 & 0x20) != 0;
    out->chg_status = (uint8_t)(st2 & 0x07);
    out->mv        = mv;
    out->gauge_pct = pct;
    return ESP_OK;
}

int axp2101_vbus_mv(void)
{
    if (!s_ready || !lock()) {
        return 0;
    }
    int mv = 0;
    esp_err_t err = adc_read_mv(REG_VBUS_H, &mv);
    unlock();
    return (err == ESP_OK) ? mv : 0;
}

void axp2101_dump(void)
{
    if (!s_ready) {
        printf("\nPMIC: not initialised — no AXP2101 answered at 0x34\n");
        return;
    }
    static const uint8_t regs[] = {
        REG_STATUS1, REG_STATUS2, REG_CHIP_ID, REG_IIN_LIMIT, REG_MODULE_EN,
        REG_ADC_EN, REG_TS_CTRL, REG_IPRECHG, REG_ICC, REG_ITERM, REG_CV,
        REG_BAT_DETECT, REG_GAUGE_PCT,
        /* The side PWRKEY. Read-only here and deliberately not configured:
         * what this button does is decided by the chip's eFuse, and the only
         * way to find out what that eFuse says is to look. */
        REG_PWRON_STS, REG_PWROFF_STS, REG_PWROFF_EN, REG_KEY_LEVELS,
    };
    if (!lock()) {
        printf("\nPMIC: busy\n");
        return;
    }
    printf("\nPMIC registers\n");
    for (size_t i = 0; i < sizeof regs / sizeof regs[0]; i++) {
        uint8_t v = 0;
        esp_err_t err = reg_read8(regs[i], &v);
        printf("  REG%02X = %s", regs[i], err == ESP_OK ? "" : "read failed");
        if (err == ESP_OK) {
            printf("0x%02X", v);
        }
        printf("\n");
    }
    unlock();
}
