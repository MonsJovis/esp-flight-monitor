# Design

Chosen direction: **B · Cockpit**. Screens live on the canvas at
<https://claude.ai/artifact/FrhCvcyrCiArHpg6eUreq9> (7 screens, plus the two rejected
studies A · Abflugtafel and C · Postkarte kept below for the record).

All mockups use **real flight data** captured over Gloggnitz on 2026-09-18 — no invented
flights, no lorem ipsum. `MEA201 · Beirut → London · 15 km NO` and
`DERKL · Diamond DV20 · 18 km OSO, ohne Route`.

---

## 1. The principle

**One screen answers one question, in type he can read from his armchair.**

He is elderly, non-technical, and currently reaches for a phone. The device wins only if
the answer arrives before the phone would have. So the default screen states the
destination and nothing competes with it. Everything else is a second tier, read when he
walks closer or picks it up.

This is the Nest "Farsight" idea: the resting state is one large fact; detail is revealed
on approach. We have no proximity sensor, so the tiers are spatial rather than temporal —
hero type at the top of the visual hierarchy, supporting data below it.

---

## 2. Colour

Six chromatic codes plus grey. **That is the hard ceiling** — RTCA DO-257A §2.1.6 limits
colour coding to six. Do not add a seventh; replace one instead.

Semantics follow **FAA AC 25-11A** (Electronic Flight Displays), not decoration:

| Token | Hex | Meaning (AC 25-11A) | Used for |
|---|---|---|---|
| `magenta` | `#FF3FDA` | Pilot-selectable reference / active route — *the thing you are heading toward* | The route, the bearing marker, the selected aircraft |
| `green` | `#00E676` | Engaged modes, normal conditions | "ÜBER DIR" status, home marker, saved WiFi |
| `cyan` | `#22E3FF` | Armed modes / secondary data | Altitude, distance, settings values |
| `amber` | `#FFB300` | Caution, abnormal source | "KEIN FLUGPLAN", missing values, no network |
| `white` | `#FFFFFF` | Scales, figures, units, labels | The hero destination |
| `grey` | `#94A5B2` | — | Labels (AAA contrast, see below) |

Ground `#0A0B0D`, hairline `#1A3340`, primary text `#DDE6EC`, tertiary `#6E8494`.

**Rules that come with the semantics:**

- **Never encode by colour alone** (DO-257A §2.1.6). Every coloured element here carries a
  word or a shape too. Assume reduced colour discrimination.
- **Red is reserved for warnings** and is currently unused. Keep it that way — if
  everything can be red, nothing is.
- ⚠️ **Magenta on black is a documented high-confusion pair** (AC 25-11A 31.c(5)(g)).
  We use it anyway because the semantic fit is exact, and mitigate with an adjacent text
  label. **Verify on the real panel before treating this as settled.**

### Ground is not pure black

`#0A0B0D`, not `#000000`. Pure black maximises halation — the glow bleed around bright
glyphs that aging eyes suffer most from.

### Contrast target: AAA (7:1)

WCAG AA (4.5:1) is the floor; we hit AAA throughout because a single-purpose appliance
has no reason not to. In practice: **no text dimmer than about `#949494` on this ground.**
The first draft used `#7C92A3` for labels — 4.0:1, passes AA, fails AAA. Corrected.

---

## 3. Typography

**IBM Plex Mono** for anything numeric. **IBM Plex Sans Condensed** for words.

### Why Mono for numbers — a real trap

**`lv_font_conv` does not apply OpenType features.** It takes default glyph advances, so a
font whose tabular figures exist only behind the `tnum` feature will produce **digits that
visibly jitter on every refresh**. That is exactly the kind of thing that makes a
non-technical user stop trusting a device.

IBM Plex Mono is tabular **by default** — safe. For reference, fonts that are *not*:
Barlow Condensed (42% digit-width spread, `tnum` only), Saira Condensed and Oswald (no
tabular figures and no feature at all). This is why direction A would have needed a font
change had it won.

### Why Condensed for words

German runs long. *Voraussichtliche Ankunftszeit* is 29 characters; even
*Frankfurt-am-Main* overflows a 480 px line at hero size. Condensed buys width back
without dropping size, which is the wrong lever for this user.

### Size floor — measured, not guessed

ISO 9241-303/306: minimum Latin character height **16 arcmin**, displays should reach
**20–22 arcmin**. At a 70 cm viewing distance that is a **4.5 mm cap height**. A 4″ 480×480
panel is 6.7 px/mm, so:

> **Absolute floor: 30 px cap height ≈ 43 px font size.**

That floor is for *reading*. The hero datum should be 2–3× it.

| Role | Size | ≈ cap height | Notes |
|---|---|---|---|
| Hero (destination, type) | 100 px | ~70 px | Must auto-shrink for long names — ship 3 sizes and pick |
| Secondary hero (origin) | 34 px | ~24 px | |
| Airline / body | 22–25 px | ~16–18 px | |
| Data values | 32 px mono | ~22 px | |
| Labels, chrome | 12–13 px mono | ~9 px | Chrome only — **never** carries information he needs at a glance |

The first draft had data values at 20 px — roughly half the readable minimum. Corrected.

### Font subsetting for `lv_font_conv`

Umlauts are not in the default ASCII range. Include Latin-1 supplement explicitly:

```
-r 0x20-0x7F                          # ASCII
-r 0xC4,0xD6,0xDC,0xE4,0xF6,0xFC,0xDF # Ä Ö Ü ä ö ü ß
-r 0xB0,0xB7                          # ° ·
-r 0x2192,0x2014                      # → —
```

Also needed for Polish and Romanian destinations that appear in real route data
(`Poznań`, `Timişoara`): `0x0104-0x017C` covers Latin Extended-A.

Flash budget: the 100 px hero face at 4 bpp, subset to mixed-case + umlauts (~70 glyphs),
costs roughly **150 KB**. Trivial against 16 MB. Big type is cheap here — it lives in
flash, not RAM.

### Weights

**Nothing below Regular (400).** Age-related contrast-sensitivity loss hits thin strokes
first. Body at 400–500, hero at 600–700. Avoid Black at small sizes — counters fill in.

---

## 4. Layout

480 × 480, 20 px side padding, 8 px base unit (480 = 60 × 8).

A square has **no dominant axis** — unlike 16:9 or a phone, it supplies no reading
direction, so the hierarchy has to be manufactured entirely by size and weight. Corners are
the lowest-attention zone: chrome only (time, location, status dot).

Vertical band structure, top to bottom: **chrome → compass tape → hero → supporting →
data**. Consistent across all seven screens so the eye learns one map.

> If the hardware ever changes to a round panel, keep content inside the inscribed circle
> (~340 px diameter) and the design ports with no rework.

---

## 5. Screens

| # | Screen | Purpose |
|---|---|---|
| 1 | **Über dir jetzt** | Default. Nearest aircraft, destination as hero. No interaction needed. |
| 2 | **Ohne Route** | Same screen when there is no flight plan — see below. |
| 3 | **Himmel frei** | Empty sky: clock, date, last aircraft seen. Never a blank panel. |
| 4 | **Liste** | Everything nearby, sorted by distance. Tap a row for its card. |
| 5 | **Radar** | PPI scope, range rings, heading-rotated glyphs. |
| 6 | **Einstellungen** | Location preset, radius, brightness. |
| 7 | **WLAN** | Provisioning, both networks remembered — the device travels. |

### The no-route case is not an edge case

Measured on real traffic over Gloggnitz: **airline flights resolve a route 92% of the
time; private aircraft 0%** — and always will, because a Cessna doing circuits has no
flight plan. In one 40 nm sample, **14 of 38 aircraft** were local light aircraft.

The aircraft he actually *sees and hears* skew towards exactly those: low, slow, loud. The
airliner at 9.100 m with the beautiful route is a speck.

So screen 2 is not a fallback, it is a co-equal state. It keeps the layout and swaps the
hero from destination to aircraft type, and it **says why** there is no route
("Eine Route gibt es nur bei Linienflügen"). A blank slot reads as broken; a sentence
reads as informative.

---

## 6. Open

1. **Magenta on black** — see §2. Needs eyes on the real panel.
2. **Light theme.** The evidence on polarity is genuinely split: Piepenbrock et al. (2013)
   favours dark-on-light for all ages; Wang et al. (2024, n=134 incl. 66 aged 60+) found
   neither polarity consistently better and recommends shipping both. A glowing dark panel
   in a dim living room at 22:00 is glare. **Auto-dim is not optional; a light theme is
   worth considering.**
3. **Hero auto-shrink** for long destination names — needs the 3-size ladder built.
4. **Aircraft photos** — still deferred (AGENTS.md §8).

## Sources

- FAA AC 25-11A/B, Electronic Flight Displays — colour semantics
- RTCA DO-257A §2.1.6 — six-colour limit, no colour-alone encoding
- ISO 9241-303/306 — character height, viewing distance
- WCAG 2.2 §1.4.3 / §1.4.6 — contrast
- Piepenbrock et al., *Ergonomics* 2013; Wang et al., arXiv:2409.10841 — display polarity
