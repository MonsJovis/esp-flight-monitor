/* The AXP2101 PMIC, over the I2C bus the BSP already owns (0x34, GPIO47/48).
 *
 * WHY THIS FILE EXISTS AT ALL. The Waveshare BSP does not touch the PMIC —
 * `grep -i axp managed_components/waveshare__esp32_s3_touch_lcd_4b` returns
 * nothing — so the board boots, runs and charges on whatever the chip's
 * power-on defaults and eFuse happen to say. That is fine while USB is the
 * only supply. It is not fine once a cell is on the PH2.0 header, because one
 * of those defaults decides whether the cell charges at all (see TS below).
 *
 * WHAT THE HARDWARE ACTUALLY DOES, from the board schematic:
 *   - AXP2101 DCDC1 (pins 23/22/21) is VCC_3V3, and VCC_3V3 feeds the
 *     ESP32-S3, the ST7701 panel, the GT911 and the AP3032 backlight boost.
 *     VSYS is fed from VBUS or BAT automatically, so pulling the USB cable
 *     does not interrupt anything: this is a power path, not a changeover
 *     switch, and the backlight stays lit because it hangs off 3V3 and not
 *     off the USB 5 V.
 *   - J1 is the PH2.0 battery header: pin 1 GND, pin 2 VBAT1, silkscreened
 *     + and -. Cell vendors are not consistent about which pin gets the red
 *     wire. Meter it.
 *   - The PMIC's IRQ pin has a pull-up and no second occurrence anywhere in
 *     the schematic, so it does not reach an ESP32 GPIO. Everything here is
 *     POLLED, for the same reason the GT911 is (AGENTS.md §2).
 *
 * NOTHING IN HERE WRITES A RAIL. No DCDC enable, no LDO voltage, no BATFET,
 * no PWROFF threshold. One wrong byte to REG80 or REG90 powers down the
 * panel or the ESP32 itself, and the only way back is the PWRKEY on the side
 * edge. The registers this file touches are exactly the charger, the ADC and
 * the fuel gauge.
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "battery_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Probes 0x34 and applies the charging configuration. Safe to call once, at
 * boot, after the BSP has brought the display (and therefore the I2C bus) up.
 *
 * Returns ESP_ERR_NOT_FOUND when the PMIC does not answer, and ESP_OK
 * otherwise. NEITHER IS FATAL: a device with no working PMIC driver is the
 * device this project shipped for its first sixty decisions, and it must go
 * on running on USB exactly as it did. The caller logs and carries on.
 */
esp_err_t axp2101_init(void);

/* True once axp2101_init() has succeeded. */
bool axp2101_present(void);

/* One unjudged reading: battery present, VBUS good, charge status, cell
 * millivolts, gauge percentage. Returns an error if the bus misbehaves, in
 * which case *out is untouched and the caller should keep whatever it had —
 * a transient I2C fault is not news, and it is certainly not a reason to
 * redraw the screen.
 */
esp_err_t axp2101_read(battery_raw_t *out);

/* VBUS millivolts, for the console. 0 when unavailable. */
int axp2101_vbus_mv(void);

/* Prints the registers this file cares about, raw, to the serial console.
 * For the morning a cell is first plugged in and the question is "is it
 * charging, and if not, which register says no". */
void axp2101_dump(void);

#ifdef __cplusplus
}
#endif
