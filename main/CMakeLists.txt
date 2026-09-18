# Sources are listed explicitly rather than globbed: the host test runner links a
# different subset (main/data + main/net parsers only), and a glob here would
# silently pull host-only or half-finished files into the firmware.
set(SRCS
    "main.c"
    "ui/display.c"
    "debug/dbg_screen.c"
    "debug/dbg_bench.c"
    "debug/dbg_metrics.c"
    "data/tbl_airport.c"
    "data/tbl_airline.c"
    "data/tbl_actype.c"
    "data/fmt_de.c"
)

# Generated font faces are globbed: they are build output with stable names,
# and listing ten of them by hand invites drift from tools/build_fonts.sh.
file(GLOB FONT_SRCS "${CMAKE_CURRENT_LIST_DIR}/ui/fonts/*.c")

idf_component_register(
    SRCS ${SRCS} ${FONT_SRCS}
    INCLUDE_DIRS "." "ui" "data" "net" "debug" "ui/fonts"
    REQUIRES esp_driver_usb_serial_jtag esp_lcd esp_timer
             espressif__esp_lcd_touch espressif__esp_lvgl_port
             waveshare__esp32_s3_touch_lcd_4b
)
