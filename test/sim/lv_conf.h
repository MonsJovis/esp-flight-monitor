/* LVGL configuration for the HOST simulator (test/sim) — not for the device.
 *
 * The device configures LVGL through Kconfig (sdkconfig). The values here are
 * the ones from that sdkconfig that change BEHAVIOUR rather than performance:
 * colour depth, the long-press and refresh timings, and the software renderer
 * with its complex shapes (circles, radii) switched on. Everything else is
 * LVGL's default. If one of the sdkconfig lines below changes, change it here
 * too, or the simulator is testing a different device.
 *
 *   CONFIG_LV_INDEV_DEF_LONG_PRESS_TIME=400
 *   CONFIG_LV_INDEV_DEF_LONG_PRESS_REP_TIME=100
 *   CONFIG_LV_DEF_REFR_PERIOD=33
 *   CONFIG_LV_DRAW_SW_COMPLEX=y
 *   CONFIG_LV_USE_OS=0
 *   CONFIG_LV_COLOR_FORMAT_RGB565=y
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565

#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_USE_OS LV_OS_NONE

#define LV_DEF_REFR_PERIOD 33
#define LV_INDEV_DEF_LONG_PRESS_TIME     400
#define LV_INDEV_DEF_LONG_PRESS_REP_TIME 100

#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_COMPLEX 1

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_ASSERT_NULL   1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_OBJ    1

#endif
