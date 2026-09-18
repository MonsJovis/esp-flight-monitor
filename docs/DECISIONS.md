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

## D10 — Custom partition table with two OTA slots

**Decision:** `partitions.csv` — 5 MB `ota_0`, 5 MB `ota_1`, 1 MB storage.

**Why:** the font set pushes the app image to 1.16 MB, past the 1 MB default slot. Since
the table had to change anyway, the OTA slots cost nothing to lay out now — and the device
spends half the year in Thailand, where "plug it into a computer" is not a repair plan.
OTA itself stays an optional M8 item; only the layout is committed to.

## D11 — Fonts are generated uncompressed

**Decision:** `--no-compress`, plus a post-processing step that injects
`.static_bitmap = 1`.

**Why:** this cost real debugging time and is worth recording precisely. `lv_font_conv`
compresses glyph bitmaps by default (`.bitmap_format = 1`). LVGL 9 gates that decoder
behind `LV_USE_FONT_COMPRESSED`, which is off — so **every glyph rendered as nothing at
all, with no error logged anywhere**. The panel showed a perfectly working LVGL perf
monitor above a completely blank font card.

Uncompressed is also the better trade on the merits: 482 KiB instead of 232 KiB, but no
per-frame decompression, and `.static_bitmap = 1` lets LVGL use the const data in place
with no copy. This product is render-bound; flash is not scarce.

## D12 — Two framebuffers, anti-tearing on

**Decision:** `CONFIG_BSP_LCD_RGB_BUFFER_NUMS=2`, `AVOID_TEAR=y`, `DIRECT_MODE=y`.
Settles AGENTS.md §8 open question 1.

**Why:** measured, not assumed — and the assumption was wrong. More framebuffers was
framed as a bandwidth *cost* to be traded against tearing. In fact two is **faster** than
one (28.5 vs 21.4 FPS) *and* tear-free, because direct mode removes a full-frame copy.
Three is indistinguishable from two and costs another 461 KB.

The same measurement retires the project's stated #1 risk: at two framebuffers the 100 px
PSRAM-resident hero face costs **zero** FPS against the built-in 48 px flash face.

## D13 — Benchmark measures full-screen invalidation

**Decision:** `dbg_bench` invalidates the entire screen every frame.

**Why:** a partial-redraw benchmark would flatter the numbers. The real UI redraws
everything when a poll lands, so the worst case is the honest case. Note the corollary —
28.5 FPS is a vsync ceiling at 1% CPU, not a load limit.

## D14 — WiFi credentials live in NVS from day one

**Decision:** PLAN M2 says "credentials hard-coded for now"; they are in NVS instead,
with a serial provisioning command.

**Why:** AGENTS.md §10 says secrets never go in the repo, and "temporarily" hard-coded
credentials are exactly how they end up committed. NVS costs nothing extra here and is
the seed of the M6 provisioning screen, which has to read from the same place anyway.

**Consequence:** the device cannot join a network until someone provisions it once. That
is the one step in this build that needs a human — see the note at the end of PLAN M2.

## D15 — No adsb.fi failover in M2; degrade instead

**Decision:** ship one position source (adsb.lol over plain HTTP). On repeated failure,
back off and keep showing the last good data behind an amber "keine Verbindung" caution,
rather than switching to a second source. The source table and switching logic are built,
with the second slot deliberately empty.

**Why:** probed live on 2026-09-18 —

| Endpoint | Plain HTTP |
|---|---|
| `api.adsb.lol/v2/point/...` | **200** |
| `api.adsb.lol/v2/lat/.../lon/.../dist/...` | **200** (same host, second URL shape) |
| `adsb.im/api/0/routeset` | **200** |
| `opendata.adsb.fi/api/v2/...` | **301 → https** |
| `api.adsb.one/v2/point/...` | 403 |
| `api.airplanes.live/v2/point/...` | 403 |

adsb.fi is HTTPS-only, which AGENTS.md's fallback table already said. But adding TLS for
the failover path contradicts §4's plain-HTTP architecture, and the failover exists mainly
to survive adsb.lol *throttling us* — which a second URL on the same host does not help
with either. Given AGENTS.md §1 ("never show an empty screen — show the last aircraft
seen"), degrading honestly is closer to the product's own design language than a second
source that costs 40 KB of heap.

**Revisit in M4 with numbers**, not before: we now know there is 186 KB of internal SRAM
free, so TLS may well be affordable — and adsb.fi returns an inline `desc` field that would
remove a lookup. That is a measurement, not an assumption.

**Consequence worth knowing:** AGENTS.md §5's "treat a spurious 308 as throttling" must
NOT be generalised to all 3xx. A 301 is a real redirect — it is exactly what adsb.fi
returns — so the HTTP client does not auto-follow redirects, and a redirect can never
silently become a TLS connection we did not intend.

## D16 — Backlight polarity is the one thing not verified on the panel

The BSP inverts brightness (`flipped = 100 - percent`) and configures LEDC with **no**
`output_invert` flag, so `bsp_display_backlight_on()` drives GPIO4 to a constant LOW. That
is correct only if the backlight circuit is active-low. Every vendor demo uses this same
path, so it almost certainly is — but framebuffer screenshots prove what LVGL *rendered*,
not what the panel *emitted*, so this is the one claim in this build resting on inference
rather than measurement. Needs a human to confirm the panel is lit.

## D17 — Place names are "what he would say out loud", and always exactly one name

**Decision:** `Laibach → Ljubljana`, `Pressburg → Bratislava`,
`Klausenburg (Cluj-Napoca) → Klausenburg`. Enforced table-wide by a test.

**Why:** two separate faults, found by reading the generated table rather than by a
failing test.

The parenthetical dual names are unrenderable by construction. The hero fits roughly 9–10
characters at 100 px and DESIGN.md §3 forbids truncating a city name ever — so
`"Klausenburg (Cluj-Napoca)"`, at 25 characters, could only ever be shown small or wrong.
The hero shows one name.

Laibach and Pressburg are real German exonyms, but they are *historical* rather than
current Austrian usage. The test for a place name on this device is not "does German have
this word" — it is "would he recognise it in under two seconds, without translating it
back". Mailand, Prag, Warschau and Kopenhagen pass that; Laibach does not. Klausenburg
stays because German-language media still uses it.

`test_tables.c` now asserts across the whole table that no name contains a parenthesis and
none exceeds 24 bytes, so a future addition cannot reintroduce either fault.
