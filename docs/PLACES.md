# Locations

Where the device actually stands, and what follows from that for the clock, the poll
radius and the airports worth carrying. Routed here from AGENTS.md §6.

Three presets plus a custom entry. Store in NVS.

| Preset | Address | Lat / Lon |
|---|---|---|
| Gloggnitz (AT) | Semmeringstraße 11, 2640 Gloggnitz | `47.6691` / `15.9303` |
| Wien (AT) | Meiselstraße 79, 1140 Wien | `48.1984` / `16.3074` |
| Pattaya (TH) | 154 Thappraya Rd, Pattaya City, Chon Buri 20150 | `12.9211` / `100.8721` |

**The enum values are written to NVS, so the list is append-only.** `LOC_WIEN` is 3, after
`LOC_CUSTOM`, even though it belongs next to Gloggnitz on screen — renumbering would move a
device already in the field to a different city on a firmware update, silently. The order he
sees comes from `location_display_order()`, which exists for exactly that reason.

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
- **Switching location should be one tap**, not a coordinate entry form. Three named
  presets (Gloggnitz, Wien, Pattaya) plus an advanced custom option.
  **Built, 2026-09-20 — and the conclusion was not what this line assumed.** "Eigener Ort"
  shipped in M6 as a card with no way to set it: `custom_lat`/`custom_lon` were whatever
  `settings_defaults()` had put there, and a TODO in `screen_settings.c` marked coordinate
  entry as intentionally unimplemented *because a numeric keypad is the very form this rule
  forbids*. That reasoning was right and its conclusion was wrong. The way to set a
  location without a coordinate form is to **search for it by name** — §5.8 "Ort suchen",
  one row under the cards. He types a town, taps Suchen, taps the right hit out of a list;
  he never sees a coordinate and never types a decimal point.
- **Timezone changes with the location** — CEST and ICT are 5–6 h apart depending on the
  season. Bind the timezone to the location preset; do not make him set a clock.
  **This was broken for LOC_CUSTOM the whole time and nobody could have noticed**, because
  nobody could reach that preset with real coordinates in it: its row in `k_presets[]` said
  `"UTC0"`, so tapping "Eigener Ort" moved the panel clock two hours without moving the
  device an inch. A searched place now carries its own POSIX rule
  (`settings_t.custom_tz`, from the geocoder's IANA zone via `main/net/tz_table.h`), and
  `settings_tz()` is what the clock is set from. **Call `settings_tz(&settings)`, never
  `location_tz(settings.preset)`** — the second one cannot know about a custom place and
  answers Gloggnitz for it.
- Consider auto-detecting the location preset from the WiFi SSID he connects to.
