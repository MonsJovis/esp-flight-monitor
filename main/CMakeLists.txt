# Sources are listed explicitly rather than globbed: the host test runner links a
# different subset (main/data + main/net parsers only), and a glob here would
# silently pull host-only or half-finished files into the firmware.
set(SRCS
    "main.c"
    "ui/display.c"
    "debug/dbg_screen.c"
)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "." "ui" "data" "net" "debug"
    REQUIRES esp_driver_usb_serial_jtag esp_lcd
             espressif__esp_lcd_touch espressif__esp_lvgl_port
             waveshare__esp32_s3_touch_lcd_4b
)
