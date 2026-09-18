# Implementation Plan

Companion to [AGENTS.md](../AGENTS.md) (constraints, hardware, data architecture) and
[RESEARCH.md](./RESEARCH.md) (prior art, API survey).

## Sequencing principle

**Retire the biggest risk first, then build the smallest thing that is actually useful.**

The three real risks on this project, in order:

1. **PSRAM bandwidth.** A 450 KiB framebuffer, WiFi, and JSON parsing all compete for the
   same bus. If this does not hold, the UI design has to change — so measure it before
   designing anything.
2. **Display tears on flash writes.** A known open bug on this exact silicon+panel combo
   (espressif/esp-bsp#570). Needs to be characterised on *our* unit, not assumed.
3. **Free APIs can vanish.** airplanes.live gated itself in 2026. The failover path has to
   exist before we depend on the data.

Milestones **M0–M3** exist to retire those. **M3 is the payoff milestone** — the point where
the device answers the question it was built for. Everything after M3 is improvement, and
the project is already a success if it stalls there.

---

## M0 — Repo and toolchain

**Goal:** a reproducible build, and proof the hardware is healthy before we write any code.

- [ ] `.gitignore` for ESP-IDF (`build/`, `sdkconfig`, `sdkconfig.old`, `managed_components/`,
      `dependencies.lock`, `.vscode/`)
- [ ] ESP-IDF project skeleton: `CMakeLists.txt`, `main/`, `sdkconfig.defaults`
- [ ] `idf.py add-dependency "waveshare/esp32_s3_touch_lcd_4b^2.0.0"`
- [ ] `sdkconfig.defaults` from the vendor demos — critically:
      `CONFIG_SPIRAM_MODE_OCT=y`, `CONFIG_SPIRAM_SPEED_80M=y`,
      `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y`, `CONFIG_SPIRAM_RODATA=y`,
      `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, `CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y`
- [ ] Flash the **stock vendor `02_lvgl_demo_v9`** first and read its on-screen FPS counter

**Done when:** the vendor demo runs on our unit and we have written down its FPS. That number
is the ceiling everything else is measured against.

> Do this before writing code. If the panel misbehaves, we want to know it is the board, not us.

---

## M1 — Hardware bring-up

**Goal:** our own firmware drawing on the panel, with the memory budget known.

- [ ] BSP init: display, backlight (GPIO4 LEDC), I²C bus, GT911 touch
- [ ] LVGL 9.2 via `esp_lvgl_port`, display task owning **all** LVGL calls behind a mutex
- [ ] "Hello" screen + a touch-position readout to prove GT911 polling works
- [ ] **Log the memory budget at boot** — free internal SRAM and free PSRAM with the
      framebuffer allocated. Record it in this file.
- [ ] Try `BSP_LCD_RGB_BUFFER_NUMS` = 1, 2, 3 and note FPS and PSRAM headroom for each

**Done when:** we can state, with numbers, how much internal SRAM is left for the network
stack. That number gates M2.

**Risk:** if free internal SRAM is tight, this is where the plain-HTTP decision pays off —
no TLS means roughly 40 KB more headroom. Do not silently add HTTPS later.

---

## M2 — Network spine (no UI)

**Goal:** real aircraft data in the log. Proves the whole data chain in isolation, where
it is easy to debug.

- [ ] WiFi station + provisioning. Captive portal when no known network is found
      (see AGENTS.md §6 — this device travels)
- [ ] HTTP client, **plain HTTP**, descriptive User-Agent with contact info
- [ ] `GET http://api.adsb.lol/v2/point/{lat}/{lon}/30` → filtered JSON parse
      (only ~6 of 50+ fields per aircraft)
- [ ] `Aircraft` struct with fixed `char` arrays, no heap strings
      (copy MatixYo's 40-byte struct, MIT)
- [ ] `POST http://adsb.im/api/0/routeset` batched for the callsigns on screen
      (copy `RouteParser.h` from kovaacs/sky_overhead, MIT)
- [ ] Route cache keyed on callsign, in memory for now
- [ ] **Host-side unit tests for both parsers** — capture real responses as fixtures
- [ ] Log the nearest aircraft every poll: callsign, type, route, distance, bearing

**Done when:** `idf.py monitor` prints something like
`AUA453 | A320 | Wien -> London | 12.4 km NE` on a real flight, for ten minutes without a
crash or a rate-limit trip.

**Watch for:**
- `flight` is space-padded to 8 chars — trim before lookups
- `alt_baro` is the **string** `"ground"` when on the ground
- `r` and `t` can be absent (military, blocked, TIS-B)
- Poll floor 10–15 s. A tight retry loop gets the IP banned.

> Fixture-backed parser tests are the highest-value tests on this project. Parsing is where
> the bugs are, and it is the only part testable off-device in milliseconds.

---

## M3 — Vertical slice: "Über dir jetzt" ⭐

**Goal:** the device answers the actual question. **This is the milestone that matters.**

- [ ] Single screen, the nearest aircraft, no interaction required
- [ ] **Route as the headline, in the largest type on screen** — `Wien → London`
- [ ] Airline name, plain-language type ("Airbus A320neo"), altitude, distance + direction
- [ ] Empty-sky fallback: clock, never a blank panel
- [ ] Auto-return to this screen when traffic appears

**Done when:** it can be set on a desk, glanced at from across the room, and read correctly.
Test it on someone who has not seen it before.

**Design note:** every existing project builds a radar scope first and hides the route behind
a tap. That is backwards for this user. Resist the urge to build the radar first — it is more
fun to write and less useful to him.

---

## M4 — Robustness

**Goal:** survives a week unattended.

- [ ] Exponential backoff on `429`/`503`. Treat spurious `308` as throttling, not a redirect
- [ ] Source failover: 2 consecutive failures → `adsb.fi` for a few minutes.
      One parser serves both; only the wrapper key differs (`ac` vs `aircraft`)
- [ ] Route cache persisted to NVS, survives reboot
- [ ] **Characterise the tearing bug**: write NVS while the UI animates, observe, then
      mitigate by pausing LVGL around commits. Record the result here.
- [ ] Watchdog + auto-reconnect; recover from a router reboot unattended
- [ ] Distinguish *"looking…"* from *"no route on file"* in the UI — a 2E0LXY lesson

**Done when:** it runs seven days untouched, through at least one WiFi drop, with no manual
intervention and no rate-limit ban.

---

## M5 — Second screen: what else is up there

**Goal:** his "just browsing" mode.

- [ ] Swipe to a list of nearby aircraft, sorted by distance (`dst` is pre-computed)
- [ ] Tap a row → its detail card
- [ ] Radar/PPI view if the FPS budget from M1 allows
      (geo projection from MatixYo `radar_display.cpp`, MIT)
- [ ] Aircraft glyph rotated by heading (ThingPulse `PlaneSpotter.cpp` `drawPlane()`, MIT)

**Done when:** he can see the whole sky and get from any aircraft to its route in one tap.

---

## M6 — Travel and settings

**Goal:** he carries it to Thailand and it just works.

- [ ] Location presets: **Gloggnitz** `47.6691/15.9303`, **Pattaya** `12.9211/100.8721`, custom
- [ ] Timezone bound to the preset — he never sets a clock
- [ ] **Multiple WiFi networks remembered**, not reconfigured on arrival
- [ ] Optional: auto-select the location preset from the connected SSID
- [ ] Radius and brightness settings
- [ ] All settings in NVS (mind the tearing interaction from M4)

**Done when:** unplug in Gloggnitz, plug in at Thappraya Rd, and it shows Thai traffic in
local time without anyone touching a setting.

---

## M7 — German polish

**Goal:** it reads like it was made for him, not translated.

- [ ] **LVGL font rebuilt with the Latin-1 supplement** — ä ö ü ß. Built-in Montserrat is
      ASCII-only and umlauts render as blanks
- [ ] **Airport → German name table.** The route API returns English: "Vienna", "Munich",
      "Prague". Map the common European destinations (Wien, München, Zürich, Prag, Mailand,
      Athen, Kopenhagen, Warschau …); fall back to the API name when absent
- [ ] German compass bearings ("nordöstlich"), units, date/time format
- [ ] ICAO type → German plain language where it differs
- [ ] All strings in one translation unit
- [ ] Data attribution line in the UI (adsb.lol is ODbL)

**Done when:** no English leaks into a normal session.

---

## M8 — Ship it

- [ ] Desk stand — printed or sourced. Board is 86.5 × 86.5 × 14 mm, ships as a flush
      86-type faceplate, so the stand is the missing piece
- [ ] Power: USB-C (side edge — fine for a desk unit)
- [ ] Optional: OTA update (ironicbadger's `ota_update.cpp`, MIT) so it can be fixed
      remotely while he is in Thailand
- [ ] README with a photo and a one-paragraph "what it does"

---

## Accelerators

- **Browser mockup before firmware UI.** capsule-radar ships
  `assets/plane_radar_2.0_mockup.html` (MIT). Iterating layout in a browser is minutes per
  cycle; flashing is not. Worth doing before M3.
- **Fixture-driven parser tests** (M2) catch most bugs off-device.
- **Vendor demo as reference** — when something looks wrong, flash `02_lvgl_demo_v9` and
  compare. It isolates our bugs from the board's.

## Numbers to fill in as we go

| Measurement | Value | When |
|---|---|---|
| Vendor demo FPS | _TBD_ | M0 |
| Free internal SRAM with framebuffer | _TBD_ | M1 |
| Free PSRAM with framebuffer | _TBD_ | M1 |
| FPS at 1 / 2 / 3 framebuffers | _TBD_ | M1 |
| Tearing severity on NVS write | _TBD_ | M4 |
| Typical poll payload at 30 nm | ~4 KB | measured 2026-09-18 |

## Explicitly not doing

- **Live map tiles.** Nobody does this on an ESP32; RAM makes it impractical. A single
  pre-dimmed static image under the radar (TheJinxNL's trick) is the ceiling if we want one.
- **Local SDR receiver.** A Pi + dongle would cut latency, but it would have to be rebuilt
  for Thailand and still would not provide route data.
- **Aircraft photos** — deferred, open question in AGENTS.md §8.
