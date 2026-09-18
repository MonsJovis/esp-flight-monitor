# Design

Chosen direction: **B · Cockpit**. Screens live on the canvas at
<https://claude.ai/artifact/FrhCvcyrCiArHpg6eUreq9> (7 screens, plus the two rejected
studies A · Abflugtafel and C · Postkarte kept below for the record).

All mockups use **real flight data** captured over Gloggnitz on 2026-09-18 — no invented
flights, no lorem ipsum. `MEA201 · Beirut → London · 15 km NO` and
`DERKL · Diamond DV20 · 18 km OSO, ohne Route`.

Implementation sequencing for everything here is in [PLAN.md](./PLAN.md). The tokens in §2
become `main/ui/theme.h` in M1; nothing downstream should contain a hex literal.

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
| `grey` | `#94A5B2` | — | Labels |

### Text tones

Three, and only three. The mockups drifted to five — `#D8E2E8` alongside `#DDE6EC`
(15.0:1 vs 15.6:1) and `#98A9B6` alongside `#94A5B2` (8.1:1 vs 7.8:1). Those pairs are
indistinguishable on a panel and exist only because two screens were drawn on different
days. Collapsed:

| Token | Hex | Ratio on ground | Use |
|---|---|---|---|
| `text-primary` | `#DDE6EC` | 15.6:1 | Body text, airline names, sentences |
| `text-label` | `#94A5B2` | 7.8:1 | Labels, chrome, secondary values |
| `text-tertiary` | `#6E8494` | 5.1:1 | Units and prepositions **only**, always adjacent to a brighter value — "m hoch", "von", "km Nordost" |

### Surfaces and structure

Non-text, so contrast rules do not apply — but they are part of the system and belong in
`theme.h` like everything else:

| Token | Hex | Use |
|---|---|---|
| `ground` | `#0A0B0D` | Page background |
| `surface-sel` | `#0C131A` | Selected list row (§5.4) |
| `surface-green` | `#08130D` | Saved-network card fill (§5.7) |
| `surface-magenta` | `#140A15` | Active-preset card fill (§5.6) |
| `hairline` | `#1A3340` | Section rules, outer range ring |
| `hairline-dim` | `#12262F` | Inner range rings (§5.5) |
| `divider` | `#101C24` | List row dividers (§5.4) |
| `border-idle` | `#1C2B35` | Unselected cards, slider tracks |
| `border-green` | `#1C3B2A` | Saved-network card border (§5.7) |

**Rules that come with the semantics:**

- **Never encode by colour alone** (DO-257A §2.1.6). Every coloured element here carries a
  word or a shape too. Assume reduced colour discrimination.
- **Red is reserved for warnings** and is currently unused. Keep it that way — if
  everything can be red, nothing is.
- ⚠️ **Magenta on black is a documented high-confusion pair** (AC 25-11A 31.c(5)(g)).
  We use it anyway because the semantic fit is exact, and mitigate with an adjacent text
  label. **Verification is scheduled in PLAN.md M1 — treat it as unsettled until then.**

### Ground is not pure black

`#0A0B0D`, not `#000000`. Pure black maximises halation — the glow bleed around bright
glyphs that aging eyes suffer most from.

### Contrast — measured, not claimed

An earlier draft of this document claimed AAA throughout. Measured against the `#0A0B0D`
ground, that was wrong in three places. The real picture:

| Colour | Ratio | AA | AAA | Role |
|---|---:|:--:|:---:|---|
| `#FFFFFF` | 19.7 | ✅ | ✅ | Hero |
| `#DDE6EC` | 15.6 | ✅ | ✅ | Body |
| `#22E3FF` | 12.7 | ✅ | ✅ | Data values |
| `#FFB300` | 11.0 | ✅ | ✅ | Caution |
| `#00E676` | 11.8 | ✅ | ✅ | Status |
| `#94A5B2` | 7.8 | ✅ | ✅ | Labels |
| `#FF3FDA` | **6.5** | ✅ | ❌ | Route |
| `#6E8494` | **5.1** | ✅ | ❌ | Units, prepositions |

**The standing rules:**

1. Anything that carries information on its own reaches **AAA (7:1)**.
2. Two deliberate exceptions, both AA: **magenta** and **tertiary**. Both are only ever
   used adjacent to an AAA element carrying the same meaning — `042°` beside a magenta
   arrow, `m hoch` beside a 32 px cyan number. DO-257A's never-colour-alone rule is doing
   double duty here, and it is why these two are acceptable rather than sloppy.
3. **Nothing below AA (4.5:1), ever.** The first draft had `#8A7440` at 4.4:1 for the radar's
   "privat" label. Removed — that label uses `amber` at label size. Dimming was solving a
   loudness problem with the wrong lever, the same mistake the first draft made with type.

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

### Two viewing distances, two floors

ISO 9241-303/306: minimum Latin character height **16 arcmin**, displays should reach
**20–22 arcmin**. That is an *angular* figure, so the floor moves with distance — and this
device is read at two distances. One floor cannot serve both.

| Tier | Distance | Cap-height floor | ≈ font size | Screens |
|---|---|---|---|---|
| **Glance** | ~70 cm, from the armchair | 30 px | 43 px | §5.1–5.3 |
| **Near** | ~40 cm, in hand or leaned into | 17 px | 24 px | §5.4–5.7 |

A 4″ 480×480 panel is 6.7 px/mm; the glance floor is the 4.5 mm cap height that 70 cm
implies. **Chrome — time, location, status dot — is exempt at either distance**, because
it is never the answer to a question.

> ⚠️ **As drawn, §5.4 and §5.5 are below even the near floor** — list secondary lines at
> 12 px, radar city labels at 10–11 px, and the radar's city names are the single thing
> this product exists to show. Both need a type pass before implementation; the room
> exists, since the list shows five rows in 300 px and could show four. Tracked in
> PLAN.md M5.

### The scale

| Role | Size | ≈ cap height | Tier | Notes |
|---|---|---|---|---|
| Hero (destination, type) | 100 px | ~70 px | Glance | Auto-shrinks — see below |
| Hero, stepped down | 76 / 56 px | ~53 / 39 px | Glance | Both still clear the glance floor |
| Secondary hero (origin) | 34 px | ~24 px | Glance | |
| Airline / body | 22–25 px | ~16–18 px | Glance | |
| Data values | 32 px mono | ~22 px | Glance | |
| Near-view body | 24 px min | ~17 px | Near | List rows, settings labels |
| Labels, chrome | 12–13 px mono | ~9 px | — | **Never** carries information he needs |

**Hero auto-shrink.** At 100 px in Plex Sans Condensed roughly **9–10 characters** fit the
440 px content width. "London" fits comfortably, "Kopenhagen" is at the edge,
"Thessaloniki" is not. Measure the rendered width and step 100 → 76 → 56; do not
truncate a city name, ever — a half-name is worse than a smaller one.

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
costs roughly **150 KB**. Trivial against 16 MB.

⚠️ But note **where it actually lives**: `CONFIG_SPIRAM_RODATA=y` — which AGENTS.md §7
mandates as the tearing mitigation — relocates `.rodata` into PSRAM, and LVGL fonts are
`.rodata`. So the font set competes with the framebuffer for PSRAM bandwidth, which is the
project's #1 risk. Measured in PLAN.md M1, before the screens are built on top of it.

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
| 5.1 | **Über dir jetzt** | Default. Nearest aircraft, destination as hero. No interaction needed. |
| 5.2 | **Ohne Route** | Same screen when there is no flight plan — see below. |
| 5.3 | **Himmel frei** | Empty sky: clock, date, last aircraft seen. Never a blank panel. |
| 5.4 | **Liste** | Everything nearby, sorted by distance. Tap a row for its card. |
| 5.5 | **Radar** | PPI scope, range rings, heading-rotated glyphs. |
| 5.6 | **Einstellungen** | Location preset, radius, brightness. |
| 5.7 | **WLAN** | Provisioning, both networks remembered — the device travels. |

### The no-route case is not an edge case

Measured on real traffic over Gloggnitz: **airline flights resolve a route 92% of the
time; private aircraft 0%** — and always will, because a Cessna doing circuits has no
flight plan. In one 40 nm sample, **14 of 38 aircraft** were local light aircraft.

The aircraft he actually *sees and hears* skew towards exactly those: low, slow, loud. The
airliner at 9.100 m with the beautiful route is a speck.

So §5.2 is not a fallback, it is a co-equal state. It keeps the layout and swaps the
hero from destination to aircraft type, and it **says why** there is no route
("Eine Route gibt es nur bei Linienflügen"). A blank slot reads as broken; a sentence
reads as informative. It ships in the same milestone as §5.1 — PLAN.md M3.

---

## 6. Navigation

Three swipe pages, one deck:

```
        ←   Über dir jetzt   ·   Liste   ·   Radar   →
             §5.1/5.2/5.3       §5.4        §5.5
                  ●               ○           ○
```

- **§5.1, §5.2 and §5.3 are one page in three states**, not three pages. The device picks
  the state; he never navigates between them.
- **The page indicator shows three dots because the deck has three pages.** The mockups
  currently mark Liste as position 1 and Radar as position 2 — under this model they are
  2 and 3. Minor mockup fix.
- **§5.6 Einstellungen is not in the deck.** Long-press the chrome bar. He will be shown it
  once and then never need it; putting it in the swipe path means finding it by accident,
  which for this user means being lost.
- **§5.7 WLAN** is reached from Einstellungen, and appears by itself when no known network
  is in range — the one case where the device must interrupt him.
- **Auto-return** to §5.1 happens **only from §5.3**, and only after 30 s without a touch.
  If he is reading the list, traffic appearing must not yank the screen away.

---

## 7. Open

1. **Magenta on black** — see §2. Measured at 6.5:1 and flagged by AC 25-11A as a
   high-confusion pair. Needs eyes on the real panel; scheduled in PLAN.md M1.
2. **Light theme.** The evidence on polarity is genuinely split: Piepenbrock et al. (2013)
   favours dark-on-light for all ages; Wang et al. (2024, n=134 incl. 66 aged 60+) found
   neither polarity consistently better and recommends shipping both. **Auto-dim is settled
   and scheduled (M6); a full light theme is not.**
3. **The §5.4 / §5.5 type pass** — see the warning in §3. Tracked in PLAN.md M5.
4. **Aircraft photos** — still deferred (AGENTS.md §8).

## Sources

- FAA AC 25-11A/B, Electronic Flight Displays — colour semantics
- RTCA DO-257A §2.1.6 — six-colour limit, no colour-alone encoding
- ISO 9241-303/306 — character height, viewing distance
- WCAG 2.2 §1.4.3 / §1.4.6 — contrast
- Piepenbrock et al., *Ergonomics* 2013; Wang et al., arXiv:2409.10841 — display polarity
