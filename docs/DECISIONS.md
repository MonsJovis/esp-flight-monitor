# Decisions taken during implementation

Running log of calls made while building, with the reasoning, so they can be found
and reversed. Settled *product* decisions live in AGENTS.md §8; this file is for
the engineering calls made along the way.

---

## D1 — Skipped the stock vendor demo flash (PLAN M0)

**Decision:** went straight to our own BSP bring-up instead of first flashing
`02_lvgl_demo_v9`.

**Why:** M0's stated purpose is "proof the hardware is healthy before we write any
code". Our own minimal BSP app proves the board *and* the toolchain in one step; the
vendor demo only proves the board. If our bring-up had misbehaved the demo was the
fallback to isolate board-vs-us — it was not needed, because bring-up worked first
time. The vendor demo remains the right tool the moment something looks wrong.

## D2 — LVGL is 9.6.0, not 9.2.x

**Decision:** accepted what the BSP's dependency solver chose.

**Why:** `waveshare/esp32_s3_touch_lcd_4b^2.0.0` resolves LVGL to `9.6.0~1`. AGENTS.md
§3 says 9.2.x. Pinning 9.2 would fight the BSP's own constraints for no benefit — 9.6
is API-compatible for everything we use. Noted here because the docs now differ from
the build; AGENTS.md §3 should be corrected rather than the build.

**Watch for:** `lv_scr_act()` is deprecated in favour of `lv_screen_active()`.

## D3 — Console on USB-Serial-JTAG, not UART0

**Decision:** `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`.

**Why:** the board exposes a native USB port, so the 115200 baud UART ceiling is
artificial. The framebuffer grab (D4) moves 450 KiB per screenshot — 53 s over UART,
**1.1 s** over USB. That difference is what makes visual verification routine instead
of something you avoid doing.

## D4 — Framebuffer screenshots over USB for visual verification

**Decision:** built `main/debug/dbg_screen.c` + `tools/grab_screen.py`. Sending `s`
over serial dumps the live RGB565 framebuffer as CRC-checked base64; the host turns it
into a PNG.

**Why:** the build host cannot see the panel, so "does it look right" was otherwise
unanswerable without a human in the room — and this project is almost entirely a
visual design problem. It reads the actual framebuffer being scanned out, not a
re-render, so it catches font, colour and layout faults truthfully. Measured cost:
1.1 s per 480×480 frame, and nothing at runtime until a command byte arrives.

## D5 — Our own `display_init()` instead of `bsp_display_start()`

**Decision:** `main/ui/display.c` replicates the BSP's init path.

**Why:** the BSP keeps the `esp_lcd_panel_handle_t` in a file-static with no accessor,
and we need it twice — to read the framebuffer (D4) and to vary the framebuffer count
for the M1 bandwidth measurement. ~60 lines copied from the BSP, which is Apache-2.0
(AGENTS.md §9: safe to copy, keep the headers).

## D6 — cJSON, shared between device and host tests

**Decision:** parsers use cJSON, which ships inside ESP-IDF; the host test Makefile
compiles the *same* `cJSON.c` from `$IDF_PATH`.

**Why:** AGENTS.md §7 recommends ArduinoJson's filter feature, but ArduinoJson is a
C++ Arduino library and this is an ESP-IDF C project. cJSON is already in the tree, so
it costs no new dependency, and compiling the identical source on both sides means a
host test failure is a real device failure — not a difference between two parsers.

## D7 — Parsers contain no ESP-IDF headers

**Decision:** `main/net/*_parse.c` take `(const char *json, size_t len)` and fill plain
structs. All networking lives in separate files.

**Why:** it is what makes them testable on the host in milliseconds, which AGENTS.md
§10 calls the highest-value tests on the project. `main/compat.h` maps `ESP_LOG*` to
printf so module code is byte-identical on both targets.

## D8 — The German airport table is keyed on ICAO, not IATA

**Decision:** `airport_de("EDDM")` → `"München"`.

**Why:** verified against the live `adsb.im/routeset` response: `airport_codes` returns
ICAO pairs (`"LRCL-EDDM"`) directly, while IATA only appears in the `_airport_codes_iata`
convenience field. ICAO is also unambiguous, where IATA has collisions. Fallback when
there is no entry is the API's own English `location` field.

## D9 — Shared data model owned centrally

**Decision:** `main/flight_types.h` defines `aircraft_t`, `route_t`, `ac_type_t` and the
altitude sentinels; no module redefines them.

**Why:** four workstreams were built in parallel against this contract. Two sentinels
(`ALT_GROUND`, `ALT_UNKNOWN`) rather than one, because the panel must be able to say
"am Boden" and "—" differently.
