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
Settles the framebuffer question, now listed as settled in AGENTS.md §8.

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
through ordinary `stdout` and polls a non-blocking `stdin`. (Since D81 it polls the USB-Serial-JTAG RX FIFO
directly instead: on IDF 5.4.4 a driverless `stdin` never delivers a byte.) The task watchdog is enabled
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
there is nowhere to host one yet. **Amended 2026-09-21 (D72):** they have since run on the
unit against a real release, including a wrong-key image being refused. The policy layer
is exhaustively host-tested (14,055 checks, including all 13,824 combinations of
hour × window against the dimmer's own answer).

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

## D58 — The WLAN screen crashed the device, twice over

Opening WLAN panicked the panel with `LoadProhibited`, `EXCVADDR 0x00000008`, **every
time** — on the one path he needs to join a new network, while sitting in a holiday
apartment whose WiFi had just stopped working. Two independent bugs, one hiding the other.

**One: LVGL's heap is a fixed 64 KiB static pool, and it does not fail gracefully.**
`CONFIG_LV_USE_STDLIB_MALLOC=0` gives LVGL a static array; when it is exhausted
`lv_obj_add_style()` takes NULL back from the allocator and dereferences it at offset 8 —
which is exactly the fault address. Measured on the device: the deck uses 45% of the pool,
Einstellungen 60-64%, and `screen_wifi_create()` builds its full-screen `lv_keyboard`
**eagerly**, which does not fit in what is left. It never could.

Growing the pool was not available. It is static internal DRAM, and this board ran with
~23 KB of internal heap free in steady state, so the 32 KiB the pool needed did not exist.
`CONFIG_LV_USE_CLIB_MALLOC=y` routes LVGL through the system allocator instead: small
objects still land in internal RAM, and it spills into the 4.6 MB of free PSRAM when they
cannot. **Internal free heap went the other way — 23 KB to 55 KB — because the 64 KiB
static pool went back to the heap it had been carved out of.**

**Two: the WiFi scan task writes into a screen that may no longer exist.** With the
allocation fixed, the screen opened — and then 12 of 40 rapid navigations still panicked.
`wifi_scan_task` finishes three to five seconds after the screen opens and calls
`screen_wifi_set_networks()`; if the overlay was closed in between, every pointer in
`screen_wifi.c` is dangling. That is precisely what he does when he opens WLAN, reads
"Suche Netzwerke...", and taps Zurück without waiting.

The guard is a liveness flag **cleared by LVGL itself** through `LV_EVENT_DELETE` on the
screen's root, not by anything remembering to call a teardown function. Set last in
`create()`, because until every widget exists there is nothing safe to write into. After:
**0 of 40.**

**Why neither was found before.** The WLAN screen had been opened exactly once on the
device, straight from the deck, at a moment when the pool happened to have room — and the
screenshot looked perfect. The use-after-free needs the scan to outlive the screen, which
slow deliberate tapping never produces. Both were found by stress-navigating, which is
worth doing on every screen that allocates or that anything writes to asynchronously.

**A note on the fixed pool as a design.** A static pool is chosen for determinism, and
determinism is the right instinct for this device. But it only pays if exhaustion is
handled, and LVGL's is not — it is a NULL dereference in a library function, with a
backtrace pointing at whichever widget happened to be unlucky. Determinism that ends in a
panic is not determinism.

## D59 — The three touch features, confirmed by the person holding the device

Everything in D58 and the radar work was shipped with an explicit caveat: the taps could not
be verified from here. Confirmed working on 2026-09-20 by the user: tapping a mark
re-points the caption, tapping the caption opens the full view, and the Liste scrolls.

Worth recording rather than quietly dropping the caveat, because the split held up exactly
as intended. What a machine could check, a machine checked — 17 of 19 radar marks measured
moving 5–8 px per 10 s by diffing framebuffer captures, 0 crashes in 40 rapid overlay
transitions, memory flat across a three-minute soak on live traffic. What needed a finger
needed a finger. Neither substitutes for the other, and saying "verified" for the half I had
not touched would have been the easy lie.

**Where this leaves the deck.** Three pages, and now three ways in: the hero answers
unprompted, the Liste scrolls to everything in range, and the Radar is a picture you can
interrogate — tap a mark to ask "which one is that", tap the caption to commit. The caption
tap reuses the selection path the list already had, so there is still exactly one "detail
view" (§5.1) and no fourth layout to learn, which was the original argument in main.c and
still holds.

## D60 — Radar is the default, and the hero moved a layer down

**Decision:** the deck is two pages — **Radar, then Liste**. §5.1/§5.2 is no longer a page;
it is the layer beneath both, opened by tapping an aircraft and left by tapping anywhere.

**What this trades away, stated once.** AGENTS.md §1 asks for the answer "in under two
seconds with **no interaction**", and the hero was page 0 precisely because of that sentence.
The Radar answers a different question first — *what is up there* — and names only the
nearest aircraft, in its caption. Destination, distance and bearing are still there without
a tap; airline, type and altitude are now one tap away. Decided by the man who uses it,
after using it, which is better evidence than the sentence in the brief.

**The way out is the whole screen.** There is nowhere on §5.1 for a 44 px button: the chrome
row is 24 px tall and the compass tape starts at y=40, so anything finger-sized either
covers the tape or pushes the hero down. A target you cannot miss beats one you have to aim
at, and since nothing else on that screen is tappable, a tap is never ambiguous. The word
"Zurück" still appears, in the slot the clock used — an invisible affordance is not one, and
on a view he opened on purpose the time is not what he came for.

**Two things the restructure would have quietly removed, and did not.**

The clock lived on the hero screen, which is now a layer down — so making Radar the default
would have taken the clock off the device entirely. It is now top right on the Radar,
balancing the range read-out. Losing a feature as a side effect of moving things around is
not a decision, it is an accident.

And `s_detail_from` remembers which page he opened the layer from, so "back" means back to
the Radar *or* the Liste. Returning always to page 0 would have been one line shorter and
would have moved him somewhere he did not ask to be, which for this user is the same as
being lost (the same reasoning nav.h already gives for overlays).

**Closed when the aircraft leaves.** If the aircraft he is reading about drops out of range,
the layer closes rather than silently swapping in a different one under the same heading.
A panel caught substituting is a panel he stops believing.

**Verified:** 20 open/close cycles leak nothing (−132 B then +52 B, noise either side of
zero), 0 crashes across page and overlay stress, and the layer renders correctly
(Rom → Breslau · Ryanair · 10,2 km östlich). Debug key 'i' added for the same reason
g/e/k exist (D41) — a screen only reachable by tapping the glass is a screen nobody checks.

Also: `lvgl_mem_report()` was printing `lv_mem_monitor()`, which reports zeros now that
LVGL allocates through the system heap (D58). A diagnostic that answers every question with
"0" is worse than none, because it looks like an answer. It reports the largest contiguous
internal block instead — the number that actually decides whether the next keyboard fits.

## D61 — A cell on the PH2.0 header, and why it lives in the stand

**Decision:** the device supports a 3.7 V Li-ion cell as an uninterruptible supply. It is
optional, it is off the critical path, and **it does not go inside the case.**

**The case cannot hold one, and the back cover says so.** The enclosure is 86.5 × 86.5 ×
**14 mm** with the display, the PCB, two USB-C sockets and a 2.0 mm expansion header inside
it. The thinnest cell worth having is 4 mm, and it would sit pressed against the board with
no airflow, held at full charge, behind glass, in a Thai living room — the exact conditions
a pouch cell swells under. Waveshare's own dimension drawing shows a **cutout in the back
cover exposing the PH2.0 socket**, so a cell plugs in from outside with the case still shut.
That is the intended shape: the cell goes in `hardware/desk_stand.scad`, which has to be
redesigned anyway and has never been printed, and 33 g in the base improves the tipping
moment the stand already worries about.

**What the hardware actually does, traced from the board schematic rather than assumed.**
AXP2101 **DCDC1** (pins 23/22/21) is `VCC_3V3`, and `VCC_3V3` feeds the ESP32-S3, the panel,
the GT911 and — this is the one that could have killed the idea — the **AP3032 backlight
boost**. A backlight hung off the USB 5 V rail would have gone dark the moment the cable
came out. It is not; it hangs off 3V3, and VSYS switches between VBUS and BAT by itself. So
this is a power path, not a changeover switch: pulling the cable interrupts nothing.

**The BSP does not touch the PMIC at all.** `grep -i axp` over the Waveshare component
returns nothing, so charging was whatever the chip's power-on defaults and eFuse happened to
say. That is fine while USB is the only supply and not fine afterwards — see TS, below.

**Four settings, four reasons.**

- **REG50[4] = 1, the TS pin out of the charger's decision.** The board wires TS to a plain
  resistor to ground, not to a battery thermistor, and the reset value of that bit comes
  from the chip's eFuse — so whether a cell charges out of the box was decided by a fuse
  nobody here can read. Waveshare's own ESP-IDF example calls `disableTSPinMeasure()` with
  the comment "otherwise it will cause abnormal charging". Without this line the likely
  failure is a device that silently never charges.
- **500 mA (REG62 = 0x0B), which is 0.25C on the 2000 mAh cell fitted.** Deliberately below
  the cell's 0.5C recommendation. The device is on USB roughly 23.5 hours out of 24, so
  charge time is the one variable here that genuinely does not matter, while heat inside a
  sealed 14 mm box does.
- **4.1 V, not 4.2 (REG64 = 0x02).** Costs about 15 % of capacity and buys back most of the
  cell's calendar life. This cell will be held at full charge permanently and kept warm,
  which is the condition that ages and swells them. Four hours minus 15 % is still six times
  the half hour that was asked for.
- **1500 mA input limit (REG16 = 0x04)**, which is also the power-on default — written out
  so the value lives in this repository instead of in an eFuse. A weak charger is handled by
  VINDPM backing the charge current off, not by starving the panel.

**No automatic power-off when the cell runs out.** The tempting move is `REG10[0] = 1` at
some low threshold. It is refused: a fuel gauge that has not learned the cell can read
nonsense (see below), and the failure mode of acting on a wrong number is a device that
switches itself off in front of him with USB plugged in, recoverable only by finding the
PWRKEY on the side edge. The PMIC's own VOFF and the cell's protection board already end the
discharge safely. Firmware warns, dims, and otherwise stays out of it.

**The gauge is not trusted blindly.** AXP2101 has a fuel gauge that learns the cell, and
until it has, REGA4 reports 0. Printing "0 %" beside a cell sitting at 3.9 V is the kind of
wrong that makes every other number on the screen suspect, so 0 and anything above 100 are
treated as *no answer* and an eleven-point open-circuit curve answers instead. A genuinely
empty cell reads ~3.2 V and the curve says 0 too, so refusing to believe the register costs
nothing at the bottom of the range.

**The split is the usual one.** `main/power/axp2101.c` is registers and I²C; everything with
judgement in it — the states, the thresholds, the hysteresis, the German — is
`battery_policy.c`, which builds on the host. 4,165 checks, including a monotonicity sweep
over the whole 2500–4400 mV range, because a curve that dips one percent in the middle makes
a resting cell look like it is recovering charge.

**Two UI decisions.** The badge sits in the chrome strip at the bottom right, in the 24 px
band `screen_list.c` already keeps its rows out of, and it appears **only while actually
running on the cell** — a badge that is always there is chrome he stops seeing, and this one
has to be noticed the one time it matters. Grey down to 21 %, amber at 20 % and under; the number carries
the meaning either way (DO-257A §2.1.6). It is not on the hero layer, which answers the
question the device exists for and does not need a fifth thing on it. And Einstellungen
gains an **Akku** line, which reads "Kein Akku" on a device with no cell — that line is the
whole reason the section exists, because on the day a battery is first fitted it is the only
thing on the device that can say whether the plug went in and whether the charger took it,
without a laptop and a serial cable.

**A cold start from battery alone needs the PWRKEY.** Datasheet §6.5.2: with only a battery
present the BATFET is off until the key is pressed or an adapter appears. Unplugging a
running device is seamless; starting a flat one is not, and no register changes that.

**Verified on the unit, 2026-09-20, with no cell fitted.** Every configured register read
back correct over the console (`y`): REG50 = 0x12 (TS ignored), REG62 = 0x0B, REG64 = 0x02,
REG16 = 0x04, REG18 = 0x0A, REG30 = 0x0D, REG68 = 0x01, and REG03 = 0x4A — the AXP2101 chip
ID, which is the identification this driver logs and deliberately does not gate on. REG00 =
0x20 and REG01 = 0x15 read as "VBUS good, no battery, not charging", and the policy reports
`-1 %` and "Kein Akku" rather than inventing a zero. The panel stayed up across the whole
configuration write. 40 rapid Einstellungen/WLAN navigations with the poll running: zero
crash markers (AGENTS.md §11 rule 3). Suspend to a fixture and resume rebuilds the badge —
confirmed by counting amber pixels in the framebuffer, because "it looked right" is not a
measurement.

**A pretended battery, on the 'Y' key.** The badge, the amber caution and the backlight cap
can otherwise only be seen by flattening a real cell, which takes hours and could not be
done at all before one existed. That is D41's argument exactly — a screen reachable only by
an hours-long physical event is a screen nobody checks — and this one is a warning, the
single element on the device that has to be right the first time it ever appears. It cycles
60 %, 18 %, 5 %, off; nothing persists it and a reboot clears it.

**What is NOT verified: the cell itself.** No battery has been connected to this board. The
charge current, the 4.1 V termination, the runtime and the gauge's behaviour on a real cell
are all still calculated numbers, and AGENTS.md §2's power figures are derived from the
schematic (200 mV over the 5.1 Ω sense resistor, so 39 mA through the backlight string), not
measured. The firmware logs one line a minute while discharging so the first unplugging
produces the real curve without anyone having to remember to measure it.

**Amended the same day, by the owner: no desk stand.** The cell is taped to the back of the
case with double-sided foam tape and plugged into the cutout the back cover already has. The
argument above is unaffected — it was never "a stand is needed", it was "the cell cannot go
*inside* the case", and that stands. What changes is that `hardware/desk_stand.scad` is no
longer pending work and the runtime measurement no longer blocks anything. Practical notes
worth keeping: foam tape, not cyanoacrylate, which attacks the pouch; no clamping or
folding; slack in the lead so the plug is not carrying the cell; and low on the back rather
than centred, where 10 mm of cell leans the panel back instead of letting it rock.

## D62 — The long press into Einstellungen had never worked

**What was wrong:** you could not reach the settings screen by touching the panel. Not
since the screen was built. The only way in was the serial console's 'e' key, which is why
nobody noticed — every check of that screen in this repo went through the build host.

**Why.** The long-press handlers are bound to the tileview, which sits *underneath* every
page. In LVGL 9 the `lv_obj` constructor sets `obj->clickable = 1`, so the full-screen
container each screen creates for its own layout (`screen_radar.c:447`,
`screen_list.c:832`) is a hit target in its own right — and LVGL passes an event to a
parent only when the child carries `LV_OBJ_FLAG_EVENT_BUBBLE`. Every press landed on the
page's own background and stopped there. Swiping between pages kept working the whole time,
which is exactly why this looked like a threshold problem rather than a plumbing one:
scrolling searches UP the parent chain for a scrollable ancestor, clicking does not.

**The first fix was too timid, and the device said so.** Bubbling one level — the page's
direct children — still did not work, because the radar draws its range rings as
`lv_obj_create()` circles and a 400 px ring's bounding box covers most of the scope. Almost
every press "inside the circle" landed on scenery two levels down.

**The rule that came out of it:** `lv_obj_get_event_count() == 0` means nothing was ever
wired to this object, so it is scenery, and scenery passes touches on. `bubble_decorative()`
walks the page and sets the flag on those, stopping at anything that does have a callback —
an aircraft caption, a list row — which keeps its own taps along with everything inside it.
Bubbled events stop at the tile: the flag is deliberately NOT set on the tile itself, so a
scroll inside a page can never reach `lv_tileview`'s own `LV_EVENT_SCROLL_END` handler
(`lv_tileview.c:147`) and snap the deck to another page.

**`nav_touch_report()` on the 'x' key exists because of how long this took to see.** A long
press cannot be triggered from the build host, so "he did not press" and "the press never
arrived" are the same observation, and I guessed at which one it was twice. The device now
counts presses, long presses and how long the last one was held, so the question is settled
by reading a counter rather than by asking someone to try again while I listen.

**Verified by him, on the glass:** 4 presses reaching the deck, 2 long presses recognised,
the last held 1260 ms against the 1200 ms threshold, and the overlay open when the counter
was read. The counters were 0 immediately after the flash, so those are his fingers.

**The part worth remembering.** The commit before this one fixed the long-press *threshold*
— carefully, with reasoning about keyboards and scroll suppression — on a code path no
finger has ever reached. When he said he still could not get in, the natural reading was
"1.2 s is too long" and the correct reading was "no event arrives at all". AGENTS.md §11
rule 1 is about comments that drift from code; this is its cousin: a handler bound to an
object the hardware never hands anything to looks exactly like working code.

## D63 — "Eigener Ort" gets a place search, and the geocoder picked itself

**What was wrong:** the settings screen had a fourth location card, "Eigener Ort", and no
way on earth to set it. `custom_lat`/`custom_lon` were only ever written by
`settings_defaults()`, so the card showed Gloggnitz's coordinates and would have shown them
forever. Tapping it selected a preset that pointed at the same place as the card above it —
except for the clock, which jumped two hours, because the preset table gave `LOC_CUSTOM` a
timezone of `"UTC0"`. A card whose only effect was to break the clock.

The TODO beside it was not lazy; it was half-right. It argued that a numeric keypad is
exactly the "coordinate entry form" AGENTS.md §6 forbids, and it is. What it missed is that
a keypad is not the only alternative to a form. **You set a location by searching for it by
name.**

**Which geocoder, and why it was not a matter of taste.** Measured on 2026-09-20 with this
project's own User-Agent:

| | plain HTTP | street addresses | timezone |
|---|---|---|---|
| Open-Meteo geocoding | **`200`, no redirect** | no | **yes, IANA** |
| Nominatim (OSM) | `301` → https | yes | no |
| Photon (komoot) | `301` → https | yes | no |

The plain-HTTP column decides it. AGENTS.md §4 has no TLS on the data path because a
handshake wants ~40 KB of internal heap on a board with ~24 KB free, and that decision is
load-bearing for everything else in the network layer. The cert bundle *is* linked, for
OTA — so an HTTPS geocode was technically available. It was still the wrong trade for a
capability that buys 600 m of precision inside a 55 km radius.

Because the second column is the one that looks like a loss, say plainly what it costs:
**this finds places, not addresses.** "Gloggnitz" resolves; "Semmeringstraße 11" does not.
At a 30 nm default radius, the difference between a town centre and a house on its edge
changes which aircraft come back not at all, and `adsb.lol`'s bearing to a target 15 km out
moves by about two degrees. Markus was asked and chose this over the address search he had
originally asked for, once the numbers were on the table.

The third column is the one nobody asked for and it may be the most valuable. AGENTS.md §6
says the timezone follows the location and he never sets a clock. A searched place has to
keep that promise, and Open-Meteo hands over `"Europe/Vienna"` with every hit.

**Which newlib cannot use.** ESP-IDF carries no zoneinfo database; `setenv("TZ", …)`
understands only a POSIX rule like `CET-1CEST,M3.5.0,M10.5.0/3`. So there is a table, and
it is **generated, not typed**: `tools/build_tz_table.py` reads the POSIX footer out of each
TZif file in the system tzdata (RFC 8536 §3.3) — the exact string the people who maintain
those rules for a living wrote. 114 zones, 48 distinct rules, ~7 KB. Typing them by hand
means typing `M3.5.0/3` correctly sixty times and then being wrong about Israel, which
changes its DST rule by government decision. Anything outside the table falls back to a
whole-hour offset from the longitude, which is deliberately a dumb answer: it can be an
hour out in a DST country, and it never pretends to know a rule it does not have.

`Europe/Vienna` resolving byte-identically to the string AGENTS.md §6 already carried for
Gloggnitz is pinned in `test_geo.c`, because a searched Austrian place running a different
clock from the preset beside it on the same screen would be absurd.

**The NVS blob had to grow, and that is the dangerous part.** `custom_label` and
`custom_tz` moved every field after them, so the blob changed size and the load path's
`len == sizeof blob` test would have rejected it — a device that takes this update comes up
on defaults, back in Gloggnitz, brightness reset, with nothing on screen to say why. There
is exactly one such device and for half the year it is 9,000 km from anyone who could fix
it. So version 1 is transcribed into `settings.c` verbatim and migrated field by field.

The migration also came out from behind `#ifndef HOST_TEST`, where nothing could test it:
`settings_decode_blob()`/`settings_encode_blob()` are pure and `settings_load()`/`_save()`
are now thin wrappers that only move bytes. Fifteen new checks cover a v1 round trip, both
discriminators, a right-size-wrong-version blob, a wrong-size-right-version blob, and 512
bytes of `0x7F` wearing a valid version word. What the host suite still cannot prove is that
`settings_v1_t` matches the bytes a device actually wrote in M6 — only the device settles
that, and it did: it came up reporting `brightness=45%`, which is neither a default nor a
clamp bound, so those are his settings and not a reconstruction.

**Two lifetime bugs a review found afterwards, both the same shape.** The first version
kept the query and the result array in file scope, under a comment asserting that only one
search could ever be in flight. That was wrong by one finger: "Neu suchen" puts the Suchen
button back on the glass while the previous request is still out, and `geocode.c` waits ten
seconds before giving up — so two tasks could parse into one array. And a lookup that landed
after he had tapped Zurück and reopened the screen painted into the NEW instance, because
`s_alive` is true again by then and `nav_overlay_open()` cannot tell one overlay from
another: the keyboard he was typing on would flip to a result list for a question he had
stopped asking.

Both are now one mechanism. Each search gets its own `geo_job_t` — query, results and the
autopick intention — allocated by the starter, freed by the task, and stamped with a
generation number that only the search the screen is still waiting for can match. The
autopick flag moved INTO the job for the same reason: as a file-scope flag cleared by the
task it left a narrower version of itself, because a stale search is not allowed to act on
the flag and therefore never cleared it, arming whatever he started next with his own
finger.

**Not done, deliberately:** no type-ahead. One request per tap on Suchen, and nothing polls
the endpoint. Also no umlaut keys — `?name=Munchen` finds München, measured, and a keyboard
layout is a bigger change than this feature deserved.

## D64 — The WLAN keyboard had never been on the screen

**What was wrong:** `lv_keyboard` places itself. Its constructor calls
`lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, 0, 0)`, and in LVGL 9 an object's x/y become an
offset *from its alignment* the moment one is set. So `lv_obj_set_pos(kb, 0, py)` — which
is how every other widget in this codebase is positioned, and which reads as obviously
correct — asks for a keyboard `py` pixels **below the bottom edge of the panel**.

`screen_wifi.c` has done exactly that since M6. The password step rendered perfectly: the
network name, the field, the show/hide toggle, Verbinden and Abbrechen, and no keyboard,
with no way to type a password into it and nothing logged anywhere.

**Why nobody found it.** That step needs a finger on an *unknown* network. Every check of
the WLAN screen in this repo went through the build host, and the build host stops at the
network list — `tools/grab_screen.py` can photograph any screen it can reach, and it could
not reach this one. D58 hardened that same screen against two panics found by stress
navigation; neither pass ever got as far as looking at the password step.

It surfaced because the Ortssuche keyboard hit the identical wall one screen away, where it
*was* reachable from the host, and the framebuffer came back with 240 px of black where a
keyboard should be.

**What came out of it.** Three things, in the order they were found by reading pixels back
rather than by reading code:

1. `lv_obj_align()`, not `lv_obj_set_pos()`, for a keyboard. Both screens.
2. LVGL's default theme is **light**, and an unstyled keyboard is a near-white slab across
   the bottom of a panel whose ground is `#0A0B0D` specifically because pure black
   maximises halation for aging eyes (DESIGN.md §2). On a device that dims itself at 22:00
   to avoid exactly that.
3. Styling `LV_PART_ITEMS` darkens the letters and leaves **nine near-white control keys**
   sitting among them — `lv_keyboard` marks shift, `1#`, `ABC`, backspace, enter, close and
   the two cursor keys `LV_BUTTONMATRIX_CTRL_CHECKED`, and the default theme gives
   `LV_STATE_CHECKED` a style of its own. And every `lv_button` carries a default shadow
   that renders here as a 2 px `#525152` band — a grey line under every list divider and a
   grey column down both edges of a list. Probed out of the framebuffer at x=22, y=199;
   nothing in the source asks for a shadow, so nothing in the source looks wrong.

The styling lives in `main/ui/widget_input.c`, shared rather than copied — a deliberate
break from the `make_label()`/`make_button()` convention next door. Those are eight lines
and it does not matter if two screens differ by a pixel. This is thirty lines of colour
that must be identical on both, because it is the same keyboard on the same device and a
man who has learnt one has learnt the other.

**The part worth remembering.** AGENTS.md §11 rule 1 is "the comment had drifted from the
code, and the review believed the comment". This is its cousin, and D62's: **code that
reads correctly and was never once executed on the glass.** D62 was a long-press handler
bound to an object no finger could reach; this is a keyboard positioned off the edge of the
world. Both survived every review and every host test, and both took a photograph.

The 'q', 'Q', 'z' and 'Z' console commands exist because of it. The Ortssuche screen's
result list, its two failure states and the whole pick-a-place-and-move-the-device path can
all be driven from the build host now — D4 and D41's rule applied to a screen whose
interesting states otherwise need a finger and a broken network.

---

## D65 — The keyboard is German, and it takes two fonts to draw it

**2026-09-20.** M10 shipped a place search with LVGL's stock keyboard on it: US QWERTY,
with `_ - . , :` filling the bottom letter row. For a man searching for the town he lives
near, that is five keys he will never press and two he needs — ö and ä — that are not
there at all. `Munchen` does find München (measured: Open-Meteo folds accents), so this was
a nice-to-have rather than a defect, and it was offered and asked for.

**The layout.** German QWERTZ, umlauts where a German keyboard has always had them: ü right
of p, ö ä right of l, ß on the bottom row. Four rows, **every row adding up to 11 units**,
so the columns line up down the whole keyboard — LVGL's own rows come to 52, 40, 12 and 14,
and at 480 px the ragged grid is visible. The cursor keys and the close-keyboard glyph are
gone: both screens carry a 64 px Zurück/Abbrechen **in words** above the keyboard, which is
an easier target than a key and cannot be mistaken for the tick beside it, and `lv_textarea`
moves the cursor when he taps into the text. That bought the width for a bottom row of
three large targets. The `1#` layer is left as LVGL's own, because a WPA passphrase is
arbitrary ASCII and that layer is what makes it typeable.

**Neither font can draw this keyboard.** LVGL's built-in Montserrat is generated with
`-r 0x20-0x7F,0xB0,0x2022` plus FontAwesome — read off the top of the generated file, not
assumed — so it has **no umlauts**, and a `ü` key drawn in it is a key with nothing on it.
The Plex subset has the umlauts and none of the `LV_SYMBOL_*` private-use codepoints, so a
backspace drawn in Plex is a key with nothing on it either. The same AGENTS.md §7 trap from
both directions, and in both directions the key still works when pressed; only the label is
missing.

The way through is a per-state font. `lv_buttonmatrix` re-initialises `LV_PART_ITEMS`'s
label descriptor **per button, with that button's own state** (`lv_buttonmatrix.c`,
`draw_main`), and every control key carries `LV_BUTTONMATRIX_CTRL_CHECKED`. So
`LV_PART_ITEMS` gets `plex_sans_cond_34` and `LV_PART_ITEMS | LV_STATE_CHECKED` gets
Montserrat 24, and each key is drawn by the face that has its glyph. That is not a
documented feature; it is a read of the draw loop, and then a photograph.

**Two things about it are fragile, and both are checked rather than trusted.**

`lv_keyboard_set_map()` is **process-wide**: it writes into a file-scope table inside
`lv_keyboard.c` that every keyboard reads at redraw. Two keyboards cannot have two layouts
in this LVGL. Here that is exactly what is wanted — widget_input.h's whole argument is that
the WLAN keyboard and the Ortssuche keyboard must be the same keyboard — so the installer
is called from the styling function and the two can never come apart.

And the three layer keys are a **contract with LVGL spelled by hand**:
`lv_keyboard_def_event_cb()` decides whether a key switches layer by comparing its cap text
against `LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_LOWER` / `_UPPER` / `_SPECIAL`, macros
`lv_keyboard.c` keeps to itself. Get one wrong and nothing warns: the key stops switching
and starts typing `ABC` into the search field. So `widget_keyboard_debug_layer()` presses
each of the three through the real event path and the `a` console key photographs the
result — abc → ABC → 1# → abc, with the search field staying empty, which is the actual
check.

**The string gate got a rule, not thirty exemptions.** Thirty of the thirty-six key caps
are a single letter, four of them umlauts. Putting them in `main/strings_de.h` would bury
the sentences in the lexicon that `--list` exists to print for a native speaker, and would
dress up `"q"` as a translation decision. `is_single_letter()` says: exactly one character,
and it is a letter. No German word is one letter long. Proved it still bites by injecting
`"ja"` as a key cap (caught) and `ő` as one (caught by the font gate instead). It also made
four `_NON_UI_LITERALS` entries stale — `"N"`, `"E"`, `"S"`, `"W"` — and the stale-exemption
warning said so out loud, which is what that warning is for.

---

## D66 — One moving thing, and the states it is not for

**2026-09-20.** Three screens wait on the network in front of him, and all three said so in
words alone: *Suche Netzwerke…*, *Suche Orte…*, *ROUTE WIRD GESUCHT*. A sentence says what
is happening. It cannot say that anything **still is**: static text looks identical two
seconds in and twenty seconds in, so a slow answer and a dead device are the same picture,
and he taps again. AGENTS.md §1 says never a silent panel; a frozen sentence meets that on
the letter and misses the point.

So: **one 4 px cyan line, the same one everywhere, and nothing else on this device moves.**
On a panel where nothing else ever animates, movement does not have to be labelled. The
rule is DESIGN.md §4 "Motion"; the implementation is `main/ui/widget_busy.c`, shared for the
same reason `widget_input.c` is — three copies would be three decorations instead of one
idea.

**It sweeps inside the track and never leaves it.** The first version did what a phone
does: one-way from off the left edge to past the right one, eased, repeating. Measured
across three frames, **two of them caught the segment at x=439 of 440, with one pixel
showing.** That is not a sampling fluke — an ease-in-out is slowest at the ends of its
travel, and at that end most of the segment is outside the track, so for roughly a quarter
of every cycle the bar is a blank line. Which is the one thing a "still working" indicator
must never look like, and precisely the impression it exists to prevent. Travelling
`0 → track_w - ind_w` with a reverse leg keeps the whole segment on the track at every
instant and removes the jump back to the start as well.

**Skeleton rows say where, the bar says whether.** On a list that is empty because the
answer has not arrived, ghost rows stand exactly where the real ones will, same height,
uneven widths — three bars of equal length read as a finished graphic rather than as text
that has not come yet. Nothing pulses or shimmers: the bar is the one moving thing, and a
skeleton that breathes turns a calm wait into a busy one. They are for an **empty** list
only; a rescan over networks he can already read keeps them and shows the bar alone,
because replacing a list he is reading with grey bars throws away what he has and tells him
nothing.

**The bar goes in the gap that was already there.** On the Ortssuche the hit list is sized
so that three rows and a sliver of a fourth are visible, and that sliver is the only thing
telling him the list continues past the fold. Twelve pixels spent on a bar of its own would
have bought a list that ends on a clean row edge and looks complete when it is not. Four
pixels inside a sixteen-pixel gap costs nothing.

**And it is for a wait with an end, never for a condition.** §5.3's *Kein Netz — Ich suche
ein bekanntes WLAN* deliberately has no bar. That state can last all night, and a bar that
sweeps until morning stops meaning "still working" and starts meaning "this device
animates" — besides burning a redraw a frame on a panel that dims itself at 22:00 to save
power. One request in flight gets a bar. A standing condition gets a sentence.

---

## D67 — The stress run found two crashes and the camera was lying

**2026-09-20.** Three of M11's four verification findings were about the verification, not
the feature. They are written down together because they are one lesson.

**`nav_create()` never cleared `s_overlay`.** Every caller has just run `lv_obj_clean()` on
the active screen, which deletes an open overlay along with everything else — but nothing
told `nav.c`, so the pointer was left dangling and the next `nav_open_overlay()` dealt with
it by calling `lv_obj_delete()` on freed memory. LoadProhibited inside
`lv_obj_get_parent()`. Reachable today from the serial console: open any overlay, press `0`
to restore the live view, open one again.

**`screen_overhead.c` had no `s_alive` guard** — the one `screen_wifi.c` and `screen_geo.c`
both carry, and which both of them got by panicking first (D58). It never seemed to need
one because nothing wrote into it from another task. Something does now: the fixture
commands. It is only ever built as the detail layer, so the same `lv_obj_clean()` left every
pointer in the file dangling and `dbg_fixture_show()` then called `lv_label_set_text()` on a
freed label.

Both are pre-existing, both are console-only, both are two lines, and **neither was found
by reading.** They were found by 25 cycles of tearing screens down while something was
animating on them — which is AGENTS.md §11 rule 3, and which is also how both WLAN panics
were found.

**The camera was lying.** `tools/grab_screen.py` triggers `dbg_screen.c`'s capture, which
reads `esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb)` — the FIRST buffer, of two. LVGL
renders into them alternately, so after a single redraw buffer 0 still holds the frame
**before** the change. Every screenshot of a screen that had just been changed and then gone
still was one state out of date, and said nothing about it: the image is a perfectly valid
picture of the wrong moment.

It went unnoticed for as long as it did because it only bites a **static** screen. Anything
animating redraws continuously, both buffers converge within a frame or two, and the grab is
correct — so M11's loading states photographed correctly while the no-route fixture beside
them came back twice showing the state before it, and the only reason that was caught at all
is that the two pictures disagreed with the log line between them. The capture now
invalidates the whole screen once per buffer before reading.

**And one of the four was mine, in the same session.** The first version of
`geo_demo_states()` asked `nav_overlay_open()` before opening the screen — "is SOME overlay
up", which is the exact question PLAN.md M10 records `geo_demo_search()` getting wrong three
weeks of work earlier. With Einstellungen open it would have skipped opening the Ortssuche,
set three states on a screen that was not there, and reported three states drawn. It now
asks `screen_geo_is_up()` and logs `SCREEN NOT UP` when the answer is no.

Four harness bugs in one feature, every one of them reporting a pass. **A harness bug reads
exactly like a passing test** — and the corollary M11 adds is that the harness deserves the
same suspicion as the code, including the camera.

---

## D68 — A fix that only the panel could tell you was wrong

**2026-09-20.** A review of M11 found four things. Two of them this milestone had not
caused but had made **silent**, which is worse than causing them.

`ui_resume()` — the `0` console key — rebuilds the deck after a debug view has had the
screen, and it already knew to forget the battery badge memo, because the deck it had been
drawn on is gone. It did not know to forget `s_detail_open`. The `lv_obj_clean()` two lines
above deletes the detail overlay, so `ui_task`'s update branch went on choosing
`screen_overhead_update()` for a screen that no longer existed. Before M11 that wrote
through freed labels and the device rebooted, which at least announced itself. With
`screen_overhead.c`'s new `s_alive` guard (D67) it returns quietly — and **neither Radar nor
Liste is ever updated again.** The panel just stops moving, with nothing in the log. The
same guard that made the crash impossible made the freeze invisible.

The fixture keys `1`–`5` had the same shape: §5.1/§5.2 is the detail *layer*, not a deck
page, so unless it happened to be up the fixtures wrote into a screen that was not there
and `dbg_fixture_show()` still logged the hero line it had not drawn. A check reporting a
state it never rendered — the harness bug this milestone already found three times. They
open the layer they need now rather than assuming a finger did.

**The third was mine and it was real.** "Neu suchen" did not cancel anything. It put the
typing sheet back up while the previous request was still on the wire, and geocode.c waits
ten seconds before giving up — so the answer to a word he had abandoned arrived mid-
keystroke, called `show_results()`, and took the keyboard out from under his fingers.
`show_typing()` had a comment saying the wait was over; it had only stopped the animation.
The generation counter next door does not reach it, because tapping Neu suchen is neither a
new search nor a re-open. `s_awaiting` is the missing half: the screen answers a question
only while it is still asking it.

**And then the fix broke two console commands, and only the device said so.** `Q` and `z`
call `on_geo_search()` directly instead of going through `do_search()`, so they send a
request without ever telling the screen a question was asked. With answers now being
dropped when nothing is waiting, both silently drew nothing — the panel sat on the typing
keyboard through a whole request-and-timeout cycle while the log reported a completed
lookup. It builds, the gates pass, the host suite passes, and the feature is dead. Nothing
short of flashing it and looking would have caught that, which is the entire argument for
D4 and D41 in one sentence.

**The race is built, not raced.** Three attempts to abandon a search from the host failed:
the endpoint at this location refuses in ~100 ms, and the second keystroke arrived 33 ms
late every time — including as a single unpaced burst, which is M10's own remedy. So `C`
holds the display lock across both steps. The search task has to take that lock before it
can paint, so it cannot slip an answer in between "started" and "abandoned", and the
scenario is constructed rather than gambled on. A harness that cannot win its race reports
a pass and has exercised nothing.

**The fourth finding was doc drift, and it got a gate.** The header block and the boot
`ready:` line both still described a console this firmware no longer has. That is AGENTS.md
§11 rule 1 pointed the other way — not a comment that lies about what the code does, but a
comment that never learned what the code gained, which puts the screen behind an
undocumented key back to being one nobody checks. Two consecutive reviews had found those
two blobs stale, so they are now parsed and compared against `on_cmd()` by
`tools/check_console_keys.py`, in the host suite with the other two gates. It found four
more omissions on its first run that nobody had reported: `u`, `i`, `p`, `t` and `v` had
never been in the ready line, and `u` had never been in the header at all.

**The pattern across D67 and D68 is one thing.** Every safety net added in this milestone —
the `s_alive` guard, the dropped stale answers — converts a loud failure into a quiet one.
That is the right trade for a device in somebody's living room and the wrong one for a
build host, so each of them has to arrive with a way to see the quiet case: a log line that
says which branch was taken, a command that constructs the state, or a gate. A guard
without one of those does not remove a bug, it removes the evidence.

---

## D69 — The WLAN screen was telling three different untruths

**2026-09-21.** The owner looked at M11's screenshots and said the WLAN states did not seem
right. Four things were wrong, and only one of them was new.

**Tapping a network never reported an outcome.** `screen_wifi.h` documents
`screen_wifi_set_status()` as the way to tell the screen what happened, and every caller of
it was in the scan path — not one in the join path. So after a tap the status line sat on
"Verbinde mit X..." for as long as the screen stayed open, whatever actually happened. That
was survivable as a stale sentence for four milestones. Then M11 swept a progress bar under
it, which is the same claim made far more confidently and never ends, and what had been a
wart started looking like a fault. D66 says the bar is for a wait with an end; this is that
end, and the bar is what finally made its absence visible.

**And the tap did nothing at all when the device was online.** `wifi_reconnect_now()` sets
`g_force_retry`, and the only place that read it was the backoff wait — which lives inside
`if (!g_connected)`. Connected, the flag was set and never looked at again. So the whole
gesture was inert in exactly the situation it exists for (AGENTS.md §6): he lands, the
flat's router is remembered but weak, the device is clinging to whatever it found first,
and he taps the right one. An explicit pick now outranks "already associated" — the loop
drops the association and re-scans, which costs a 2 s backoff if it fails.

The watcher reports **the network the device actually ended up on**, read back from the
driver, not the one he tapped. wifi.c picks whichever remembered network is in range rather
than obeying a specific SSID, so those two genuinely differ, and "Verbunden mit X" while
sitting on Y is the kind of confident wrong answer this panel must never give.

**A failed scan was reported as an empty one.** `wifi_scan()` returns -1 when the radio
could not look, and the screen clamped that to zero, producing "Keine Netzwerke gefunden" —
telling a man sitting next to his own router that no networks exist. The Ortssuche one
screen away has separated those two answers since M10 (STR_GEO_NONE vs STR_GEO_FAILED) for
precisely this reason: one is a fact about the world, the other a fact about the device, and
only one of them is worth tapping Suchen over. A `-1` turned up in the wild within minutes
of the new log line going in. On a failure the list is now left exactly as it was, because
nothing was learned.

**And the skeleton promised the wrong shape.** `update_row()` draws a saved network as a
filled, fully bordered card and an unsaved one as a transparent row with a hairline under
it. The ghosts drew rounded boxes outlined on all four sides — neither — so the list
visibly changed construction at the moment the answer arrived. `screen_geo.c` takes care
over exactly this; it was missed here because that screen's rows are all one shape and this
screen's are two.

**A fifth thing fell out of fixing the first, and the first fix for it was wrong.** The
status line had no width and no long mode, so "Verbindung fehlgeschlagen:
Apartamentos_Jose_Cruz" ran straight off the right edge of the panel. It had been that way
since M6 and could not have been seen before, because nothing ever put a long sentence into
it — the failure and success lines had no caller.

Setting the width and asking for `LV_LABEL_LONG_MODE_DOTS` looked like the complete
gesture. It was not, and the result was worse than the overflow: LVGL only ellipsises when
the text is taller than the OBJECT (`lv_label.c`: `size.y > lv_area_get_height(&txt_coords)`),
and the height was still `LV_SIZE_CONTENT` — so the label grew a second line instead, and
the network list, laid out once from the label's height at build time, was drawn straight
over it. The owner photographed it: "Verbindung fehlgeschlagen:" on one line, the SSID on a
second, and a green card sitting on top of the second. Pinning the height to one line is
what makes DOT mode do its job, and laying the band below out from the font's line height
rather than the label's measured one means nothing there can move again.

Worth noticing how it failed: the fix was written, built, gated, flashed, and the screen was
photographed — and the photograph was of a state that did not contain a long enough string
to show the bug. Verification that does not put the failing input in front of the code is
not verification, however many pictures it produces.

**A sixth thing, found while answering "why does my phone work and this not".** The answer
turned out to be measurable and the tools were not measuring it.

`wifi_scan()` threw the RSSI away, so the one number that answers the question was not
obtainable from the device at all. It now logs each SSID with its signal and channel. Here:
**-76 to -81 dBm**, and the same SSID appearing on channel 1 AND channel 11 — a repeater
pair. A phone showing "two of three bars" is not disagreeing; Android maps roughly
-55…-85 dBm onto that scale, so two bars IS about -78. The phone simply has two or three
antennas with diversity and beamforming to talk to, against this module's single chip
antenna, which is a real 5-10 dB and at -78 dBm is the difference between fine and marginal.

**And the link probe was measuring the case that does not fail.** `probe_link()` fetched a
5 nm query into a 1 KB buffer and reported fifteen cheerful "ok"s at 120-680 ms about a
device that had not shown an aircraft all day. It now issues the IDENTICAL request the
poller does — same URL built from the same settings, same 16 KB buffer, same timeout, with
the byte count printed — and the picture inverts: **10 to 27 seconds per request, and a
third of them never completing at all, against a 10 s socket timeout.** The responses are
only 1-3 KB, so this is not bandwidth; it is retransmission on a link that is associated and
barely working. `POLL_BUF_SZ` and `POLL_HTTP_TIMEOUT_MS` moved into flight_source.h so the
probe uses the numbers themselves rather than a copy of them.

**And the probe's headline number was still wrong, the other way.** It fires fifteen
requests two seconds apart at an API documented to throttle at about the seventh
(AGENTS.md §5), so it trips that throttle on every single run — and counted each 429 as a
link failure. It reported "8/15 succeeded (53%)" on a run whose four consecutive real
fetches took 877, 970, 1065 and 966 ms and returned 13.7 KB each. A perfectly healthy link,
reported as half broken, by the one number the command exists to produce. Throttled attempts
are now counted and printed separately, and the rate is over the attempts that actually
reached the API: the same link then reads **6/6 (100%), 9 throttled, mean 898 ms**.

Two wrong headline numbers from one diagnostic in one session, in opposite directions.

**What the numbers actually say.** At -74 to -76 dBm the real request takes about 900 ms and
returns 15 KB. At -80 dBm it takes 10 to 60 seconds and mostly does not finish. That is a
five-decibel swing across the cliff edge of 2.4 GHz, not a gradual degradation, and it is
why the panel alternates between full and empty in the same room. `POLL_HTTP_TIMEOUT_MS`
went from 10 s to 25 s (and the route POST from 8 to 20) because requests measured at 10-27 s
were being cut off by their own deadline — a longer timeout issues FEWER requests, so it
cannot annoy the API §5 protects. It is reasoned from the measurement; it has NOT been shown
to raise the success rate, because the link recovered to 900 ms before a fair before/after
could be taken.

That is the whole answer to the owner's question, and neither half of it could be measured
before: the radio hears the AP about as well as his phone does and has a far worse antenna
to hear it with, and the request the device depends on takes twice its own deadline. The
backoff and the reconnect-reset were both already correct and were never the problem —
which is exactly what a diagnostic that exercises the wrong case costs you.

**What this says about the milestone.** Every one of these except the ghost shape predates
M11 and every one of them was invisible until M11 made it visible: a bar that never stops
exposes a wait with no end, an outcome that gets reported exposes a label that cannot hold
it, a log that prints the sign exposes a -1 that was being rounded to "nothing here". D66
claims the bar is honest about whether the device is working. That honesty is load-bearing
in both directions — it also refuses to hide the places where the device was not working at
all.

**And what it says about the check.** Four of the five were reachable only by standing in
front of the panel and tapping, which is why a person found them and the reviews did not.
The two that were then testable from the host got a key (`j`) so they stay testable. The
four that need an access point willing to associate are recorded as unverified rather than
claimed, because at this location none would.

---

## D70 — The corner says what the radio hears, on a ladder measured off this radio

**2026-09-21.** The owner asked for a small, unobtrusive WLAN signal icon at the top right
of the screen "so one can keep an eye on it", and for the signal strength of each network
on the WLAN settings screen. Both are the same question D69 ended on — *why does my phone
have reception and this thing not* — asked as a standing instrument instead of a one-off
measurement.

**The ladder is not the textbook one.** Four bars, but the boundaries come from this
device's own radio rather than from a table: at **-74 to -76 dBm** the real 16 KB aircraft
poll completes in about 900 ms, and at **-80 to -81 dBm** the identical request takes 10 to
60 seconds and usually does not finish (D69). So the 2 → 1 boundary sits at **-79**, which
makes **one bar mean "measured unusable on this hardware"** rather than "weak but fine".
That is the only fact the meter exists to deliver: when the screen stops filling, the
corner says why, and the answer is to move the device — not to go and look at the router.

The thresholds live in `main/data/wifi_bars.c`, which touches no `esp_*` header, so
`test/host/test_wifi_bars.c` pins them: the rungs, the -76/-81 pair on different rungs
(stated as a test, because that pair is the whole point), monotonicity across the plausible
window, and that a nonsense reading is **not** clamped into a plausible one. A meter that
rounds nonsense into an answer is how a wrong number survives.

**Zero bars is reserved, and gets a stroke.** Zero means "there is no link", never "the
signal is weak" — a network a scan reported is one the radio heard, so every row of the
WLAN list is at least one bar. And an empty ladder alone was not enough: four dark bars at
18 px in a near-black corner are indistinguishable from the icon not being there, and "the
icon is missing" and "the device has no network" must not look the same. So no-link draws
an amber stroke through it. The stroke is a *shape*, so it carries on its own; amber only
agrees with it, and amber is already this device's "no network" (DO-257A §2.1.6).

**One object, not five.** The meter is a single `lv_obj` with a draw callback — the idiom
`screen_radar.c` uses for its aircraft marks — because the WLAN list is a pool of
twenty-four rows and four child rectangles each would be ninety-six more objects on a board
with ~24 KB of internal heap. The state lives in the caller (a static in `nav.c`, a field of
the row struct in `screen_wifi.c`), so there is nothing to allocate on the display task and
nothing to free when LVGL takes the tree apart.

**`W` cycles a pretended signal**, the same argument `Y` makes for a pretended battery:
four of the five states are a property of where the device is standing, and walking out of
range to check that the amber stroke appears is not a test anyone runs twice. The four
levels are the measured dBm figures, not round numbers, so what is on the glass under the
simulation is what is on the glass at those readings.

**The scan is now sorted strongest-first, which also decides which duplicate survives.**
This flat's network is on channel 1 and channel 11 — a router and a repeater — and the
de-duplication keeps the first sighting. Unsorted, "first" meant whichever the radio
happened to report, so the list could show the far end of the flat while the near one was
15 dB stronger.

**And it turned up two layout bugs that had been on the glass since M6.** Fitting a meter
into the WLAN row narrowed the SSID label, and the name of the network this device is
actually on — `Apartamentos_Jose_Cruz` — broke across two lines inside a 64 px row instead
of ellipsising. That is D69's own trap a second time: `LV_LABEL_LONG_MODE_DOTS` only fires
when the text is taller than the object, and the label had a width but no height. Pinning
it then exposed the second: the ellipsis was drawn straight through the green tick beside
it, because `lv_button` arrives carrying the default theme's padding — about 13 px each
side at this DPI — and both `lv_obj_set_pos()` and `lv_obj_align()` measure from the
**content** area while every width in the file was computed from `CONTENT_W`. Every inset
on that row was off by the padding, in both directions at once, and had been for five
milestones. `screen_geo.c`'s rows are built from the same pattern and had the same defect;
both now zero their padding so the insets in the code are the insets on the panel.

Neither was found by reading. Both were photographed.

---

## D71 — A review round: fifteen findings, two crashes, and one fix that was wrong

**2026-09-21.** A review of the whole branch reported fifteen findings. All fifteen were
real and all fifteen are fixed. What is worth writing down is the three things the round
taught that the findings themselves do not say.

**The two that mattered were both "the panel silently stops".** `s_detail_open` was a bool
main.c maintained about an overlay nav.c owns, and `nav_open_overlay()` closes whatever is
already there before opening the next one — so tapping an aircraft and then long-pressing
into Einstellungen deleted the detail layer behind main.c's back and left the flag true.
Its one reader chooses what to repaint, so from that moment neither Radar nor Liste was
ever updated again. `ui_resume()`'s own comment describes exactly this failure; this was a
second door into it, and the fix is to stop keeping the flag at all —
`nav_overlay_is(build_detail_screen)` asks the file that knows. **By the BUILDER, not by
the name string**: a caller comparing against a spelling can mistype the spelling and get
a silent `false` forever, and a function pointer either links or it does not.

The second was the same shape one level down: nothing ever told nav.c its widgets were
gone. `nav_create()` cleared `s_overlay` on the way *in*, which covers the caller that
rebuilds the deck and not the two that do not — `dbg_fontcard.c` and `dbg_bench.c` both
call `lv_obj_clean(lv_screen_active())` to take the panel over. Every pointer in nav.c was
then dangling and its NULL guards were dead code. An `LV_EVENT_DELETE` handler on the
tileview fixes it the way screen_wifi.c already does: LVGL tells the file, so nothing has
to remember to call anything.

**And the review's own fix for one finding would not have worked.** It correctly spotted
that `attempt_connect()` clears its event bits *after* `esp_wifi_disconnect()`, and
prescribed clearing them first. That does not help: the disconnect is asynchronous, the
handler answers every `WIFI_EVENT_STA_DISCONNECTED` by setting `WIFI_FAIL_BIT`, and the
event lands after the clear either way. The fix is to **consume** the teardown —
`xEventGroupWaitBits(..., pdTRUE, ...)` before the connect — not to move the clear. The
finding was worth its weight; the remedy attached to it was not, which is the ordinary
relationship between a reviewer and a fix and the reason applying one unread is not the
same as reviewing.

**Then the panel found a crash the review had not**, in the same area and older than the
whole branch. Exercising finding 2's sequence by hand — `e`, `f`, `1` — panicked in
`lv_obj_get_parent()` from an LVGL timer: `screen_list.c`'s visibility poller reads
`s_cont` thirty times a minute for the life of the process, and `lv_obj_clean()` frees it.
Reachable since the timer landed in `8080dc8`, and missed by every stress run in this repo
because they churn overlays and never press the two keys that clean the screen. The quieter
half: `screen_list_create()` runs again on every `ui_resume()`, so each debug view left
another timer behind pointing at the same freed object. Fixed with the handle and the
delete callback together; the stress sequence now includes `f` and `b`.

**And one fix of mine was wrong for one flash.** Pinning the settings card's coordinate
label to one line used `plex_mono_17`'s line height — but `screen_settings_update()` puts
`plex_sans_cond_22` on that same label when a place has been searched, and the taller face
was drawn into a box shorter than itself with its descenders sliced off. Caught by
photographing the card rather than by rebuilding and trusting it. The label now allows for
the taller of the two faces, which is what the card below it had always done and said so.

**Two of the fifteen were mistakes in things this session had just written**, and both are
the same mistake in opposite directions: the link probe allocated 16 KB of PSRAM per press
and never freed it on the path everyone takes, and `wifi_bars()`'s floor of -100 dBm turned
a real scan reading of -101 to -105 into "nothing measured" — the one thing its own header
promises zero is reserved for.

## D72 — A release the device will accept, signed by a key that never enters the repo

**2026-09-21.** The OTA client has been finished since M8 and switched off ever since, for
one reason: there was nowhere to update *from*. PLAN.md M8 says so plainly — "there is no
release infrastructure yet, so it ships off". This is that infrastructure. **No firmware
code changed.** `ota.c` and `ota_policy.c` were already right; what was missing was a
publisher.

**GitHub Releases, because the redirect was already handled.** `https_get()` in `ota.c`
follows redirects by hand, and the comment above that loop names the reason: "the obvious
place to put a manifest is a GitHub release asset, and that answers 302 every single
time". The device has been able to do this since before there was anything to fetch. The
URL is `.../releases/latest/download/manifest.json` — 84 bytes against an `OTA_URL_LEN` of
192 — and the manifest inside names the *versioned* asset, so the manifest and the image
it points at cannot drift apart.

**The repo is public now, and that is what made signing worth doing.** HTTPS proves where
an image came from, not who built it. Once the release pipeline is public, it is the most
attractive thing here to attack, and it ends at a panel in an eighty-year-old man's living
room. So: Secure Boot V2's signature scheme with **no hardware secure boot** — no eFuse
burned, nothing one-way, the bootloader still replaceable over USB. Secured against the
network, not against a screwdriver, which is the right trade for a device whose threat
model is a stranger with a release token rather than a stranger in the kitchen.

**RSA-3072, not ECDSA**, and this is worth writing down because the docs read as though it
were a free choice. `SECURE_SIGNED_APPS_ECDSA_V2_SCHEME` depends on
`SECURE_BOOT_V2_ECC_SUPPORTED`, which is ESP32-C2. The S3 is an RSA part and the Kconfig
choice defaults accordingly.

**Where the trust actually comes from, which is not where you would guess.** With no eFuse
to read, `get_secure_boot_key_digests()` in
`bootloader_support/src/secure_boot_v2/secure_boot_signatures_app.c` takes the trusted
digest **from the running app's own signature block**. The device will accept an update
only if it is signed by the same key that signed whatever is currently running. Two
consequences fall straight out of that, and both are permanent:

- **The key cannot be rotated remotely.** Whichever key signs the image that goes on the
  device pins it for that device's life. Lose the private key and the panel can never be
  updated again without opening the case.
- **The transition is safe in exactly one direction.** The build running today has no
  verification code at all, so it will install the first signed image without checking
  anything. From then on, every image is checked. That is why the desk test installs 0.2.0
  from an unsigned 0.1.0 and only *then* proves verification with a second hop — a hop that
  does not exercise the check is not a test of the check.

**Every build signs, including yours.** `CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=y`, key
path in `sdkconfig.defaults`, key itself gitignored. This is not convenience. An app built
with these options and left unsigned does not fail to link and does not fail to flash — it
aborts on boot:

```
secure_boot_v2: No signatures were found for the running app
secure_boot: This app is not signed, but check signature on update is enabled in config.
```

"Sign it afterwards" is one forgotten command away from a panel that does not come up, so
there is no afterwards. A clone with its own fresh key builds and runs perfectly; it
simply cannot update the one device flashed with the real one.

**`PROJECT_VER` stopped being a thing anyone has to remember.** It was a literal in
`CMakeLists.txt` with a comment asking a human to bump it in the same commit that
publishes a build. That is AGENTS.md §11 rule 1 written as a to-do: the cost of missing it
is not a wrong version string, it is a device that compares every future release against
0.1.0, concludes it is already up to date, and **silently declines the fix it was sent**
for the rest of its life. It now comes from the git tag, and a local build gets
`0.0.0-dev` — honest, and below everything.

**The gate reads the artifact, not the build system.** `tools/check_release.py` opens the
finished `.bin` and checks the app descriptor at offset 0x20 (magic `0xABCD5432`, version
at +0x10, project name at +0x30) — the same struct `esp_app_get_description()` hands to
`ota.c` at runtime, so it is literally the string the device will compare. It also
verifies the Secure Boot V2 signature against the committed public key, and the size
against the real `ota_0` entry in `partitions.csv`. Asking CMake what version CMake was
told to build would have been the `check_font_coverage.py` hole again (§11 rule 2): a gate
that agrees with itself whatever happened in between.

It was watched failing before it was trusted, on all four cases that matter: a wrong
version, the unsigned binary `idf.py` leaves next to the signed one, an image signed with
a **different** key, and 200 KB of random bytes. The third is the one worth having.

**The manifest is written by the same tool that checks the image.** It started as a
heredoc in the workflow YAML, which meant the version, the byte count and the filename
existed twice — once in Python being verified and once in shell being published. That is
the shape §11 rule 1 describes, so it is now one `--manifest` flag on
`check_release.py`, emitting the numbers it has just verified. The tool also asserts that
`OTA_URL_LEN` and `MANIFEST_MAX` still say 192 and 4096 in the firmware headers, so its
copy of those limits cannot drift away in silence.

**Two jobs, and only one of them sees the key.** `build` runs in the ESP-IDF container with
`secrets.SIGNING_KEY`; `publish` runs on a clean runner with nothing but the automatic
token and calls `gh`. No third-party actions anywhere on the release path — a convenience
action there is a convenience action with the signing key in its environment.

**`dependencies.lock` is committed now.** `idf_component.yml` pins only
`waveshare/esp32_s3_touch_lcd_4b: ^2.0.0`, and the drivers under it float on ranges. An
unpinned CI resolve could build an `esp_lcd_st7701` that was never the one measured on
this unit and then push it 9,000 km. The lock costs 6 KB.

**CI found a real bug on its first run, and it was not in any of this.**
`main/data/extrapolate.c` used `M_PI`, which is not ISO C — it is a POSIX/X-Open
extension, and glibc hides it when `__STRICT_ANSI__` is set, which `-std=c11` does.
Apple's libc exposes it either way. So the file compiled on the one machine it was ever
compiled on, and the 32,668-check suite had been green for days on a translation unit
that did not build on Linux. This is §11 rule 2 with the gate pointed at itself: the
suite was not failing to check, it was only ever checking on one platform, and a suite
that has only run in one place has told you less than it appears to. It is now defined
with a `#ifndef` guard in the file rather than fixed with a compiler flag, so the
translation unit is self-contained instead of depending on which libc it meets.

**And the desk test found the bug it existed to find, on the first real fetch.** The
device refused the live manifest with `ota: manifest: connect failed: ESP_FAIL`, which
reads like a network fault and is not one — the line above it said
`esp-x509-crt-bundle: Certificate validated`, three times. The TLS handshake had
succeeded. The actual error was `HTTP_CLIENT: Out of buffer`.

`esp_http_client`'s header buffer defaults to `DEFAULT_HTTP_BUF_SIZE`, 512 bytes, and
GitHub does not fit in 512 bytes. Measured against the live release:
`releases/latest/download/manifest.json` takes two hops; hop 2 answers with a 918-byte
`Location` and a **3,683-byte `content-security-policy`**. The buffer must hold the
longest single header line, so the CSP header sets the floor — and it is a header nobody
here controls. `HTTP_HEADER_BUF` is now 8192 on both the manifest fetch and the image
download, which costs no internal heap: `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` is 4096, so
an allocation above that goes to PSRAM. The TX buffer needed it too, less obviously —
after hop 1 the request line carries that 918-byte URL.

This is D45's lesson a second time ("a TLS allocation failure that presented as a network
error"), and the reason it was worth insisting the test happen on the desk: **the running
build could not fetch the manifest, so it could not have been sent the fix.** It went on
over USB. From 9,000 km away that is not a repair.

It also caught a foot-gun in the build itself. `PROJECT_VER` is read at CMake *configure*
time and cached, so rebuilding with a new version in the environment quietly produced a
binary still carrying the old one. `check_release.py` refused it — the artifact said
0.2.0 while the tag said 0.3.0 — which is precisely why the gate reads the binary instead
of asking the build system.

**It works, and it was watched working.** 0.3.0 pulled 0.4.0 from a real release: 2.4 MB
into `<ota_1>` in about 26 seconds, signature verified by exactly the mechanism described
above — `Take trusted digest key(s) from running app`, `#0 app key digest == #0 trusted
key digest`, RSA-PSS — then a slot switch, a reboot from `0x520000`, and
`new image 0.4.0 confirmed: ESP_OK` once it had held WiFi. Then the case that matters
more: an image signed with a throwaway key was offered and refused, with
`image valid, signature bad`. The download was intact and the signature was not, which is
the check doing its job rather than a corruption false positive. The device stayed on
0.4.0 and never switched the boot partition.

Forcing the night window took no new code and no finger on the glass. `o` cycles the
location, Pattaya is `ICT-7`, and five hours ahead of CEST put the clock inside
22:00–07:00; cycling all the way round restored `Eigener Ort` exactly. A console key that
already exists for one reason turned out to be the lever for another.

**What this does not cover.** Nothing in M8 now — but the honest limit is that all of this
was proven on one device, on one network, against one host. The night-window gate in
particular has still never fired on its own schedule; it was reached by moving the clock
to it. And the reason that window exists is still unmeasured: a 2.4 MB write did finally
run, but whoever was present was reading the serial log, not looking at the panel, so
whether a full image write tears this display is exactly as open as it was before. The
next install is the chance to answer it — watch the glass, or grab framebuffers through
it.

## D73 — A check that failed does not get to spend the whole day

**2026-09-21.** `ota_check()` stamps `s_last_check_ms` on the line after `https_get()`
returns, before it looks at what came back. So a fetch that failed — DNS, a handshake
that lost a race with the WiFi coming up, a GitHub 5xx, the 512-byte header buffer of
D72 — cost the full `OTA_CHECK_INTERVAL_MS`. Twenty-four hours, bought by a request that
never happened.

That is the wrong shape for this feature specifically. The whole reason this device has
OTA is that it spends half the year 9,000 km from anyone who could fix it, and the gap
between "the fix is published" and "he has the fix" was allowed to be two days because of
one bad second. Nothing about a transient failure justifies that, and the 24-hour cadence
was never an argument for it — it is a deliberate *politeness* interval, and spending it
on nothing is not polite to anyone.

**`OTA_RETRY_INTERVAL_MS` is one hour**, and the reasoning for the number is short: what
is being retried is a request to somebody else's free service, so 24 GETs a day in the
worst case is the ceiling, and that is nothing. Minutes would be hammering; a day is what
is being fixed.

**Flat, not backed off.** `ota_should_check()` already refuses to try while offline or
before the clock is set, so this is specifically the online-but-unreachable case, which is
almost always transient. A backoff curve is more machinery than the problem deserves, and
more machinery is more that can be wrong in a way nobody can see from here.

**The distinction that carries it: "nothing newer" is a SUCCESS.** The manifest arrived
and was read. So are all four of the judgements below it — not newer, not https, too big,
known-bad — because each one is an answer about bytes the device is holding. Retrying any
of them in an hour would ask the same question of the same bytes for the rest of the
device's life. `s_last_check_failed` is therefore set to true immediately after the fetch
and cleared the moment a manifest parses, which puts the two states on the same code path
rather than leaving one of them to be remembered at four separate returns.

**It went in the policy layer, not in `ota.c`.** The decision is a judgement about when,
which is what `ota_policy.c` is for and why it touches no `esp_*` header. `test_ota.c` now
pins it with **16,975 checks**, including a minute-by-minute sweep across the first
twenty-four hours asserting both intervals against both outcomes — a sweep rather than
three points, because the two must not swap or blur anywhere in between. Reverting the
one-line policy change was watched failing those tests before they were trusted (§11
rule 2). The suite is **35,562** overall.

## D74 — The other half of updating: the one he can reach

**2026-09-23.** D72 and D73 built the unattended path: check daily, install between 22:00
and 07:00, tell nobody. That is the right default and it is not the whole job. The owner
asked for the half that has a person in front of it — a row that checks now, and offers to
install now — and the reason is the same one the night window exists for, pointed the other
way: he is *looking at the panel*, something seems wrong, and "wait until tonight" is not an
answer.

**The night window is skipped on purpose, and only here.** `ota_request_install_now()` sets
a flag the task honours instead of `ota_should_install()`. The window is a precaution
against a 2 MB flash write tearing the panel in front of someone who did not ask for it
(D72 — still unmeasured). A tap on "Jetzt installieren" is exactly the case that reasoning
does not cover: he asked, and the takeover tells him to expect a wait and a reboot. Every
gate that is about *safety* rather than *timing* still stands — https only, the signature,
the size check, and the version that already rolled back once.

**It may also be how the tearing question finally gets answered.** If a full image write
does garble this display, it now does so on a screen that is two lines of static text and a
4 px bar, in front of someone who has been told to wait — which is the cheapest possible
place to find out.

**A tap must always come back with an answer.** The daily tick is allowed to find no
network and say nothing; a tap is not, because a row left on "Suche nach Updates..."
forever is the silent panel §1 forbids. So `s_check_requested` distinguishes the two, and
when the policy refuses a requested check — no URL, no network, no clock — the task reports
`UPD_CHECK_FAILED` rather than going quiet. The status callback also fires for the *daily*
check, so a row opened the morning after a 03:00 install says what actually happened
instead of whatever it said yesterday.

**The wording is state, not sentences in the screen.** `fmt_update_status()` and
`update_action_label()` live in `main/data/fmt_de.c` with every other German decision, and
`test/host/test_fmt_de.c` pins them at **401 checks**: every state produces a non-empty
line (the real assertion — a blank status line under a heading is the silent panel again),
the version is spoken only when there is one, a NULL version degrades to the neutral prompt
rather than printing "Version  ist verfügbar", and a sweep of every buffer size from 1 to
39 proves it truncates without ever writing past the end.

**A bordered row on that screen means "tappable".** The Akku section is deliberately not
one, because "a row that looks tappable and does nothing is how a non-technical user
decides the device is broken". So while a check or an install is running,
`update_action_label()` returns NULL and the row is *hidden* rather than greyed — there is
nothing a second tap could start. The border turns cyan when something is installable,
reusing the "live" this device already has; DESIGN.md §2's six colours stand.

**The section does not exist without an update source.** A device that ships with no URL
contacts nothing (D44), so offering it a "Nach Updates suchen" row whose only possible
answer is "Keine Verbindung" would be a button that lies. `ota_has_url()` gates the whole
section — a feature that ships off costing nothing while off (D52), on the glass as well
as in the heap.

**The takeover lives on `lv_layer_top()`, not in the settings tree.** Closing Einstellungen
mid-install must not delete the one thing explaining why the panel is about to go dark, and
during those seconds the device must not accept navigation at all: the next event is a
reboot. It is clickable with no handler, so every tap lands on it and goes nowhere.

**The state outlives the screen**, which is D58 again — the WiFi scan task writing into a
deleted tree. Einstellungen is rebuilt on every open, the OTA task reports whether it is
open or not, so the state lives in statics and every pointer *into* the tree is dropped by
`on_cont_deleted()`. `screen_settings_set_update_state()` is safe to call with the screen
closed and simply renders the next time it is built.

**`U` walks all seven states**, the same argument as `Y` for a pretended battery and `W`
for a pretended signal: six of them need a release, a dead network or a failed flash write
to reach, and none of those is something anyone provokes twice. The console-key gate caught
it immediately — the key was in `on_cmd()`, the header and the ready line, and missing from
AGENTS.md §3.

**Seen on the glass, 2026-09-24.** It was tagged before that, at the owner's call, and the
panel took v0.7.0 by itself the following night — skipping v0.6.0 entirely, because it had
been offline the day that one was published and each release is a whole image, so there is
nothing to catch up on. The owner then looked at it and confirmed it reads correctly.

What that does NOT settle is the tearing question, and it is worth being precise about why:
the install that delivered this ran unattended at night, so nobody was in front of the
panel while 2 MB went into flash. The one measurement this feature was expected to make
comes the first time somebody taps **Jetzt installieren** and watches.

## D75 — What the caption is talking about gets a ring, and the clear that never ran

**Decision:** the aircraft the radar caption names is drawn with a **white ring around it**.
Magenta keeps meaning what it has always meant on this screen — the single nearest aircraft
— and does not move when he taps.

**Why the two had to come apart.** Magenta and the caption were one channel answering two
questions. *Which is nearest* is a fact the device computes, and it changes between polls
with nobody touching anything: every fix is carried forward and the array re-sorted
(main.c), so the magenta mark can hop across the scope on its own. *Which one is the
caption about* is a fact **he** establishes with a finger. Tying the second to the first
meant tapping a mark changed nothing anywhere near the mark — the caption re-pointed 190 px
away at the bottom edge and the scope looked exactly as it had. D59 recorded the tap as
confirmed working, and it was: it just could not be seen where it happened.

It also silently broke the one thing the scope was still saying. Ask about a distant
aircraft and the panel showed a magenta mark near the centre over a caption reading
`42,8 km` — two pieces of chrome flatly disagreeing, with nothing on screen to say which
one to believe. Found by the owner looking at a photograph of his own device and asking
what the colours meant.

**Why a ring and not a colour.** DESIGN.md §2's ceiling is six chromatic codes (DO-257A
§2.1.6) and it is a hard one; "replace one instead" was not on offer here because all six
are load-bearing. An enclosure is a different visual channel from hue, so it costs the
ceiling nothing, it survives reduced colour discrimination, and a ring drawn round the
thing you picked is the oldest selection idiom there is. THEME_WHITE, already in the
palette.

**They coincide until he taps.** The caption defaults to the nearest, so a radar nobody has
touched shows the ring sitting on the magenta mark and reads as one indicator rather than
two. They can only come apart as the direct result of something he just did — which is
precisely when the difference is worth a second glyph. Moved in the same tick as the
caption, from the same cache, for the reason the caption already did not wait: feedback
that lands twelve seconds after the finger lifts is feedback he has given up on.

**DESIGN.md §2 said "the selected aircraft" in the magenta row and it was never true of
this screen.** Corrected there rather than here.

**The second bug, which is the reason to read this entry even if the ring is uncontroversial.**
`screen_radar_clear_selection()` — "leaving the radar drops whatever mark he had tapped" —
was guarded by `prev_page == 2`. The deck had three pages when that was written and the
radar was the third. **D60 collapsed it to two** (`PAGE_RADAR 0`, `PAGE_LISTE 1`) and this
line was not one of the places that got updated, so `nav_page()` has returned 0 or 1 ever
since and the branch was unreachable. The selection was never cleared: a mark tapped once
stayed the subject of the caption across every visit to the radar until that aircraft
flew out of range, however many minutes later.

Nothing failed, nothing logged, and a confident comment sat on top of it describing
behaviour the code had stopped having. **AGENTS.md §11's first failure pattern, in a
function three lines long** — and worth recording as a data point about where that pattern
actually lives: not in the gnarly code, which gets read carefully, but in a literal beside
a comment nobody doubted. The page constants existed already; the line simply did not use
them. It does now.

Note the clear deliberately does **not** fire for the detail layer, which is an overlay and
leaves `nav_page()` alone — tap the caption, read the card, come back, and the same
aircraft is still ringed, which is the whole point of having tapped it.

**`screen_radar_clear_selection()` still touches no LVGL**, and that is not laziness: its
caller in `ui_task()` runs *before* `display_lock()` is taken. Hiding the ring there would
have been a one-line lock violation of the rule AGENTS.md §7 calls non-negotiable, in the
commit that fixed the function. The ring catches up on the next `screen_radar_update()`,
which is under the lock, and the page is not on screen at that moment anyway.

**Verified:** host suite green (all gates), firmware builds and signs. **Not verified on
the glass** — no board was attached when this was written. The ring's geometry is arithmetic
(r=16 with a 2 px border clears the triangle's 11 px nose by 3 px at every rotation), but
whether a 2 px white circle reads as "this one" from his chair is D59's kind of question and
needs the finger and the eye of the person holding it.

## D76 — The radar, challenged: seven findings, a simulator to test them, and what it found

**Where this came from.** D75 came out of the owner photographing his own panel and asking
what the colours meant. Asked for the rest of the radar to be challenged against real radar
displays and against UX practice, the review found seven things. All seven are fixed here.
An eighth — rotating the scope so "up" is the direction the wall faces, instead of north — is
a question for the man in the chair, not a defect, and is left open.

**1. Amber no longer means "no flight plan".** AC 25-11A makes amber the caution colour, and
TCAS spends it on exactly one thing: a traffic advisory. The radar was spending it on every
private aircraft in the sky — by DESIGN.md's own count 14 of 38 aircraft, and precisely the
low, loud ones he actually hears. It was also redundant, because filled-vs-hollow already
carried route-known without loss. Route-less aircraft are now **cyan and hollow**. On this
screen amber means one thing: what you are looking at is not live (item 4). DO-257A's rule
is that colour must never be the *only* carrier, not that every fact needs a colour, so a
shape-only distinction is fine for a fact with no colour meaning of its own. The detail
layer's amber `KEIN FLUGPLAN` tag is DESIGN.md §2's own assignment and was deliberately
left alone. Whether it has the same problem is a fair question, but it is a separate one.

**2. Altitude is shown, as size.** Every real traffic display puts altitude on every
target — TCAS as a relative-altitude tag with a trend arrow, ATC in the data block. This
scope showed it nowhere, so it could not answer the reactive question (AGENTS.md §1):
*which one is the one I can hear?* Distance does not answer it: a jet 3 km out at 11 000 m
is silent, and a Cessna 8 km out at 700 m rattles the window. Text is ruled out by the type
pass, so the answer uses a channel that was free: **mark size in three bands**, large below
5 000 ft, small at or above 20 000 ft. It is three steps rather than a continuous scale,
because a continuous size can only be compared, and he is not comparing, he is looking for
the big one. Unknown altitude and "on the ground" both get the middle size: a taxiing
airliner at Schwechat drawn as the lowest thing in the sky would be a confident wrong answer.
This is a UX inference, not a certified-avionics convention. The mark box grew from 28 to
34 px to fit the largest band and the touch pad shrank to match, so the finger target is
unchanged at 56 px. The selection ring now scales with the mark it circles.

**3. Trails.** Every PPI radar draws its history as fading returns behind each target. Here
it is up to four dots, 15 s apart, stored as (distance, bearing) so they survive a change of
radius, and dropped after 75 s so there is no long jump when he comes back from the Liste.
A trail also shows direction for an aircraft that reports no track, without claiming a
heading nobody sent, which is the rule the plain dot already follows. Two rules came out of
the simulator rather than out of the review:

- **A jump no aircraft could make restarts the trail.** The limit is 800 kt plus 1 nm of
  slack, checked on *every* observation. ADS-B does report the odd wild position. The first
  simulator frames showed the result without this rule: a dot 190 px from the aircraft it
  belonged to, a ghost on the scope. Checking only when a fix is due would miss a jump and a
  jump back inside one step, which is the case the simulator actually produced.
- **Nothing is recorded while the data is stale.** The first reason written for this — that
  frozen positions would pile the trail up under the mark — was wrong. A mutant that did
  record while stale passed every check, because the pile sits under the mark and is never
  drawn. The real reason only appears when the data returns. A fix taken while stale is
  stamped "now" for a position that is minutes old. When a *slow* aircraft turns out to have
  moved a few pixels, that fix is drawn as its newest trail dot, at a spot it left long ago;
  a fast aircraft trips the jump guard either way. The simulator now builds exactly that
  case, and the comment now gives that reason.

**4. Stale data shows on the default screen.** `screen_radar_update()` took no network
state. When the feed died, the detail layer said **KEINE DATEN**, while the radar — the
default screen since D60, the thing on the wall — kept drawing the last picture. The marks
froze where they were, and with WiFi fine the corner meter showed four bars. The radar now
takes the same `net` the detail layer's tag is built from (`screen_radar_set_net()`), so the
two cannot disagree. When stale, every mark and trail is drawn at 50 %, trails stop and fade
(item 3), and the amber tag takes the identity line's slot, top centre. It is the same face
and the same two words as the detail layer's tag.

**5. The affordances the rest of the device already had.** Every list and settings row fills
with `THEME_SURFACE_SEL` when pressed, and every row that leaves the screen carries
`STR_ROW_ARROW`. The radar had neither — on the one screen where "did my tap land?" was a
real problem.

- **Marks:** the ring jumps to the mark the moment a finger lands, and goes back if the
  finger slides off into a swipe. A 16 px glyph has nothing to fill, so the ring that is
  about to move there anyway serves as the pressed state.
- **Caption:** a pill (`SURFACE_SEL` fill, `BORDER_IDLE` edge — the device's existing
  pressed card) shows behind the caption while it is pressed. Its top is pinned 1 px above
  the caption box instead of padded evenly: the S cardinal above ends at y=413, and an
  even 4 px drew the pill through it. That was measured in the simulator, not judged by eye.
- **The arrow**, and the ladder that decides what gives way. The first cut let the arrow
  outrank the name. The simulator's frames showed the cost: "Unbekanntes Flugzeug 11,1 km NO"
  and "Cessna 172 Skyhawk 22,2 km SSW" both fit on one line before the arrow existed, and
  both lost their name to it — a regression against D50 introduced by this very change. The
  order now:
  1. name, distance and arrow, if all three fit;
  2. otherwise the **arrow gives way first**, because the name is information and the arrow
     is a hint he learns once;
  3. then the name gives way (D50).

  The distance never gives way.

**6. A way out of a selection.** A tapped mark stayed the caption's subject until the
aircraft left range. D75 made that at least end when he leaves the page. Now:
- **tap the empty scope** to let go — the gesture had no meaning before, and this is the one
  it has everywhere else;
- a selection nobody has touched the screen about for **30 s** goes back to the nearest —
  the same 30 s as DESIGN.md §6's auto-return, for the same reason.

The first of these needed care. `nav.c`'s `bubble_decorative()` stops at any object with a
callback, so once the scope container owned a click, nothing inside it would bubble a press
any more. The long press to Einstellungen from the scope — which D62 found had never once
worked by finger, and fixed — would have died again, silently. So the container bubbles itself, and every
decorative object inside it (rings, home marker, trail layer, pill) is made non-clickable,
so a press falls through to the container. The simulator checks the long press on a ring's
outline, in the corner, and on a mark (where it must *not* reach the deck).

**7. The magenta mark no longer flickers.** Two aircraft at nearly the same range swapped the
nearest on every poll, so the magenta mark jumped between them with nobody touching
anything. The previous nearest now keeps the title until another aircraft is more than 10 %
closer (with a 0.25 nm floor for aircraft right overhead). That opened a hole the simulator
then caught: the caption named the radar's own nearest, but a caption tap opened index 0 of
the caller's array — which, with hysteresis holding, can be a different aircraft. He would
have read one name and been shown another. The tap now opens what the caption names.

**Found on the way, and fixed.**

- **The range read-out was top-left by accident.** The code asked for bearing 135 (SO, down
  by the scope) and then "clamped it into the panel" using `lv_obj_get_x/y`. Those return
  the coordinates of the *last layout pass*, not the position just set, so a label never
  laid out read (0,0), was clamped to (20,20), and stayed there on every update after.
  Everything else in the top row — the clock's "opposite corner", the identity line's
  neighbour check — had long since been built around the accident. So the accident is now
  what the code says, explicitly. It also cost the first version of item 4 a collision: the
  stale tag went into the "free" top-left corner, printed over "55,6 km", and was caught
  only because it was rendered. That is AGENTS.md §11's first pattern twice in one file.
- The file header still described "exactly TWO labels, beside the nearest and
  second-nearest". That had not been true since D35. Corrected.

**The simulator — `make -C test/sim`.** D59 recorded that the radar's taps "could not be
verified from here" and needed a finger. Every item above is drawing or touch routing, and
the board was not attached for any of this. So there is now a finger. The simulator runs
real LVGL 9.6 compiled for the host and the real `screen_radar.c`, inside a real
`lv_tileview` as `nav.c` builds the deck. It renders into a 480×480 RGB565 framebuffer, a
pointer device follows a script, and the clock only moves when the script says so. It
checks from pixels, never from the screen's private state: pure white is drawn only by the
ring, exact magenta only by the nearest mark, and exact amber only by the stale tag.
Screenshots of every step land in `test/sim/out/`. Things worth knowing about it:

- **It renders in DIRECT mode, like the device** (`CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=y`).
  The first version used FULL mode, which repaints every pixel on every frame, and so it
  could not see a forgotten `lv_obj_invalidate()`. A mutant that removed the trail layer's
  invalidate survived it. A harness that is kinder than the device tests a different device.
- **Mutation-tested**, per §11's second pattern. 21 deliberate breakages across the screen
  and `radar_logic.c`; 20 are caught. The survivor is an equivalent mutant — removing the
  per-mark invalidate — because the trail layer's invalidate already covers the whole scope
  on every update. Two false passes were found and fixed along the way. A stale binary made
  "caption opens index 0" look like a survivor (the edit and the build fell in the same
  second). And "recorded while stale" really did survive, until the test was rebuilt around
  the case that matters (item 3).
- **ASan and UBSan are on, and fatal.** Proved by planting a one-past-the-end read in the
  trail loop: the build fails with `index 4 out of bounds for type 'const radar_fix_t[4]'`.
  The compiler also flagged one undefined behaviour in the test itself: two
  random-number-generator (LCG) calls in one expression, unsequenced. That was fixed too.
- **A stress run** of 600 random steps — taps, swipes, long presses, changing skies,
  outages — checks invariants after every step: with live data and aircraft up there is
  exactly one ring and it sits on an aircraft; live data means no amber; an empty sky has no
  ring. Run locally across 25 seeds, that is 15 000 steps with no failures. It first reported
  41 "rings on no aircraft", which turned out to be the harness: its measurement box cut off
  the outer arc of rings round aircraft clamped to the outer ring.
- **In CI**, as a step after the firmware build, because LVGL's source lives in
  `managed_components/`, which only the build fills in. It was run locally in the exact CI
  image (`espressif/idf:v5.4`, Linux, gcc) before being trusted there: all suites green,
  zero warnings. Leak detection is off, because LVGL's objects are never freed and
  LeakSanitizer — on by default under Linux — would report them.

**A review round, and two more gaps in item 6.** A code review of this change found three
things. All three were reproduced in the simulator before they were fixed.
- **The 30 s timeout ran while he read the detail card.** `ui_task` never calls
  `screen_radar_update()` while the detail layer is up, and no press lands on the radar
  then, so reading a card for 40 s cost him the very aircraft he had opened it for. That
  broke the promise main.c makes ("tap the caption, read the card, come back, and the same
  aircraft is still ringed"). The timeout now counts only time spent *looking at the radar*:
  a gap of more than 5 s between two updates means the radar was covered, and the idle
  clock restarts.
- **A long press on the empty scope also let go of the selection.** LVGL 9 sends `CLICKED`
  on release even after a long press; only `SHORT_CLICKED` is skipped. So opening
  Einstellungen from the scope — or holding past 400 ms and thinking better of it — dropped
  the aircraft he had tapped. The deselect now listens for `SHORT_CLICKED`: a hold is not a
  tap.
- **Two header comments still described the first cut**: no-route drawn amber, and the
  stale tag in the top-left corner. Both corrected. That is §11's first pattern for the
  third time in this entry, in comments written the same day as the code.

**Cost.** Static RAM in `screen_radar.c` goes from 3 756 to 5 340 bytes (+1 584, nearly all
of it the trail table), in internal RAM, because `CONFIG_SPIRAM_ALLOW_BSS_EXT_MEM` is off and
turning it on is a device-wide change nobody asked for. For scale, M12 measured 58 KB of
internal RAM free after its stress run. Four more LVGL objects. The trail layer invalidates
a 288 px square every 2 s. `radar_logic.c` holds no state of its own.

**Verified:** host suite green, including the new `test_radar` (43 checks, mutation-tested);
simulator green (82 checks, after the review round); firmware builds and signs with no warnings in the new code;
both suites also green in the CI container.

**Not verified on the glass — no board was attached.** Everything above was checked on the
host, against the device's own LVGL, fonts, colour format and render mode, and that settles a
lot. It does not settle these, which need his eye and his finger:
- whether the three mark sizes are distinguishable from the armchair;
- whether the trails read as history or as clutter;
- whether a 13 px amber tag is findable when the data is stale — the same question
  AGENTS.md §8 already asks about the 13 px identity line;
- and, once the board is attached, whether the internal-heap headroom is still comfortable:
  `v` on the console, before and after a WLAN keyboard open (D58's failure).

## D77 — Busy sky, empty panel: a 16 KB buffer and a parser that kept the wrong 24

**Found by testing D76 on the device, not by looking for it.** With the new firmware flashed,
the radar said **KEINE DATEN** and drew nothing. The log said why, every 12 s:
`response truncated: buffer holds 16383 of the real body` → `malformed JSON`. The release it
replaced (v0.7.0) was failing identically — `failures=5` before anything was flashed — but
showed a frozen picture as though it were live, which is D76's item 4 caught in the wild.

**1. The poll buffer was smaller than the sky.** The device's own request (the Vienna
preset, 33 nm) returned **17,784 bytes for 32 aircraft**, 11 of them on the ground at
Schwechat — 1.4 KB over a 16 KB buffer. Measured once from the host with the identical URL:
572 bytes per aircraft median, 738 max. `POLL_BUF_SZ` is now **64 KB**, in PSRAM (4.4 MB
free), sized for 80 aircraft at 740 bytes. A `_Static_assert` in `flight_source.h` now carries
that arithmetic, so shrinking the buffer fails the build instead of the sky.

**2. Past `MAX_AIRCRAFT`, the parser kept the first 24, not the nearest 24.** It stopped at
`count >= max`, in array order, and adsb.lol does not sort by distance: the same capture
opened 31.6, 23.5, 31.3 nm, and the first-24 rule dropped aircraft at 12–22 nm while keeping
ones at 31. The aircraft overhead is as likely as any other to be at the end of the array.
Hidden until now because the truncation killed every response big enough to reach it. Every
aircraft is now parsed; once full, a newcomer replaces the farthest kept one if it is nearer,
and one with no distance never displaces one that has one (`cmp_dst_nm`'s own rule).
`test_parse.c` builds a sky with the nearest last. It failed before the fix, keeping 9–32 nm
and dropping 1–8.

**Checked on the device after the fix,** because a bigger parse is a bigger transient
allocation on a board whose internal heap has crashed things before (D58):
- polls succeed from the first one: `stale=0 failures=0`;
- internal free at steady state is 43,039 B, and the largest block 21,504 B (31,744 B before
  the first successful parse — fragmentation from real parses, where before there were none);
- **the WLAN keyboard still opens**, by the real path (tapping an unsaved row). With it up,
  internal free is 21,687 B and the largest block is still 21,504 B. It closes cleanly, and
  memory returns to 42,715 B;
- the flight task's stack headroom is 6,860 B of 16 KB, down from ~12 KB — because polls now
  succeed and the route lookup runs, which it never did while every parse failed. That is
  the first measurement of this task on a working feed, not a regression against one.
- TLS for the nightly update is unaffected: its record buffers are in PSRAM (D52).

**One thing this surfaced and did not decide.** With the whole sky now arriving, the eleven
aircraft on the ground at Schwechat sit together as a clutter of marks near the right-hand
edge of the inner ring. Real approach radars filter ground traffic, and "that plane up there"
is never one of them — but hiding them is a product decision about what the panel is for, so
it is a question for the owner, not a fix.

## D78 — Flight number and model move into the caption, above the distance

**Decision, by the owner:** the radar's caption box has two lines.

```
      AUA1Y · Airbus A321            line 1 — who and what
   Frankfurt  16,0 km SO  →          line 2 — where to, how far, which way
```

Line 1 follows identity.c's one rule: callsign first, registration where there is no
flight number, never both, never a raw ICAO designator. The model falls back to
"Unbekanntes Flugzeug" rather than to nothing (D46). Line 2's name is now **only the
destination**, and only when the route is known. A route-less aircraft's model moved up to
line 1, so its line 2 is just `22,2 km SSW →`.

**What that settles.**
- **The 13 px identity line in the top row is gone.** It said the same thing, and AGENTS.md
  §8 had it listed as an open question: was 13 px tertiary type findable from the chair?
  The owner answered by asking for it somewhere else, at 25 px. The top row's centre now
  holds only the stale tag.
- **D50's worst case disappears.** "Unbekanntes Flugzeug" and "Cessna 172 Skyhawk" used to
  compete with the distance for one line and lose it. Only destinations compete now, and
  the ladder (arrow first, then name, never the distance) still decides.

**Type and colour.** Plex Sans Condensed 25, the smallest face that clears this screen's
near floor (the type pass at the top of `screen_radar.c`), in `THEME_TEXT_LABEL`. The detail
layer shows the same identity in `TEXT_PRIMARY`; here it is grey on purpose, so that line 2
— the answer — is the brighter of the two. Line 1 is part of the caption's button: tapping
it opens the card, and the pressed pill covers both lines.

**Where the room came from.** Two lines need about 68 px, and the band between the S
cardinal and the page dots had 50. The scope moved up 22 px (centre 240 → 218) and shrank
slightly (outer ring 140 → 136, cardinal radius 164 → 160). The N still clears the top
chrome row, the S's ink ends at y≈387, the caption runs from y=394 to 462, and the dots
start at 464. The pill hugs that box with 1 px above and below it. The last 4 px of the lift
came from looking at the first render: at 222 the grey S sat 9 px above the grey first line
and read as part of it.

**The geometry is public now.** `RADAR_CX/CY`, `RADAR_R_OUTER`, `RADAR_CARDINAL_R` and the
caption's position live in `screen_radar.h`, so `test/sim` measures against the screen's own
numbers. Before, it hardcoded a dozen copies of 240, 140 and 405–463, and every one of them
had to be found by hand for this change.

**Verified** in the simulator (92 checks, 25 stress seeds under ASan/UBSan):
- line 1 is present and the top row's centre is empty;
- a route-less aircraft gets its model on line 1 and no name on line 2;
- a tap on line 1 opens the card;
- the N clears the top row, and the caption clears the dots;
- both ladder steps still hold, now with **real destination names**: the step-2 case is
  found by searching the airport table for a name that fits without the arrow but not with
  it, rather than assumed.

Two test fixes came from the smaller scope, and neither loosened a check. The top-row check
had reached down into the N. The frozen-trail window now grazed the aircraft's own glyph, so
it now ignores a 10 px disc round the mark, and the record-while-stale mutant still fails it
(68 px). **Not yet on the glass** — the board was unplugged by then.

**Noticed while rendering the detail layer for the owner, and not changed:** D48's
"supporting text gives way" rule drops more than it has to. Take a private aircraft whose
long type name wraps the hero onto two lines, OE-AHM's "Diamond DV20 Katana" for example.
Its reason sentence is correctly dropped for space. But the identity line, placed below that
sentence, is dropped as well — although it would fit in the space the sentence freed. The
card then shows ~70 px of nothing and no registration anywhere. Positions are computed once
and not reflowed after a drop. A fix is small, but the card is not what was asked about, so
it is recorded here rather than made.

## D79 — The detail card gains speed, an arrival estimate and the distance from the origin — and stops losing lines

**The request, from the owner, with a Flightradar24 screenshot as inspiration and not as a
spec:** departure time ("x h ago"), arrival time ("in x"), and ground speed on the detail
card.

**What the data allows, checked before anything was built.**
- **Ground speed** comes in every poll (`gs_kt`). Shown in km/h, rounded to 10, because a
  last digit that changes every twelve seconds is noise.
- **Arrival: no schedule anywhere this device can reach.** adsb.lol has positions and
  adsb.im's routeset has the two airports — it does return their coordinates, which the
  parser now keeps. So the estimate is arithmetic on what is known: the great-circle
  distance still to fly, over the ground speed. It is always worded as an estimate: *Landung
  in etwa 45 Minuten*.
- **Departure time: not knowable, so not shown.** Only airline schedule feeds know when an
  aircraft took off, and a guess would miss by half an hour. The owner chose the honest half
  instead: **how far it is from where it took off**, *43 km von Wien entfernt*. That is the
  straight-line distance, and the wording says exactly that. "zurückgelegt" would claim the
  distance flown, which is always longer and which nothing here measures.

**The arrival rules (`main/data/arrival.c`), each there because the simple version looks
right and is wrong:**
- **The last 40 nm count at no more than 200 kt.** Distance over cruise speed alone ran 25 %
  early against the owner's own FR24 example: 483 km out, 34 min against FR24's 45. With the
  approach allowance it is 41.
- **Nothing while climbing out** — below 20 000 ft and still nearer the origin than the
  destination. A departure's ground speed is far below cruise, and the estimate runs 30 % or
  more late, which "etwa" cannot cover. The Vienna preset sees a great many departures.
- **Nothing when flying away** from the destination more than 60 nm out, which means the
  route match is probably wrong. Closer in it is allowed: a downwind leg points away from the
  runway by design.
- **Nothing** without a resolved, plausible route with coordinates, an airborne altitude, a
  position and 60 kt, or when the answer would exceed 18 hours.
- **Precision falls as the number grows:** to the minute under 15, then to 5 minutes, then
  in hours and minutes.

`test_arrival.c` has 48 checks, including the FR24 cross-check. A mutation run broke every
rule in turn and the tests caught each one — except one: a special case for the last 2 nm,
which changed nothing (the arithmetic already rounds to 0–2 minutes) and was deleted.

**Only one route line fits, and that shaped the design.** The first build had the
distance-from-origin and the arrival as two lines. The simulator showed neither of them ever
appearing. Under a one-line destination the card has room for exactly **two** supporting
lines. Measured, the combined sentence is 506–584 px against 440, and only abbreviations
like "in 45 Min." fit, which cost the plain language this screen exists for. So one line,
chosen by `view_build` (the screen chooses no text), and the data splits it cleanly:
- **arriving or cruising:** *Landung in etwa 3 Minuten*;
- **climbing out** (no estimate, by design): *43 km von Wien entfernt* — the moment when
  "how long ago did it leave" is the natural question.

Getting even two lines needed 8 px. The gap under the hero went from 16 to 8 px (its 100 px
line box already carries ~20 px of descender space). The margin above the data band went from
8 to 4 px (a wrapped 25 px label measured 35 px tall, not the nominal 31, and route line +
identity came to 363 px against a 362 px limit).

**Speed sits beside the altitude** — the ALT/GS pair every traffic display uses — not on a
row of its own, which the card has no room for. It is never shown for the empty sky's
"last seen" aircraft: a speed from minutes ago beside a live-looking altitude would be a
stale number dressed as a current one.

**The bug the owner's screenshot request surfaced, fixed here.** The rule that gives way
when supporting lines do not fit (D48) HID every line whose already-computed position crossed
the limit, and nothing moved up into the space. A private DV20 whose type name wraps the hero
to two lines lost its reason sentence (correctly) and its registration too — which would have
fitted exactly where the sentence had been. The card showed ~70 px of nothing and no
identifier at all. Now the least important line goes and the rest **close up**. Keep order,
most important first: identity, then reason, then route line, then type line. The identity
comes first because it is the only line that says *which* aircraft, and the owner has asked
for it twice. That puts it above the reason, the reverse of the old drop order; the old
order never actually kept the reason visible either, because nothing reflowed.

**Verified:**
- host suites: `test_arrival` (48 checks), `test_view` (+ route-line choice), `test_parse`
  (airport coordinates);
- **`test/sim/sim_detail.c`**, new: the detail layer rendered by its own code from real
  aircraft — the 15:14 capture over the Vienna preset and real routes — plus two constructed
  positions, which say so. It checks from pixels: the number of lines between hero and band,
  no supporting text inside the band, no hole between lines, and speed beside the altitude
  but not on the empty sky. 23 checks. All seven mutants tried were caught: the old
  no-close-up behaviour, the identity ranked last, both gaps restored, a stale speed on the
  empty sky, no speed at all, and the departed distance preferred over the arrival;
- shared harness: `test/sim/sim_common.h` now carries what both simulators need;
- the exact CI image (Linux, gcc): all green, no warnings; the firmware builds.

**Not verified:** on the glass, and against real landings. The estimate matched FR24 on one
example. Whether it is usually within "etwa" can only be learned by watching it against
arrivals over Schwechat. The new German strings (*Landung in etwa …*, *… km von …
entfernt*) still need the read-aloud pass (D51, D56, D57).

**Amended the same day, by the owner: abbreviated units.** *Landung in etwa 45 Min.*, *… 1
Std. 40 Min.*, *… 2 Std.* (the Duden forms). The singular/plural split goes with them, since
"Std." has none. *Landung in wenigen Minuten* stays spelled out: no number stands in front of
it, so it is a phrase, not a unit. The examples above keep the long forms they were written
with.

**Open, for the owner:** both route facts at once would need ~40 px from somewhere else on
the card. The candidate is the compass tape, which repeats what "südöstlich" already says.
That is a design trade only he can make.

## D80 — D75–D79 on the glass: what the panel confirmed, one thing it caught

**Tested on the unit on 2026-09-24 between 23:20 and 23:45, over the Vienna preset at 33 nm,
on a weak link (−70 dBm).** The test build was `main` at `f430e2a`, plus the fix below.

**The update path proved itself first.** Straight after the first flash the device was
inside its night window, and within a minute and a half it downloaded v0.8.0 from the
GitHub release, wrote it and rebooted into it — replacing the test build. That was the real
end-to-end OTA of a real release, and it worked. To keep testing, updates were switched
off from the console (`u`, then `-`) and the test build flashed again. **At the time of
writing they are still off**, so that the test build is not replaced before the last touch
check. They must go back to exactly
`https://github.com/MonsJovis/esp-flight-monitor/releases/latest/download/manifest.json`
(PLAN.md M13). Restored while v0.8.0 is still the latest release, the device would reinstall
v0.8.0 that same night; restored after a v0.9.0 release, it installs that.

**Confirmed on the panel:**
- **Radar (D75, D76, D78):** the two-line caption on real flights
  (`TVS2965 · Boeing 737-800` / `Prag 9,9 km O →`, and for a route-less aircraft
  `ENT4804 · Boeing 737-800` / `9,8 km NO →`), the ring, trails, altitude sizes, and the
  scope lifted clear of the caption.
- **Detail card (D79):** AIZ282 Prag → Tel Aviv at cruise, "Landung in etwa 2 Std. 45 Min."
  and `10.058 m  910 km/h` — plausible against a ~2,290 km straight line to Tel Aviv. And
  AUA75J Wien → Tirana still climbing at 3,703 m, "30 km von Wien entfernt" with no
  estimate: the climb-out rule, working on a real departure.
- **Memory:** 50.9 KB internal free at rest; with the WLAN keyboard open, 32.1 KB free and a
  26.6 KB largest block. (An earlier reading of 40 KB was taken while the device was
  downloading v0.8.0.)
- **Stress (§11's third pattern):** 60 rapid cycles of opening and closing the card, with
  page switches and 6 full UI rebuilds — no panic, no reset, memory flat (50,679 → 50,503 B).
- **Touch, by the owner's finger, read back from 118 framebuffer captures and the log:**
  - tap a mark → the ring moves there and the caption follows;
  - tap the caption → the card opens (4×); after closing it, the same aircraft is still
    ringed;
  - tap empty scope → back to the nearest within 5 s;
  - 30 s untouched → back to the nearest, to the frame.

  The long press into Einstellungen with an aircraft selected was not done in this
  session. The device's touch counter confirms zero long presses.

**Caught by the panel and fixed: the nearest could be hidden.** Marks drew in feed order.
In one frame the magenta nearest sat half under a filled cyan dot that came later in the
array — the one aircraft the scope singles out, covered by one it does not. The nearest now
draws above every other mark; a tapped aircraft draws above that, and the ring above it. It
is set where the ring is placed, so taps, press previews and updates all get it, and
hit-testing follows the drawing. The simulator reproduced it first: with an aircraft right
on top of it, 14 of the nearest's 186 magenta pixels were visible. It now checks that at
least 80 % stay visible. 94 radar checks, 25 stress seeds.

**Not seen on the glass:** the KEINE DATEN screen with the new caption (an outage cannot be
forced from the console — the simulator covers it), and the long press above.

**Afterwards, 2026-09-25: v0.9.0 released, installed over the air, and one crash found.**
Updates were turned back on to the exact URL they had. The device checked at once, found
0.9.0, downloaded it and wrote it. It rebooted into it and confirmed it after its two
healthy minutes. A deliberate reset afterwards booted the same slot (`0x520000`), which is
the proof that the confirmation took: an unconfirmed image would have been rolled back.

**But the OLD image crashed on its way down.** Right after `ota: update written; rebooting
into 0.9.0`, with WiFi already stopped inside `esp_restart()`:

```
Guru Meditation Error: Core 0 panic'ed (StoreProhibited)
EXCVADDR 0xbad00c11   (A2 = 0xbad00bad, IDF's poison value)
backtrace: xt_highint4 -> panicHandler -> esp_panic_handler_reconfigure_wdts -> ROM
```

The panic handler itself faulted. It was entered through a level-4 interrupt, which on the
ESP32-S3 is where cache errors and the interrupt watchdog arrive, and the original trigger
was lost with the stack. The ELF matched (`ca4f8ba1c`). The likeliest story: the RGB panel
keeps DMA-reading its framebuffer from PSRAM while the restart path takes caches down. There
is no shutdown handler in this firmware to stop it first. **Wrong — see D81:** an ESP-IDF
5.4.0 race in `esp_restart()` itself, reproduced and fixed by moving to 5.4.4.

**Why nothing was lost, and why it still matters.** `esp_https_ota()` writes the whole image,
checks its hash and switches the boot partition before it returns. The panic came after
that, so the chip reset into the new image just as a clean restart would have, and core
dumps are not written to flash (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH` is off). But it is a
crash on what may be every install. Nobody would see it on the panel, which is exactly why
it needs writing down. Probably not new: nothing in D75–D80 touches the restart path, and
earlier installs were not watched on the serial log. Open in PLAN.md M13.

## D81 — The restart panic was ESP-IDF's, not ours: moved to IDF 5.4.4

D80's guess — the RGB panel's DMA still reading PSRAM — was wrong. The panic is a race
inside ESP-IDF 5.4.0's own `esp_restart_noos()` on the ESP32-S3, fixed upstream in March
2025 (espressif/esp-idf `6de3fde3c2`, "fix possible cache_error by another core accessing
flash in esp_restart", in every tag from v5.4.1 on).

**The race.** The restarting core turns the shared caches off, and only *then* resets the
other core. In between, the other core is still running — and with
`CONFIG_SPIRAM_FETCH_INSTRUCTIONS` nearly all of this firmware's code is fetched from PSRAM
through those caches. If it is busy at that moment it takes a cache-error interrupt (level
4: that is the `xt_highint4` in the backtrace). Its panic handler re-enables the cache and
stalls the restarting core, which is still inside `Cache_Disable_DCache()` writing dirty
lines back to PSRAM — so nobody resets it, and the handler runs on into
`esp_panic_handler_reconfigure_wdts()`, reads its `&TIMERG0` literal from a cache that has
just gone off again, and gets the S3's invalid-access value `0xbad00bad`. The store through
it is the `StoreProhibited` at `0xbad00c11` that D80 recorded. The fixed IDF resets and
stalls the other core *first*, so nothing is running when the caches go.

**Why only sometimes, and why the install.** It needs core 0 busy on cached code *and* a
data cache full of dirty lines at the instant core 1 restarts. An install is exactly that:
LVGL has been drawing into PSRAM framebuffers, WiFi has just been torn down on core 0.

**Reproduced before it was fixed.** New console key `R` restarts the way an install does
(`esp_restart()` from core 1, WiFi up, panel live) while a lowest-priority task keeps core 0
busy dirtying a 256 KB PSRAM buffer. The harness counts a run only if the request, a ROM
reset banner and a fresh `ready:` line appear after the key, in that order — the first
version stopped at the *previous* boot's `ready:` still sitting in the USB buffer and
reported ten clean restarts that never happened.

| build | restarts | panics |
|---|---|---|
| IDF 5.4.0, core 0 idle | 10 | 0 (the race needs a busy core 0) |
| IDF 5.4.0, core 0 busy | 30 | **5** — four with D80's exact registers (PC `0x4004795c`, A2 `0xbad00bad`, EXCVADDR `0xbad00c11`), one un-nested: *Cache disabled but cached memory region accessed* |
| IDF 5.4.4, core 0 busy | 39 | **0** |
| IDF 5.4.4, final build (FIFO console below) | 30 | **0** |

At 5 in 30, 69 clean restarts in a row would be chance about once in 300,000. "Rebooted" is
judged by the uptime counter restarting, not by the ROM banner: on 5.4.4 the host misses
that banner while USB re-enumerates in about one run in six.

**The fix is the IDF version.** `~/esp/esp-idf` is on v5.4.4, both workflows build in
`espressif/idf:v5.4.4`, and AGENTS.md/README say so. v5.4.4 also resets the DMA and crypto
peripherals on the way down (upstream `147ec10b86`, `5cdc53df23`), which the panel's DMA
guess would have wanted anyway. `sdkconfig` regenerated from the defaults under 5.4.4
differs from the 5.4.0 one only in its version stamp and `PERIPH_CTRL_FUNC_IN_IRAM`, which
5.4.4 dropped. Host suite and both simulators unchanged and green.

**The upgrade broke the console, and the first fix for that cost 4.6 KB.** On 5.4.4 the
device ignored every key — no screenshot, no provisioning, no `R`. The first 40-restart run
on the new IDF therefore counted nothing at all, which is how it was noticed. Espressif made
the USB-Serial-JTAG `read()` "POSIX compliant" (`8182f20774`): it now sizes each read by the
bytes waiting in the *driver's* RX ring, and D22 is why this firmware never installs that
driver, so the answer was always zero. (`clearerr()` on the sticky EOF was tried first, on
a guess, and changed nothing.) Installing the driver for input only worked — output stayed
driverless, as D22 requires — but measured back to back on the same IDF it cost **4.6 KB of
internal RAM at steady state** (31.4 KB free against 36.0 KB). And leaving the install out
of a test build to measure that crash-looped the board past esptool's reach until it was
unplugged: `usb_serial_jtag_read_bytes()` dereferences its NULL context without a driver.
What shipped instead: `dbg_screen.c` reads the hardware RX FIFO directly
(`usb_serial_jtag_ll_read_rxfifo()`), the same call 5.4.0's driverless read made — no
driver, no ring, no interrupt, no RAM. Keys, screenshots (CRC ok) and a 97-character line
through `dbg_read_line()`, longer than the 64-byte FIFO, all verified on the device.

Seen on the way and left alone: in `dbg_read_line()` a bare ENTER never returns (leading
newlines are skipped), so the update console's "ENTER alone leaves it as it is" really
means "wait two minutes for the timeout". Harmless — the timeout stores nothing — but the
prompt says otherwise.

**What 5.4.4 costs.** The same source built on both, measured back to back on the device:

| | IDF 5.4.0 | IDF 5.4.4 |
|---|---|---|
| internal free, steady state | ~40.2 KB | ~36.2 KB |
| internal free, WLAN keyboard open | 21.8 KB | 21.8 KB |
| largest block, keyboard open | — | 16.0 KB (was 21.5 KB in D77, on 5.4.0) |

About 4 KB less at rest, which is IDF's own; the keyboard — the worst case this firmware
knows (D58) — lands on exactly the same free total. After it closes, the largest block stays
at 16 KB. Polls kept succeeding through all of it (the one failure seen was adsb.lol
answering HTTP 429), and TLS keeps its record buffers in PSRAM (D52), so nothing is known
to need more. Written down because the margin is smaller than it was, not because anything
failed.

**What this does not fix.** The restart code runs from the image being *replaced*. The
install that brings 0.9.1 onto a device still ends in 0.9.0's `esp_restart()` and can still
panic, as harmlessly as before (D80: the image is written, verified and selected first).
Every install after that is clean.

**Found on the way, not changed:** `sdkconfig.defaults` sets
`CONFIG_MBEDTLS_DYNAMIC_FREE_PEER_CERT`, which no 5.4 release has ever had — every build
warns *unknown kconfig symbol* — and `DYNAMIC_FREE_CA_CERT` depends on
`DYNAMIC_FREE_CONFIG_DATA`, which is not set, so neither line does anything. Left for its
own change: enabling them alters TLS memory behaviour on a board with 40 KB internal free.

## D82 — A move of the device shows the new place's sky, or says it is looking

**The question, from the owner, before 1.0:** when the location is switched, is it clear
whether the radar and the list are refreshing? **It was not, and it was worse than
unclear.** Read in the code, then seen on the device:

- `flight_source_set_location()` changed three numbers and nothing else. Its header said
  so on purpose: *does not clear the published snapshot*.
- adsb.lol's `dst` and `dir` are measured **from the query point**. So until the next
  poll, the radar drew the **old place's aircraft around the new place**, at the old
  distances and bearings, and the list counted them *in Reichweite*. Nothing on either
  screen said so.
- "Until the next poll" was up to 12 s at best. After a run of failures it was the
  backoff, up to **five minutes**, because the old place's failures carried over.
- A poll already on the wire when he moved finished afterwards and **published the old
  place again**, over whatever the new one had shown.
- Nothing separated "no answer yet" from "nothing up there". At boot and after a move,
  the list said *Der Himmel ist frei.*, the one false sentence on the screen, and the
  radar was an empty scope, which reads the same way.

**What a move does now (`flight_source.c`):**
- It clears the snapshot, the last-success time and the failure count.
- It bumps a location generation. A poll remembers the generation it was built for and
  **throws its answer away** if the device moved while it was out. That answer counts
  neither as data nor as a failure.
- It wakes the poller at once; the old code waited out a fixed delay.
- `SRC_MOVE_MIN_GAP_MS` (3 s) keeps two quick moves from becoming the burst adsb.lol
  throttles (AGENTS.md §5). The wait lives in the poller, never on the caller: the
  caller is the LVGL thread, a tap on a search hit.
- A radius change alone clears nothing and costs no request. The aircraft are still
  measured from the right point. `main.c` drops the ones beyond a **smaller** ring
  until the next poll, instead of the radar pinning them to the edge. A bigger ring
  fills in at the next poll.
- Settings changes that are not moves, such as brightness or the night window, go
  through the same `apply_settings()` and are recognised as not moves.

**What the screens say until the first answer for this place** (`flight_source_has_data()`):

| | network up | network down |
|---|---|---|
| **Liste** | *Suche Flugzeuge...* in the header line, the bar under it, three ghost rows where the rows will land | *Noch keine Flugdaten.* — nothing moves (DESIGN.md §4: a condition gets a sentence) |
| **Radar** | *Suche Flugzeuge...* top centre in label grey, the bar under it | the amber `KEIN NETZ` / `KEINE DATEN` it already had |

This is DESIGN.md §4's busy pattern, the same as the WLAN scan and the place search: one
moving thing, and skeleton rows only on an empty list. A list he can read is never replaced
with ghosts.

**What the UI forgets on a move (`main.c`, `screen_radar_forget_place()`):** the tapped
aircraft, the open detail card, the "last seen" aircraft, the radar's trails and its
nearest-mark hysteresis. All of them describe a sky he is no longer under.

**Checked:**
- Host: `test/sim/sim_list.c` is new and covers the list's three empty states and "rows
  win". `sim_radar.c` gained the wait, the wait under a dead network, the wait ending,
  and the forget.
- On the device, via the real path (place search, tap the first hit): the poll for the
  new place left 36 ms after the hits came back, and its answer was parsed 0.46 s after
  that. Before, the wait was 12 s at best, with the old sky on screen throughout.
- Also on the device: a request already hanging when the device moved (the link was
  slow, and it took 25 s to fail) was discarded, logged as such, and the new place was
  polled immediately after.

**Found on the way, not changed: a move can start the night update.** The night window is
local time (D74), and the timezone follows the place. Switching the device to Pattaya at
17:18 in Austria made it 22:18 there, inside the window, and the pending v0.9.1 installed
and restarted within a minute. That is the policy working as written: a device that stands
in Pattaya would update at that hour anyway. It is recorded because, from his chair, it
looks like "I picked a town and it restarted".

## D83 — Ground traffic: shown only if it has just landed, because departures are not knowable

**The owner's rule:** show an aircraft on the ground only if it departs within ten minutes
or landed within the last ten, *if we have this data*. This settles what D77 left open: the
parked aircraft at Schwechat that crowd the inner ring and the top of the list. Over Vienna
on 2026-09-25 that was 8 of 35 aircraft.

**Half of the rule is knowable, and only half is built.**
- **No feed this device can reach has a schedule** (D79). adsb.lol has positions and
  adsb.im has the two airports.
- **"Landed in the last ten minutes" is knowable by watching.** An aircraft this device
  saw airborne, and now sees with `alt_baro: "ground"`, has landed within a poll (12 s)
  of that last airborne sighting. `main/data/ground_filter.c` remembers that time per
  aircraft: 160 slots in PSRAM, expired slots reused first.
- **"Departs in the next ten minutes" is not knowable.** A taxiing aircraft may be
  going out, coming in, or on a tow bar. A guess would be the confident wrong answer
  this device is built not to give. A departure appears the moment it is airborne,
  seconds into its take-off roll.
- **Everything else on the ground stays hidden.** That includes an aircraft that
  landed before the device was switched on or moved: nobody saw it land.
- **An unknown altitude is not "on the ground",** and it is kept as before. It is not
  evidence of flight either, so it cannot become the airborne half of a landing.

**The filter runs inside the parser, before the nearest-24 cut** (`adsb_parse_ex()`).
Filtered after the cut, the apron would take its places among the 24 nearest and push
airborne aircraft out. `test_parse.c` builds exactly that sky, and `test_ground.c` covers
the rule and the table.

**Checked on the device over Vienna, 15 minutes on 2026-09-25:**
- 3 to 9 aircraft on the ground were hidden per poll. The log line is `on the ground:
  N hidden, M shown as just landed`.
- Three landings were caught and shown from the first poll after touchdown: AUA64A,
  RYR9VJ and RYR525D.
- Each dropped out of the feed 6 to 9 minutes later, most likely with its transponder
  switched off at the gate. So the ten-minute cut-off itself was not reached live, and
  `test_ground.c` covers it.

## D84 — The detail card waits in the shape of its answer

**The owner's request:** a better loading state for the detail view, perhaps with skeletons.

**What it was.** While a route was being looked up, the card showed:
- an **amber** *ROUTE WIRD GESUCHT* in the top row, with the bar;
- the **model name as headline**, in the destination's place ("Airbus A321");
- *Die Route wird noch gesucht.* under it.

Two things were wrong with it. Amber is this device's caution colour, and a lookup that
takes a second is not a caution. And the headline changed meaning when the answer landed:
"Airbus A321" became "Frankfurt" in the same 100 px slot, which is the jump §5.1/§5.2's
fixed layout exists to avoid.

**What it is now (`screen_overhead.c`).**
- A ghost where the origin goes (30 % wide), with the bar under it at the same width.
  The bar is for the thing that is still coming, not for the whole screen.
- A ghost where the destination goes (62 % wide), an x-height tall, on the hero's line.
- Then *Die Route wird noch gesucht.*, because a skeleton says where the answer will
  land but only words say what is happening (DESIGN.md §4).
- Then the identity line, now with the model: "AUA1Y · Airbus A321". `view_build.c`
  keeps the type out of the dedup while searching, since the hero no longer shows it.
- The data band is unchanged. Height, speed and distance are known, so they are shown.
- The model is untouched otherwise: `vm.hero` is still the type and `route_searching`
  still marks the state, so test_view's M4 check ("pending and settled share the hero")
  stands. The skeleton is purely how the screen draws that state.

The settled answers are as before: a route replaces the ghosts with "Wien → Frankfurt",
and no route brings the amber *KEIN FLUGPLAN* and the model name as headline.
`STR_ROUTE_SEARCHING` is gone.

**`widget_busy_ghost()` caps its corner radius at 10 px.** A fully round 52 px ghost is a
pill, and a pill on a touch screen is a button. Every ghost up to 20 px tall, which is all
of them before this one, draws exactly as before.

**Checked:** `sim_detail.c` renders both states and checks them from pixels:
- searching: no amber, no white headline, both ghosts, the bar, and the model in the
  identity;
- settled: the amber tag, the headline, and no ghost left behind.

On the panel, console key `5` (the replay with the lookup outstanding) matches the
simulator.

**Found on the way, and fixed:** every fixture replay (keys `1`–`5`) showed an amber
*KEIN NETZ* over a working network. `dbg_fixture.c` still passed `true` for what was a bool
"online" until M8 made it `net_state_t`, and `true` converts to `NET_NO_WIFI`. It is
`NET_OK` now. `test_view.c` passes the same stale `true` in eight places. That affects no
check there, and it is left for a separate cleanup.

## D85 — Every detail card opens on its skeleton, and fills in at once

**The owner's choice,** after D84's skeleton turned out to be almost never seen: routes are
cached (and survive a restart), and a new callsign resolves within ~15 s, so the lookup
state is over before he can tap the aircraft. Of the two options put to him, (1) leave it
or (2) show it on every open for a short moment, he chose (2). I had recommended (1),
because (2) is a wait that has no work behind it.

**What looking for it found: the card had a real loading gap, and it was broken.**
`ui_task` repaints on a 2 s tick, and opening a card did not wake it. So for 0–2 s after
every tap the card was a freshly built, never-updated tree. The compass tape showed and
pointed north at nothing. Every other label was hidden, including **"Zurück"**, so the
card could not visibly be left. `sim_detail.c` now renders that moment
(`detail_00_just_opened`).

**Now:**
- **`screen_overhead_create()` draws the whole card as ghosts.** Origin with the bar
  under it, destination, two supporting lines, and the altitude and distance rows,
  anchored to the bottom edge like the real band. "Zurück" sits where it always is.
- **The first `screen_overhead_update()` takes the skeleton down.** Every label sets its
  own visibility on every update (checked), so only the extra ghosts need hiding.
- **`open_detail()` wakes `ui_task`,** and the card holds its skeleton for
  `DETAIL_SKELETON_MS` = **600 ms**. Then `ui_task` comes back the moment that runs out
  instead of on the next tick.
- **A card opens in 0.6 s, every time.** Before, it was blank for anywhere between 0 and
  2 s.
- **The route-lookup state from D84 continues seamlessly.** It shares the origin and
  destination ghosts at the same positions, so a card whose route is still coming keeps
  those two ghosts and gains its sentence, identity and data.

**A ghost's height is a constant, not `lv_obj_get_height()`.** Right after create nothing
has been laid out, the height reads 0, and the hero ghost was drawn 26 px low. The
simulator caught it by rendering the two states side by side.

**Checked:**
- `sim_detail.c`: just opened, there is a ghost in every band, the bar, "Zurück", and no
  compass; after the first update, no ghost pixel is left anywhere below the chrome.
- On the panel via console `i`: the skeleton at 150 ms after opening, and a complete
  card (Pegasus, London → Istanbul) at 1 s.

## D86 — The opening skeleton comes out again

**The owner, on the panel, after a day with v1.0.0:** the loading state in the detail
view is too much. Remove it.

**What is removed.** D85's whole-card skeleton and its 600 ms minimum. A card no longer
opens on placeholders that it then has to replace.

**What stays, and why:**
- **`open_detail()` still wakes `ui_task`.** That was the real fix in D85: without it the
  card sat blank for up to two seconds after a tap. Now it is filled as soon as the task
  runs, well under a frame's worth of waiting.
- **The moment before that fill is quiet.** It shows only "Zurück". Before D85 it showed
  the compass tape pointing north at nothing (`start_unfilled()` in `screen_overhead.c`).
- **D84's route-lookup skeleton stays.** It shows only while a route is genuinely still
  being looked up, which is rare (D85 found how rare), and it stands for a real wait,
  unlike the one removed here.

`sim_detail.c` now checks the opposite of D85's check: a card that has just opened shows
no ghost, no bar and no compass, only "Zurück".

## D87 — Backoff only when the service says no, never because the network is down

**The owner, 2026-09-26:** after a start, the radar took forever to show anything.

**What it was, checked from both ends.**
- **The device's side:** WiFi associated in 5 s and DHCP gave it an address and the
  router as DNS. After that, every DNS lookup timed out after 7 s: for adsb.lol, and
  for SNTP (the clock never set). A connection by raw IP failed too.
- **The Mac's side, same router at the same moment:** it pinged the device with no loss
  (some replies took 360 ms), got answers from the router's DNS in 6 ms, and fetched
  the same adsb.lol URL in 0.1 s.
- **Then it recovered.** About 20 minutes after boot the device's lookups started
  working again, with no change on the device (adsb.lol also resolved to a different
  address). So the outage was the local network or access point failing this one
  client, not the firmware. 1.0.x changed nothing in the network path, and earlier
  builds on the same network had data 15 s after every boot.

**What the firmware made worse.** Every failed poll doubled the wait before the next,
from 12 s up to 5 minutes (`source_backoff_delay_ms`). That backoff exists to keep a free
community service from banning the device for hammering it (AGENTS.md §5). But a lookup
that times out never reaches the service. So once the network came back, the panel could
still sit empty for up to five minutes, protecting nobody. The only existing escape was
WiFi reconnecting, and here WiFi never dropped.

**Now:**
- **`source_failure_backs_off()`** (`source_logic.c`, pure and host-tested) is true only
  when the service answered with an error: throttling (429, 503, adsb.lol's spurious
  308) or any other non-200 status.
- **No answer at all** (DNS, connect or timeout) retries at the normal 12 s cadence. So
  does a 200 whose body did not parse, since that is almost always the link cutting it
  short.
- **The two counters are separate.** `service_failures` drives the backoff.
  `consec_failures` still drives the "KEINE DATEN" caution after three misses, exactly
  as before.
- **AGENTS.md §5's floor still holds:** nothing polls faster than every 10 s, and a
  service that does answer with an error still backs off to 5 minutes.
- **Net effect:** data is on screen within one poll of the network coming back.

**Seen again at 17:53 the same day,** after another restart: the same wrong entry
(`20:e1:5d:9f:42:e7`), read twice 35 s apart, with no data since boot.

**Confirmed from a Mac on the same WiFi,** by capturing the ARP exchange while it asked
for 192.168.0.1. Two devices answered, 2 µs apart:
`54:67:51:bb:3b:26` (Compal, the router) and `20:e1:5d:9f:42:e7` (TP-Link). Whichever
reply lands first wins. The Mac got the router's reply first and the panel got the
TP-Link's; that is the whole difference between them. A TP-Link router or extender
ships with 192.168.0.1 as its own address, the same as this router.

**Resolved on the network, the same evening.** The TP-Link was an Easy Smart **managed
switch**, whose factory address is 192.168.0.1. Its web UI was reached from the Mac by
pointing 192.168.0.1 at the switch's MAC for ten minutes; `arp -s … ifscope en0` is
needed, because macOS ignores an entry that is not bound to the interface. It was then
moved to **192.168.0.250**. Afterwards only the router answered ARP for 192.168.0.1, and
the panel had its first aircraft **9.5 s after power-on**.

**Checked on the device (v1.0.2):** after a fresh boot the outage came straight back, so
the new cadence could be watched live. Failed polls followed each other every ~19 s (12 s
plus the 7 s DNS timeout) for minutes, never growing.

**The cause, found because it comes back on every boot.** The console's `n` now also
prints which hardware address the device's ARP table holds for the gateway
(`log_gw_arp()` in `main.c`):

    gw 192.168.0.1 is at 20:e1:5d:9f:42:e7 in the ARP table

**The router is 54:67:51:bb:3b:26**, as the Mac on the same network sees it. Something
else answers ARP for 192.168.0.1 on the device's side. The device connects through
another access point (BSSID 90:41:b2:…), and the Mac cannot see that MAC at all. So the
device sends everything meant for the router to that box:
- **LAN traffic works:** the Mac pings the device.
- **DNS mostly times out.** The one answer that did come back, 89.58.11.153, is what
  the real router also returns, so it proves nothing about where it came from. An
  earlier note here called it stale; it was not.
- **Nothing reaches the internet.**
- **It clears when that ARP entry is replaced,** which took about 20 minutes on
  2026-09-26.

This is a **network misconfiguration**: a second device claiming the router's address,
typically a repeater, a second router, or a box with a static IP of 192.168.0.1. The fix
is on the network. D87 only makes sure the panel has data within one poll once it is
fixed.
