# Data architecture, and the rate limits under it

Which services this device talks to, why those and not the obvious ones, and what they
do when you ask too often. The rate limits were measured against the live endpoints,
not read off a documentation page — most of these services document none.
Routed here from AGENTS.md §4 and §5.

---

The ESP32 **cannot receive ADS-B** — 1090 MHz needs an SDR. All aircraft data comes from
free community HTTP APIs. Every endpoint below was live-tested on 2026-09-18, and again on
2026-10-19, when adsb.lol had moved to HTTPS (D92).

```
Positions  →  GET  https://api.adsb.lol/v2/point/{lat}/{lon}/{radius_nm}
Routes     →  POST http://adsb.im/api/0/routeset            (batched!)
Type names →  const table in flash: ICAO code → "Airbus A320neo"
Places     →  GET  http://geocoding-api.open-meteo.com/v1/search?name=…&language=de
```

### Why this combination

- **~~Both work over plain HTTP with no redirect.~~ Amended 2026-10-19 (D92): adsb.lol
  now answers plain HTTP with a `301` to https://, every request, so positions are polled
  over HTTPS.** The rule used to be "no TLS on the data path": no handshake, and roughly
  40 KB more free heap per connection. OTA had already brought TLS and the Mozilla bundle
  in (D72) with its record buffers in PSRAM, so the heap half of that argument was gone
  before adsb.lol forced the question. What is left is one handshake per poll. Hosts that
  still answer plain HTTP keep it: adsb.im's routeset (`200`, and tar1090's own source
  notes adsb.im *prefers* HTTP) and Open-Meteo (`200`), both checked 2026-10-19.
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
| Routes | `adsb.im` routeset (batch) | `hexdb.io/callsign-route-iata` (**7 bytes**, HTTPS only since 2026-10) | `adsbdb.com/v0/callsign` (adds airline + airport names; HTTPS only) |
| Airline | `airline_code` from routeset | `hexdb.io/hex-airline` | `adsbdb /v0/airline` |
| Photo | `planespotters.net` (**custom UA required**) | `hexdb.io/hex-image` | — |

None of the fallbacks is wired into `main/`. Every one of them redirects plain HTTP to
https:// as of 2026-10-19 (`301`), which now only means each needs an `https://` URL.

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
- **adsb.lol now advertises a daily quota.** Every answer on 2026-10-19 carried
  `x-ratelimit-limit: 4, 4;w=1, 60;w=60, 500;w=86400`: 4 per second, 60 per minute,
  **500 per day**. A panel at the 12 s cadence makes about 7,200 polls a day. Whether the
  daily figure is enforced per IP was not tested, because testing it means spending it.
  Their README says limits are "dynamic based on the environment load" and that an API
  key for feeders is coming. If the panel's log starts showing `throttled (HTTP 429)`
  after about 100 minutes of a day, this is why (D92).
- **`adsb.fi` is 1 request/second** and returns a bare `400` when annoyed.
- **`adsbdb.com`**: 512 req/min → `429`; ≥1024 → **5-minute lockout**.
- **`hexdb.io`** publishes no limit but asks you not to scrape. Cache aggressively.

**Implement exponential backoff and a source-health check from day one.** Two consecutive
failures on the primary → switch to the fallback for a few minutes. A tight retry loop
will get the device IP-banned from a free community service.

**Cache routes per callsign for the whole flight** — a route never changes mid-flight.
In practice that means one `routeset` POST every few minutes, not one per poll.

---

## Flight numbers for alphanumeric callsigns (D94)

Nothing above knows that `AUA25L` is OS 254. ADS-B carries only the callsign, and every
route database stores its suffix as the "number" (adsb.im: `"number": "25L"`, adsbdb:
`"callsign_iata": "OS25L"`, both checked 2026-10-19). The research that day found no free
source that maps the two and may be read by a program. The ones that know are
Flightradar24's feed and flightaware.com, whose terms forbid robots, and Vienna Airport's
board JSON, whose terms forbid automated access.

What the panel does instead is match on airline, route and time against a timetable that
CI builds:

```
CI      →  POST https://api.lufthansa.com/v1/oauth/token        (client credentials)
CI      →  GET  https://api.lufthansa.com/v1/operations/flightstatus/{arrivals|departures}/VIE/{yyyy-MM-ddTHH:mm}
CI      →  GET  https://davidmegginson.github.io/ourairports-data/airports.csv   (IATA → ICAO)
Panel   →  GET  https://raw.githubusercontent.com/MonsJovis/esp-flight-monitor/timetable/vie.csv
```

- **Lufthansa Open API, Public Plan.** Free, registration at developer.lufthansa.com,
  OAuth2 client credentials. A window is four hours and returns at most 80 flights; one
  flight comes back as an object, several as a list (their Known Issues page). No callsign,
  no registration in the answer. Covers the Lufthansa Group: Austrian, Lufthansa, Swiss,
  Brussels and the rest, a little over half of VIE's movements. **Never seen answering
  for this repo:** nobody had a key when it was built.
- **The CI asks 64 times a run** (one window an hour, from 4 h back to 28 h ahead,
  arrivals and departures), every two hours. That is under 800 calls a day.
- **The panel asks once an hour**, ~25 KB, and gets a 404 until the workflow has
  published something. A 404 changes nothing on screen.
- **Austrian keeps the leading digits.** In all 11 pairs checked, the callsign's digits
  were the start of the OS number and the letters were free: AUA25L = OS 254, AUA25M =
  OS 257, AUA52TV = OS 527. So for Austrian the match also requires that prefix. No other
  airline at Vienna does this (DLH6HE = LH 1340, RYR93QM = FR 780).
- **Callsigns move between seasons.** AUA25L flew Frankfurt to Graz until 31 Oct 2025
  and Hamburg to Vienna after. The match never caches a callsign's number: it matches
  route and time afresh every two seconds, so a season change cannot leave a stale number
  behind. The next change is Sun 25 Oct 2026.
