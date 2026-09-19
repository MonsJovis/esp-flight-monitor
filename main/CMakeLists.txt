# Sources are listed explicitly rather than globbed: the host test runner links a
# different subset (main/data + main/net parsers only), and a glob here would
# silently pull host-only or half-finished files into the firmware.
set(SRCS
    "main.c"
    "ui/display.c"
    "debug/dbg_screen.c"
    "debug/dbg_bench.c"
    "debug/dbg_metrics.c"
    "debug/dbg_fixture.c"
    "ui/screen_overhead.c"
    "ui/widget_compass.c"
    "ui/nav.c"
    "ui/screen_settings.c"
    "ui/screen_wifi.c"
    "ui/screen_list.c"
    "ui/screen_radar.c"
    "data/tbl_airport.c"
    "data/tbl_airline.c"
    "data/tbl_actype.c"
    "data/fmt_de.c"
    "data/view_build.c"
    "data/settings.c"
    "net/adsb_parse.c"
    "net/route_parse.c"
    "net/source_logic.c"
    "net/wifi.c"
    "net/http_get.c"
    "net/flight_source.c"
    "net/timesync.c"
)

# Generated font faces are globbed: they are build output with stable names,
# and listing ten of them by hand invites drift from tools/build_fonts.sh.
file(GLOB FONT_SRCS "${CMAKE_CURRENT_LIST_DIR}/ui/fonts/*.c")

idf_component_register(
    SRCS ${SRCS} ${FONT_SRCS}
    # The real captured traffic, embedded so the screens can be driven without
    # a network. See main/debug/dbg_fixture.c.
    EMBED_TXTFILES "../test/fixtures/adsblol_gloggnitz_30nm.json"
                   "../test/fixtures/routeset_response.json"
    INCLUDE_DIRS "." "ui" "data" "net" "debug" "ui/fonts"
    REQUIRES esp_driver_usb_serial_jtag esp_lcd esp_timer
             json esp_wifi esp_netif esp_event nvs_flash esp_http_client lwip
             espressif__esp_lcd_touch espressif__esp_lvgl_port
             waveshare__esp32_s3_touch_lcd_4b
)
