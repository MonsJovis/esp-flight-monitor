# AGENTS.md — esp-flight-monitor

Operating manual for AI agents working in this repo. Read this before touching code.

> **Status, 2026-09-20.** This is no longer a brief. The device is built, verified against
> live traffic and running. M0–M8 and the touch work after them are closed
> ([docs/PLAN.md](./docs/PLAN.md)); seventy-two decisions are written up with their reasoning
> and their mistakes ([docs/DECISIONS.md](./docs/DECISIONS.md)); the host suite is
> **32,668 checks across twelve suites, 0 failed**.
>
> Read the rest of this file knowing which half is which. **Sections 2, 4, 5 and 6 are
> measured facts** about the hardware, the APIs and the places — still current, do not
> re-derive them. **Sections 1, 7, 8 and 10 are rules**, and the build amended several of
> them; where it did, it says so inline. Do not "restore" an amended rule to what it used
> to say.
>
> **Three things are still unexercised:** OTA's image download and slot switch (the manifest
> path is proven on device), the printed desk stand, and the battery — the PMIC driver is
> written and every register reads back correct on the unit, but no cell has been connected
> to this board yet (D61, PLAN.md M9).

## 1. What this is

A wall-mounted flight radar on a 4-inch touch panel. It answers the question its user
actually asks, without a phone:

> "That plane up there — where is it going, where did it come from, and what is it?"

**The user is Markus's father-in-law.** He is not technical. He currently pulls out his
phone and opens the Flightradar24 app. This device must be faster and easier than that,
or it has failed. Two usage modes drive every design decision:

1. **Reactive** — he hears/sees a plane, glances at the panel, wants the answer in
   under two seconds with no interaction.
2. **Browsing** — he taps over to see what else is in the sky right now.

### Design rules that follow from this

- **Route is the headline, not a detail.** `VIE → LHR` rendered as *Wien → London* is the
  single most important thing on screen. Most hobby flight radars bury this. Do not.
- **Plain language over codes.** "Airbus A320neo", not `A20N`. "Austrian Airlines", not `AUA`.
- **No interaction required for the primary answer — amended by the owner (D60).** This
  was the founding rule and it is now *one tap*. The **Radar** is the default screen, the
  **Liste** sits beside it, and the answer in words lives on a **detail layer underneath**
  either of them, reached by tapping an aircraft or the caption. Markus asked for exactly
  that after living with the device. The spirit survives — the radar answers *where* and
  *how many* with no interaction, and the nearest aircraft is already captioned along the
  bottom edge — but do not move the hero back to the default screen. It was not a
  regression.
- **Never show an empty screen.** If the sky is clear, show the last aircraft seen, or a
  clock. A blank panel reads as "broken" to a non-technical user.
- **German UI — confirmed.** He is Austrian. Keep all user-facing strings in one
  translation unit. Two consequences that are easy to miss until they bite:
  - **The LVGL font must carry ä ö ü ß.** LVGL's built-in Montserrat faces are ASCII-only.
    Build a font including the Latin-1 supplement range, or umlauts render as blanks —
    and "Zurich"/"Munchen" on a German panel looks broken. This reaches further than the
    labels: the on-screen KEYBOARD has to be able to type them too, which is why it runs
    two faces at once (§7).
  - **The route API returns *English* city names** ("Vienna", "Munich", "Prague"). Ship a
    small airport → German name table for the common European destinations (Wien, München,
    Zürich, Prag, Mailand, Athen, Kopenhagen, Warschau …) and fall back to the API's own
    name when there is no entry. Without this the headline reads "Vienna → London" to a
    man sitting in Austria.

  The one translation unit is **`main/strings_de.h`**, and "keep the strings in one place"
  is no longer a convention you can quietly break: `tools/check_strings.py` fails the build
  when a German string appears anywhere else, and `tools/check_font_coverage.py` fails it
  when a character in there has no glyph. Both run with the host tests. The aviation
  vocabulary was checked against ICAO Doc 9871 / RTCA DO-260B and read aloud by a native
  speaker — read D51, D56 and D57 before you change a word of it.

The visual system — colours, type scale, size floors, navigation — is in
**[docs/DESIGN.md](./docs/DESIGN.md)**. Read it before building any screen. The build
order is in **[docs/PLAN.md](./docs/PLAN.md)**.

## 2. Hardware — verified, do not re-derive

**Board: Waveshare ESP32-S3-Touch-LCD-4B** ("Smart 86 Box"). Confirmed via the official
BSP and repo; the non-"B" `ESP32-S3-Touch-LCD-4` is a *different, industrial* board whose
wiki pin table is **wrong for this one**. Do not follow it.

Read live from the attached board with `esptool`:

```
Chip type:  ESP32-S3 (QFN56) revision v0.2
Features:   WiFi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
Flash:      16MB, quad (4 data lines), 3.3V
MAC:        44:1b:f6:89:95:dc
USB mode:   USB-Serial/JTAG
```

Module is `ESP32-S3-WROOM-1-N16R8`. PSRAM is **8 MB octal**; flash is 16 MB quad.

| Part | Detail |
|---|---|
| Display | 4" IPS 480×480, **ST7701** controller, **16-bit parallel RGB565** |
| LCD init bus | 3-wire SPI **through the TCA9554 IO expander** — costs no ESP32 GPIO |
| Pixel clock | 16 MHz (BSP) → ~60 Hz panel refresh |
| Framebuffer | 480×480×2 = **450 KiB**, in PSRAM |
| Touch | **GT911**, 5-point, I²C `0x5D`, **polled — INT is behind the expander** |
| I²C bus | single shared bus, **GPIO47 SDA / GPIO48 SCL**, 400 kHz |
| I²C devices | TCA9554 `0x20`, ES8311 `0x18`, AXP2101 `0x34`, ES7210 `0x40`, PCF85063 `0x51`, GT911 `0x5D`, QMI8658 `0x6B` |
| Audio | ES8311 codec + ES7210 ADC + NS4150B amp (2 W), 2× MEMS mics |
| Other | AXP2101 PMIC, PCF85063 RTC, QMI8658 6-axis IMU |
| Backlight | GPIO4, LEDC PWM |
| Enclosure | 86.5 × 86.5 × 14 mm, standard 86-type wall plate, case included |

**Not on this board** (reseller listings get this wrong): no relays, no mains input, no
PoE, **no microSD**, no buzzer, no RGB LED, no CAN/RS485/Ethernet.

**Power:** 2× USB-C, or PH2.0 Li-ion, or `5V_IN` on the rear header. **USB-C is the supply**
— the side-edge port placement is fine for a desk unit and the rear header is not needed.
(It would only matter for a flush wall install, where the side ports become unreachable.)

**A PH2.0 cell is supported as a UPS** since D61, and these are schematic facts, not
assumptions:

- **AXP2101 DCDC1 (pins 23/22/21) is `VCC_3V3`**, which feeds the ESP32-S3, the panel, the
  GT911 *and the AP3032 backlight boost*. VSYS switches between VBUS and BAT by itself, so
  unplugging USB interrupts nothing and the backlight stays lit. It is a power path, not a
  changeover switch.
- **J1 is the battery header: pin 1 GND, pin 2 VBAT1**, silkscreened `+`/`-`. Cell vendors
  are not consistent about which pin gets the red wire. **Meter it.** Reversed is a dead
  PMIC.
- **The back cover has a cutout over that socket**, so a cell plugs in without opening the
  case — which is just as well, because at 14 mm total depth nothing fits inside it. **The
  cell is stuck to the back of the case** (owner's call, D61 amendment): double-sided foam
  tape, never cyanoacrylate on the pouch, no clamping or folding, and leave slack in the
  lead so the plug is not what holds the cell on. Low on the back rather than centred — a
  10 mm block at the bottom edge leans the panel back a few degrees instead of making it
  rock.
- **The PMIC's IRQ pin does not reach an ESP32 GPIO** (pull-up, no second occurrence in the
  schematic), so the battery is polled, like the GT911.
- **A cold start on battery alone needs a PWRKEY press** — datasheet §6.5.2, the BATFET is
  off until the key is pressed or an adapter appears. Unplugging a *running* device is
  seamless.
- Draw is **1.2–1.9 W calculated** (39 mA through the backlight string: 200 mV over R30's
  5.1 Ω into the AP3032), so roughly four hours from 2000 mAh. **Calculated, not measured**
  — the firmware logs the discharge once a minute so the first unplugging settles it.

**GPIO budget is effectively zero.** The RGB bus consumes nearly everything. Expansion
goes over I²C, or by repurposing TCA9554 EXIO pins after init.

## 3. Stack

**ESP-IDF 5.4 + official Waveshare BSP + LVGL 9.6.x.** (The BSP's dependency
solver resolves LVGL to 9.6, not the 9.2 originally assumed — see docs/DECISIONS.md D2.) Already installed at `~/esp/esp-idf`
(v5.4) — not on PATH, so `. ~/esp/esp-idf/export.sh` first.

```bash
idf.py add-dependency "waveshare/esp32_s3_touch_lcd_4b^2.0.0"
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash monitor
```

The serial port is **not a fixed name**: the board re-enumerates as `usbmodem1101` or
`usbmodem101` depending on how it was last plugged (D26). Everything in `tools/` discovers
it; `idf.py -p` does not, so `ls /dev/cu.usbmodem*` first.

### The loop you actually work in

Almost none of this needs the board. Run this before and after every change — under ten
seconds from a clean tree:

```bash
make -C test/host        # 32,668 checks, plus the font, string and console-key gates
```

**Before your first `idf.py build`, generate a signing key.** Every build is signed now
(D72), and an unsigned build of this firmware does not fail to link — it builds, flashes,
and then aborts on boot with "No signatures were found for the running app". One command,
once per machine:

```bash
idf.py secure-generate-signing-key --version 2 --scheme rsa3072 secure_boot_signing_key.pem
```

That is a *fresh* key, and that is fine: a device you flash yourself will run it happily.
It is not the key the published releases are signed with, so it cannot update the one
device in the field — which is the point of the whole arrangement.

For anything visual, the panel reports on itself; you do not have to be in the room:

```bash
python3 tools/grab_screen.py shot.png    # the real RGB565 framebuffer, read back over USB
python3 tools/provision.py               # WiFi credentials → NVS, never through you
```

Every key below has to appear in main.c's header block AND in the boot `ready:` line —
`tools/check_console_keys.py` parses `on_cmd()` and fails the build otherwise. Two reviews
in a row found those two lists describing a console this firmware no longer had, and a key
nobody has written down puts the screen behind it back to being one nobody checks.

The firmware takes single command bytes on the same serial link (`on_cmd()` in
`main/main.c`, plus `s` handled in `main/debug/dbg_screen.c`):

- `s` (or `S`) screenshot — `1`–`5` show a captured fixture (`5` is §5.2 with the route
  lookup still outstanding) — `0` back to live
- `g` next page — `i` toggle the detail layer — `e` settings — `k` WLAN — `d` scroll to end
- `q` open Ort suchen — `Q` run a real search on it — `z` search and take the first hit
  — `Z` step through its three states (waiting / nothing found / no answer), one per press
  — `a` press the keyboard's layer key (abc → ABC → 1# → abc)
- `c` tap "Neu suchen" — `C` start a lookup and abandon it before the answer can land,
  which is the one race in this feature that cannot be won from the host: the endpoint
  answers or refuses faster than a second keystroke arrives, so `C` holds the display lock
  across both steps and constructs the scenario instead of gambling on it.
- `j` tap the first SAVED network — a real reconnect, and the one path on that screen
  whose wait had no end until main.c grew a watcher for it.
- `K` open WLAN and go straight to the password step, which is the one screen a finger is
  otherwise needed for. It says in the log whether it got there by tapping an unsaved
  network (the real path) or had to force it open because everything in range is already
  saved — those are not the same check and it does not report them as one.
- `n` network status — `p` probe the link — `w` provision WiFi — `o` cycle location
- `u` update console — `v` LVGL heap report
- `y` battery status and the PMIC registers — `Y` pretend to be a battery (60/18/5/off)
- `W` pretend a WLAN signal, so the corner meter can be seen at all five of its levels
  (four bars, three, two, one, no link at all) without walking the device out of range
- `x` what the touch layer has actually registered (presses, long presses, the last hold)
- `b` benchmark — `t` tearing test — `f` font card — `m` hero metrics

Driving a screen from the host and reading its framebuffer back is what turns "does it look
right" into a measurable question. Use it rather than asking Markus to look (D4, D41).

Do **not** use Arduino for this project. Arduino_GFX works on this panel but hardcodes
`num_fbs = 1`, falls back to a 12 MHz pixel clock (vs 16), and offers no anti-tearing or
FreeRTOS-aware LVGL integration. Start from the official `02_lvgl_demo_v9` example.

Reference sources:
- BSP: https://components.espressif.com/components/waveshare/esp32_s3_touch_lcd_4b
- Demos: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-4B (Apache-2.0)

## 4. Data architecture

The ESP32 **cannot receive ADS-B** — 1090 MHz needs an SDR. All aircraft data comes from
free community HTTP APIs. Every endpoint below was live-tested on 2026-09-18.

```
Positions  →  GET  http://api.adsb.lol/v2/point/{lat}/{lon}/{radius_nm}
Routes     →  POST http://adsb.im/api/0/routeset            (batched!)
Type names →  const table in flash: ICAO code → "Airbus A320neo"
Places     →  GET  http://geocoding-api.open-meteo.com/v1/search?name=…&language=de
```

### Why this combination

- **Both work over plain HTTP with no redirect.** No TLS means no `WiFiClientSecure`, no
  cert bundle, and roughly 40 KB more free heap per connection. This is the single biggest
  win available on this platform. tar1090's own source notes adsb.im *prefers* HTTP here.
- **`adsb.lol` pre-computes `dst` (distance, nm) and `dir` (bearing)** from the query point.
  No haversine needed on-device; sort by `dst` for free to find "the plane overhead".
- **`adsb.im/routeset` is batched** — one POST resolves every callsign on screen, instead
  of N TLS handshakes. It also returns city names and a `plausible` flag that filters
  nonsense matches. Verified: `AUA453` → `LOWW-EGLL` / Vienna → London.
- **Open-Meteo is the only free geocoder that answers over plain HTTP.** Measured
  2026-09-20 with this project's own User-Agent: Open-Meteo `200`, no redirect;
  `nominatim.openstreetmap.org` `301` → https; `photon.komoot.io` `301` → https. Since the
  whole memory argument below rests on never opening a TLS connection on the data path,
  that settles it. It pays a second time: each hit carries its IANA **timezone**, which is
  what lets §6's "the clock follows the location" hold for a place that is not a preset.
  The cost is that it finds PLACES and not street addresses — which does not matter here,
  because the default radius is 30 nm (55 km) and moving the query point by the 600 m
  between a town centre and a house on its edge changes nothing about which aircraft come
  back. Only ever requested when somebody taps Suchen in §5.8; nothing polls it.
- **Type names belong in flash, not on the network.** `adsb.lol` gives `t` = `A20N`; a
  ~200-entry table costs a few KB and removes a whole API dependency. Built:
  `main/data/tbl_actype.c`, `tbl_airline.c`, `tbl_airport.c` — all three were resized
  against real traffic rather than guessed at (D43, D51).

### Fallbacks

| Layer | Primary | Fallback 1 | Fallback 2 |
|---|---|---|---|
| Positions | `adsb.lol` /v2/point (HTTP) | `adsb.fi` v3 (HTTPS, adds `desc` inline) | local tar1090 `aircraft.json` |
| Routes | `adsb.im` routeset (batch) | `hexdb.io/callsign-route-iata` (**7 bytes**) | `adsbdb.com/v0/callsign` (adds airline + airport names) |
| Airline | `airline_code` from routeset | `hexdb.io/hex-airline` | `adsbdb /v0/airline` |
| Photo | `planespotters.net` (**custom UA required**) | `hexdb.io/hex-image` | — |

`adsb.fi` and `adsb.lol` share the tar1090 JSON shape, so **one parser handles both** —
only the wrapper key differs (`ac` vs `aircraft`; adsb.fi v3 uses `ac`, v2 uses `aircraft`).

### Do not use

- **airplanes.live** — now returns `403`, requires emailing for approval. Repo archived.
- **OpenSky** — 400 credits/day anonymous (≈ one poll per 3.6 min), and returns **no
  aircraft type, no registration, no route**. Wrong tool for this job.
- **Flightradar24** — no free tier; scraping breaches ToS and they actively block.
- **`api.adsb.lol/api/0/routeset`** — returns `201` with an empty body. Broken. Use `adsb.im`.

## 5. Rate limits — measured, respect them

These are not documented numbers; they were hit for real during research.

- **`adsb.lol` throttles at roughly the 7th rapid request.** It then returns `429`, and
  sustained abuse escalates to a `503` cooldown lasting **several minutes**. During
  throttling it may also emit spurious `308` redirects — treat those as throttling, not
  as a real redirect.
- **Poll positions every 10–15 s.** Never faster.
- **`adsb.fi` is 1 request/second** and returns a bare `400` when annoyed.
- **`adsbdb.com`**: 512 req/min → `429`; ≥1024 → **5-minute lockout**.
- **`hexdb.io`** publishes no limit but asks you not to scrape. Cache aggressively.

**Implement exponential backoff and a source-health check from day one.** Two consecutive
failures on the primary → switch to the fallback for a few minutes. A tight retry loop
will get the device IP-banned from a free community service.

**Cache routes per callsign for the whole flight** — a route never changes mid-flight.
In practice that means one `routeset` POST every few minutes, not one per poll.

## 6. Locations

Three presets plus a custom entry. Store in NVS.

| Preset | Address | Lat / Lon |
|---|---|---|
| Gloggnitz (AT) | Semmeringstraße 11, 2640 Gloggnitz | `47.6691` / `15.9303` |
| Wien (AT) | Meiselstraße 79, 1140 Wien | `48.1984` / `16.3074` |
| Pattaya (TH) | 154 Thappraya Rd, Pattaya City, Chon Buri 20150 | `12.9211` / `100.8721` |

**The enum values are written to NVS, so the list is append-only.** `LOC_WIEN` is 3, after
`LOC_CUSTOM`, even though it belongs next to Gloggnitz on screen — renumbering would move a
device already in the field to a different city on a firmware update, silently. The order he
sees comes from `location_display_order()`, which exists for exactly that reason.

Measured traffic on 2026-09-18 (aircraft returned by `adsb.lol`):

| Location | 30 nm | 60 nm | 100 nm |
|---|---|---|---|
| Gloggnitz | ~9 | ~45 | ~89 |
| Pattaya | 7 | 36 | 55 |

Both locations have good coverage. **Default radius: 30 nm** (~55 km) — that is roughly
what "overhead" means, and it keeps the payload near 4 KB instead of 60 KB at 100 nm.

Coverage is *not* uniform across Thailand — Chiang Mai returned only 2 aircraft at 80 nm.
If he ever moves, re-measure before assuming the device is broken.

### The desk stand means the device travels

A desk unit will be carried between Austria and Thailand twice a year, by someone who will
not read a manual. Design for that:

- **Two WiFi networks must both be remembered**, not reconfigured on arrival. Store a list,
  not a single SSID, and reconnect to whichever is in range.
- **Provisioning must survive a non-technical user in a foreign country.** Settled in §8:
  an **on-device** network list that appears by itself when no known network is in range.
  Not a captive portal — that assumes a phone, a second network join and a browser.
- **Switching location should be one tap**, not a coordinate entry form. Three named
  presets (Gloggnitz, Wien, Pattaya) plus an advanced custom option.
  **Built, 2026-09-20 — and the conclusion was not what this line assumed.** "Eigener Ort"
  shipped in M6 as a card with no way to set it: `custom_lat`/`custom_lon` were whatever
  `settings_defaults()` had put there, and a TODO in `screen_settings.c` marked coordinate
  entry as intentionally unimplemented *because a numeric keypad is the very form this rule
  forbids*. That reasoning was right and its conclusion was wrong. The way to set a
  location without a coordinate form is to **search for it by name** — §5.8 "Ort suchen",
  one row under the cards. He types a town, taps Suchen, taps the right hit out of a list;
  he never sees a coordinate and never types a decimal point.
- **Timezone changes with the location** — CEST and ICT are 5–6 h apart depending on the
  season. Bind the timezone to the location preset; do not make him set a clock.
  **This was broken for LOC_CUSTOM the whole time and nobody could have noticed**, because
  nobody could reach that preset with real coordinates in it: its row in `k_presets[]` said
  `"UTC0"`, so tapping "Eigener Ort" moved the panel clock two hours without moving the
  device an inch. A searched place now carries its own POSIX rule
  (`settings_t.custom_tz`, from the geocoder's IANA zone via `main/net/tz_table.h`), and
  `settings_tz()` is what the clock is set from. **Call `settings_tz(&settings)`, never
  `location_tz(settings.preset)`** — the second one cannot know about a custom place and
  answers Gloggnitz for it.
- Consider auto-detecting the location preset from the WiFi SSID he connects to.

## 7. Gotchas that will cost you a day

**Firmware / display**
- **Flash writes tear the display — MEASURED, and they do not.** espressif/esp-bsp#570 is
  real on this silicon+panel combo, but it does not reproduce in this configuration.
  Measured: an NVS commit costs **3 µs** against a 49.7 ms worst-case frame gap, and
  sustained commits under a high-contrast moving pattern produce **no visible tearing**.
  Two framebuffers (AGENTS.md §8, PLAN.md M1) are the likely reason.
  **Do NOT pause LVGL around NVS writes.** Holding the display lock across a commit burst
  stalls rendering for ~2.85 s to save 3 µs — the mitigation is far worse than the disease.
  Keep `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y` and `CONFIG_SPIRAM_RODATA=y` on. Full numbers
  in docs/DECISIONS.md D29.
- **A screenshot reads frame buffer 0, which is not necessarily what is on the glass.**
  `esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb)` hands back the FIRST buffer and this
  build has two, so after a single redraw buffer 0 still holds the frame BEFORE the change.
  Every grab of a screen that had just been changed and then gone still was one state out
  of date, and said nothing about it — the image is a valid picture of the wrong moment.
  It only bites a STATIC screen: anything animating redraws continuously and both buffers
  converge, which is why the M11 loading states photographed correctly while the no-route
  fixture beside them came back twice showing the state before it. `dbg_screen.c` now
  invalidates the whole screen once per buffer before capturing. If you add a third
  buffer, that loop already follows `CONFIG_BSP_LCD_RGB_BUFFER_NUMS`.
- **Stay at 80 MHz PSRAM.** 120 MHz is experimental and temperature-sensitive — a real
  risk for an always-on panel behind glass.
- **WiFi bursts compete for PSRAM bandwidth** and cause visible drift. This is the #1
  field failure mode for RGB panels. Keep network work off the render path.
- **Strapping pins are on the RGB bus**: GPIO3 (VSYNC), GPIO45 (G3), GPIO46 (HSYNC), and
  GPIO0 is BOOT. Do not back-drive these during reset.
- Anti-tearing is **off** in the BSP default (`BSP_LCD_RGB_BUFFER_NUMS=1`); **we set 2**,
  and that is measured, not assumed. Two is both *faster* than one (28.5 vs 21.4 FPS) and
  tear-free; three buys nothing (D12, PLAN M1).
- **Large fonts land in PSRAM, not flash.** `CONFIG_SPIRAM_RODATA=y` — mandated at the top of
  this block as the tearing mitigation — relocates `.rodata`, and LVGL fonts *are* `.rodata`.
  So the 100 px hero face and its shrink ladder compete with the framebuffer for PSRAM
  bandwidth: the project's #1 risk and its most distinctive design choice pulling on the
  same bus. **Measured in M1, and it did not materialise:** at two framebuffers the 100 px
  face costs **zero** FPS; at one it costs 6%. The full font set is 735 KiB and buys back
  nothing by shrinking. Do not re-open this without a new measurement.

- **`lv_keyboard` positions itself, and `lv_obj_set_pos()` then means something else.**
  Its constructor calls `lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, 0, 0)` on itself
  (`lv_keyboard.c`), and in LVGL 9 an object's x/y become an OFFSET FROM ITS ALIGNMENT once
  one is set. So `lv_obj_set_pos(kb, 0, 240)` — which is how every other widget in this
  codebase is placed, and which reads as obviously correct — asks for a keyboard 240 px
  **below the bottom edge of the panel**. The screen renders perfectly, with no keyboard on
  it, and nothing is logged. **The WLAN password step shipped like this from M6 until
  2026-09-20** and nobody saw it, because that step needs a finger on an unknown network
  and so was never once reached from the build host. Use `lv_obj_align(kb,
  LV_ALIGN_TOP_LEFT, 0, y)`. Both keyboards are styled by `main/ui/widget_input.c`, which
  says the same thing where someone writing the next one will read it.
- **`lv_keyboard_set_map()` is PROCESS-WIDE, not per keyboard.** It writes into a
  file-scope table inside `lv_keyboard.c` (`kb_map[mode] = map`) that every keyboard reads
  at redraw, so two keyboards cannot have two layouts and installing one from either screen
  changes both. Here that is what we want — the WLAN keyboard and the Ortssuche keyboard
  must be the same keyboard — so `widget_input.c` leans on it and installs the German
  QWERTZ layout from the styling function. If you ever DO need two, the only per-instance
  hook is `LV_KEYBOARD_MODE_USER_1..4`.
- **The three layer keys are a contract with LVGL, spelled by hand.**
  `lv_keyboard_def_event_cb()` decides whether a key switches layer by comparing its CAP
  TEXT against `LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_LOWER` / `_UPPER` / `_SPECIAL` — macros
  `lv_keyboard.c` keeps to itself and does not export, so a custom map has to carry the same
  three strings (`"abc"`, `"ABC"`, `"1#"`). Get one wrong and nothing warns: the key stops
  switching layers and starts typing its own cap into the field. `widget_keyboard_debug_layer()`
  and the `a` console key exist to press all three and photograph the result, because this
  is the kind of thing that breaks silently on an LVGL bump.
- **The built-in Montserrat faces have no umlauts.** LVGL generates them with
  `-r 0x20-0x7F,0xB0,0x2022` plus FontAwesome — read it off the top of
  `lv_font_montserrat_24.c`, it is in the file. So an `ü` key drawn in Montserrat is a key
  with nothing on it; and the Plex subset has none of the `LV_SYMBOL_*` private-use
  codepoints, so a backspace drawn in Plex is a key with nothing on it either. Neither face
  can draw a German keyboard alone. The way out is a per-state font: `lv_buttonmatrix`
  re-reads `LV_PART_ITEMS`'s label style per button with that button's own state
  (`lv_buttonmatrix.c`, `draw_main`), and every control key carries
  `LV_BUTTONMATRIX_CTRL_CHECKED` — so `LV_PART_ITEMS` gets Plex and
  `LV_PART_ITEMS | LV_STATE_CHECKED` gets Montserrat.
- **`LV_LABEL_LONG_MODE_DOTS` needs a fixed HEIGHT, not just a fixed width.** Its
  implementation only ellipsises when the rendered text is taller than the object
  (`lv_label.c`: `size.y > lv_area_get_height(&txt_coords)`), so a label given a width but
  left at `LV_SIZE_CONTENT` height does not truncate — it grows another line, and draws it
  over whatever was laid out underneath. That is how the WLAN screen ended up with
  "Verbindung fehlgeschlagen:" on one line, the SSID on a second, and the network list
  painted on top of the second. **The code reads as though the problem had been fixed**,
  which is the whole trap: setting the width and asking for dots looks like the complete
  gesture. Pin both dimensions, and lay the band below out from the font's line height
  rather than from the label's measured one. **It was in the file twice**: every SSID in
  the WLAN list had the same missing height, and the network this device is actually on —
  `Apartamentos_Jose_Cruz` — had been breaking across two lines inside a 64 px row since
  M6. Both were found by looking at the glass, neither by reading the code.
- **`lv_button` arrives padded, and both `lv_obj_set_pos()` and `lv_obj_align()` measure
  from the CONTENT area.** LVGL's default theme gives a button `pad_hor = PAD_DEF` and
  `pad_ver = PAD_SMALL` — about 13 px and 8 px at this panel's 130 DPI — so a label placed
  at `(ROW_INSET, ROW_PAD_V)` inside one actually lands 13 px right and 8 px down of that,
  while any width computed from the row's own `CONTENT_W` overflows the content box by
  26 px. The visible result is subtle and looks like a different bug: an ellipsis drawn
  through the badge beside it, or a two-line row sitting too low. `widget_kill_button_chrome()`
  does NOT cover this — it clears the shadow and the outline, not the padding. Any
  `lv_button` used as a layout container wants an explicit `lv_obj_set_style_pad_all(b, 0, 0)`
  so that the insets in the code are the insets on the panel.
- **An LVGL timer outlives the object it reads, and every debug view frees that object.**
  `lv_timer_create()` runs until something deletes it; `lv_obj_clean(lv_screen_active())`,
  which `dbg_fontcard.c` and `dbg_bench.c` both call, deletes widgets and tells nobody. A
  poller reading a screen's own pointer is then a LoadProhibited a quarter of a second
  later, from a stack with no application frame in it. Keep the handle, delete it from an
  `LV_EVENT_DELETE` handler on the object it reads, and guard the callback — the same
  arrangement `widget_busy.c` uses to tie an animation's life to its object (D58). Note
  also that a `*_create()` called again on `ui_resume()` creates a SECOND timer: the old
  one has to go first.
- **LVGL's default theme draws a shadow under every `lv_button`**, which on this ground is
  a 2 px band of `#525152` all round — a grey line under every list divider and a grey
  column down both edges of a list. Nothing in the source asks for it, so nothing in the
  source looks wrong; it was found by reading the panel's framebuffer back and probing
  pixels. `widget_kill_button_chrome()`.
- **In LVGL 9 every `lv_obj_create()` is a touch target, and no event ever bubbles.**
  The `lv_obj` constructor sets `obj->clickable = 1` (labels are the exception — theirs
  sets it false), and LVGL passes an event to a parent only if the child carries
  `LV_OBJ_FLAG_EVENT_BUBBLE`. So a full-screen container a screen creates for layout
  silently eats every press aimed at anything underneath it, and a decorative
  `lv_obj_create()` circle eats every press inside its bounding box — which for the radar's
  range rings is most of the panel. Scrolling is not affected, because scrolling searches
  UP the parent chain for a scrollable ancestor; clicking does not. This cost the long press
  into Einstellungen its entire life (D62). `nav.c`'s `bubble_decorative()` is the rule that
  came out of it: an object with no event callback of its own is scenery and passes touches
  on.

**Power and the battery**
- **The BSP does not touch the AXP2101 at all** — `grep -i axp` over the Waveshare component
  returns nothing. Everything about charging is `main/power/axp2101.c`, and before it existed
  the charger ran on whatever the chip's eFuse said.
- **REG50[4] must be set or the cell may never charge.** The TS pin is wired to a plain
  resistor to ground, not a thermistor, and that bit's reset value comes from the eFuse.
  Waveshare's own example disables it with the comment "otherwise it will cause abnormal
  charging". The symptom is a device that looks fine and quietly stays flat — the Akku line
  in Einstellungen says "wird nicht geladen" for exactly this case.
- **Never write a rail.** REG80/REG90 and friends turn the panel or the ESP32 off, and the
  only way back is the PWRKEY on the side edge. The driver touches the charger, the ADC and
  the gauge, nothing else.
- **A zeroed `battery_status_t` means a black screen.** Its `brightness_cap_pct` is 0 and the
  dimmer takes the minimum of the schedule and the cap, so the one in main.c is initialised
  explicitly. Copy that if you ever add another.

**Fonts and text**
- **`lv_font_conv` does not apply OpenType features.** A font whose tabular figures exist
  only behind the `tnum` feature will render digits that visibly jitter on every refresh.
  Use a font that is tabular *by default* — IBM Plex Mono is; Barlow Condensed, Saira
  Condensed and Oswald are not.
- **`lv_font_conv` compresses glyph bitmaps by default, and LVGL 9 will not decode
  them.** `.bitmap_format = 1` needs `LV_USE_FONT_COMPRESSED`, which is off in this
  build — so every glyph renders as *nothing at all*, with no error logged anywhere.
  Generate with `--no-compress`. Uncompressed is the better trade regardless: it costs
  flash but no per-frame CPU, and this product is render-bound.
- **`lv_font_conv` predates LVGL 9.3's `.static_bitmap` flag** and cannot emit it, so
  `tools/build_fonts.sh` patches it in after generating. Without it LVGL copies every
  glyph instead of using the const data in place.
- **Umlauts are not in the default ASCII range.** Subset Latin-1 supplement explicitly or
  ä/ö/ü/ß render as blanks. Exact ranges in docs/DESIGN.md §3.
- **Minimum readable cap height on this panel is ~30 px** (ISO 9241-303 at 70 cm). Chrome
  may be smaller; anything he needs at a glance may not.

**Data parsing**
- **`flight` is space-padded to 8 characters** (`"AUA453  "`). Trim before sending to
  `routeset` or lookups will silently miss.
- **`alt_baro` is the string `"ground"`**, not a number, when the aircraft is on the
  ground. Parsing it as an int will break.
- **`r` (registration) and `t` (type) can be absent** — military, blocked, and TIS-B
  targets. Always null-check.
- **Parse with cJSON, not ArduinoJson.** This is ESP-IDF; cJSON already ships with it, and
  that is what lets the *same* parser link into the host tests (D6, D7). You need ~6 of 50+
  fields per aircraft: pull those into the fixed-size struct and free the document, rather
  than keeping it alive.
- Send a **descriptive User-Agent** on every request — planespotters.net rejects generic
  ones outright, and it is the courteous thing to do with free services. It identifies the
  project by **repo URL, not by email**: `HTTP_USER_AGENT` in `main/net/http_get.h`. The
  address in the git history is for authorship; it does not get handed to a third party.
  This was deliberate — do not "improve" the UA by putting contact details back in.

## 8. Decisions

**Settled (2026-09-18):**
- **Visual direction: B · Cockpit** — avionics colour semantics, IBM Plex Mono/Sans
  Condensed, dark ground. Full system and screens in [docs/DESIGN.md](./docs/DESIGN.md).
- **UI language: German.** See §1 for the font and place-name consequences.
- **Form factor: desk stand**, powered over USB-C. The rear `5V_IN` header is not needed.
  It also means the device travels between the two locations — see §6. **Amended by D61:**
  a PH2.0 cell now rides along as a UPS for about four hours off the cable. It is taped to
  the back of the case, and it is optional — every code path is a no-op without one.
- **Two framebuffers, anti-tearing on** — measured on the unit, not guessed. Two is
  both faster than one (28.5 vs 21.4 FPS) and tear-free; three buys nothing. Numbers in
  PLAN.md M1.
- **WiFi setup is on-device, not a captive portal.** A portal needs a phone, a second
  network join and a browser — in a foreign country, by someone who will not read a manual.
  An on-panel network list with `lv_keyboard` costs one screen and lets him fix it standing
  in front of it. See DESIGN.md §5.7 and PLAN.md M6.

**Settled since, by building it:**
- **Magenta on black works** — settled by measurement, not by argument. `#FF3FDA` reads
  as clearly distinct from the white above it and the cyan below it at ≥ 56 px on this
  panel (PLAN M1). It has been on screen ever since and nothing has argued against it.
- **Navigation: radar first, the answer one layer down** — the owner's own call after
  using the device. See §1 and D60.
- **Over-the-air updates exist and ship off** — no URL stored, so a new device contacts
  nothing until someone sets one; `https://` only, enforced in code and in sdkconfig;
  rollback armed. A feature that ships off must cost nothing while it is off (D44, D52).

**Still open — ask Markus, do not guess:**
1. **Light theme** — the polarity evidence is genuinely split (DESIGN.md §7). Auto-dim is
   settled and shipped; a second full theme is not.
2. **Aircraft photos** — nice touch, but costs flash, RAM and a third-party dependency.
3. ~~**Stand / enclosure**~~ — **closed by the owner, 2026-09-20: there will be no printed
   stand.** The cell is taped to the back of the case instead (§2). `hardware/desk_stand.scad`
   stays in the tree as an unprinted, unmeasured sketch; do not treat it as pending work and
   do not spend a milestone on it. If it is ever printed, its dimensions still come from the
   datasheet rather than from calipers, so `part = "fittest"` first.
4. **Is the 13 px identity line findable from his chair?** It is deliberately dimmed to
   `THEME_TEXT_TERTIARY` so it cannot crowd out the answer. Only his eye can settle that;
   the question is with him and unanswered.

Design-side open questions live in DESIGN.md §7 and are mirrored here. One list, not two.

## 9. Licence policy when copying code

We are deliberately reusing prior art. Respect the licences. See `docs/RESEARCH.md` for
the per-project survey.

- ✅ **Safe to copy (MIT/Apache-2.0):** MatixYo/ESP32-Plane-Radar, ironicbadger/ESP32-Plane-Radar,
  kovaacs/sky_overhead, capsule-radar, TheJinxNL/ESP32FlightRadar, ThingPulse PlaneSpotter,
  nicholaswilde/cyd-flight-radar. **Keep the copyright headers.**
- ⚠️ **Read, do not paste:** FlightScnr (CC BY-NC-SA — non-commercial + viral share-alike),
  carlobolla/epaper-plane-tracker (GPL-3.0 — copyleft).
- ⛔ **No licence file = all rights reserved.** Eiswolf-BG, asanga-sci, Coreymillia,
  2E0LXY, arvis91/deskradar. Learn from their *ideas*; do not copy their code.

**Data licences:** adsb.lol is ODbL 1.0. adsb.fi is personal/non-commercial only and
requires attribution. adsbdb's route data may be displayed but **not mirrored into another
database**. Open-Meteo's geocoding data is GeoNames under **CC BY 4.0**, and its free tier
is non-commercial. Show an attribution line in the UI — there are two now, at the foot of
Einstellungen (`STR_ATTRIBUTION`, `STR_ATTRIBUTION_2`), because all of it does not fit on
one line at that size and a truncated attribution is not a cosmetic problem.

## 10. Conventions

Most of these used to be requests. Three of them now have gates, and the gates are there
because the convention was broken once each.

- **Every user-facing German string lives in `main/strings_de.h`.** Enforced by
  `tools/check_strings.py`. Four exceptions are documented at the top of that header —
  the indexed tables in `fmt_de.c`, `settings.c`, `tbl_airport.c` and `tbl_actype.c`, all
  of which `check_strings.py --list` still dumps for review. There is no fifth.
- **Every character used must have a glyph.** Enforced by `tools/check_font_coverage.py`.
  LVGL draws a missing glyph as *nothing at all*, with no error anywhere (D32).
- **No colour literals outside `main/ui/theme.h`.** Every token is named there, from
  DESIGN.md §2. It is the only way the DO-257A six-colour ceiling stays enforceable once
  there are seven screens and three people editing them.
- **One rule names an aircraft**, and it lives in `main/data/identity.c`: callsign first,
  registration where there is no flight number, never both, and never the raw ICAO
  designator in front of him (D36, D46).
- **The UI formats nothing.** `view_model_t` carries final display text — German, correct
  units, correctly grouped numbers. Screens position strings and pick colours. That split
  is what lets the whole language layer be tested on the host in milliseconds.
- Network work lives on its own FreeRTOS task; **all LVGL calls happen on the display
  task** behind a mutex. This split is non-negotiable on this hardware.
- Prefer fixed-size `char` arrays over dynamic strings in aircraft structs — see MatixYo's
  40-byte `Aircraft` struct.
- **Anything host-testable is host-tested.** `test/host/` needs no board and no network.
  Parsers, formatters, tables, settings, failover, backoff and the OTA policy all run
  there. If a bug can be reproduced off-device, reproduce it off-device first.

### Privacy and secrets — not negotiable

- **WiFi credentials go in NVS, never in the repo, and never through a transcript.**
  `tools/provision.py` prompts locally (`getpass`, or a hidden-answer macOS dialog when
  stdin is not a TTY), sends the password straight down the serial link and keeps nothing.
  `wifi_creds_list()` returns SSIDs only. No code path logs a password. Do not add one,
  and do not ask Markus to type a password into a chat window.
- ~~**The repo is private on purpose.**~~ **Amended by the owner, 2026-09-21: the repo is
  PUBLIC.** It was made public so releases could be published to an unauthenticated HTTPS
  URL the device can fetch from (D72). The reason the old rule existed has not gone away,
  it was accepted: §6 of this file lists three residential addresses with coordinates,
  §1 and the README say who lives at them and that he splits the year between them,
  `main/data/settings.c` carries the same three as presets, and `docs/screens/` shows
  one set of coordinates and one real SSID. All of it is in the history of all sixty-eight
  commits, including some commit subject lines, so none of it can be taken back by editing
  a file. The owner was shown that list and chose to publish anyway. **Do not "restore"
  this rule, and do not quietly redact §6 either** — half a redaction on a public history
  is worse than none, because it reads as a mistake rather than a decision.

  What is still not negotiable, and now matters more rather than less:
  - **The signing key never enters the repo.** `secure_boot_signing_key.pem` is gitignored
    and lives in the GitHub Actions secret `SIGNING_KEY`. Only `tools/ota_signing_key.pub.pem`,
    the public half, is committed. Never paste the private key into a transcript, an issue
    or a third-party service — a public repo plus that key is a firmware push to a device
    in somebody's living room.
  - **Nothing else new goes in.** A public repo is not an invitation to add the next
    address, SSID or screenshot. What is published is what was reviewed and accepted; a
    fresh leak is not covered by that decision.
- **OTA is `https://` only** — refused in `ota_set_url()` and again by
  `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=n`. It ships with no URL stored, so a fresh device
  contacts nothing. Since D72 an image must also be **signed** by the key above, verified
  by the running app against its own signature block, so HTTPS is no longer the only thing
  standing between a release and the panel.

## 11. How this repo has actually failed

Sixty decisions are a lot to read. These three patterns caused most of the real bugs, and
they will catch you too.

**1. The comment had drifted from the code, and the review believed the comment.**
`.disable_auto_redirect` "for GitHub releases" was inert on the code path it sat in. A
clamp that "does not wrap" was non-monotonic. A size check "to reject an obviously wrong
image" checked nothing of the sort. Each one was read, believed and shipped. **Verify the
claim a comment makes against the code under it**, especially when the comment sounds
confident.

**2. A gate can quietly stop checking.** `check_font_coverage.py` scanned `main/ui` and
`main/data`; `main/strings_de.h` sits one directory above both, so the day every German
string moved into it the gate went on passing with nothing left in its scan path (D39).
It had a second silent hole in the same breath: it compared *source spellings*, so a
deliberate `"\xE2\x80\x94"` read as plain ASCII. `check_strings.py` then shipped with the
exact bug it was written to catch (D55). Both now assert a **floor on how many files they
saw**. When you touch a gate, prove it still bites — inject a violation, watch it fail,
and only then trust a pass.

**3. Stress-navigating finds what careful tapping never will.** Both WLAN panics needed
rapid repeated navigation to appear: LVGL's fixed heap could not fit the keyboard, and the
WiFi scan task wrote into a screen that had been deleted (D58). Twelve failures in forty
navigations, zero in forty careful ones. **Open and close a new screen forty times before
you call it done.**

One more, which is not a pattern but is worth knowing: `esp_lvgl_port_touch.c` wraps its
I²C reads in `ESP_ERROR_CHECK`, so a single transient bus fault panics the device. It is a
managed component and deliberately unpatched (D47) — but it is the first place to look at
any unexplained reboot.
