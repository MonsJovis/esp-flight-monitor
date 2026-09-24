# AGENTS.md — esp-flight-monitor

Operating manual for AI agents working in this repo. Read this before touching code.

> **Status, 2026-09-24.** Not a brief any more: the device is built, running, and updating
> itself. What is closed → [docs/PLAN.md](./docs/PLAN.md). Why anything is the way it is →
> [docs/DECISIONS.md](./docs/DECISIONS.md). How big the suite is → whatever
> `make -C test/host` prints. Numbers do not live here; they rot here.
>
> **This file is a router.** The measured facts live in `docs/` and each section below
> points at its own — hardware, data and rate limits, places, gotchas. What stays here is
> what you must obey: the product intent (§1), the loop and the console (§3), the settled
> decisions (§8), the licence policy (§9), the conventions (§10) and the three ways this
> repo has actually failed (§11). Several rules were amended by the build; where they
> were, they say so inline. Do not "restore" an amended rule to what it used to say.
>
> **Two things are still unexercised:** the printed desk stand, and the battery — the PMIC
> driver is written and every register reads back correct on the unit, but no cell has been
> connected to this board yet (D61, PLAN.md M9).
>
> **OTA is no longer one of them.** 2026-09-21: the unit downloaded 0.4.0 from a real
> GitHub release, verified its RSA-3072 signature against its own running image, switched
> slots, rebooted, and confirmed itself — and refused an image signed with a different key,
> staying where it was. D72.

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

**Board: Waveshare ESP32-S3-Touch-LCD-4B** ("Smart 86 Box"), ESP32-S3-WROOM-1-N16R8:
16 MB flash, 8 MB octal PSRAM, 480×480 ST7701 panel on a 16-bit RGB565 bus, GT911 touch
polled over I²C, AXP2101 PMIC. Powered over USB-C.

**The full profile — pin tables, I²C addresses, framebuffer budget, the battery header, what
is NOT on this board — is [docs/HARDWARE.md](./docs/HARDWARE.md).** Read it before you touch
a peripheral. Two things to carry in your head until you do:

- The non-"B" `ESP32-S3-Touch-LCD-4` is a **different, industrial board** and its wiki pin
  table is wrong for this one. Do not follow it.
- Reseller listings for this board are wrong about what it has. There is no microSD, no
  buzzer, no relays, no Ethernet.

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
make -C test/host        # the whole suite, plus the font, string and console-key gates
make -C test/sim         # the radar screen itself: real LVGL, a scripted finger, ASan/UBSan
```

`test/sim` needs `managed_components/` (one `idf.py build` fills it) and compiles LVGL once
(about 20 s). It renders the real `screen_radar.c` in the device's DIRECT mode into a
framebuffer and checks it from pixels. Screenshots of every step land in `test/sim/out/`.
It is how the radar's taps got verified with no board attached (D76). Extend it rather than
trust a comment about what the screen does.

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

**Shipping is a tag.** `git tag -a v0.x.0 && git push origin v0.x.0` →
`.github/workflows/release.yml` runs the suite, builds with `PROJECT_VER` from the tag,
signs, gates the finished binary with `tools/check_release.py`, and publishes it with its
manifest. `ci.yml` runs the suite on every push. Details: README "Updating it remotely".

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
- `U` step the **Software** row in Einstellungen through all seven update states —
  idle, checking, available, current, no connection, failed, installing — without
  publishing a release. The installing state raises the full-screen takeover, which
  swallows touch on purpose; the next `U` takes it down again.
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

Positions from **adsb.lol**, routes from **adsb.im/routeset** (batched), place names from
**Open-Meteo geocoding**. All three over **plain HTTP**, which is the single biggest heap
win on this platform and is why `main/net/http_get.h` carries no TLS.

**Endpoints, field semantics, the fallback order and why these three and not OpenSky or
Flightradar24 — [docs/DATA.md](./docs/DATA.md).**

## 5. Rate limits — measured, respect them

Measured against the live endpoints, not read off a documentation page. The short version:
**one position poll every 12 s and never faster than 10**, because adsb.lol starts throttling
at roughly the seventh rapid request and then escalates to a multi-minute `503`.

**The numbers for every service, and what each one does when annoyed, are in
[docs/DATA.md](./docs/DATA.md).** They are a courtesy to volunteer-run infrastructure, not a
suggestion.

## 6. Locations

The device travels: it spends about half the year in Austria and half in Thailand, and the
clock, the poll centre and the airport table all follow it. `main/data/settings.c` carries
the presets.

**The coordinates, timezone rules, radius and which fields matter per place are in
[docs/PLACES.md](./docs/PLACES.md).**

## 7. Gotchas that will cost you a day

Grouped by where they bite: firmware and display, power and the battery, fonts and text,
data parsing. Every one was found the expensive way on this exact hardware — the panel's
tearing behaviour, the touch INT behind the IO expander, LVGL's fixed heap, PSRAM
bandwidth, the task stacks, the APIs that lie about their own behaviour.

**They are in [docs/GOTCHAS.md](./docs/GOTCHAS.md). Read it before your first build, not
after your first day lost.** The one that catches everyone: **all LVGL calls happen on the
display task behind `display_lock()`**, and the network never touches the tree directly.

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
- **Updates: signed, nightly, and reachable by hand.** A tag publishes; the device checks
  daily and installs inside the night window, or immediately from the Software row in
  Einstellungen. `https://` only, signature verified against the running image, rollback
  armed, and a fresh device with no URL stored contacts nothing. D44, D52, D72–D74.

**Still open — ask Markus, do not guess:**
1. **Light theme** — the polarity evidence is genuinely split (DESIGN.md §7). Auto-dim is
   settled and shipped; a second full theme is not.
2. **Aircraft photos** — nice touch, but costs flash, RAM and a third-party dependency.
3. ~~**Stand / enclosure**~~ — **closed by the owner, 2026-09-20: there will be no printed
   stand.** The cell is taped to the back of the case instead (§2). `hardware/desk_stand.scad`
   stays in the tree as an unprinted, unmeasured sketch; do not treat it as pending work and
   do not spend a milestone on it. If it is ever printed, its dimensions still come from the
   datasheet rather than from calipers, so `part = "fittest"` first.
4. **Is the 13 px identity line findable from his chair?** **Answered for the radar
   (2026-09-24):** the owner asked for the flight number and model in its caption, above the
   distance, at 25 px, and the radar's top-row line is gone (D78). **Still open** for the
   two places that keep it at 13 px tertiary: the Liste rows and the detail layer's top line.

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
  it was accepted: [docs/PLACES.md](./docs/PLACES.md) lists three residential addresses
  with coordinates,
  §1 and the README say who lives at them and that he splits the year between them,
  `main/data/settings.c` carries the same three as presets, and `docs/screens/` shows
  one set of coordinates and one real SSID. All of it is in the history of every
  commit, including some commit subject lines, so none of it can be taken back by editing
  a file. The owner was shown that list and chose to publish anyway. **Do not "restore"
  this rule, and do not quietly redact those places either** — half a redaction on a public history
  is worse than none, because it reads as a mistake rather than a decision.

  What is still not negotiable, and now matters more rather than less:
  - **The signing key never enters the repo.** `secure_boot_signing_key.pem` is gitignored
    and lives in the GitHub Actions secret `SIGNING_KEY`, with a backup in Markus's personal
    1Password ("esp-flight-monitor — OTA signing key"). Only `tools/ota_signing_key.pub.pem`,
    the public half, is committed. If the local copy is missing, restore it from 1Password —
    never generate a new one for a release: the panel would refuse every image it signed.
    Never paste the private key into a transcript, an issue or a third-party service — a
    public repo plus that key is a firmware push to a device in somebody's living room.
  - **Nothing else new goes in.** A public repo is not an invitation to add the next
    address, SSID or screenshot. What is published is what was reviewed and accepted; a
    fresh leak is not covered by that decision.
- **OTA is `https://` only** — refused in `ota_set_url()` and again by
  `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=n`. It ships with no URL stored, so a fresh device
  contacts nothing. Since D72 an image must also be **signed** by the key above, verified
  by the running app against its own signature block, so HTTPS is no longer the only thing
  standing between a release and the panel.

## 11. How this repo has actually failed

The decisions are a lot to read. These three patterns caused most of the real bugs, and
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
