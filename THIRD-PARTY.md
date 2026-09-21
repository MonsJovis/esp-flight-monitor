# Third-party code, fonts and data

This project is MIT (see [LICENSE](LICENSE)). That grant covers this project's
own code and nothing else. What is listed here belongs to other people, is
used under their terms, and those terms travel with it — including into any
fork of this repo and into every firmware image built from it.

`docs/RESEARCH.md` is the survey this reuse came out of, and AGENTS.md §9 is
the policy: MIT and Apache-2.0 may be copied with their notices kept,
copyleft and non-commercial sources are read but not pasted, and a project
with no licence file is left alone.

---

## Code compiled into the firmware

### Waveshare ESP32-S3-Touch-LCD-4B BSP — Apache-2.0

    Copyright Waveshare / Espressif Systems
    https://github.com/waveshareteam/Waveshare-ESP32-components
    Licensed under the Apache License, Version 2.0
    http://www.apache.org/licenses/LICENSE-2.0

Pulled in as a managed component (`waveshare/esp32_s3_touch_lcd_4b`), and in
addition **about sixty lines of its display init path are reproduced in
`main/ui/display.c`**. That copy exists because the BSP keeps the
`esp_lcd_panel_handle_t` in a file-static with no accessor and this project
needs it twice — to read the framebuffer back over USB, and to vary the
framebuffer count for a bandwidth measurement (docs/DECISIONS.md D5).

### ESP-IDF — Apache-2.0

    Copyright Espressif Systems (Shanghai) CO LTD
    https://github.com/espressif/esp-idf

Includes cJSON, which is compiled into both the firmware and the host test
suite.

### LVGL — MIT

    Copyright (c) 2021 LVGL Kft
    https://github.com/lvgl/lvgl

---

## Code read, adapted or lifted from other flight-radar projects

All MIT. Each notice below is reproduced from the project's own LICENSE file;
the MIT permission notice is the one printed in [LICENSE](LICENSE) and applies
to each of them.

| Taken | From | Notice |
|---|---|---|
| The 40-byte fixed-`char`-array `Aircraft` struct; the geo projection and rim-dot bearing maths behind the radar | [MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar) | `Copyright (c) 2026 MatixYo` |
| The batched `routeset` request and its parser (`RouteParser.h`, `fetchRoute()`) | [kovaacs/sky_overhead](https://github.com/kovaacs/sky_overhead) | `Copyright (c) 2026 Marcell Kovács` |
| The heading-rotated aircraft glyph (`PlaneSpotter.cpp`, `drawPlane()`) and the Web-Mercator maths | [ThingPulse esp8266-plane-spotter-color](https://github.com/ThingPulse/esp8266-plane-spotter-color) | `Copyright (c) 2017 Daniel Eichhorn` |
| The two-task LVGL/network split and its memory budget | [TheJinxNL/ESP32FlightRadar](https://github.com/TheJinxNL/ESP32FlightRadar) | `Copyright (c) 2026 TheJinxNL` |
| Route-cache shape: TTL, backoff, rate limiting | [ironicbadger/ESP32-Plane-Radar](https://github.com/ironicbadger/ESP32-Plane-Radar) | `Copyright (c) 2026 MatixYo` |

---

## Fonts

`main/ui/fonts/*.c` are ten LVGL font files generated from **IBM Plex Sans
Condensed** and **IBM Plex Mono**. They are glyph bitmaps converted from the
original outlines, which makes them a derivative of the Font Software, so
OFL-1.1 travels with them and its text has to ship alongside. It is reproduced
in full below.

`tools/build_fonts.sh` regenerates them; the `.ttf` sources are downloaded on
demand from the official releases and are not stored in this repo.

> **Reserved Font Name.** OFL-1.1 §3 forbids using the name "Plex" for a
> modified version. The generated files here keep the name because they are a
> subset and format conversion rather than a modification of the design; if
> anyone ever alters the outlines, they must be renamed.

```
Copyright © 2017 IBM Corp. with Reserved Font Name "Plex"

This Font Software is licensed under the SIL Open Font License, Version 1.1.

This license is copied below, and is also available with a FAQ at: http://scripts.sil.org/OFL


-----------------------------------------------------------
SIL OPEN FONT LICENSE Version 1.1 - 26 February 2007
-----------------------------------------------------------

PREAMBLE
The goals of the Open Font License (OFL) are to stimulate worldwide
development of collaborative font projects, to support the font creation
efforts of academic and linguistic communities, and to provide a free and
open framework in which fonts may be shared and improved in partnership
with others.

The OFL allows the licensed fonts to be used, studied, modified and
redistributed freely as long as they are not sold by themselves. The
fonts, including any derivative works, can be bundled, embedded, 
redistributed and/or sold with any software provided that any reserved
names are not used by derivative works. The fonts and derivatives,
however, cannot be released under any other type of license. The
requirement for fonts to remain under this license does not apply
to any document created using the fonts or their derivatives.

DEFINITIONS
"Font Software" refers to the set of files released by the Copyright
Holder(s) under this license and clearly marked as such. This may
include source files, build scripts and documentation.

"Reserved Font Name" refers to any names specified as such after the
copyright statement(s).

"Original Version" refers to the collection of Font Software components as
distributed by the Copyright Holder(s).

"Modified Version" refers to any derivative made by adding to, deleting,
or substituting -- in part or in whole -- any of the components of the
Original Version, by changing formats or by porting the Font Software to a
new environment.

"Author" refers to any designer, engineer, programmer, technical
writer or other person who contributed to the Font Software.

PERMISSION & CONDITIONS
Permission is hereby granted, free of charge, to any person obtaining
a copy of the Font Software, to use, study, copy, merge, embed, modify,
redistribute, and sell modified and unmodified copies of the Font
Software, subject to the following conditions:

1) Neither the Font Software nor any of its individual components,
in Original or Modified Versions, may be sold by itself.

2) Original or Modified Versions of the Font Software may be bundled,
redistributed and/or sold with any software, provided that each copy
contains the above copyright notice and this license. These can be
included either as stand-alone text files, human-readable headers or
in the appropriate machine-readable metadata fields within text or
binary files as long as those fields can be easily viewed by the user.

3) No Modified Version of the Font Software may use the Reserved Font
Name(s) unless explicit written permission is granted by the corresponding
Copyright Holder. This restriction only applies to the primary font name as
presented to the users.

4) The name(s) of the Copyright Holder(s) or the Author(s) of the Font
Software shall not be used to promote, endorse or advertise any
Modified Version, except to acknowledge the contribution(s) of the
Copyright Holder(s) and the Author(s) or with their explicit written
permission.

5) The Font Software, modified or unmodified, in part or in whole,
must be distributed entirely under this license, and must not be
distributed under any other license. The requirement for fonts to
remain under this license does not apply to any document created
using the Font Software.

TERMINATION
This license becomes null and void if any of the above conditions are
not met.

DISCLAIMER
THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
OF COPYRIGHT, PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL THE
COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL
DAMAGES, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM
OTHER DEALINGS IN THE FONT SOFTWARE.
```

---

## Data

The firmware displays data from free, community-run services. Their terms are
also shown to the user, at the foot of Einstellungen (`STR_ATTRIBUTION`,
`STR_ATTRIBUTION_2`) — attribution that only a developer ever reads is not
attribution.

- **[adsb.lol](https://adsb.lol)** — aircraft positions. Contains information
  made available under the [Open Database License (ODbL) v1.0](https://opendatacommons.org/licenses/odbl/1-0/);
  rights in individual contents under the [Database Contents License](https://opendatacommons.org/licenses/dbcl/1-0/).
- **[adsb.im](https://adsb.im)** / **adsbdb** — route lookups. Displayed only.
  Their terms do not permit mirroring the route data into another database,
  and this project does not: routes are cached per callsign for the flight and
  go no further.
- **[Open-Meteo](https://open-meteo.com/en/docs/geocoding-api)** — place names
  and timezones when someone searches for a location. Derived from
  [GeoNames](https://www.geonames.org/) under
  [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Open-Meteo's free
  tier is non-commercial.

`test/fixtures/*.json` are real captured responses from these services, kept
so the parsers can be tested without a network, and covered by the same terms.

The airline, airport and aircraft-type tables in `main/data/tbl_*.c` are
hand-built from public ICAO designator lists and are this project's own work.
