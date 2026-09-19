# Implementation Plan

> **Status, 2026-09-19.** M0–M3 are built and **verified against live traffic** on the
> real unit. **M3 — the payoff milestone — is done:** the panel answers "where is that
> plane going" in German, with no interaction.
>
> Proven end to end on real aircraft, not fixtures:
> ```
> PGT61V | A21N | Amsterdam -> Istanbul | 6.1 nm SW
> EWG4FX | A319 | Stuttgart -> Stuttgart | 10.2 nm N
> DMAVT  | ?    | no route              |  9.2 nm NE
> ```
> and on the panel: **Bodrum → London · Boeing 737 MAX 8 · 10.973 m · 16,7 km Süden**.
>
> Soak on a −72 dBm holiday-apartment link: **11 of 12 polls succeeded, 0 reboots,
> 0 watchdog trips, memory flat**. Verified by reading the panel's own framebuffer back
> over USB as a PNG (`tools/grab_screen.py`), not by assertion. 1,984 host-side checks pass.
>
> Five defects that only live traffic could find are written up in docs/DECISIONS.md D24 —
> the sharpest being a routeset buffer smaller than a fixture already sitting in this repo.

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
- [ ] **Read every screen aloud with someone Austrian.** Translated-sounding German is
      worse than English — it reads as a cheap product.
      → `python3 tools/check_strings.py --list` prints all 110 strings grouped by screen for
      exactly this. Two already fixed on a first pass (D42); two flagged as judgement and
      deliberately left: "Nachtabsenkung" and "in Reichweite".

**Done when:** no English leaks into a normal session.

**Status:** three of four done. The fourth needs a native Austrian speaker and is the one
item in this plan a tool cannot close.

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
      in NVS**, and there is no release infrastructure yet, so it ships off. D44–D45.
- [x] README with photos and a one-paragraph "what it does" — six real framebuffer
      captures, not mockups, plus the full ODbL notice.

**Done.** What is verified and what is not, precisely: the manifest path is proven on the
device end to end (DNS, TLS, root-bundle validation, 2,262 bytes byte-exact, parse, field
rejection). The image download and slot switch are not — that needs a hosted build and
there is nowhere to host one. The policy layer is host-tested to 14,055 checks.

Three defects surfaced while building it, all worth more than the feature (D45): a 4 KB
task stack that presented its overflow as an I²C fault in the touch driver; a TLS
allocation failure that presented as a network error; and a single `esp_http_client_read()`
mistaken for the whole body. A fourth, D46, was a raw ICAO designator — **"C177"** — in the
hero at 76 px, the exact bug D36 thought it had removed, still alive in the file D36 did
not finish.

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
buys nothing measurable. AGENTS.md §8 open question 1 is settled by measurement.

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
