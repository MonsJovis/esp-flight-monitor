# Flugradar

A 4-inch panel that answers one question, in German, without being touched:

> **Das Flugzeug da oben — wo fliegt es hin, wo kommt es her, und was ist es?**

![Über dir jetzt](docs/screens/1-ueber-dir.png)

Real traffic over Gloggnitz, Lower Austria. A British Airways A321neo at 10.371 m,
10,7 km to the south, on its way from Rhodes to London. Nobody touched anything —
this is what the panel shows by itself.

## Why it exists

It was built for one person: my father-in-law, who is Austrian, in his eighties, and not
technical. When he hears a plane he pulls out his phone and opens Flightradar24. This has
to be faster and easier than that, or there is no reason for it to exist.

So the whole design follows from two things he does. He **hears a plane and glances up** —
the answer has to already be on the screen, in under two seconds, with no interaction. Or
he **wonders what else is up there** — and taps.

That is why the destination is the headline in 76 px type rather than a field in a table,
why it says "Airbus A321neo" and not `A21N`, and why the screen is never blank: an empty
panel reads as *broken* to someone who did not build it.

He splits the year between Gloggnitz and Pattaya. One tap moves the device — location,
time zone, clock and all.

## The three screens

| | | |
|---|---|---|
| ![Über dir jetzt](docs/screens/1-ueber-dir.png) | ![Liste](docs/screens/2-liste.png) | ![Radar](docs/screens/3-radar.png) |
| **Über dir jetzt** — the nearest aircraft, and where it is going. The one screen that matters. | **Liste** — everything in range, nearest first. | **Radar** — where they are and which way they point, north up. |

Swipe between them; it returns to the first screen by itself after 30 seconds of an empty
sky. Aircraft with no filed route are not a failure case — seven of the thirteen in our
first live capture were light aircraft with no flight plan, and those are precisely the
ones he *hears*, low and slow over the house. They keep the layout and say why:

> **Eine Route gibt es nur bei Linienflügen.**

Settings are one screen: where he is, how far to look, how bright, and when to dim.

| | |
|---|---|
| ![Einstellungen](docs/screens/4-einstellungen.png) | ![WLAN](docs/screens/6-wlan.png) |

## Hardware

**Waveshare ESP32-S3-Touch-LCD-4B** — the "Smart 86 Box". ESP32-S3-WROOM-1-N16R8, 16 MB
flash, 8 MB octal PSRAM, a 480 × 480 ST7701 RGB565 panel and a GT911 touch controller, in
a standard 86-type wall-plate form factor (86.5 × 86.5 × 14 mm). Power is USB-C on the
side edge.

Built with **ESP-IDF 5.4**, the Waveshare BSP and **LVGL 9.6**, in C.

```
idf.py build
idf.py -p /dev/cu.usbmodem101 flash monitor
```

WiFi credentials are typed straight into the device over serial and stored in NVS. They
are never in this repository and never pass through a config file:

```
python3 tools/provision.py
```

## Standing it up

The board is a flush wall plate, which is the wrong shape for a table, so
[`hardware/desk_stand.scad`](hardware/desk_stand.scad) is a parametric wedge that holds it
at 20° off vertical — square to the sight line of someone seated about 70 cm away, which
is the same geometry the type sizes are set from. 95 × 55 mm footprint, one part, no
supports, with a notch so a right-angle USB-C plug leaves sideways instead of pushing the
stand off the table.

**It has not been printed, and its dimensions come from the datasheet rather than from
calipers on the unit in hand.** Render `part = "fittest"` first — it is the slot and the
lip alone, four minutes of filament, and it tells you whether the depth is right before
you commit to an hour.

## How it gets verified

The panel is the product, so the panel is what gets checked. `tools/grab_screen.py` pulls
the live framebuffer back over USB and writes it as a PNG — every screenshot above is the
real thing, read off the hardware, not a mockup. Every layout bug in `docs/DECISIONS.md`
was found that way, including several no host test could ever have seen.

Everything that can be tested off the device is:

```
cd test/host && make
```

3.638 checks across six suites, plus two gates that run with them:

- **`check_font_coverage.py`** — LVGL draws a missing glyph as *nothing at all*. No error,
  no placeholder, just text that is shorter than you wrote. This fails the build if any
  string needs a character the generated font subset lacks.
- **`check_strings.py`** — every user-facing word lives in `main/strings_de.h`. This fails
  the build if one leaks into a widget constructor. `--list` prints the whole German
  vocabulary, grouped by screen, for reading aloud.

## Data

Positions come from **[adsb.lol](https://adsb.lol)**, routes from
**[adsb.im](https://adsb.im)**. Both are free, community-run, and this device is a polite
client: one request per location every 15 seconds, route lookups batched and cached to
NVS so a reboot does not re-ask.

> Contains information from **adsb.lol**, which is made available under the
> [Open Database License (ODbL) v1.0](https://opendatacommons.org/licenses/odbl/1-0/).
> Any rights in individual contents of the database are licensed under the
> [Database Contents License](https://opendatacommons.org/licenses/dbcl/1-0/).

## Reading the repo

| | |
|---|---|
| [`AGENTS.md`](AGENTS.md) | The brief. Product intent, verified hardware profile, data architecture, the gotchas that cost a day each. |
| [`docs/DESIGN.md`](docs/DESIGN.md) | Colour semantics, type scale, screen inventory. Read before building any screen. |
| [`docs/PLAN.md`](docs/PLAN.md) | Milestones M0–M8 and where the build actually is. |
| [`docs/DECISIONS.md`](docs/DECISIONS.md) | Every decision and why, including the ones that turned out to be wrong. |
| [`docs/RESEARCH.md`](docs/RESEARCH.md) | Survey of the existing open-source flight radars, and what was worth taking. |

The colour vocabulary is borrowed from aviation, not chosen for looks: magenta is the
active route, green is normal, amber is a missing value, and no colour ever carries
meaning on its own — that is FAA AC 25-11A and RTCA DO-257A, and they were written by
people who had already learned what happens when a glance goes wrong.
