# Implementation Plan

> **Status, 2026-09-21.** **M0–M8 are built and verified on the real unit against live
> traffic.** Every checklist item in the plan is closed, including the one no tool could
> close: the German was read through by a native speaker (M7).
>
> What the panel says, unprompted, on real aircraft over Gloggnitz:
> ```
> Wien → Bologna · Austrian Airlines · 4.793 m · 16,4 km nordöstlich
> Scheibe SF-25 Falke · Eine Route gibt es nur zu Flügen mit Flugnummer. · 11,5 km NW
> München → Seoul · Lufthansa · 10.211 m · 8,9 km nordwestlich
> ```
>
> **35,562 host checks across twelve suites, 0 failed**, plus three gates that run with
> them: a font-coverage check (LVGL draws a missing glyph as *nothing*), a string audit
> (every German word must come from `main/strings_de.h`) and a console-key check (every
> key the firmware answers to has to be written down in all three places that describe
> the console). Every screenshot in README.md is
> the panel's own framebuffer read back over USB by `tools/grab_screen.py`.
>
> Past M8 the deck grew teeth: the radar marks are carried forward between polls so they
> move instead of jumping (D59), both are tappable, and the Liste scrolls. Two latent
> panics fell out of testing that — the WLAN screen never fitted in LVGL's fixed heap, and
> the WiFi scan wrote into it after it was closed (D58).
>
> Fifty-nine decisions are written up in docs/DECISIONS.md, including the ones that were
> wrong. The sharpest of the late ones: **consolidating every German string into one file
> walked them out from under the font gate** (D39), which then passed for having nothing
> left to check — a silent hole in the one tool whose entire job is catching silence.

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

### What M3 needs that is not obvious

M3 renders a 100 px headline in German. Three subsystems have to exist first, and none of
them are UI work:

- a **font pipeline** — LVGL's built-in Montserrat stops at 48 px (**M1**)
- **German place, airline and type names** — the APIs return English and codes (**M2.5**)
- **unit formatters** — the APIs speak nautical miles and feet; the panel speaks km and m (**M2.5**)

Filing those under "polish" is the easiest way to stall this project at 90%. They are
scheduled before M3 on purpose.

---

## M0 — Repo and toolchain

**Goal:** a reproducible build, and proof the hardware is healthy before we write any code.

- [x] `.gitignore` for ESP-IDF (`build/`, `sdkconfig`, `sdkconfig.old`, `managed_components/`,
      `dependencies.lock`, `.vscode/`)
- [x] ESP-IDF project skeleton: `CMakeLists.txt`, `main/`, `sdkconfig.defaults`
- [x] `idf.py add-dependency "waveshare/esp32_s3_touch_lcd_4b^2.0.0"`
- [x] `sdkconfig.defaults` from the vendor demos — critically:
      `CONFIG_SPIRAM_MODE_OCT=y`, `CONFIG_SPIRAM_SPEED_80M=y`,
      `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y`, `CONFIG_SPIRAM_RODATA=y`,
      `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, `CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y`
- [~] Flash the **stock vendor `02_lvgl_demo_v9`** first — *skipped deliberately*: our own
      BSP bring-up worked first time and proves the board **and** the toolchain. See
      docs/DECISIONS.md D1; the demo remains the fallback if the panel ever misbehaves.

**Done when:** the vendor demo runs on our unit and we have written down its FPS. That number
is the ceiling everything else is measured against.

> Do this before writing code. If the panel misbehaves, we want to know it is the board, not us.

---

## M1 — Hardware bring-up and the visual foundation

**Goal:** our own firmware drawing on the panel, with the memory budget known *and the
design system rendering*. The font work is here rather than late because M3 cannot draw its
own headline without it.

**Bring-up**

- [x] BSP init: display, backlight (GPIO4 LEDC), I²C bus, GT911 touch
- [x] LVGL 9.2 via `esp_lvgl_port`, display task owning **all** LVGL calls behind a mutex
- [ ] "Hello" screen + a touch-position readout to prove GT911 polling works
- [x] **Log the memory budget at boot** — free internal SRAM and free PSRAM with the
      framebuffer allocated. Record it in this file.
- [x] Try `BSP_LCD_RGB_BUFFER_NUMS` = 1, 2, 3 and note FPS and PSRAM headroom for each

**Font pipeline** — infrastructure, not polish

- [x] `lv_font_conv` build script in `tools/`, subset ranges taken from DESIGN.md §3
- [x] Generate IBM Plex Sans Condensed at the hero ladder (100 / 76 / 56 px) and body
      (34 / 25 / 22 px); IBM Plex Mono at 32 / 17 / 13 / 12 px
- [x] Verify **ä ö ü ß** and **→ · °** actually render — one test string, on the panel
- [x] **Re-measure FPS with a 100 px face on screen.** `CONFIG_SPIRAM_RODATA=y` relocates
      `.rodata` into PSRAM, and LVGL fonts *are* `.rodata` — so glyph reads share the bus
      the framebuffer writes to. Record the delta and the PSRAM cost of the font set.

**Design system**

- [x] `main/ui/theme.h` — every token from DESIGN.md §2 as a named constant. Screens
      reference tokens, never hex literals. That is the only way the six-colour ceiling
      from DO-257A stays enforceable once seven screens exist.
- [x] **Magenta-on-black check.** `#FF3FDA` route text beside `#FFFFFF` and `#22E3FF`,
      photographed on the real panel. AC 25-11A flags this pair specifically, and it
      measures 6.54:1 — below our AAA target. If it reads badly the colour system changes,
      and that is enormously cheaper now than after the screens exist.

**Done when:** we can state, with numbers, how much internal SRAM is left for the network
stack, *and* a German string renders at 100 px with no blank glyphs. Those two gate M2 and
M3 respectively.

**Risk:** if free internal SRAM is tight, this is where the plain-HTTP decision pays off —
no TLS means roughly 40 KB more headroom. Do not silently add HTTPS later.

---

## M2 — Network spine (no UI)

**Goal:** real aircraft data in the log. Proves the whole data chain in isolation, where
it is easy to debug.

- [x] WiFi station, credentials hard-coded for now (provisioning UI is M6)
- [x] HTTP client, **plain HTTP**, descriptive User-Agent with contact info
- [x] `GET http://api.adsb.lol/v2/point/{lat}/{lon}/30` → filtered JSON parse
      (only ~6 of 50+ fields per aircraft)
- [x] `Aircraft` struct with fixed `char` arrays, no heap strings
      (copy MatixYo's 40-byte struct, MIT)
- [x] `POST http://adsb.im/api/0/routeset` batched for the callsigns on screen
      (copy `RouteParser.h` from kovaacs/sky_overhead, MIT)
- [x] Route cache keyed on callsign, in memory for now
- [x] **Host-side unit tests for both parsers** — capture real responses as fixtures
- [x] Log the nearest aircraft every poll: callsign, type, route, distance, bearing

> **Provisioning** is the one step that needs a human, because credentials live in NVS and
> never in the repo (AGENTS.md §10): run `python3 tools/provision.py`. It picks a free slot,
> so setting up a holiday network does not erase the one that gets him home, and it
> reconnects immediately instead of sitting out a backoff. Done for real on 2026-09-19,
> from Gloggnitz to a holiday apartment, which is precisely the case AGENTS.md §6 designs
> for.
>
> `1`/`2`/`3` on the console still replay §5.1/§5.2/§5.3 from the captured 2026-09-18
> traffic, which is how a specific state gets summoned on demand instead of waiting for the
> sky to produce one.

**Done when:** `idf.py monitor` prints the raw truth —
`AUA453 | A320 | Vienna -> London | 12.4 nm NE` — on a real flight, for ten minutes without
a crash or a rate-limit trip. English names and nautical miles are correct at this stage;
M2.5 translates them.

**Watch for:**
- `flight` is space-padded to 8 chars — trim before lookups
- `alt_baro` is the **string** `"ground"` when on the ground
- `r` and `t` can be absent (military, blocked, TIS-B)
- Poll floor 10–15 s. A tight retry loop gets the IP banned.

> Fixture-backed parser tests are the highest-value tests on this project. Parsing is where
> the bugs are, and it is the only part testable off-device in milliseconds.

---

## M2.5 — Words and numbers

**Goal:** everything the panel says, in his language and his units. No hardware, no
network — pure data and string work, every line of it testable on the host in milliseconds.

- [x] **nm → km** and **ft → m** converters. The APIs give `dst` in nautical miles and
      `alt_baro` in feet; every mockup shows km and metres. Nothing in the chain does this.
- [x] **German number formatting** — thousands dot, decimal comma: `9.100 m`, `12,4 km`.
      `printf("%d")` gives `9100`, which is wrong on an Austrian panel.
- [x] **Airport → German name table**, ~60 common European entries plus the Thai set
      (Wien, München, Zürich, Prag, Mailand, Athen, Kopenhagen, Warschau, Bangkok …).
      Fall back to the API's own name when there is no entry.
- [x] **Airline code → display name table.** `routeset` returns `MEA`; the hero screen
      shows "Middle East Airlines". ~150 entries covers everything he will ever see.
      **No API in our chain provides this** — it has to be shipped in flash.
- [x] **ICAO type → structured entry**, not a single string. The screens need manufacturer
      ("Diamond"), model ("DV20"), full name ("Airbus A321neo"), a size class
      ("Zweisitzer") and a category (airline / private-or-training, which drives §5.2).
      ~200 entries.
- [x] **German weekday and month names.** ESP-IDF's newlib ships **no locales** —
      `strftime("%A")` returns "Friday" regardless of `TZ`. Nineteen strings, by hand.
- [x] SNTP + timezone, bound to a compiled-in location for now (the preset UI is M6)
- [x] Host-side tests for all of the above

**Done when:** the M2 log line reads
`AUA453 | Airbus A320 | Wien → London | 12,4 km Nordost`.

---

## M3 — Vertical slice: "Über dir jetzt" ⭐

**Goal:** the device answers the actual question. **This is the milestone that matters.**

Builds DESIGN.md **§5.1, §5.2 and §5.3** — all three. They are one screen in three states,
and shipping fewer than three means shipping a screen that is sometimes blank.

- [x] **§5.1 Über dir jetzt** — nearest aircraft, no interaction required
- [x] **Route as the headline, in the largest type on screen** — `Wien → London`
- [x] Compass tape band, bearing from `dir` — a custom widget, no LVGL equivalent
- [x] Airline name, plain-language type, altitude, distance + direction
- [x] **Hero auto-shrink ladder.** At 100 px in Plex Sans Condensed roughly **9–10
      characters** fit the 440 px content width. "London" fits, "Kopenhagen" is at the
      edge, "Thessaloniki" is not. Measure the rendered width, step 100 → 76 → 56.
- [x] **§5.2 Ohne Route — required, not a fallback.** Airline callsigns resolve a route 92%
      of the time; private aircraft 0%, and always will. In one 40 nm sample **14 of 38**
      aircraft over Gloggnitz were local light aircraft — precisely the ones he hears. The
      screen keeps the layout, swaps the hero to the aircraft type, and says *why* there is
      no route.
- [x] **§5.3 Himmel frei** — clock, German date, last aircraft seen. Never a blank panel.
- [ ] Auto-return to §5.1 when traffic appears — **only from §5.3, and only after 30 s
      without a touch.** Yanking him out of a screen he is reading is worse than showing a
      stale one.

**Done when:** it can be set on a desk, glanced at from across the room, and read correctly
— with a **light aircraft** overhead as well as an airliner. Test it on someone who has
not seen it before.

**Design note:** every existing project builds a radar scope first and hides the route behind
a tap. That is backwards for this user. Resist the urge to build the radar first — it is more
fun to write and less useful to him.

---

## M4 — Robustness

**Goal:** survives a week unattended.

- [x] Exponential backoff on `429`/`503`. Treat spurious `308` as throttling, not a redirect
- [~] Source failover — *resolved differently*: adsb.fi is HTTPS-only, so the device
      degrades behind an amber caution instead of switching source. The source table and
      switching logic exist with the second slot deliberately empty. See DECISIONS D15.
- [x] Route cache persisted to NVS, survives reboot
- [x] **Characterise the tearing bug**: write NVS while the UI animates, observe, then
      mitigate by pausing LVGL around commits. Record the result here.
- [x] Watchdog + auto-reconnect; recover from a router reboot unattended
- [x] Distinguish *"Route wird gesucht…"* from *"Kein Flugplan"*. §5.2 covers the settled
      case; this is the transient one, in the seconds before `routeset` answers. A 2E0LXY
      lesson — showing "no route" while still looking teaches him to distrust the panel.

**Done when:** it runs seven days untouched, through at least one WiFi drop, with no manual
intervention and no rate-limit ban.

---

## M5 — Second screen: what else is up there

**Goal:** his "just browsing" mode.

- [x] **Build the screen graph** — DESIGN.md §6. Three swipe pages
      (Über dir · Liste · Radar); Einstellungen behind a long-press on the chrome bar and
      *not* in the deck. The page indicator shows three dots because there are three pages.
      **Superseded after M8 (D60):** the deck is now two pages, Radar then Liste, and
      Über dir became a detail layer underneath both. Two dots, not three.
- [x] **§5.4 Liste** — nearby aircraft sorted by distance (`dst` is pre-computed)
- [x] Tap a row → its detail card
- [x] **Type pass on §5.4 and §5.5 before building them.** As drawn they sit below even the
      near-view floor in DESIGN.md §3 — list secondary lines at 12 px, radar city labels at
      10–11 px. The room exists; the list can show four rows instead of five.
- [x] **§5.5 Radar** — PPI view if the FPS budget from M1 allows
      (geo projection from MatixYo `radar_display.cpp`, MIT)
- [x] Aircraft glyph rotated by heading (ThingPulse `PlaneSpotter.cpp` `drawPlane()`, MIT)

**Done when:** he can see the whole sky and get from any aircraft to its route in one tap.

---

## M6 — Travel and settings

**Goal:** he carries it to Thailand and it just works.

- [x] **§5.6 Einstellungen** — location preset, radius, brightness
- [x] Location presets: **Gloggnitz** `47.6691/15.9303`, **Pattaya** `12.9211/100.8721`, custom
- [x] Timezone bound to the preset — he never sets a clock (the SNTP plumbing landed in M2.5)
- [x] **§5.7 WLAN — on-device network list, with `lv_keyboard` for passwords.** Decided
      against the captive portal: that needs a phone, a second network join and a browser,
      in a foreign country, by someone who will not read a manual. Costs one keyboard
      screen; buys a device he can fix standing in front of it.
- [x] **Multiple WiFi networks remembered**, not reconfigured on arrival
- [ ] Optional: auto-select the location preset from the connected SSID
- [x] **Auto-dim.** DESIGN.md §7 — a glowing dark panel in a dim living room at 22:00 is
      glare, and the clock is already correct, so a schedule is enough. Not optional.
- [x] All settings in NVS (mind the tearing interaction from M4)

**Done when:** unplug in Gloggnitz, plug in at Thappraya Rd, and it shows Thai traffic in
local time without anyone touching a setting.

---

## M7 — Language sweep

**Goal:** it reads like it was made for him, not translated. The tables landed in M2.5;
this is the sweep that catches what escaped.

- [x] German compass bearings ("nordöstlich"), units and date formats applied *everywhere*,
      not just on §5.1 — `compass_de_adv()`, D37. The panel read "16,8 km Nordosten"; it now
      reads "9,4 km nördlich". The abbreviation ("NO") stays on Liste, Radar and the compass
      tape, where it is right.
- [x] All user-facing strings in one translation unit — `main/strings_de.h`, D38, with
      `tools/check_strings.py` as the audit rather than a convention. It found `"%d°"` living
      in `widget_compass.c`.
- [x] Data attribution line in the UI (adsb.lol is ODbL) — at the foot of Einstellungen:
      **Flugdaten adsb.lol (ODbL) · Routen adsb.im**. Verified on the panel.
- [x] **Read every screen aloud with someone Austrian.** Translated-sounding German is
      worse than English — it reads as a cheap product.
      → `python3 tools/check_strings.py --list` prints all 110 strings grouped by screen.
      Read through by a native speaker on 2026-09-19. Two were changed on my own first pass
      (D42); the two I flagged as judgement — "Nachtabsenkung" and "in Reichweite" — were
      both ruled to stay. Both of my doubts were wrong in the same direction: I read precise
      German as cold.

**Done when:** no English leaks into a normal session.

**Status: M7 complete.** The last item was the only one in the whole plan a tool could not
close, and it closed the way it was supposed to — by someone reading it.

Two defects the sweep turned up that had nothing to do with language, and one that was
caused by the sweep:

- **The font gate had stopped checking anything** (D39). Gathering the strings into
  `main/strings_de.h` moved them one directory above the checker's scan path, and the
  checker read hex escapes as ASCII backslashes besides. Both holes silent, both in the
  one tool whose job is to catch silence.
- **The deck indicator drew two of its three dots on top of each other** (D40) — LVGL
  layout timing, invisible to every host test, found by enlarging 30 px of a framebuffer
  capture.
- `is_placeholder_type()` in `view_build.c` had been dead since D36. Removed.

---

## M8 — Ship it

- [x] Desk stand — `hardware/desk_stand.scad`, a parametric wedge at 20° off vertical.
      **Not printed**: dimensions are datasheet figures, not calipers, so it ships with a
      four-minute fit-test part and says so in three places.
- [x] Power: USB-C (side edge — fine for a desk unit)
- [x] OTA update — written from scratch rather than adapted: `net/ota.c` +
      `net/ota_policy.c`. HTTPS only, rollback on, installs only inside the night dim
      window because flash writes tear this panel. **Off unless an update URL is stored
      in NVS.** D44–D45.
- [x] **A manual update path — D74.** A **Software** section in Einstellungen: one row
      that checks now, and — once something is offered — installs now, without waiting for
      the night window. Wording and row state are in `fmt_de.c` and host-tested (401
      checks in that suite); the install raises a full-screen takeover that swallows touch
      until the reboot. `U` walks all seven states from the console. **Confirmed on the
      panel by the owner, 2026-09-24**, after it pulled v0.7.0 down by itself overnight.
- [x] **Release infrastructure — D72.** The repo is public, `.github/workflows/release.yml`
      publishes a signed image and its manifest on every `v*` tag, and
      `tools/check_release.py` reads the finished binary back to prove its version and
      signature before anything is uploaded. Images are signed (Secure Boot V2 scheme, no
      hardware secure boot, RSA-3072), `PROJECT_VER` comes from the tag instead of from a
      literal somebody has to remember, and `dependencies.lock` is committed so a CI
      resolve cannot ship a driver that was never measured on this unit. **No firmware
      code changed** — the client had been finished since M8 and only ever lacked a
      publisher.
- [x] README with photos and a one-paragraph "what it does" — six real framebuffer
      captures, not mockups, plus the full ODbL notice.

**Done.** What is verified and what is not, precisely: the manifest path is proven on the
device end to end (DNS, TLS, root-bundle validation, 2,262 bytes byte-exact, parse, field
rejection). The policy layer is host-tested to 16,975 checks.

**The image download and slot switch have now run — 2026-09-21, on the unit.** Every
check this section was waiting on is done:

- [x] **Real download and slot switch.** `0.3.0 -> 0.4.0` from
      `releases/download/v0.4.0/esp-flight-monitor-0.4.0.bin`, written to `<ota_1>` at
      `0x520000`, ~26 s for 2,428,928 B. Bootloader then reported `Loaded app from
      partition at offset 0x520000`.
- [x] **Signature verified on the device**, and by the mechanism D72 describes rather than
      one assumed: `secure_boot_v2: Take trusted digest key(s) from running app` →
      `#0 app key digest == #0 trusted key digest` → `Verifying with RSA-PSS...` →
      `Signature verified successfully!`
- [x] **Probation and confirm.** `new image 0.4.0 confirmed: ESP_OK`, after the new build
      had held WiFi.
- [x] **A wrong key is refused.** An image built at 0.5.0 and signed with a throwaway key
      was offered and declined: `Secure boot signature verification failed` /
      `image valid, signature bad` / `update failed: ESP_ERR_OTA_VALIDATE_FAILED — staying
      on 0.4.0`. "image valid, signature bad" is the line that matters — the download was
      intact, so this was the signature check and not a corruption false positive. The
      boot partition was never switched and no bad-version record was left behind.

Forcing the night window needed no new code and no finger on the glass: `o` cycles the
location, Pattaya is `ICT-7`, and five hours ahead of CEST put the clock inside the
22:00–07:00 window. Cycling all the way round restored `Eigener Ort` exactly.

**What the test found is worth more than the test passing.** The very first fetch failed —
`esp_http_client`'s 512-byte default header buffer against GitHub's 918-byte `Location`
and 3,683-byte CSP header. The running build could not fetch the manifest, so it could not
have been sent the fix; it went on over USB. That is the whole argument for doing this on
the desk (D72).

Three defects surfaced while building it, all worth more than the feature (D45): a 4 KB
task stack that presented its overflow as an I²C fault in the touch driver; a TLS
allocation failure that presented as a network error; and a single `esp_http_client_read()`
mistaken for the whole body. A fourth, D46, was a raw ICAO designator — **"C177"** — in the
hero at 76 px, the exact bug D36 thought it had removed, still alive in the file D36 did
not finish.

---

## M9 — Akku (a cell in the stand)

Not in the original plan: it came from the owner wanting to pick the panel up and carry it
outside for half an hour. Full reasoning in docs/DECISIONS.md D61.

- [x] `main/power/axp2101.c` — the PMIC the BSP never touches. TS pin taken out of the
      charger's decision (without it, the likely behaviour is a device that silently never
      charges), 500 mA to **4.1 V**, gauge and ADC on, and nothing anywhere near a rail.
- [x] `main/power/battery_policy.c` — states, thresholds, hysteresis, backlight cap and the
      German, all host-testable. **4,165 checks**, including a monotonicity sweep of the
      open-circuit curve over 2500–4400 mV.
- [x] The badge in the chrome strip (only while discharging; grey, then amber under 20 %)
      and the **Akku** line in Einstellungen (reads "Kein Akku" with no cell fitted).
- [x] Backlight capped at 40 % when low and 25 % when critical — a saving and a signal.
      Nothing is capped while the cell is healthy: four hours is already eight times what
      was asked for, and a panel that dims the moment it is unplugged reads as a fault.
- [x] `y` prints the judged status and the registers under it; `Y` pretends to be a battery
      so the badge and the cap can be seen without flattening a real cell.
- [ ] **A cell.** Ordered, arriving 2026-09-26: 3.7 V 2000 mAh 103450, JST-PH 2.0. Check the
      polarity against the board's `+`/`-` silkscreen before plugging it in — J1 pin 1 is
      GND, and cell vendors are not consistent about which pin gets the red wire.
- [x] ~~A pocket in the desk stand.~~ **Dropped, 2026-09-20, by the owner: no stand will be
      printed.** The cell goes on the back of the case with foam tape, plugged into the
      socket the back cover already exposes. That removes the last thing the runtime
      measurement was blocking, and `hardware/desk_stand.scad` is now a sketch nobody owns.
- [ ] **The runtime, measured.** The firmware logs percentage and millivolts once a minute
      while discharging, so the first time it is unplugged it produces its own discharge
      curve. AGENTS.md §2's 1.2–1.9 W is calculated from the schematic and stands only
      until that log exists. Nothing is blocked on the answer any more — it is now just a
      number this repo would rather have measured than calculated.

**Verified on the unit with no cell fitted** (2026-09-20): every configured register reads
back correct, the panel survives the configuration write, 40 rapid overlay navigations
produce zero crashes with the poll running, and the badge is rebuilt after a fixture
suspend/resume. The one thing not verified is the battery.

---

## M10 — Ort suchen (DESIGN.md §5.8)

Not in the original plan either. It came from the owner reading the settings screen and
asking what "Eigener Ort" actually was — the honest answer being "a card that cannot be
set, whose only working effect is to move the clock two hours". Full reasoning in
docs/DECISIONS.md D63 and D64.

- [x] `main/net/geo_parse.c` — the URL builder, the response parser and the IANA → POSIX
      timezone conversion. Pure, so it is globbed into the host suite by the existing
      `net/*_parse.c` rule. **100 checks** against real captured responses.
- [x] `main/net/tz_table.h`, generated by `tools/build_tz_table.py` from the system tzdata
      (114 zones, 48 rules). Newlib has no zoneinfo, so `"Europe/Vienna"` has to become
      `"CET-1CEST,M3.5.0,M10.5.0/3"` somewhere, and that somewhere should not be a person
      typing DST rules out by hand.
- [x] `main/net/geocode.c` — one plain-HTTP GET, on its own task, only when he taps Suchen.
- [x] `main/ui/screen_geo.c` — two states, a real keyboard, a hit list whose second line is
      what tells three Wiens apart, and three outcomes that each say something in words.
- [x] `settings_t` grew `custom_label` and `custom_tz`, **with a version 1 → 2 NVS
      migration** so the one device in the field keeps everything it had. The blob encoder
      and decoder came out from behind `#ifndef HOST_TEST` to be tested at all — that was
      the riskiest code in the change and the only part nothing could reach.
- [x] `settings_tz()` replaces `location_tz(preset)` at every call site. `LOC_CUSTOM`'s
      preset row no longer claims to be in UTC.
- [x] Attribution: Open-Meteo/GeoNames is CC BY 4.0, so there are two lines at the foot of
      Einstellungen now. Both fit; measured on the panel.
- [x] `main/ui/widget_input.c` — the shared keyboard and field styling, and the fix for a
      keyboard that had been positioned off the bottom of the panel since M6 (D64).

**Verified on the unit, 2026-09-20**, by driving it from the build host and reading the
framebuffer back (D4, D41): the typing state with a real keyboard on it; a live search
against the real endpoint returning eight places with German region lines; picking one and
watching `settings applied: Eigener Ort (48.2085/16.3721) tz=CET-1CEST,M3.5.0,M10.5.0/3`
go past; the place surviving a reboot out of NVS; the settings card reading
"Wien · Bundesland Wien"; and both failure states. The v1 migration is confirmed by the
device coming up on `brightness=45%`, which is neither a default nor a clamp bound.

**Stress-navigated** (AGENTS.md §11 rule 3 — both WLAN panics needed rapid repeated
navigation, and one of them was a task writing into a deleted screen):

| | |
|---|---|
| Ortssuche opened and closed | **62 times, 0 crashes.** Twenty of those tore the overlay down *while a real lookup was on the wire*, which is D58's shape exactly. |
| A hit tapped | **21 times, 0 crashes**, and 21 of 21 reported picks actually moved the device. Each one deletes this screen from inside one of its own event callbacks, via `screen_geo_debug_tap()` rather than by calling the callback directly, so the teardown is what is being tested and not just the settings write. |
| Searches abandoned and overlapped | **42 lookups, 0 crashes, 0 wrong paints.** Console bytes sent as one unpaced burst, because the endpoint answers in ~250 ms and a test that pauses between keystrokes never wins the race it is aiming at — the first attempt at this reported a clean pass and had exercised nothing. |

Two harness bugs are worth writing down, because both reported a pass. The first stress run
claimed forty cycles and had performed twenty — `geo_demo_search()` opened the screen only
when no overlay was already up, so half of them were no-ops against Einstellungen. The
second looked like it had raced an abandoned search and had not: the endpoint answers in
about 250 ms, faster than the test could send the next keystroke, so every "abandoned"
lookup had already landed. **A harness bug reads exactly like a passing test.**

**Not verified on the glass:** the WLAN password step's keyboard, and only that. It is the
same one-line fix as the Ortssuche keyboard and the same shared styling, but reaching that
step needs a finger on an *unknown* network — which is exactly why the bug lived there for
four milestones. One tap settles it. **Closed in M11** — from the build host, not with a
finger.

---

## M11 — Umlauts, and the one moving thing

Two open items from M10 plus a request: make the waiting states look like something.

- [x] **A German QWERTZ keyboard** (`main/ui/widget_input.c`). ü right of p, ö ä right of l,
      ß on the bottom row — where a German keyboard has always had them. LVGL's stock
      layout is US QWERTY with `_ - . , :` filling the bottom letter row: five keys he will
      never press, and the two he needs missing entirely. Four rows, every row adding up to
      11 units so the columns line up down the whole keyboard (LVGL's own rows come to 52,
      40, 12 and 14, and the ragged grid is visible at 480 px). Cursor keys and the
      close-keyboard glyph dropped — both screens have a 64 px Zurück/Abbrechen in words
      above the keyboard, and `lv_textarea` moves the cursor when he taps into the text.
- [x] **Two faces on one keyboard, because neither can draw it alone.** LVGL's built-in
      Montserrat has no umlauts (`-r 0x20-0x7F,0xB0,0x2022`, read off the generated file);
      Plex has no `LV_SYMBOL_*`. Letters get `plex_sans_cond_34`, control keys get
      Montserrat 24, split by `LV_PART_ITEMS | LV_STATE_CHECKED` — which works because
      `lv_buttonmatrix` re-reads that part's label style per button with that button's own
      state. Not a documented feature; a read of `draw_main`, then measured on the panel.
- [x] **`main/ui/widget_busy.c`** — the sweeping bar and the ghost rows, shared by all three
      waiting screens for the same reason `widget_input.c` is shared: three copies would be
      three decorations instead of one idea. DESIGN.md §4 "Motion" is the rule.
- [x] Wired into **Ortssuche** (bar + 3 ghost rows), **WLAN** (bar + 4 ghost cards, ghosts
      only when the list is empty), and **§5.2's route lookup** (bar under the amber tag,
      sized to the tag). §5.3's *Kein Netz* deliberately gets none — see §4, a wait with an
      end gets a bar, a standing condition gets a sentence.
- [x] `tools/check_strings.py` grew **one rule, not thirty exemptions**: a literal that is a
      single letter is the alphabet, not prose. Proved it still bites by injecting `"ja"`
      as a key cap (caught) and `ő` as one (caught by the font gate).

**Verified on the unit, 2026-09-20**, by driving it from the build host and reading the
framebuffer back (D4, D41):

| | |
|---|---|
| The German keyboard | ü ö ä ß all render, columns line up, no blank keys. |
| All three layers | abc → ABC → 1# → abc, pressed through `lv_keyboard_def_event_cb` and photographed. The search field stayed empty, which is the actual check: a wrong layer token types its own cap instead of switching. |
| **The WLAN password keyboard** | **On the glass.** M10's last open item, closed. |
| The three Ortssuche states | Waiting has 556 cyan bar pixels and 517 ghost samples; "Kein Ort" and "Die Suche hat nicht geantwortet" have **0 and 0**. |
| §5.2's two amber tags | 424 bar pixels under "ROUTE WIRD GESUCHT", **0** under "KEIN FLUGPLAN", 0 with a route. |
| The sweep | Measured across six frames: segment constant at 138 px and never off the track. The first version was one-way and exited right — two of three frames caught it with **one pixel showing**, because an ease-in-out is slowest at the ends of its travel. A quarter of every cycle looking blank is the one thing this indicator must not do. |

**Stress-navigated**, 25 cycles × 3 teardowns = **75 screens torn down while a bar was
animating on them, 0 crashes**, 50 Ortssuche states drawn and 0 landing on a dead screen.
Internal heap fragmentation measured against the committed build on the same 20-cycle
overlay churn: **largest free block 31,744 → 7,168 on both**. The churn is pre-existing;
the loading states cost nothing.

**One caveat, stated rather than glossed:** the network at the current location blocks
outbound HTTP, so the live hit list could not be re-photographed — only the two failure
answers, which reach the screen through the same entry point and both show a cleared bar.
And `K` reported `FORCED`: the only network in range is already saved, so the password step
was opened directly and the row-tap that normally leads there was not exercised.

**Two crashes found by the stress run, both pre-existing, both console-only, both fixed:**

- `nav_create()` never cleared `s_overlay`. Every caller has just run `lv_obj_clean()` on
  the active screen, which deletes an open overlay with everything else — so the pointer
  was left dangling and the next `nav_open_overlay()` handled it by calling
  `lv_obj_delete()` on freed memory. LoadProhibited in `lv_obj_get_parent()`. Reachable
  today: open any overlay from the console, press `0`, open one again.
- `screen_overhead.c` had no `s_alive` guard, the one `screen_wifi.c` and `screen_geo.c`
  both carry. It is only ever built as the detail layer, so the same `lv_obj_clean()` left
  every pointer in the file dangling and `dbg_fixture_show()` then called
  `lv_label_set_text()` on a freed label.

**A review round after the fact found four more, and its own fix broke a fifth**
(D68). Two were product bugs this milestone had made *silent* rather than caused:
`ui_resume()` never cleared `s_detail_open`, so after `0` the deck's update branch kept
taking the dead detail path and Radar and Liste simply stopped moving — which before the
new `s_alive` guard was a crash, and is now nothing at all; and the fixture keys `1`–`5`
draw on the detail *layer*, so unless it happened to be up they logged a hero line they had
not rendered. Both fixed, both verified on the glass (the clock advances again after `0`;
`1` on a fresh deck now draws §5.1 instead of the radar).

The third was mine and older: **"Neu suchen" did not cancel anything.** `show_typing()`
stopped the animation and said in its own comment that the wait was over, but the answer
was still coming — up to ten seconds later it called `show_results()` and took the keyboard
out from under his fingers to show hits for a word he had stopped typing. The generation
counter in main.c does not cover it, because tapping Neu suchen is neither a new search nor
a re-open. `s_awaiting` closes it.

**The review's fix for that broke `Q` and `z`, and only the panel said so.** Both console
commands call `on_geo_search()` directly rather than through `do_search()`, so the screen
was never told a question had been asked — the reply then arrived at a screen that was not
waiting, was dropped as stale, and `Q` drew nothing while the log reported a completed
lookup. Found by running it: the panel sat on the typing keyboard after a full request
cycle. Both now go through the same `begin_search()` the button does.

The abandoned-search race is **built, not raced** (`C`). Three attempts to win it from the
host failed — the endpoint at this location refuses in ~100 ms and the second keystroke
arrived 33 ms late every time — so the command holds the display lock across both steps,
which the search task must also take before it can paint. Verified: the reply lands after
the abandon and the keyboard stays.

**A gate came out of it.** `tools/check_console_keys.py` parses the keys `on_cmd()` actually
answers to and fails if either the header block or the boot `ready:` line omits one. Two
consecutive reviews had found those two blobs stale; the gate immediately turned up four
more nobody had reported — `u`, `i`, `p`, `t` and `v` had never been in the ready line, and
`u` had never been in the header. 34 keys, both places, proved to bite.

**Then the owner said the WLAN states looked wrong, and four things were** (D69). Only one
of them was new. Tapping a network had never reported an outcome — `screen_wifi_set_status()`
is the screen's documented way to say what happened and its only two callers were both in
the scan path — so the line sat on "Verbinde mit X..." for as long as the screen stayed
open. M11 then swept a bar under that sentence, which is the same claim made far more
confidently, and turned a four-milestone-old wart into something that looked broken.
Worse: `wifi_reconnect_now()` only set a flag that `wifi_task` read inside
`if (!g_connected)`, so **tapping a network while online did nothing whatsoever** — the
exact thing §6's travel case is about. A failed scan was reported as "Keine Netzwerke
gefunden", telling him no networks exist while he sits next to his router. And the ghost
rows drew outlined cards where an unsaved network is a hairline row, so the list changed
construction at the one moment he was looking at it.

Fixed: an explicit pick now outranks "already associated"; a join watcher reports the
outcome, and reports the network actually landed on rather than the one tapped, because
wifi.c picks by range and not by SSID; a failed scan says so in amber and leaves the list
it could not refresh alone; the ghosts take the unsaved-row shape; and the status line is
clamped to the content column, which it never was — the first long sentence ever put into
it ran straight off the panel.

Verified on the unit: a real tap → "Verbinde mit ..." with the bar → twenty seconds later
"Verbindung fehlgeschlagen: ..." in amber with the bar gone; a genuine `-1` scan caught in
the wild by the new log and drawn as "Die Suche hat nicht geklappt"; the empty case still
"Keine Netzwerke gefunden" in grey; the ghosts as hairline rows. **Not verified — no access
point here would associate:** the success branch, the already-on-it short-circuit, the
forced re-pick while connected, and the one-watcher guard.

**Then he asked why his phone has reception and this does not** — and neither tool on the
device could answer it. `wifi_scan()` discarded the RSSI, and `probe_link()` fetched a 1 KB
query and reported fifteen cheerful "ok"s at 120-680 ms about a device that had not shown an
aircraft all day. Both fixed: the scan logs signal and channel, and the probe now issues the
identical request the poller does (same URL, same 16 KB buffer, same timeout — `POLL_BUF_SZ`
and `POLL_HTTP_TIMEOUT_MS` moved into flight_source.h so it uses the numbers rather than a
copy). Measured then: **-76 to -81 dBm**, the SSID on two channels (a repeater), and the real
poll request taking **10-27 s with a third never completing, against a 10 s timeout**.
The backoff and its reconnect-reset were already correct and were never the problem.

Then the probe's headline was wrong the other way: it trips adsb.lol's documented throttle
on every run (15 requests, 2 s apart, throttled at ~7) and counted each 429 as a link
failure — "8/15 (53%)" on a run whose four consecutive real fetches took under a second for
13.7 KB each. Throttled attempts are now separated out. **Two wrong headline numbers from
one diagnostic in one session, in opposite directions.**

What the numbers say: **at -74 dBm the real request takes ~900 ms for 15 KB; at -80 dBm it
takes 10-60 s and mostly does not finish.** A five-decibel swing across the cliff edge, not
a slope. `POLL_HTTP_TIMEOUT_MS` 10 s → 25 s and the route POST 8 s → 20 s, reasoned from
those measurements — but NOT demonstrated to help, because the link recovered before a fair
before/after could be taken. D69.

**And a third harness bug of the shape M10 records twice** — the check ran, reported
nothing, and had not looked. `tools/grab_screen.py` reads frame buffer 0 of two, so a
screenshot of a screen that had just changed and then gone still showed the state BEFORE
it. It only bites a static screen, which is why the animating loading states photographed
correctly while the fixture beside them lied twice. `dbg_screen.c` now invalidates the
screen once per buffer before capturing. A fourth, mine, in the same session: the first
version of `geo_demo_states()` asked `nav_overlay_open()` — "is SOME overlay up" — which is
the exact question M10 records `geo_demo_search()` getting wrong, so it now asks
`screen_geo_is_up()` and logs `SCREEN NOT UP` when the answer is no.

---

## M12 — The corner says what the radio hears

One request, two places: a small unobtrusive WLAN signal meter at the top right of the
deck, and the signal strength of every network on the WLAN screen. Both answer the question
M11 ended on — *why does the phone have reception and this does not* — as a standing
instrument rather than a one-off measurement. D70.

- [x] **`main/data/wifi_bars.c`** — the dBm → bars ladder, one rule for the whole device,
      with no `esp_*` header in it so the host suite compiles it. The boundaries are
      **measured off this radio**, not copied from a table: the 2 → 1 step is at -79 dBm,
      so **one bar means "measured unusable on this hardware"** (900 ms for the real poll at
      -76, 10-60 s and usually unfinished at -81). `test/host/test_wifi_bars.c`, 208 checks,
      pins the rungs, the -76/-81 pair, monotonicity, and that nonsense is not clamped into
      a plausible answer.
- [x] **`main/ui/widget_signal.c`** — one `lv_obj` with a draw callback, not a container of
      four rectangles: the WLAN pool is twenty-four rows and the child version would be
      ninety-six more objects. State lives in the caller, so nothing is allocated on the
      display task. Two sizes, one ladder.
- [x] **Top right of the deck** (`nav_set_signal()`), on the root beside the battery badge
      at the bottom right — the link belongs to the device, not to a page. The radar's clock
      was right-aligned into that same corner and now keeps `WIDGET_SIGNAL_CHROME_SLOT`
      clear; the meter's feet land on the clock's own baseline, so the two read as one row of
      chrome. Non-clickable, or it would have eaten the long press to Einstellungen in that
      corner.
- [x] **A meter at the right-hand end of every WLAN row**, at the same x on every row so the
      ladders line down the edge of the list — a column can be compared at a glance, and
      comparing is what he is doing there. The ghost rows grew a ghost meter for the same
      reason they took the unsaved-row shape in M11: the list must not change construction
      at the moment he is looking at it.
- [x] **`wifi_scan()` returns the dBm and sorts strongest-first**, which also decides which
      duplicate survives — this flat's SSID is on channel 1 and channel 11, and the dedup
      keeps the first sighting.
- [x] **`W` cycles a pretended signal** through all five states, the argument `Y` makes for
      the battery: four of them are a property of where the device is standing.

**Verified on the unit, 2026-09-21**, by driving it from the console and reading the
framebuffer back (D4, D41):

| | |
|---|---|
| All five meter states | Photographed via `W`: 4/3/2/1 lit bars and the amber stroke. |
| Against the radio | `n` reported **-68 dBm ch 11**; `wifi_bars(-68) = 3`, and both meters — the corner and the row — showed three. |
| The radar's clock | "10:57" and the meter side by side on one baseline, no overlap. |
| The WLAN row | SSID, ✓ gespeichert, meter, in one line with the insets the code asks for. |
| The skeleton | Ghost name and ghost meter, with the cyan bar sweeping above them. |
| Ortssuche | Re-photographed after the padding fix: 8 hits, rows correctly inset, the fourth row still a visible sliver. |

**Two layout bugs that had been on the glass since M6, both found by fitting the meter in:**

- The SSID label had a width and no height, so `LV_LABEL_LONG_MODE_DOTS` never fired and
  `Apartamentos_Jose_Cruz` broke across two lines inside a 64 px row. Exactly D69's trap, a
  second time, in the same file.
- `lv_button` arrives carrying the default theme's padding (~13 px horizontally at this
  DPI), and both `lv_obj_set_pos()` and `lv_obj_align()` measure from the content area — so
  every inset on that row was off by it while every width was computed from `CONTENT_W`.
  The visible result was an ellipsis drawn through the green tick. `screen_geo.c`'s rows had
  the identical defect and are fixed with it.

**A review of the branch then found fifteen things, and the panel found a sixteenth**
(D71). The two that mattered were both "the panel silently stops": `s_detail_open` was a
flag main.c kept about an overlay nav.c owns, so opening Einstellungen over the detail
layer left it true and neither Radar nor Liste was ever repainted again; and nothing told
nav.c when `lv_obj_clean()` freed its widgets, so its NULL guards were dead code and the
next overlay deleted freed memory. Both are now derived rather than remembered — the
overlay is identified by its BUILDER, and an `LV_EVENT_DELETE` handler forgets the deck.

Exercising that by hand turned up a crash older than this whole branch and missed by every
stress run in the repo: `screen_list.c`'s visibility timer reads its container thirty times
a minute for the life of the process, and `f` or `b` frees it. The stress sequence now
presses those two keys. 228 presses across overlays, debug views and fixtures, no crash
markers, 58 KB internal free afterwards.

Also fixed: the WiFi teardown race the explicit re-pick made reachable (and the review's
own prescription for it, which would not have worked — the event lands after the clear
either way, so it has to be consumed); a 16 KB PSRAM leak per link probe; `wifi_bars()`
reading a genuine -101 dBm scan result as "nothing measured"; the geocoder left as the only
short timeout on the device after D70 widened the other two; `widget_busy_set_active()`
restarting its sweep on every call while documenting itself as idempotent; two more
labels with the `DOTS`-needs-a-height defect; UTF-8 truncation that could cut an umlaut in
half on its way into NVS and into a URL; the full/not-charging line sitting on an OCV knee
with no hysteresis; two unserved waits on failed `xTaskCreate`; and a timezone test that
compared a buffer with itself.

**Not verified:** the no-link state on a real outage. The device has an association here,
and the simulation is what stands in for walking out of range — which is exactly what `W`
exists for, and is recorded as a simulation rather than claimed as a live observation.


---

## M13 — The radar, challenged

Started by the owner's photo of his own panel and one question: *what do the colours
mean?* The radar was then checked against real traffic displays (AC 25-11A, TCAS, ATC
PPI) and against UX practice. D75 and D76 have the reasoning.

- [x] **The tapped aircraft is ringed** — magenta keeps "nearest", a white ring says what
      the caption is about (D75). Plus the selection-clear on leaving the page, which had
      been dead code since D60 (`prev_page == 2` in a two-page deck).
- [x] **Amber freed** — route-less aircraft are cyan and hollow; on the radar amber means
      only "not live".
- [x] **Altitude as mark size**, three bands; the ring scales with the mark.
- [x] **Trails** — four fading fixes, 15 s apart, with a jump guard, and not recorded
      while stale. `main/data/radar_logic.c`, `test/host/test_radar.c`.
- [x] **Stale data on the default screen** — marks dimmed, amber `KEINE DATEN` top centre,
      from the same `net` as the detail layer.
- [x] **Touch feedback** — ring preview on press, pressed pill and `→` on the caption, and
      a ladder in which the arrow gives way before the name.
- [x] **A way out of a selection** — tap the empty scope, or 30 s on the radar without a
      touch; the long press to Einstellungen still reaches the deck from the scope. After
      review: time on the detail card no longer counts, and a long press no longer lets go.
- [x] **Hysteresis on "nearest"**, and the caption tap opens what the caption names.
- [x] **`test/sim/`** — the radar on the host: real LVGL in the device's DIRECT mode,
      scripted touch, pixel checks, ASan/UBSan fatal, a stress run, in CI. Mutation-tested:
      20 of 21 breakages caught, the one survivor equivalent. Found three bugs the review
      had not (arrow over name, trail ghosts, pill through the S) and one old one (the range
      read-out's position was an accident of `lv_obj_get_x()`).
- [x] **On the glass, what a console can check** (2026-09-24): flashed, `KEINE DATEN`
      shown during a real outage, then live aircraft with the ring on the nearest, hollow
      cyan for route-less, trails, sizes, the caption arrow; survives a full UI rebuild;
      the WLAN keyboard still fits. Testing it found D77 (the feed was dead in busy sky).
- [x] **Two-line caption** (D78, the owner's request): flight number and model above
      the distance; the top-row identity line is gone; the scope moved up to make room.
- [x] **Detail card: speed, arrival, distance from origin** (D79): ground speed beside the
      altitude; one route line — *Landung in etwa 45 Min.*, or while climbing out *43 km
      von Wien entfernt*; departure time deliberately not shown (no source knows it). The
      give-way rule now closes up, so a two-line type name no longer costs the registration.
- [ ] **Read aloud** the four new strings from D79 (D51, D56, D57).
- [ ] **On the glass, what needs a finger and an eye:** tap a mark (the ring moves as the
      finger lands), tap the caption (the pill lights up, the card opens), tap the empty
      scope, long-press with an aircraft selected, 30 s idle. Then his eye: three sizes
      readable from the chair, trails read as history not clutter, 13 px amber findable.
- [ ] **Open, for the owner:** rotate the scope so up is the direction the wall faces,
      instead of north? A question, not a defect — only he knows whether mapping screen-north
      to room-north is a problem he actually has.

---

## Accelerators

- **The screens already exist.** Seven artboards built from real 2026-09-18 traffic:
  <https://claude.ai/artifact/FrhCvcyrCiArHpg6eUreq9>. Implement from those plus
  DESIGN.md — the layout iteration is done, and re-deciding it in LVGL costs a flash cycle
  per guess.
- **Fixture-driven parser tests** (M2) catch most bugs off-device.
- **M2.5 is entirely host-side.** It needs no board and no network, so it can be written
  while waiting on hardware, or by someone else, or on a train.
- **Vendor demo as reference** — when something looks wrong, flash `02_lvgl_demo_v9` and
  compare. It isolates our bugs from the board's.

## Numbers to fill in as we go

| Measurement | Value | When |
|---|---|---|
| Vendor demo FPS | _not run_ — our own bring-up worked first time (DECISIONS D1) | M0 |
| Free internal SRAM, no display | 242,771 B | M1 |
| Free internal SRAM with framebuffer | 194,999 B (1 fb) · 186,743 B (2 fb, steady) | M1 |
| Free PSRAM with framebuffer | 6.81 MB (1 fb) · 5.88 MB (2 fb) · 5.42 MB (3 fb) | M1 |
| FPS at 1 / 2 / 3 framebuffers | **21.4 / 28.5 / 28.4** (100 px face, full-screen invalidate) | M1 |
| FPS delta with a 100 px face on screen | 1 fb: −1.38 FPS (−6%) · **2 fb: zero** | M1 |
| Flash + PSRAM cost of the full font set | **752,589 B (735 KiB)** uncompressed, 10 faces, incl. Latin-1 accents at every size. Costs **0 FPS** (28.5 before and after) | M1 |
| Magenta-on-black verdict | legible and clearly distinct beside white and cyan at ≥56 px — see M1 note | M1 |
| Longest destination name that fits at 100 px | **"Innsbruck", 430 px of 440**; 63/74 fit at 100, 9 at 76, 2 at 56, **0 fail** | M1 |
| Tearing severity on NVS write | **none.** 3 µs on a 49.7 ms worst-case frame gap (1.0× idle), and **no tearing visible on the panel** under sustained NVS writes | M4 |
| Typical poll payload at 30 nm | 7,628 B / 13 aircraft (measured 2026-09-18) | measured |
| Route resolution rate, real sample | **6 of 13** — 6/6 airline callsigns, 0/7 GA (measured 2026-09-18) | measured |

### What M1 actually found

**The headline risk did not materialise.** `CONFIG_SPIRAM_RODATA=y` does put the fonts
in PSRAM — the boot log confirms `Read only data copied and mapped to SPIRAM` — but with
two framebuffers the 100 px face costs **zero** FPS against the built-in 48 px flash face.
At one framebuffer it costs 6%. The font set and the framebuffer do share the bus; it just
does not matter at this workload.

**Two framebuffers is strictly better than one**, which was not the assumed trade-off: it is
both *faster* (28.5 vs 21.4 FPS) and tear-free, for 735 KB of PSRAM we have to spare. Three
buys nothing measurable. The framebuffer question in AGENTS.md §8 is settled by
measurement.

**28.5 FPS is a ceiling, not a load limit** — all three faces hit exactly 28.50 at two
framebuffers, which is the vsync-locked rate in direct mode, with CPU at 1%. The benchmark
invalidates the entire screen every frame; the real UI redraws on a 12 s poll.

**The magenta check** was done on the framebuffer, not by eye on the panel: `#FF3FDA`
renders correctly and reads as clearly distinct from `#FFFFFF` above it and `#22E3FF` below
it at 76 px. That answers "are the colour values right and separable". Whether it reads
*badly* to a human in a dim room is still a human question — but nothing found so far
argues for changing the colour system.

## Explicitly not doing

- **Live map tiles.** Nobody does this on an ESP32; RAM makes it impractical. A single
  pre-dimmed static image under the radar (TheJinxNL's trick) is the ceiling if we want one.
- **Local SDR receiver.** A Pi + dongle would cut latency, but it would have to be rebuilt
  for Thailand and still would not provide route data.
- **Captive-portal provisioning** — superseded by the on-device list in M6. See the
  reasoning there.
- **Aircraft photos** — deferred, open question in AGENTS.md §8.
