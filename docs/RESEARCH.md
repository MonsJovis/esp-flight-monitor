# Flight Radar on ESP32 — Research & Recommendations

Research date **2026-09-18**. Every API endpoint and payload size below was called live
from a real connection on that date, not quoted from documentation. Two commonly
recommended options turned out to be dead or broken — see §3.

---

## 1. Executive summary

**Do not build this from scratch.** This niche has ~15 working open-source projects and
the field has converged on one architecture. The plan:

1. Fork the structure of **MatixYo/ESP32-Plane-Radar** (MIT, 994★) — the de-facto upstream.
2. Take the route/type enrichment from **ironicbadger/ESP32-Plane-Radar** (MIT).
3. Take the batch route parser from **kovaacs/sky_overhead** (MIT) — the best approach,
   and almost nobody uses it.
4. Port the UI to this board's 480×480 RGB panel with ESP-IDF + LVGL 9.
5. Diverge from all of them on one point: **make the route the headline**, not a detail.

The one genuinely novel thing this project should do is put *"Wien → London, Airbus
A320neo, Austrian Airlines"* in large type as the default screen. Every existing project
builds a radar scope first and treats route as a tap-to-reveal detail. That is backwards
for this user.

---

## 2. The hard part is already solved

The brief assumed aircraft **type** and **route** would be the difficult part. It is not,
and the answer is better than expected.

### Type is free

`adsb.lol` and `adsb.fi` are derived from readsb/tar1090, so the position response
**already carries the aircraft type** — no second call:

```json
{"hex":"46b826","flight":"AEE13   ","r":"SX-NAF","t":"A21N",
 "alt_baro":36000,"dst":82.702,"dir":253.4, ...}
```

`adsb.fi` goes further and adds `"desc":"AIRBUS A-321"` and `"ownOp"` inline. `adsb.lol`
gives only the `t` code — which is fine, because type names belong in a flash table
anyway (§5).

### Route via batch POST — the key finding

This is the most valuable result of the research, and only one ESP32 project uses it.
It is the endpoint tar1090 itself uses for its route column:

```
POST http://adsb.im/api/0/routeset
Content-Type: application/json
{"planes":[{"callsign":"AUA453","lat":47.67,"lng":15.93}]}
```

Verified response:

```json
[{"callsign":"AUA453","airline_code":"AUA","number":"453",
  "airport_codes":"LOWW-EGLL","_airport_codes_iata":"VIE-LHR","plausible":true,
  "_airports":[{"iata":"VIE","icao":"LOWW","location":"Vienna","name":"Vienna International Airport"},
               {"iata":"LHR","icao":"EGLL","location":"London","name":"London Heathrow"}]}]
```

Why this beats the alternatives:

- **Batched** — one POST resolves every aircraft on screen, not N TLS handshakes.
- **Plain HTTP** — tar1090's source carries a comment that adsb.im *prefers* HTTP here.
- **`plausible` flag** — the server cross-checks the route against the live position and
  flags nonsense. A free sanity filter.
- **City names included** — "Vienna", "London", not just ICAO codes.
- Multi-leg routes come back as >2 airports; take first and last.

Live-verified samples: `AUA453` → VIE-LHR · `THY1MJ` → IST-STN · `SXS4WY` → AYT-FRA ·
`THA921` → FRA-BKK · `UAE376` → DXB-BKK.

### Richer enrichment, if wanted

`adsbdb.com` returns full airline and airport objects plus a photo URL, at 651 bytes per
callsign over HTTPS:

```
GET https://api.adsbdb.com/v0/callsign/AEE13
→ Aegean Airlines, Athens (ATH/LGAV) → Frankfurt (FRA/EDDF), with coordinates
GET https://api.adsbdb.com/v0/aircraft/46b826
→ Airbus A321-271NX, SX-NAF, Aegean Airlines, + url_photo
```

`hexdb.io` is the opposite extreme — tiny responses, ideal for a constrained device:

| Call | Response | Bytes |
|---|---|---|
| `hexdb.io/callsign-route-iata?callsign=PGT907H` | `AYT-MUC` | **7** |
| `hexdb.io/hex-type?hex=4bb865` | `A320 251NSL` | 11 |
| `hexdb.io/hex-airline?hex=4bb865` | `Pegasus Airlines` | 16 |

---

## 3. Data source comparison

All tested live 2026-09-18.

| Source | Auth | Plain HTTP | Type? | Route? | Verdict |
|---|---|---|---|---|---|
| **adsb.lol** | none | ✅ **yes** | `t`, `r` | no | **Primary.** ODbL 1.0. Pre-computes `dst`/`dir`. |
| **adsb.im routeset** | none | ✅ **yes** | — | ✅ batch | **The route answer.** |
| **adsb.fi** | none | ❌ 301→HTTPS | `t` + `desc` + `ownOp` | no | Best fallback. Non-commercial only, attribution required. 1 req/s. |
| **adsbdb.com** | none | ❌ HTTPS | ✅ + photos | ✅ per-callsign | Rich enrichment. 512/min → 429. |
| **hexdb.io** | none | ❌ HTTPS | ✅ | ✅ | Smallest payloads anywhere. Cache aggressively. |
| **OpenSky** | OAuth2 | ❌ | ❌ | ❌ | **Avoid.** 400 credits/day anon ≈ one poll per 3.6 min. |
| **airplanes.live** | **gated** | — | — | — | **Dead.** `403`, must email for access. Repo archived 2026-04-29. |
| FlightAware AeroAPI | key | — | ✅ | ✅ | $5/mo free ≈ 16 lookups/day. Not enough. |
| Flightradar24 | key | — | ✅ | ✅ | **No free tier.** $9/mo ≈ 166 polls. Scraping breaches ToS. |

### Measured payload sizes (Gloggnitz, adsb.lol)

| Radius | Bytes | Aircraft |
|---|---|---|
| 10 nm | 804 B | 2 |
| 25 nm | 3.8 KB | 7 |
| 50 nm | 20 KB | 35 |
| 100 nm | 62 KB | 89–100 |

**Use 30 nm.** Beyond that it is not "overhead" anyway, and 62 KB does not parse
comfortably on this device while LVGL holds a 450 KiB framebuffer.

### Rate limits — measured the hard way

During research, `adsb.lol` throttled at roughly the **7th rapid request**, returned `429`,
then escalated to a **multi-minute `503` cooldown**. While throttled it also emitted
spurious `308` redirects, which look like a routing change but are not. `adsb.fi` returns
a bare `400` when annoyed. Build backoff in from the start.

### Coverage check

| Location | 30 nm | 60 nm | 100 nm |
|---|---|---|---|
| Gloggnitz (47.6691 / 15.9303) | ~9 | ~45 | ~89 |
| Pattaya (12.9211 / 100.8721) | 7 | 36 | 55 |
| Bangkok | — | 43 | — |
| Chiang Mai | — | **2** (at 80 nm) | — |

Both of his actual addresses are well covered. Note that community-feed coverage is
thinner than the Flightradar24 app in some regions (FR24 has far more receivers plus
satellite ADS-B), so northern Thailand would disappoint. Worth setting expectations.

---

## 4. Existing projects worth copying from

### Tier 1

| Project | ★ | Licence | Why it matters |
|---|---|---|---|
| **[MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar)** | 994 | MIT | The upstream everyone forks. Cleanest, smallest, best-structured. |
| **[ironicbadger/ESP32-Plane-Radar](https://github.com/ironicbadger/ESP32-Plane-Radar)** | 122 | MIT | MatixYo **plus** route/type enrichment, weather, OTA, settings page. Highest-value diff in the field. |
| **[yashmulgaonkar/FlightScnr](https://github.com/yashmulgaonkar/FlightScnr)** | 284 | ⚠️ CC BY-NC-SA | Most complete. 6-stage route waterfall, embedded airline logos, ICAO type DB. **Read, don't paste.** |
| **[kovaacs/sky_overhead](https://github.com/kovaacs/sky_overhead)** | 10 | MIT | A single "plane overhead" **card**, not a radar — closest to our use case. Only project using batch `routeset`. Has unit tests. |

### Specific files to lift

| Need | From | File |
|---|---|---|
| Project skeleton, CI, web-flash release | MatixYo (MIT) | repo structure, `scripts/`, `.github/workflows/` |
| ADS-B client, non-blocking HTTP, 40-byte `Aircraft` struct | MatixYo (MIT) | `src/services/adsb_client.cpp`, `include/services/adsb_client.h` |
| Route cache (TTL, backoff, rate limiting) | ironicbadger (MIT) | `src/services/adsb_client.cpp` |
| **Batch route parser** | sky_overhead (MIT) | `RouteParser.h` + `fetchRoute()` |
| Geo projection, rim-dot bearing math | MatixYo (MIT) | `src/ui/radar_display.cpp` |
| Web-Mercator math, rotatable plane glyph, altitude-coloured trails | [ThingPulse PlaneSpotter](https://github.com/ThingPulse/esp8266-plane-spotter-color) (MIT, 2017) | `GeoMap.cpp`, `PlaneSpotter.cpp` |
| Airport/runway DB generator | MatixYo (MIT) | `scripts/build_large_airports.py` |
| Two-task LVGL/network split, memory budget | [TheJinxNL](https://github.com/TheJinxNL/ESP32FlightRadar) (MIT) | `src/network/flight_data.cpp` |
| Host-side unit test harness | sky_overhead (MIT) | `tools/run_unit_tests.sh` |
| Browser UI mockup to iterate without flashing | [capsule-radar](https://github.com/socquique/capsule-radar) (MIT) | `assets/plane_radar_2.0_mockup.html` |

### Worth reading, not copying

- **[2E0LXY/ESP32-ADS-B](https://github.com/2E0LXY/ESP32-ADS-B)** — ⚠️ no licence, but targets
  **this exact board family** (Waveshare Touch-LCD-4, 480×480) and its v2.6.0 release notes are
  the single most useful engineering document found: TLS allocation failures masquerading as
  certificate errors, moving the PNG decoder to PSRAM to reclaim 53 KB, and a font bug where a
  missing `.` glyph rendered `ALT:3.4KFT` as `ALT 3 4KFT` — a *different number*, not an
  obviously missing character.
- **[Krasnov777/esp32-sky-gauge](https://github.com/Krasnov777/esp32-sky-gauge)** (MIT) — has
  exactly the right idle behaviour: rests on a clock/weather screen and **switches to the radar
  only when traffic is within N km**, returning 30 s after the sky clears.
- **[Eiswolf-BG/eiswolfs-flightradar-CYD](https://github.com/Eiswolf-BG/eiswolfs-flightradar-CYD)**
  — ⚠️ no licence. Good ideas: silhouettes chosen from the ICAO type code, bolder glyph for
  >136 t "heavy" jets, orange ring for military via squawk range.

### Licence summary

- ✅ **MIT / Apache-2.0:** MatixYo, ironicbadger, capsule-radar, sky_overhead, TheJinxNL,
  Krasnov777, ThingPulse, nicholaswilde, adsbdb.
- ⚠️ **CC BY-NC-SA:** FlightScnr — non-commercial, viral share-alike.
- ⚠️ **GPL-3.0:** carlobolla/epaper-plane-tracker — copyleft.
- ⛔ **No licence = all rights reserved:** Eiswolf-BG, asanga-sci, Coreymillia, 2E0LXY,
  arvis91/deskradar, andyhomecode.

---

## 5. Recommended architecture

```
┌─ Network task ────────────────┐      ┌─ Display task ──────────────┐
│ adsb.lol /v2/point  (10–15 s) │      │ ALL LVGL calls              │
│ adsb.im /routeset   (on change)│ ───▶ │ LVGL 9.2 + esp_lvgl_port   │
│ route cache (NVS, per callsign)│ mutex│ 480×480 RGB565, PSRAM FB   │
│ backoff + source failover      │      │ GT911 touch (polled)        │
└────────────────────────────────┘      └─────────────────────────────┘
```

- **Type names in a PROGMEM table**, keyed on the `t` code. Removes an API dependency.
- **Cache routes per callsign for the whole flight** — a route never changes mid-flight.
  One `routeset` POST every few minutes, not one per poll.
- **Filter the JSON during parse.** ~6 of 50+ fields are needed; filtering a 4 KB response
  keeps the document near 1 KB.

### Proposed screens

1. **"Über dir jetzt"** (default) — the nearest aircraft as a large card: route as
   *Wien → London* in the biggest type on screen, airline name, plain-language aircraft
   type, altitude, and distance/direction ("12 km nordöstlich"). No interaction needed.
2. **Radar / list** — everything nearby, tap a blip for its card.
3. **Settings** — location preset (Gloggnitz / Pattaya / custom), radius, brightness, WiFi.

When the sky is empty, fall back to a clock rather than a blank screen, and auto-return to
screen 1 when traffic appears — Krasnov777's mode logic is the model.

---

## 6. Risks

| Risk | Mitigation |
|---|---|
| **Free API disappears or gates access** (airplanes.live did exactly this in 2026) | Build the fallback chain from day one; never hardcode one source |
| **IP ban from over-polling** | 10–15 s poll floor, exponential backoff, cache routes |
| **Display tears on NVS writes** — open bug on this exact silicon+panel | Keep XIP-from-PSRAM flags on; pause LVGL around writes |
| **WiFi bursts cause RGB drift** | Keep network work off the render path; separate task |
| Coverage thinner than the FR24 app in some regions | Set expectations; Gloggnitz and Pattaya both verified good |
| Route unresolvable for some flights | Distinguish "looking…" from "no route on file" — a 2E0LXY lesson |

---

## 7. Sources

- [adsb.lol API](https://api.adsb.lol/docs) · [open data terms](https://www.adsb.lol/docs/open-data/api/)
- [adsb.fi opendata](https://github.com/adsbfi/opendata)
- [adsbdb](https://github.com/mrjackwills/adsbdb) · [hexdb.io](https://hexdb.io/)
- [wiedehopf/tar1090](https://github.com/wiedehopf/tar1090) (route API default)
- [readsb JSON schema](https://github.com/wiedehopf/readsb/blob/dev/README-json.md)
- [airplanes.live archive notice](https://github.com/airplanes-live/api-archive)
- [OpenSky REST docs](https://openskynetwork.github.io/opensky-api/rest.html)
- [Waveshare ESP32-S3-Touch-LCD-4B repo](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-4B) · [BSP](https://components.espressif.com/components/waveshare/esp32_s3_touch_lcd_4b)
- [espressif/esp-bsp#570](https://github.com/espressif/esp-bsp/issues/570) — RGB tearing on flash write
- [ESP-FAQ: LCD / PSRAM bandwidth](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html)
