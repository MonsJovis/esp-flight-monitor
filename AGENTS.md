# AGENTS.md — esp-flight-monitor

Operating manual for AI agents working in this repo. Read this before touching code.

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
- **No interaction required for the primary answer.** The default screen must already show
  the most relevant aircraft. Tapping is for browsing, never for the main use case.
- **Never show an empty screen.** If the sky is clear, show the last aircraft seen, or a
  clock. A blank panel reads as "broken" to a non-technical user.
- **German UI — confirmed.** He is Austrian. Keep all user-facing strings in one
  translation unit. Two consequences that are easy to miss until they bite:
  - **The LVGL font must carry ä ö ü ß.** LVGL's built-in Montserrat faces are ASCII-only.
    Build a font including the Latin-1 supplement range, or umlauts render as blanks —
    and "Zurich"/"Munchen" on a German panel looks broken.
  - **The route API returns *English* city names** ("Vienna", "Munich", "Prague"). Ship a
    small airport → German name table for the common European destinations (Wien, München,
    Zürich, Prag, Mailand, Athen, Kopenhagen, Warschau …) and fall back to the API's own
    name when there is no entry. Without this the headline reads "Vienna → London" to a
    man sitting in Austria.

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

**Power:** 2× USB-C, or PH2.0 Li-ion, or `5V_IN` on the rear header. **This build is a desk
stand, so USB-C is the supply** — the side-edge port placement is fine and the rear header is
not needed. (It would only matter for a flush wall install, where the side ports become
unreachable.)

**GPIO budget is effectively zero.** The RGB bus consumes nearly everything. Expansion
goes over I²C, or by repurposing TCA9554 EXIO pins after init.

## 3. Stack

**ESP-IDF 5.4.2 + official Waveshare BSP + LVGL 9.2.x.** Already installed at `~/esp/esp-idf`
(v5.4) — not on PATH, so `. ~/esp/esp-idf/export.sh` first.

```bash
idf.py add-dependency "waveshare/esp32_s3_touch_lcd_4b^2.0.0"
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash monitor
```

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
Type names →  static PROGMEM table: ICAO code → "Airbus A320neo"
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
- **Type names belong in flash, not on the network.** `adsb.lol` gives `t` = `A20N`; a
  ~200-entry table costs a few KB and removes a whole API dependency.

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

Two presets plus a custom entry. Store in NVS.

| Preset | Address | Lat / Lon |
|---|---|---|
| Gloggnitz (AT) | Semmeringstraße 11, 2640 Gloggnitz | `47.6691` / `15.9303` |
| Pattaya (TH) | 154 Thappraya Rd, Pattaya City, Chon Buri 20150 | `12.9211` / `100.8721` |

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
- **Switching location should be one tap**, not a coordinate entry form. Two named presets
  ("Gloggnitz", "Pattaya") plus an advanced custom option.
- **Timezone changes with the location** — CEST and ICT are 5–6 h apart depending on the
  season. Bind the timezone to the location preset; do not make him set a clock.
- Consider auto-detecting the location preset from the WiFi SSID he connects to.

## 7. Gotchas that will cost you a day

**Firmware / display**
- **Flash writes tear the display.** Known open bug on this exact silicon+panel combo
  (espressif/esp-bsp#570): flash and PSRAM share SPI1, so an NVS commit starves the RGB
  bounce-buffer refill. Keep `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y` and
  `CONFIG_SPIRAM_RODATA=y` on, and pause LVGL around NVS writes.
- **Stay at 80 MHz PSRAM.** 120 MHz is experimental and temperature-sensitive — a real
  risk for an always-on panel behind glass.
- **WiFi bursts compete for PSRAM bandwidth** and cause visible drift. This is the #1
  field failure mode for RGB panels. Keep network work off the render path.
- **Strapping pins are on the RGB bus**: GPIO3 (VSYNC), GPIO45 (G3), GPIO46 (HSYNC), and
  GPIO0 is BOOT. Do not back-drive these during reset.
- Anti-tearing is **off** by default (`BSP_LCD_RGB_BUFFER_NUMS=1`). 8 MB PSRAM has room
  for 2–3 framebuffers at 450 KiB each, but each one costs bandwidth. Measure.
- **Large fonts land in PSRAM, not flash.** `CONFIG_SPIRAM_RODATA=y` — mandated at the top of
  this block as the tearing mitigation — relocates `.rodata`, and LVGL fonts *are* `.rodata`.
  So the 100 px hero face and its shrink ladder compete with the framebuffer for PSRAM
  bandwidth: the project's #1 risk and its most distinctive design choice pulling on the
  same bus. Measure it in PLAN.md M1, before seven screens are built on the assumption.

**Fonts and text**
- **`lv_font_conv` does not apply OpenType features.** A font whose tabular figures exist
  only behind the `tnum` feature will render digits that visibly jitter on every refresh.
  Use a font that is tabular *by default* — IBM Plex Mono is; Barlow Condensed, Saira
  Condensed and Oswald are not.
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
- **Use ArduinoJson's filter feature** (or equivalent). You need ~6 of 50+ fields per
  aircraft; filtering a 4 KB response keeps the document around 1 KB.
- Send a **descriptive User-Agent with contact info** on every request. planespotters.net
  rejects generic ones outright, and it is the courteous thing to do with free services.

## 8. Decisions

**Settled (2026-09-18):**
- **Visual direction: B · Cockpit** — avionics colour semantics, IBM Plex Mono/Sans
  Condensed, dark ground. Full system and screens in [docs/DESIGN.md](./docs/DESIGN.md).
- **UI language: German.** See §1 for the font and place-name consequences.
- **Form factor: desk stand**, powered over USB-C. The rear `5V_IN` header is not needed.
  It also means the device travels between the two locations — see §6.
- **WiFi setup is on-device, not a captive portal.** A portal needs a phone, a second
  network join and a browser — in a foreign country, by someone who will not read a manual.
  An on-panel network list with `lv_keyboard` costs one screen and lets him fix it standing
  in front of it. See DESIGN.md §5.7 and PLAN.md M6.

**Still open — ask Markus, do not guess:**
1. **Anti-tearing / framebuffer count** — needs measurement on the real unit (PLAN M1).
2. **Magenta on black** — semantically exact under AC 25-11A, but a documented
   high-confusion pair and 6.5:1 against our 7:1 target. If it reads badly on the panel the
   colour system changes, so it is checked in PLAN M1, not discovered in M3.
3. **Light theme** — the polarity evidence is genuinely split (DESIGN.md §7). Auto-dim is
   settled and scheduled in M6; a second full theme is not.
4. **Aircraft photos** — nice touch, but costs flash, RAM and a third-party dependency.
5. **Stand / enclosure** — the board ships as a flush 86-type faceplate, 86.5 × 86.5 × 14 mm.
   A desk stand has to be printed or sourced.

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
database**. Show an attribution line in the UI.

## 10. Conventions

- Keep all user-facing strings in one translation unit.
- **No colour literals outside `main/ui/theme.h`.** Every token is named there, from
  DESIGN.md §2. It is the only way the DO-257A six-colour ceiling stays enforceable once
  there are seven screens and three people editing them.
- Network work lives on its own FreeRTOS task; **all LVGL calls happen on the display
  task** behind a mutex. This split is non-negotiable on this hardware.
- Prefer fixed-size `char` arrays over `String` in aircraft structs — see MatixYo's 40-byte
  `Aircraft` struct.
- Write host-side unit tests for parsers (see `kovaacs/sky_overhead` `tools/` for the
  pattern). Parsing bugs are the most common failure and the easiest to test off-device.
- Secrets (WiFi credentials) go in NVS, never in the repo.
