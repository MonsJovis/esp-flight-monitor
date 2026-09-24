# Data architecture, and the rate limits under it

Which services this device talks to, why those and not the obvious ones, and what they
do when you ask too often. The rate limits were measured against the live endpoints,
not read off a documentation page — most of these services document none.
Routed here from AGENTS.md §4 and §5.

---

The ESP32 **cannot receive ADS-B** — 1090 MHz needs an SDR. All aircraft data comes from
free community HTTP APIs. Every endpoint below was live-tested on 2026-09-18.

```
Positions  →  GET  http://api.adsb.lol/v2/point/{lat}/{lon}/{radius_nm}
Routes     →  POST http://adsb.im/api/0/routeset            (batched!)
Type names →  const table in flash: ICAO code → "Airbus A320neo"
Places     →  GET  http://geocoding-api.open-meteo.com/v1/search?name=…&language=de
```

### Why this combination

- **Both work over plain HTTP with no redirect.** No TLS *on the data path* means no
  handshake and roughly 40 KB more free heap per connection — the single biggest win
  available on this platform. (OTA does carry TLS and the Mozilla bundle, D72. The data
  path stays plain on purpose; do not "upgrade" it.) tar1090's own source notes
  adsb.im *prefers* HTTP here.
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

---

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
