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

## D16 — Backlight polarity — CONFIRMED lit on 2026-09-19

The BSP inverts brightness (`flipped = 100 - percent`) and configures LEDC with **no**
`output_invert` flag, so `bsp_display_backlight_on()` drives GPIO4 to a constant LOW. That
is correct only if the backlight circuit is active-low. Every vendor demo uses this same
path, so it almost certainly is — but framebuffer screenshots prove what LVGL *rendered*,
not what the panel *emitted*, so this was the one claim in the build resting on inference
rather than measurement.

**Confirmed by eye on 2026-09-19: the panel is lit.** The BSP's inverted convention is
correct for this hardware, and `flipped 0%` in the log does mean full brightness. Nothing
to change — recorded so the next person reading that alarming log line does not go hunting.

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

## D18 — The feed contains things that are not aircraft, and they win the headline

**Decision:** `adsb_parse` drops entries with type designator `TWR` or ICAO emitter
category `C*`, at the parse boundary.

**Why:** found by printing what the panel would actually say for the real 30 nm capture,
rather than by a failing test. **The nearest target over Gloggnitz was not an aircraft.**
`FFMSNE` — `t:"TWR"`, `type:"mlat"`, no groundspeed, no track — is a fixed ground
transmitter used for multilateration timing. It sat at 7.7 nm, nearer than every real
aircraft, and the list is sorted by distance, so the device's answer to "what is that
plane overhead?" was **"Bodenreferenz"**.

Filtered at the boundary so no consumer has to know about it.

## D19 — ICAO emitter category is the fallback when there is no type

**Decision:** carry `category` through `aircraft_t`; when `t` is absent, the hero shows a
plain German class name — `A1 → "Leichtflugzeug"`, `A7 → "Hubschrauber"`, `B1 →
"Segelflugzeug"`.

**Why:** two of the thirteen aircraft in the real capture (`OEVSO`, `OEANW`) are genuine
aircraft doing 160 kt and 87 kt with **no `t` and no `r` at all**. The hero rendered as a
literal **`?`** — the largest text on the panel, telling a non-technical user the device
is broken. The emitter category is transmitted by the aircraft itself and still says
*what* is up there.

It also caught a wrong table entry. `G2CA` was guessed as "Experimentalflugzeug,
manufacturer unbekannt"; `OE-XNC` transmits category **A7 (rotorcraft)** at 50 kt and
1050 ft. It is a **Guimbal Cabri G2**, a two-seat training helicopter — corrected, and it
now gets the helicopter reason sentence instead of the generic private-aircraft one.

**Corollary:** a table entry whose manufacturer is `"unbekannt"` or `"-"` is a placeholder,
and its "model" is just the raw ICAO code — never shown as a hero. Note the near-miss that
makes the manufacturer the right signal rather than the model: Diamond's aircraft really
*is* called "DV20", so "model equals the ICAO code" does not mean placeholder.

## D20 — Internal SRAM is the scarce resource, and it was nearly gone

**Measured on the unit, with WiFi up:**

| Stage | internal free | largest block |
|---|---:|---:|
| boot, no display | 182.0 KB | 116 KB |
| display up, 2 framebuffers | 132.3 KB | 68 KB |
| font card drawn | 124.2 KB | 60 KB |
| **WiFi + poller started (before tuning)** | **24.9 KB** | **17 KB** |
| after tuning | **50.2 KB** | 31 KB |

WiFi and lwIP take ~75 KB of internal SRAM, which is not PSRAM-relocatable. Tuning the
buffer counts to the traffic this device actually moves — one connection at a time, 8–34 KB
of JSON every 12 s, no throughput requirement — recovered 25 KB. Settings are in
`sdkconfig.defaults` with the reasoning next to them.

**This retroactively justifies the plain-HTTP architecture on measured grounds rather than
assumed ones.** A TLS handshake wants ~40 KB; at 24.9 KB free with a 17 KB largest block it
would simply have failed, and even at 50 KB it would be marginal. AGENTS.md §4 called no-TLS
"the single biggest win available on this platform" — that is now a number, not a claim.

**A latent stack overflow found on the way.** `route_parse` filled a `route_t[64]` — about
6.9 KB — as a stack array inside the routeset handler, on an 8 KB task stack. It could only
fire once a routeset POST actually *succeeded*, which had never happened because the device
has no credentials yet, so it would have appeared as a mystery crash on the first working
network. Moved to PSRAM along with the request and response buffers, which the poll path
already used.

## D21 — `idf.py -DSDKCONFIG_DEFAULTS=...` is sticky, and silently wins

Passing `-DSDKCONFIG_DEFAULTS='sdkconfig.defaults;/tmp/.../fb3.conf'` for the M1 framebuffer
sweep wrote that path into `build/CMakeCache.txt`. Every later `idf.py build` kept using it,
including after `rm sdkconfig` — so the committed `sdkconfig.defaults` was being silently
overridden and the device ran three framebuffers while the repo said two. Caught only
because `display_init()` logs the framebuffer count at boot.

**Delete `build/` after any one-off `-D` config override**, and keep logging the values that
matter at boot — a config that lies is worse than one that is wrong out loud.

## D22 — Nothing may block waiting for a host that is not there

**Decision:** the debug console does **not** install the `usb_serial_jtag` driver. It writes
through ordinary `stdout` and polls a non-blocking `stdin`. The task watchdog is enabled
with `CONFIG_ESP_TASK_WDT_PANIC=y`, a 30 s timeout, and the UI task feeding it every 2 s.

**Why — this one was found the hard way, and it matters more than it looks.**

`dbg_screen.c` installed the `usb_serial_jtag` driver and routed the console through it via
`usb_serial_jtag_vfs_use_driver()`. That driver's write **blocks** when its TX ring fills
and no host is draining the port. During a stability run the capture script exited, the host
stopped reading, the device kept logging its 30 s memory line and its WiFi retries, the ring
filled — and the firmware wedged mid-message:

```
E (86606) esp-tls: couldn't get hostna<truncated, device gone>
```

It then stopped responding to `esptool` entirely, through every `--before` reset mode, and
needed a physical power cycle.

**The normal state of this device is "no host attached."** It sits on a desk in a living
room. A console write that blocks when nobody is listening is not a debug-tooling
inconvenience — it is a guaranteed field hang, and the panel would freeze showing a stale
but entirely plausible aircraft, which is the one failure mode a non-technical user cannot
diagnose. The only reason it surfaced during development is that a test harness happens to
attach and detach a host repeatedly.

The watchdog is the second half of the fix: the first half stops this particular hang, the
watchdog stops the *class* of it. Rebooting costs nothing here — the route cache rebuilds in
one poll — so for this device rebooting always beats hanging.

**Aside, on the crash forensics:** `Saved PC: 0x4037f94a` decoded to
`esp_cpu_wait_for_intr`, the idle task. Combined with `rst:0x15 (USB_UART_CHIP_RESET)` that
ruled out a firmware panic and pointed at an external reset plus a wedged peripheral, which
is what made the blocking-write explanation the right one rather than a stack overflow hunt.

## D23 — The first screen must not show 1970

**Decision:** `view_model_t` carries `clock_valid`. Until SNTP answers, §5.3 shows
**"Kein Netz" / "Ich suche ein bekanntes WLAN."** in the hero instead of a time.

**Why:** caught by screenshotting the device in its real state rather than a replayed
fixture. Before SNTP the clock is the Unix epoch, so the panel read
**"01:05 · Donnerstag, 1. Jänner 1970"** at 100 px. The German was flawless and the layout
was correct, which is exactly what made it bad: it looks like a working device confidently
telling you the wrong thing.

That is also the **first screen this device ever draws** — on the bench, and again when he
plugs it in in Thailand before any network exists. AGENTS.md §1 says a blank panel reads as
broken to this user; a 1970 date reads worse, because it is not obviously wrong, it is just
wrong.

`clock_valid` is an explicit field rather than the screen sniffing for `"--:--"`, so the
rule lives in the model where both sides can see it.

## D24 — What live traffic found that fixtures could not

The device polled real aircraft for the first time on 2026-09-19. Five defects surfaced
that no host test and no replayed fixture would ever have caught, because each one lives in
the gap between the code and the physical world.

**1. IPv6 broke every single request.** `api.adsb.lol` publishes AAAA records, lwIP's
`getaddrinfo()` returns the IPv6 address first, and no domestic network here or in Thailand
routes IPv6. Every poll dialled an unreachable address and waited out the timeout. A
browser hides this with Happy Eyeballs (RFC 8305), racing A against AAAA; lwIP takes the
first address and commits. `CONFIG_LWIP_IPV6=n`.

**2. My own lwIP tuning broke outbound TCP.** Shrinking `LWIP_TCPIP_RECVMBOX_SIZE` and
`LWIP_MAX_SOCKETS` to reclaim ~6 KB of internal SRAM silently dropped packets, including
the SYN-ACK. It presented as an unreachable host on a network where a laptop on the same
subnet completed the same request in 40 ms. Reverted, with the reasoning recorded in
`sdkconfig.defaults` so it is not re-done. **Correctness first; the 6 KB was not worth it.**

**3. The routeset buffer was smaller than the captured response.** 4 KB, against a fixture
sitting in this repo that is 5,833 bytes — so every live route truncated mid-JSON, failed to
parse, and the panel said "route pending" forever. A buffer size is a claim about the data,
and the data was already in `test/fixtures`. `test_source.c` now asserts it fits with room
for `MAX_AIRCRAFT`.

**4. An 8 KB task stack overflowed on the first poll that reached the network.**
`esp_http_client` plus a cJSON parse does not fit. It crashed and rebooted before any poll
completed, which presented as an endless run of `ESP_ERR_HTTP_CONNECT` rather than as a
stack problem. Now 16 KB, and the task logs its own high-water mark each poll so the number
stays honest — measured 6.8 KB free with routes resolving.

**5. SNTP only ever started at boot.** It was called inside the `wifi_start()` success path,
so a device with no stored credentials — *every* new device, and every arrival somewhere
new — never started it at all, and the clock stayed at 1970 until a reboot. It now starts
the first time a network actually appears, which is the whole point.

**And one thing that was wrong in the diagnosis, not the code.** A long stretch of
"intermittent WiFi" was chased as far as blaming RF noise from the USB port. The device had
simply been carried to a different building; `rssi=0 dBm` with 2 ms failures is not a
marginal link, it is *not associated at all*. The lesson is narrow and worth keeping: check
whether the thing is connected before theorising about why its packets are lost.

## D25 — An unactionable caution is worse than none

**Decision:** the amber "KEIN NETZ" appears when WiFi is down, or after three consecutive
poll failures — not after one.

**Why:** it was driven by `!flight_source_is_stale()`, which is true after a *single* failed
poll. On a −72 dBm link in a holiday apartment, one lost request told him the network was
down while SNTP was demonstrably syncing through it. The only thing he can do about "KEIN
NETZ" is go and look at the router, so a caution that fires on a routine dropped packet
sends him on an errand that cannot succeed. The screen already keeps showing the last
aircraft, which is the designed behaviour for exactly this case.

Still imperfect and marked TODO(M4) in the code: "no network" and "the data source is not
answering" are different problems with different fixes, and they currently share a label.

## D26 — The serial port is not a fixed name

The board enumerates as `/dev/cu.usbmodem1101` or `/dev/cu.usbmodem101` depending on the
replug, which broke every hardcoded tool. `tools/*.py` now discover it.

## D27 — "Still looking" and "no flight plan" are different answers

**Decision:** `view_model_t` carries `route_searching`. While a callsign is queued with the
routeset API and unanswered, the amber tag reads **"ROUTE WIRD GESUCHT"** with
*"Die Route wird noch gesucht."*, not "KEIN FLUGPLAN".

**Why:** PLAN.md M4 calls this the 2E0LXY lesson, and it is the sharpest one in the plan.
Asserting "this aircraft has no flight plan" and then replacing it with a route a few
seconds later does not read as a device updating — it reads as a device that was wrong.
A panel he has caught being wrong is worse than no panel, because the whole product is a
claim that it is faster and more trustworthy than reaching for his phone.

Same layout, same hero, same data band; only the explanation differs.

## D28 — New callsigns get their route asked promptly; retries do not

**Decision:** two intervals. A batch containing a callsign never sent to the API fires after
**15 s**; a batch of pure retries keeps the **120 s** floor.

**Why:** the flat 2-minute batch interval was a correct reading of AGENTS.md §5 ("one POST
every few minutes, not one per poll") applied to the wrong thing. That rule exists to stop
us re-asking about the same flight, and the per-callsign cache already achieves it. What the
flat interval actually did was make the **route** — the single most important thing on the
panel — arrive up to two minutes after the aircraft did, by which time it may have crossed
the entire 30 nm ring. Measured after the change: **route resolved 11.6 s after boot**.

The fast path cannot run away, because the cache means any callsign is asked at most once
per flight, and the `asked` flag is set on any completed attempt — success or failure — so a
failing POST drops to the slow interval instead of looping.

## D29 — The tearing bug does not reproduce here — CONFIRMED by eye

**Measured on the unit**, animating a 100 px face at full-screen invalidate while committing
20 × 2 KB NVS blobs:

| | worst frame gap |
|---|---:|
| idle | 49,729 µs |
| during NVS commits | 49,732 µs |
| during commits, **with LVGL held across them** | 2,850,903 µs |

**NVS commits cost 3 µs — 1.0× idle.** On this configuration (2 framebuffers, anti-tearing
on, direct mode, `SPIRAM_FETCH_INSTRUCTIONS` and `SPIRAM_RODATA` both on) flash writes do
not disturb rendering at all.

**So do NOT pause LVGL around NVS writes.** AGENTS.md §7 prescribes exactly that, and it is
measurably the wrong trade here: holding the display lock across a commit burst stalls
rendering for **2.85 seconds** to save 3 µs. That guidance predates the framebuffer
measurement in D12, and two framebuffers are very likely why the hazard went away.

**What this does not settle, stated plainly.** Those gaps are LVGL's refresh cadence. The
mechanism in espressif/esp-bsp#570 is the LCD peripheral's DMA starving while it reads the
framebuffer out of PSRAM, and with `bb_mode = 0` the panel reads PSRAM directly — a tear
leaves *no software trace*. It cannot be measured from inside the firmware; it has to be
looked at. `t` on the debug console therefore ends by sweeping hard-edged white bars down
the panel for ten seconds with NVS hammering underneath, which is what a tear shows up on.

**Watched on 2026-09-19: no tearing.** So the question espressif/esp-bsp#570 raises is
closed for this build, from both directions — no measurable effect on render cadence, and
nothing visible on the glass under sustained flash writes.

**Consequence:** AGENTS.md §7's instruction to pause LVGL around NVS writes is removed from
practice. It was written before the framebuffer count was measured (D12), and two
framebuffers are the most likely reason the hazard went away. Nothing in the firmware holds
the display lock across a commit.

## D30 — Location is one tap, and everything else follows from it

**Decision:** `main/data/settings.h` — two named places plus a custom escape hatch. The
poll coordinates, the timezone and (via the clock) the auto-dim window all derive from that
single choice. Persisted in NVS, sanitised on every load.

**Why:** AGENTS.md §6 says switching location must be one tap, not a coordinate form, and
that the timezone must follow the place because he never sets a clock. Those are the same
decision, so they live in the same struct. `location_tz()` sits next to the coordinates for
exactly that reason.

**Verified end to end on the device:** switched to Pattaya from a holiday apartment in
Europe and watched it track `TGW134 | A20N | Singapore -> Xianyang | 3.5 nm NW`, with the
panel showing **20:05 Thai time**. Survives a reboot.

Two guards worth keeping:
- A corrupt preset resolves to Gloggnitz, never to **0,0** — the middle of the Atlantic
  would look like a broken device rather than a misconfigured one.
- Brightness has a **floor of 10%**. A device that can be configured to invisible gives him
  no way back, because he cannot see the control that got him there.

**Auto-dim defaults ON**, per DESIGN.md §7. Someone who never opens the settings screen is
precisely who that protects.

## D31 — Two bugs the Pattaya preset exposed

Switching hemispheres turned out to be a good test of things that had only ever been seen
from one place.

**The compass readout collided with itself near north.** The bearing abbreviation and the
degree figure were positioned and edge-clamped *independently*, which is invisible until
the bearing approaches 0/360 — then both get pushed against the right edge and land on top
of each other. A live Pattaya poll at 352° printed "NNW" and "352°" as one smear. They are
now measured, centred and clamped as a single group.

**The airline table was Europe-shaped.** `TGW` — Scoot, the first Singapore carrier the
device ever saw — was missing, so the airline line simply vanished. Ten Asian carriers
added (AAR, AXM, CES, NOK, PAL, SEJ, SJX, TGW, VJC, XAX), now 206 entries. `MNA` was
deliberately **not** added: I could not state it with confidence, and an invented airline
name is worse on this panel than a blank line.

## D32 — A missing glyph renders as nothing, so it is now a test

**Decision:** `tools/check_font_coverage.py`, wired into `make all` in `test/host`.

**Why:** LVGL draws a glyph the font does not contain as **nothing at all** — no error, no
placeholder box, no log line. The text is simply shorter than it was written, and on a panel
nobody reads character by character that can survive indefinitely. AGENTS.md §7 flags this
for umlauts; the same trap catches a real `…`, a non-breaking space, or a typographic quote
pasted out of a document.

It found three on its first run, all user-facing: **Aeroméxico**, **Aerolíneas Argentinas**
and **Air Algérie** would have rendered as "Aerom xico" on the panel.

The checker **parses the ranges out of `tools/build_fonts.sh`** rather than keeping its own
copy. The first draft did keep a copy, and it went stale within minutes of the subset
changing — a checker with a private definition of the truth is a checker that lies.

## D33 — Latin-1 accents at every size, including the hero

**Decision:** widen the subset from the seven German umlauts to the whole `0xC0–0xFF`
block, on all ten faces.

**Why:** the data genuinely contains them, and not only in body text. Airline names
(Aeroméxico) sit in the supporting band, but a city name reaching the **hero** through the
API fallback can too — "Málaga", "Nîmes". Rendering that as "M laga" at 100 px is the most
visible possible failure.

**Cost, measured:** 482,256 → 752,589 B of binary (+270 KB), app image 2.09 MB with the
partition 58% free. **FPS: 28.58 / 28.50 / 28.45 — unchanged.** So the extra glyph data
costs nothing at runtime, which is consistent with D12's finding that font bandwidth is not
the constraint here.

Latin Extended-A stays off the three hero faces; Polish and Czech place names are rarer in
the hero slot and that range is where the real bytes are (tools/README.md has the numbers).

## D34 — The settings screen speaks km, not nautical miles

The radius control arrived reading **"30 NM"**. The API takes nautical miles and that is the
API's business; DESIGN.md is explicit that the panel speaks km, and "NM" means nothing to
the man this is built for. Now **"56 km"**, converted and grouped through the same `fmt_de`
helpers as every other number on the device rather than a local `snprintf`.

## D35 — The radar's words go under the scope, not on it

**Decision:** §5.5 draws marks only — shape and colour, no prose. The nearest aircraft is
captioned in a band **below** the scope, at full size.

**Why:** DESIGN.md §3 warns that §5.5 as drawn is below the readability floor, and the type
pass raises its labels from 10–11 px to the 25 px near-tier minimum. At that size two
captions cover the middle of a 280 px scope: a live capture had **"DIMO 2,1 km" printed
straight across the home marker** and over two other aircraft.

There is no way out by adjusting sizes. Shrinking the text is the exact thing the type pass
forbids, and widening the scope makes the collision worse, because the constraint is the
text, not the geometry. So the scope stops trying to be a document. It is a picture — where
things are, which way they point, which one matters — and the words he has to *read* go in
a band underneath at full size, in the same place every time. DESIGN.md §4's band order
puts data at the bottom anyway.

Only the nearest is captioned: it is the one the magenta mark already singles out, and a
second caption is the crowding problem returning by another route.

## D36 — One answer to "what is this aircraft called"

**Decision:** `actype_display_name(type, category)` in `tables.h`. Both the hero and the
list use it.

**Why:** the list screen printed **"DIMO"** and **"PA18"** at him — raw ICAO designators,
which AGENTS.md §1 forbids in as many words. The cause was not a missing table entry (though
those were missing too): `actype_full_or_code()` falls back to *the code itself*, which is
right for a log line and wrong for a panel, so the list's category fallback was never
reached. `view_build` had already grown its own private version of the correct chain, and
the two drifted — the bug was the duplication, not either copy.

Six types the live feed produced were also added: AT75, AT76, B734, DIMO, PA18, PC6T (209
total). A test now asserts the helper never returns the code it was given, for every one of
them.

## D37 — The adverb, not the noun: "16,8 km nordöstlich"

**Decision:** `compass_de_adv()` in `fmt_de.c`, and `view_model_t.direction_word` now carries
the adverb. `compass_de_word()` ("Nordosten") is still exported and still tested; nothing on
the panel uses it.

**Why:** the data band read **"16,8 km Nordosten"** — a bare noun stranded after a number.
It is not a sentence anyone says, and it is exactly the flavour of German that tells a reader
the thing was translated by a machine. PLAN.md M7 named the fix in its own checklist:
`"nordöstlich"`. Three screens draw a direction and the abbreviation ("NO") is still right for
the two dense ones (Liste, Radar) and for the compass tape; only the one place that sets a
direction next to a distance in running text needed the adverb.

The test asserts the adverb is *not equal to* the noun for all eight bearings. Without that,
a one-word edit in `view_build.c` would put the stranded noun back with every other test
still green.

## D38 — One file he could read, if he read C

**Decision:** every user-facing string lives in `main/strings_de.h`. `tools/check_strings.py`
fails the host suite if a literal a human would read appears in `main/ui/*.c` or
`main/data/view_build.c` without being defined there. `--list` prints the whole lexicon as
plain text.

**Why:** PLAN.md M7 asks for "one translation unit — audit that nothing leaked into a widget
constructor", and the previous arrangement (a documented `CHROME_*` block at the top of each
screen) was good discipline but six places to check, enforced by nothing. The audit is the
point; the move is just what makes the audit expressible.

**What the rule actually is.** After comments, `#include` lines and `ESP_LOG*` calls are
stripped, every remaining literal must be empty, or structural (no letters and nothing above
ASCII 126 once printf conversions are removed — so `"%s %s"` and `"%02d:00"` stay inline
where they belong), or defined in the header. Non-ASCII counts as user-facing whatever else
it is: that clause is what caught `"%d°"` sitting in `widget_compass.c`.

**Two deliberate exceptions**, both indexed tables, both still in the `--list` dump because a
reviewer needs them: the compass/weekday/month tables in `fmt_de.c`, and the location preset
names in `settings.c`. Naming sixteen compass points as sixteen macros and rebuilding an
array out of them would be strictly worse than the array.

**The test keeps a second copy on purpose.** `test_view.c` restates the five German reason
sentences verbatim rather than including the header. A test that asserts
`STR_REASON_NONE == STR_REASON_NONE` asserts nothing. Changing what the device says to him
costs two edits in two files, and the failing test in between is the feature.

## D39 — Consolidating the strings walked them out from under the font gate

**Decision:** `check_font_coverage.py` scans `main` whole, not `["main/ui", "main/data"]`, and
decodes C escapes before checking codepoints.

**Why:** this is the sharpest finding of the milestone, and it was self-inflicted by the
milestone. `main/strings_de.h` sits one directory above both scanned paths, so the moment
every German string moved into it, the font gate went on reporting success with nothing left
in its scan path to check. Worse, the header deliberately writes non-ASCII as hex escapes
(`"\xE2\x80\x94"`) so no editor can re-encode it — and the checker compared *source
spellings*, so it read those as plain ASCII backslashes. Two independent holes, both silent,
both in the one tool whose entire job is to catch silence: LVGL draws a missing glyph as
nothing at all.

Verified by probe rather than by reading: injecting `"\xE2\x80\xA6"` (U+2026, not in the
subset) into the header now fails the gate with `main/strings_de.h:222: U+2026 '…' is in NO
generated face`. Before the fix that probe passed.

Widening the scan also turned up four `§` literals in `dbg_fixture.c` that only ever reach
`ESP_LOGW`. Rather than grow every font by a glyph nothing draws, the checker now skips
log calls and honours an explicit `/* LOG-ONLY */` marker — written by hand, because the
checker cannot follow a variable from its assignment to its use, and a promise that has to be
typed out stays visible in the diff.

## D40 — Three dots, one of them half-swallowed

**Decision:** `nav.c` computes dot widths instead of measuring them.

**Why:** `lv_obj_set_width()` only marks an object dirty; `lv_obj_get_width()` returns the
width from the last layout pass. `paint_dots()` widened the active dot and then measured it
in the same breath, got the old 8 px back, and laid the second dot 16 px too far left —
underneath the active pill. A three-page deck looked like a two-page one with a smear on it,
and it had been shipping that way since the deck was built.

No host test can see this; it is LVGL layout timing, not arithmetic. It was found by reading
the panel's own framebuffer back as a PNG and enlarging 30 px of it. An
`lv_obj_update_layout()` between the two loops would also work — computing is better, because
it removes the dependency on layout timing rather than satisfying it.

## D41 — Every screen has to be reachable from the build host

**Decision:** debug console keys `g` (next deck page), `e` (Einstellungen), `k` (WLAN) and
`d` (scroll the current screen to its end).

**Why:** a screen that can only be reached by tapping the glass is a screen nobody checks.
Einstellungen is taller than 480 px, so the data attribution line at its foot — the one the
ODbL actually requires — could not be photographed at all until `d` existed. `scroll_to_end`
walks the tree recursively: the scrollable column is not a child of the screen, because
`nav_open_overlay()` puts an overlay root in between, and the first version scanned one level
and silently did nothing.

## D42 — Two sentences that failed a read-aloud

**Decision:** "Zu diesem Flugzeug liegt keine Routeninformation vor." became "Zu diesem Flug
ist keine Route bekannt.", and "Verbindung zu X wird hergestellt..." became
"Verbinde mit X...".

**Why:** the first is Amtsdeutsch — the register of a form he has to fill in, and he is
standing in his garden. The second put two voices on one screen: the line above it already
says "Suche Netzwerke...", and the device talks to him in the first person everywhere else
("Ich suche ein bekanntes WLAN"), so the passive construction was the odd one out.

M7's last checklist item — **read every screen aloud with someone Austrian** — is the one
thing here that cannot be done by a tool or by me. `python3 tools/check_strings.py --list`
prints all 110 strings grouped by screen for exactly that pass.

**Done, 2026-09-19.** The list was read through by a native speaker. Two entries had been
flagged as open judgement calls and both were ruled to stay:

- **"Nachtabsenkung"** — technical-sounding, and I suspected it might be opaque. It is not:
  it is the ordinary word, familiar from domestic heating controls.
- **"in Reichweite"** — I had weighed "in der Nähe" as warmer. "in Reichweite" is right; it
  also happens to be the more honest of the two, since the list really is bounded by the
  radius setting rather than by nearness.

Worth recording that both of my instincts here were wrong in the same direction: I read
precise German as cold.

**That reading was itself too neat — see the end of D56.** A third call went the other way:
I proposed replacing "Flugzeuge" with the strictly-correct *Luftfahrzeuge* and was told it
does not fit. So the pattern is not "I lean technical" or "I lean plain". It is that my
sense of German REGISTER is unreliable in both directions, while my sense of German
*correctness* has held up. The rule that follows: check facts against sources and settle
register with a native speaker — which is exactly why this item was written into the plan as
needing a person rather than a checker.

## D43 — Two tables that were too small to keep their promise

**Decision:** `tbl_airport.c` grows from 74 to 601 cities; `tbl_actype.c` grows to cover
general aviation. `resolve_city()` now logs every miss.

**Why:** taking the README screenshots put **"Rodes Island → London"** on the panel in 76 px
type, and **"Roma"** in the list. Neither is English, German, or the local name — the first
is data-entry noise in the adsb.im `location` field, showing through because LGRP was not in
the table. `resolve_city()` was structurally right the whole time (German table, then the
API's own text, never a bare code) and its comment claimed the fallback was rare. It was not
rare; nobody had ever counted. It now says so in the log, so the next gap is found by
leaving the board on a console for an afternoon instead of by noticing it in a photograph.

The naming policy did not change and is worth restating because it is the part that is easy
to get wrong in the generous direction: the German exonym **only where Austrian usage
genuinely has one** — Mailand, Warschau, Laibach, Danzig, Hermannstadt — and the local
spelling everywhere else. "Neu-York" is not a German name, it is an insult to both
languages. One more constraint that bites: the three hero faces carry Latin-1 only, so
Wrocław and Timișoara have to be spelled around. `check_font_coverage.py` enforces it.

## D44 — Over-the-air updates, off by default

**Decision:** OTA is implemented, and a device with no update URL stored in NVS never
contacts anything. HTTPS only. Rollback on. Installs only inside the night dim window.

**Why off by default:** there is no release infrastructure yet, and a firmware source is the
most dangerous string on the device — whoever controls it controls the device. A URL is
stored the same way WiFi credentials are: typed in over serial, never in the repository.
`ota_set_url()` refuses anything that is not `https://`, and `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP`
stays off, because a Kconfig flag is one edit away and code is not.

**Why the night window:** writing 2 MB to flash tears this panel — espressif/esp-bsp#570, on
this exact silicon, one of the three risks the whole build order exists to retire. An update
during the day would garble the screen for a minute in front of the one person who must never
see this thing look broken. At 3 a.m. it costs nothing. With auto-dim switched off there is
no window and therefore no safe hour, and the answer is never.

**Why rollback:** the failure worth guarding against is not a corrupt image — the bootloader
checks the hash — but a working image that cannot get online. That is the brick nobody can
fix from 9,000 km away. `ota_confirm_running_image()` is called only after four consecutive
30-second ticks with WiFi associated; if that never happens, the next reboot returns to the
build that worked.

**Verified on the device, and what was not.** DNS, TLS handshake, Mozilla-root-bundle
validation, HTTP 200, 2,262 bytes received byte-exact, cJSON parse, and our own field check
correctly rejecting a body that is valid JSON but not a manifest. The image download, flash
write and slot switch are **not** exercised end to end: that needs a hosted signed build, and
there is nowhere to host one yet. The policy layer is exhaustively host-tested (14,055
checks, including all 13,824 combinations of hour × window against the dimmer's own answer).

## D45 — Three bugs that only appeared once TLS ran

Each cost a build cycle and each is worth more than the feature that found it.

**A 4 KB stack does not report its own overflow.** `ota_check()` was first called from the
debug console task. A TLS handshake against the full root bundle needs about 8 KB. The
overflow did not announce itself — it corrupted the touch driver's context, and the device
aborted a few hundred milliseconds later inside `esp_lcd_touch_read_data()` with
`ESP_ERR_INVALID_ARG`, which reads as an I²C fault in a subsystem TLS has never heard of.
The OTA task now has 10 KB and reports 6,172 B of headroom after a real handshake — so the
4 KB task had none at all. `ota_request_check()` exists so the console never does this again.
This is the third time on this project that a stack overflow has presented as something else
entirely; it is the house failure mode.

**`mbedtls_ssl_setup returned -0x7F00` is an out-of-memory error wearing a network error's
clothes.** With the framebuffer and WiFi up, this board has ~35 KB of internal heap and a
largest free block of 15 KB; mbedTLS wants a 16 KB inbound record buffer. There are 4.7 MB of
PSRAM idle on the other side of the bus, so `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` plus
`CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`. Cost of the whole TLS + OTA stack, measured at four
boot checkpoints against the previous commit: **1,328 bytes of internal SRAM, exactly**, and
104 KB of flash.

**One `esp_http_client_read()` is not the body.** It returns whatever has arrived — one TLS
record, one chunk. The first working handshake fetched 2,262 bytes and parsed the first 1,024
of them, which failed as "not JSON" and looked exactly like a server problem. It reads in a
loop now, refuses a body that does not fit rather than parsing a truncated one (half a JSON
object can still be valid JSON, and then the device believes a version number nobody sent),
and checks `esp_http_client_is_complete_data_received()`.

**And a fourth, found by the fix.** Pasting a URL into a console whose stdin is non-blocking
(D22) means `fgets()` returns NULL instantly and every character afterwards arrives at
`on_cmd()` as a *command*. `https://...` contains `t`, which runs the tearing benchmark.
`dbg_read_line()` already existed and already solved this; the new console had not used it.

## D46 — "C177" in the hero, and why D36 did not prevent it

**Decision:** `hero_from_type()` no longer falls back to `actype_full_or_code()`.

**Why:** the panel showed **C177** — a raw ICAO designator, at 76 px, as the answer. D36
removed exactly this from the list screen by routing both callers through
`actype_display_name()`, which returns NULL when neither the table nor the emitter category
knows anything. But the hero kept a tail below that call: on NULL it asked
`actype_full_or_code()`, whose documented last resort is *the code itself*, and then replaced
the result only if it was literally `"?"`. So the fix held for every type that produced a
question mark and failed for every type that produced a designator.

D36 said the bug was the duplication, not either copy. It was right, and it did not go far
enough: deleting one copy left the other's dead fallback standing. If `actype_display_name()`
returns NULL there is no third source, and the answer is "Unbekanntes Flugzeug".

The test that locks it uses designators deliberately absent from the table and asserts the
hero neither equals nor *contains* them — plus, in the same group, that a known type is still
named, because "make every hero say Unbekanntes Flugzeug" would pass the first half and
destroy the product.

## D47 — A transient I²C error aborts the device

**Not decided — recorded.** `esp_lvgl_port_touch.c` wraps both touch reads in
`ESP_ERROR_CHECK`, so any failure on the GT911 — which sits behind a TCA9554 expander on a
shared I²C bus — panics the whole device. The stack overflow in D45 surfaced through this
path, which is how it was found at all, but the hazard is independent of that bug: a genuine
bus glitch in a living room in Pattaya would reboot the panel rather than drop a frame of
touch input.

Left alone deliberately. It is a managed component, patching it forks a dependency, and
nothing observed so far suggests spontaneous I²C failure on this unit. Written down because
the next unexplained reboot should start here.

## D48 — The supporting text gives way, not the distance

**Decision:** the data band's anchored position is fixed. When the supporting text reaches
it, supporting lines are hidden bottom-up (type, then airline, then the reason sentence).

**Why:** the band was already anchored to the bottom edge, for the reason recorded in the
code — "the distance sits in the same place on every screen, which is what makes it readable
at a glance instead of something you have to find". But it carried an escape hatch: if the
supporting text reached the band, the band flowed down instead, on the argument that an
overlap is worse than smaller type.

That argument is wrong, and the panel proved it. Flowing does not avoid the collision; it
converts it into **"9,3 km südöstlich" cut in half at y=480**. An overlap is ugly and the
distance is still there. Clipped, it is gone. The header comment two lines above the escape
hatch already described this exact failure as the thing anchoring was introduced to fix.

D46 made it common rather than rare: an unnameable aircraft now reads "Unbekanntes
Flugzeug", which is two lines at the ladder's smallest face where "C177" was one.

The drop order is the product's own priority — the hero is the answer, the distance is the
second question he asks, and the type line is the first thing he can do without, especially
in the state where the hero is already saying everything that is known. Verified both ways
on the panel: with a 76 px hero all three supporting lines stay (Samos → Amsterdam, Corendon
Dutch Airlines, Boeing 737 MAX 9, 45,8 km nordwestlich); with a 100 px hero the type line
yields (München → Seoul, Lufthansa, 8,9 km nordwestlich).

Clipping is now impossible by construction rather than by test: `y_band` is computed once
from the bottom edge and never increased. The hero itself is deliberately not droppable, and
cannot reach the band — `pick_hero_font()` only selects the 100 px and 76 px faces for text
that fits on one line, so only the 56 px rung can wrap, and two lines of it end well clear.

## D49 — "KEIN NETZ" for a working router

**Decision:** `view_model_t.online` (a bool) becomes `net` (a three-state enum). The panel
shows **KEIN NETZ** when it is not associated and **KEINE DATEN** when it is associated but
the flight-data source has stopped answering.

**Why:** the code carried its own indictment as a `TODO(M4)` — *"'no network' and 'the data
source is not answering' are different problems with different fixes, and right now they
share a label."* They do, and the shared label is the actionable one, so a source outage sent
him to look at a router with nothing wrong with it. The comment immediately above that TODO
already had the principle: **a caution he cannot act on correctly is worse than none.**

A bool cannot carry three states, which is why the TODO survived four milestones: the fix
looks like a one-line change and is actually a type change through `view_build`,
`view_build_empty`, the view model, the screen and twenty-four test call sites. Doing it
properly is still cheaper than the alternative, which is him learning that the panel's
warnings do not mean anything.

The hysteresis is unchanged: three consecutive failed polls before saying anything, because
a weak link drops one now and then and the screen keeps showing the last aircraft, which is
the designed behaviour anyway. Not being associated at all shows immediately, because that
is the one he can fix.

One detail that would have been a bug: the caution is right-aligned to the content edge, and
"KEINE DATEN" is wider than "KEIN NETZ". The text has to be set before the position is
recomputed, or the longer string hangs off the edge it is aligned to.

## D50 — The radar caption wraps into the page dots

**Decision:** the caption is one line or the name is dropped. The distance always stays.

**Why:** D35 moved the radar caption out of the scope and into the 44 px band between the
scope and the page indicator. That band cannot grow, so a name that wraps is not taller —
it is drawn across the distance and the dots. **"Unbekanntes Flugzeug"** rendered as
"Unbekannte / s Flugzeug" over the top of "9,7 km NNO", and D46 turned that string from rare
into ordinary.

Same priority as D48, for the same reason: the magenta mark already says *which* aircraft
this is, so the caption's remaining job is how far and which way. The name yields; the
distance never does.

**The measurement was also wrong, in the other direction.** The label was created with a
fixed 132 px width and `LV_LABEL_LONG_MODE_WRAP` — about eleven characters at 25 px — so
"Thessaloniki", the name DESIGN.md §3 uses as its own worked example of a long destination,
would have wrapped too, and a naive fit check would have called it a fit. The width is now
measured unwrapped and then applied to the label, so the cap *is* the measurement rather
than a second, tighter limit hiding underneath it.

## D51 — The aircraft table, and one word walked back

**Decision:** `tbl_actype.c` grows from 209 to 403 rows, weighted towards general aviation.
`BALL` is "Ballon", not "Heißluftballon".

**Why the growth:** the table was built for airliners, and the traffic he actually *hears* —
low and slow over the house — is not airliners. A live capture had **SF25**, a Scheibe Falke
motorglider on an Austrian registration, reading as "Unbekanntes Flugzeug" twice in one
list. It now reads **"Scheibe SF-25 Falke"**, with "Eine Route gibt es nur bei Linienflügen."
underneath, which is the whole §5.2 argument working as designed.

**Why "Ballon":** Doc 8643's `BALL` is the *generic* balloon designator, so the warmer word
would confidently mislabel a gas balloon. It is a small lie for a small gain in warmth, and
this device's entire claim on him is that it does not lie. "Ballon" also matches
`ac_category_de()`'s word for emitter category B2 — one thing, one way (D36).

**Two calls left as the table has them**, both defensible and both worth a second opinion
from someone who knows the field:

- **Warbirds are `AC_CAT_PRIVATE`, not military.** A Spitfire, a Ju 52 or a T-6 flying today
  is privately operated, so "Eine Route gibt es nur bei Linienflügen." is the *true*
  sentence for it. Categorising by what the airframe was built for rather than by what it is
  doing would produce a sentence about public flight plans that does not apply.
- **Five class designators** (GLID, BALL, GYRO, SHIP, ULAC) repeat the same German word in
  all four text fields. That is deliberate: `manufacturer` cannot be `"-"`, because
  `actype_is_placeholder()` reads that as "unidentified" and would demote a glider squawking
  A1 to "Leichtflugzeug".

Thirteen designators from the brief were left out because they could not be confirmed
(ARCP, HU1, SZD5, TWIN, PK20, K126, SIRA and others). That is the right trade: a wrong
designator shows him the wrong aircraft with total confidence, which is worse than
"Unbekanntes Flugzeug" — and the device now logs every miss, so the gaps name themselves.

## D52 — A feature that ships off should cost nothing while it is off

**Decision:** the OTA task is created on demand — at boot only if a URL is stored, otherwise
the first time one is. Its startup delay is an interruptible wait, not a sleep.

**Why:** a four-minute soak put steady-state internal heap at **16,383 B free, largest block
7,168 B**. About 10 KB of that was an OTA task stack, sitting idle on a device with no
update source configured and no prospect of getting one. 10 KB is cheap when it is doing
something and indefensible when it is not — this board runs for months between power cycles
and internal SRAM is the scarce resource, not PSRAM, of which 4.7 MB is free.

Creating it lazily returned **27,039 B free with a 15,360 B largest block**: ten and a half
kilobytes back, and the largest contiguous block doubled, which is the number that actually
decides whether the next allocation succeeds.

The follow-on was immediate and would have been a small, lasting annoyance. The task opened
with a 60-second settling delay — right at boot, where it keeps the radio and the PSRAM bus
clear of the thing he is actually looking at. But the task is now also created the moment
someone types in an update URL and sits watching the console for an answer, and there a
settling delay settles nothing. It is a semaphore wait now, and `ota_request_check()` has
already posted by then, so the check runs in under two seconds instead of after a minute of
apparent silence.

## D53 — A third address, and why it is numbered last

**Decision:** `LOC_WIEN` — Meiselstraße 79, 1140 Wien (Penzing), `48.1984 / 16.3074`, on
Austria's clock. Its enum value is **3**, after `LOC_CUSTOM`, while it appears **second** on
screen, above Pattaya.

**Why the split:** `location_preset_t` is written to NVS. Slotting Wien in beside Gloggnitz
where it belongs visually would renumber `LOC_CUSTOM` from 2 to 3, and a device already in
the field with 2 stored would come back from a firmware update sitting in Vienna. Nothing
would announce it — the distances would simply stop making sense, on the one screen whose
entire job is to be trusted at a glance. So the enum is append-only and
`location_display_order()` carries the order he sees. A test asserts the four values by
number and asserts the display order is a permutation, because a duplicate would make one
place unreachable by tap and a gap would give him a card that selects nothing.

Coordinates geocoded rather than estimated. A transposed lat/lon or a stale 0.0 row is
invisible on a panel that only ever shows a distance, so the test also asserts Vienna is
north **and** east of Gloggnitz — the cheapest available check that the row is in the right
hemisphere.

## D54 — The review findings, and the one that mattered most

A full review of this session's work found nine defects in the new C and thirteen in the
tooling and tests. The headline is not in either list, because it predates the session:

**A brand-new board boot-looped.** `s.mutex` is created inside `flight_source_start()`, and
`main.c` only starts the poller when `wifi_start()` succeeds — so a device with no stored
credentials never called it, and `ui_task` called `flight_source_snapshot()` two seconds
later into a NULL mutex. Reproduced on hardware: four reboots in eighteen seconds,
`assert failed: xQueueSemaphoreTake queue.c:1709`. The console message telling you to press
'w' was scrolling past in the reboot loop, so even the recovery path was a race. Seven
accessors had the hole. The module now answers "I have not been started" honestly — no
aircraft, not stale, still resolving — all of which the §5.3 empty-sky screen already
renders, because that screen was built for this.

**OTA found updates and could never install them.** `ota_settings_update()` was called only
from `apply_settings()`, and `app_main()` open-coded a *subset* of `apply_settings()` rather
than calling it. So `s_settings` stayed all-zero for the life of the device, `auto_dim` read
false, and `ota_should_install()` refused every night forever — while `update_console()`
printed the night window it believed was in force. The fix is to call the real function at
boot, which is only safe because of the guard above. Duplication that drifts, again: the
same shape as D36 and D46.

**The manifest fetch could not follow a redirect**, though its comment named GitHub release
assets as the case it existed for. `.disable_auto_redirect` is inert on the
open/fetch_headers/read path — `esp_http_client` only acts on it inside
`esp_http_client_perform()`, which is why `esp_https_ota` rolls its own loop. The failure
was asymmetric and so particularly nasty: the `.bin` named *inside* the manifest would have
downloaded fine, because `esp_https_ota` handles its own redirects. Only the first hop was
broken.

**Replacing the update source did not invalidate the image pending from the old one.** Point
the device at a new source because the old one was wrong or compromised, have the new one
fail to answer, and the night's install pulls the binary from the source you just removed.

Five smaller ones, all real: 4 KB of `.bss` — internal DRAM — spent by a feature that ships
switched off, in a module that goes to real lengths elsewhere to cost nothing while off; a
rolled-back image re-downloaded and re-flashed every night forever, because nothing survived
the reboot to say it had already failed (now recorded in NVS from the other slot's
descriptor); the manifest's `size` field documented as a safety check and never read; a
version clamp that stopped accumulating at 100000 and therefore made **"1000000" compare as
smaller than "999999"** — the exact inversion the clamp was written to prevent, moved further
up the number line; and `start_task_once()`, a check-then-act reachable from two tasks
seconds apart, where the likely outcome was not two tasks but a failed second `xTaskCreate`
NULLing the handle of the one that had started.

**What this says about the session.** Almost every one of these is a comment that had
drifted from its code — `.disable_auto_redirect` "for GitHub releases", `size` "to reject an
obviously wrong image", the clamp that "does not wrap", the 24 h interval that "would
otherwise re-check on every reboot". The code was reviewed; the comments were believed. A
comment asserting a property is a claim, and claims are the cheapest thing in a repository
to get wrong.

## D55 — Both gates could be walked around, and one had the bug it was written to catch

**Decision:** the string gate scans `main/` recursively with a minimum-file floor; both gates
share one C lexer and one unescaper; `LOG-ONLY` is anchored; the host Makefile tracks header
dependencies.

**The string gate had D39's bug, one level up.** `enforced_files()` was a non-recursive
`main/ui/*.c` glob. Moving the screens into a subdirectory made it report success —
*verbatim* the failure it was written to prevent, three days later, in the checker itself. It
also never opened a `.h`, `main.c`, `main/net` or `main/data`, so German in any of those
passed. Now 44 files instead of 8, and a floor of 38 that fails loudly. **The floor is the
fix; the glob was the symptom.** A gate whose subject can move out from under it needs to
know how much it is supposed to be looking at.

**Four evasions of the font gate, closed at the root.** Line continuation, a char literal
holding a quote, a `\U` 8-digit escape, and `ESP_LOG` sharing a line with a label call. The
root cause was that two scripts each had their own C lexer and their own unescaper, and
**they disagreed** — `check_strings.py` handled `\U`, `check_font_coverage.py` did not. One
lexer and one unescaper now, imported, with an assertion at import that pins the five
spellings of U+2026 together so the sharing cannot rot.

**`LOG-ONLY` exempted a line for merely mentioning it.** `/* NOT LOG-ONLY: this really does
reach a label */` granted the exemption — writing the negation of the claim satisfied it. So
did `/* see docs/ANALOG-ONLY.md */`. It now requires a comment whose whole body is the
marker. An escape hatch that fires on a *substring* is not an escape hatch, it is a hole with
documentation.

**The host Makefile listed no headers.** Changing `VIEW_HERO_LEN` from 48 to 6 gave
"0 failed". It gives 882 failures and a non-zero exit now. Worth recording that the obvious
`-MMD -MP` does **not** work here: one compile-and-link command over 21 sources points every
translation unit at the same `.d` file and the last one wins. Dependencies come from a second
`-MM` pass per unit instead, which make unions.

**`--list` went from 110 entries to 688.** It was missing the 556 German city names that
*are* the route headline, and the 22 aircraft-class words. A string it does not print is a
string nobody reviews — and the whole point of that listing is that someone reads it.

**Two lessons worth separating.** The first is that a gate needs a floor: "I checked
everything I found" is worthless without "and I expected to find about this much". The
second is that the exemption list is where a gate goes to die. Eight font-specimen strings
were listed in it by spelling; they moved to `main/debug/dbg_fontcard.c` instead, because
"developer diagnostics live in main/debug" is a rule, and eight spellings is a list someone
has to maintain forever.

## D56 — The aviation German, checked against the standards rather than against my ear

A term-by-term review of every aviation word on the panel, sourced to RTCA DO-260B
Table 2-21, Austro Control publications and German aviation press. Four things were wrong.

**"Eine Route gibt es nur bei Linienflügen." was false.** A *Linienflug* is specifically
scheduled, regular public transport; a *Charterflug*/*Bedarfsflug* is explicitly not one —
and charter, cargo and ambulance flights all have routes. What is actually true is narrower
and plainer: the lookup is keyed on the **callsign**, so a route exists exactly when the
aircraft flies under a flight number. Now **"Eine Route gibt es nur zu Flügen mit
Flugnummer."** — he has read a Flugnummer off a ticket his whole life.

**A1 and A2 shared a synonym pair.** German treats *Leichtflugzeug* and *Kleinflugzeug* as
two words for the same ~5.7 t class, so the table spent them on two different weight bands —
and the larger band got the word that sounds smaller. A2 reaches 34 t: a Dash 8 Q400 is not
a "Kleinflugzeug" under any German definition. A1 is *Kleinflugzeug* now (the word Austrian
press actually uses for what he sees overhead); A2 is *Mittelgroßes Flugzeug*, a description
rather than a term of art, because German has no term — A2, A3 and A4 all sit inside one
German wake class.

**A4 asserted the one thing it is known not to be.** DO-260B's A4 is "High-Vortex Large" and
names the **B-757** as its example — a narrowbody. *Großraumflugzeug* means widebody
specifically: over five metres of fuselage, two aisles. A4 now shares A3's *Verkehrsflugzeug*,
which is honest, instead of A5's, which was not.

**A6's word meant roughly the opposite of A6.** *Hochleistungsflugzeug* is a real EASA
Part-FCL term — High Performance Airplane, a single-pilot TBM, King Air or Citation. DO-260B's
A6 is ">5 g and >400 knots", which in practice is only ever a fast military jet. Now
*Militärjet*.

**Set C was missing entirely** — surface vehicles and fixed obstacles. Added, with Austro
Control's own vocabulary from its *Datenproduktspezifikation für Luftfahrthindernisse*
(Punktobjekte, Linienobjekte, and "Hindernisgruppe" for a cluster). The load-bearing property
of all six words is that none contains "Flugzeug", and a test asserts exactly that.

**Being accurate about what this fixed:** `adsb_parse.c` already drops `t == "TWR"` and
`category[0] == 'C'` at the boundary, and `test_parse.c` asserts both. So nothing was
reaching the panel, and these entries are a backstop for a path that is currently
unreachable — not a live bug. The same applies to the `TWR` row, which could never be
reached for a second reason: `manufacturer = "-"` made `actype_is_placeholder()` true, so
its carefully-written "Boden-Referenzsignal (MLAT)" was dead text. It is *Bodenstation* now,
which is what FlightAware's and AirNav's German pages call the thing, with "Kein Flugzeug"
underneath.

**Four class words changed on plainness or accuracy:** *Geschäftsreisejet* → **Privatjet**
(no aviation source uses the former; Vienna's own charter operators say the latter);
*Großraumjet* → **Großraumflugzeug** (rare, and it contradicted `k_category_de` in the same
file); *Turboprop* → **Propellerflugzeug** (correct but jargon — the plain word names what he
can see); and the A220 moved from *Regionaljet* to *Mittelstreckenjet*, which is what German
press calls a narrowbody on short and medium haul.

**And one I was wrong about.** I suspected *Flugplan* of reading as "timetable". German
Wikipedia's primary article for the word is the ATC flight plan — the timetable sense is the
disambiguated secondary one — and Austro Control uses *Flugplan* and *Flugplanaufgabe*
throughout. Left alone. Seventeen of the twenty-two class words needed no change either,
five of them confirmed against Austro Control's own category nouns.

## D57 — "Flugzeuge", not "Luftfahrzeuge"

**Decision:** the list header stays **"%d Flugzeuge in Reichweite"**, and an unidentifiable
target stays **"Unbekanntes Flugzeug"**, even though both are strictly wrong.

**Why, and why it is not a compromise.** In both legal and everyday German a *Flugzeug* is
fixed-wing; a helicopter is not one, and the umbrella term is *Luftfahrzeug*. German
registrations even separate them (D-H for rotorcraft). So the count line is inaccurate
whenever a helicopter, glider or balloon is in the list — which is most of the time.

Ruled by the native speaker: *Luftfahrzeug* does not fit. And he is right, for a reason worth
writing down rather than just recording the verdict. The panel's claim on him is that it says
true things **in his own words**. *Luftfahrzeug* is a word from a form, not from a garden, and
a man looking up at the sky counts Flugzeuge. Swapping in the legally exact term would buy an
accuracy he was never going to notice at the cost of the register the whole product depends
on — and register is not decoration here, it is the thing that makes a device feel like it
was made for you rather than issued to you.

**Where the line actually sits.** The three findings in D56 that were *changed* were factual:
a false claim about Linienflüge, a category word asserting a widebody about a narrowbody, a
term meaning the opposite of what it labelled. Those are wrong in a way he could be misled
by. "Flugzeuge" for a helicopter is imprecise in a way he would use himself. Correctness is
not negotiable; register is his call, and this is the third time it has gone against my
instinct.
