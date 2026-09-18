# Font pipeline

`build_fonts.sh` generates the LVGL 9.2 C font files under `main/ui/fonts/`
from the IBM Plex source fonts. This is infrastructure work for
`docs/PLAN.md` M1 — LVGL's built-in Montserrat faces stop at 48 px and are
ASCII-only, and the 100 px German headline is the entire point of this
device, so a real font pipeline is not optional.

Ranges, sizes and weights all come from `docs/DESIGN.md` §3 ("Typography").
Read that section before changing anything here.

## Usage

```sh
./tools/build_fonts.sh
```

Re-running is safe and idempotent:
- source `.ttf` files already in `tools/fonts/` are not re-downloaded
- generated `.c` files in `main/ui/fonts/` are always regenerated (cheap
  and deterministic — same inputs produce the same output)

The script ends by printing a per-file and total size report, e.g. (this
is the real output as of the last run):

```
=== Font set size report ===
  plex_sans_cond_100.c         319831 bytes
  plex_sans_cond_76.c          226862 bytes
  plex_sans_cond_56.c          157786 bytes
  plex_sans_cond_34.c          202373 bytes
  plex_sans_cond_25.c          144956 bytes
  plex_sans_cond_22.c          129343 bytes
  plex_mono_32.c               186551 bytes
  plex_mono_17.c                93069 bytes
  plex_mono_13.c                73665 bytes
  plex_mono_12.c                65022 bytes
  ------------------------------------------
  TOTAL                       1599458 bytes
  = 1562.0 KiB = 1.53 MiB
```

**1.53 MiB total** — this is the number for `docs/PLAN.md`'s "Flash +
PSRAM cost of the full font set" (M1). It is a small fraction of the 16 MB
flash, but note where it actually lands at runtime: `CONFIG_SPIRAM_RODATA=y`
(mandated by AGENTS.md §7 as the anti-tearing mitigation) relocates
`.rodata` — which is what these generated arrays are — into PSRAM. 1.53 MiB
is roughly **19% of the 8 MB PSRAM budget**, competing directly with the
framebuffer(s) (450 KiB each) for bandwidth. Re-measure this number
whenever the ladder or its ranges change.

## Prerequisites

- `node` (v18+; this repo was built against v24.19.0 via nvm) and `npx`
  on `PATH`, or installed under `~/.nvm` — the script sources
  `~/.nvm/nvm.sh` and/or falls back to a known nvm install path if `node`
  isn't already on `PATH`.
- `curl` and `unzip`.
- Network access to `github.com` (font source) and the npm registry
  (`lv_font_conv`, fetched on demand via `npx lv_font_conv@1.5.3` — no
  global install needed).

The script fails with a clear, specific error message if any of the above
is missing, rather than partially generating the font set.

## Source fonts

Both families are IBM Plex, licensed under the **SIL Open Font Licence
1.1 (OFL-1.1)** — free to embed in firmware, redistribute and subset.
`tools/fonts/*.ttf` are gitignored; the script downloads them from the
official GitHub releases on demand:

| Family | Weight | Used for | Source release |
|---|---|---|---|
| IBM Plex Sans Condensed | SemiBold (600) | Hero ladder: 100 / 76 / 56 px | [`@ibm/plex-sans-condensed@2.0.0`](https://github.com/IBM/plex/releases/tag/%40ibm%2Fplex-sans-condensed%402.0.0) |
| IBM Plex Sans Condensed | Medium (500) | Secondary hero + body: 34 / 25 / 22 px | same release |
| IBM Plex Mono | Medium (500) | Data + chrome: 32 / 17 / 13 / 12 px | [`@ibm/plex-mono@2.5.0`](https://github.com/IBM/plex/releases/tag/%40ibm%2Fplex-mono%402.5.0) |

Repo: <https://github.com/IBM/plex>. Licence text ships inside each
release zip as `LICENSE.txt` (identical OFL-1.1 text for both families);
it is not duplicated in this repo — see the release pages above for the
canonical copy.

### Why SemiBold for hero, Medium for body/data

DESIGN.md §3 "Weights": *"Nothing below Regular (400). Age-related
contrast-sensitivity loss hits thin strokes first. Body at 400–500, hero
at 600–700."* SemiBold (600) is the lighter of the two hero-eligible
weights (600–700); Bold (700) was not chosen because at 100 px SemiBold
is already unambiguously bold on this panel and Bold would cost more
flash/PSRAM for glyphs that are already well clear of the contrast floor.
For body/data, Medium (500) was chosen over Regular (400) — both are
allowed by DESIGN.md, and Medium gives a little more stroke weight for
the elderly reader without leaving the 400–500 band the design system
permits.

IBM Plex Mono is used for every numeric value specifically because it
ships **tabular figures by default** (not behind an OpenType `tnum`
feature) — see AGENTS.md §7 and DESIGN.md §3 "Why Mono for numbers — a
real trap": `lv_font_conv` does not apply OpenType features at all, so a
font that needs `tnum` for tabular figures would produce digits that
visibly jitter on every refresh once converted. Plex Mono needs no such
feature, so this failure mode does not apply here.

## Subset ranges

Taken **verbatim** from DESIGN.md §3:

```
-r 0x20-0x7F                          # ASCII
-r 0xC4,0xD6,0xDC,0xE4,0xF6,0xFC,0xDF # Ä Ö Ü ä ö ü ß
-r 0xB0,0xB7                          # ° ·
-r 0x2192,0x2014                      # → —
-r 0x0104-0x017C                      # Latin Extended-A (Poznań, Timişoara, ...)
```

`--bpp 4`, `--format lvgl`, via `npx lv_font_conv@1.5.3`.

### Hero subsetting decision

The three hero sizes (100 / 76 / 56 px, IBM Plex Sans Condensed SemiBold)
are subset **without** the Latin Extended-A range. Every other face
(34/25/22 px Sans Condensed Medium, and all four Mono sizes) carries the
full range including Latin Extended-A.

**ä ö ü ß and → (and °, ·, —) are present at every single size** —
that requirement from the task brief is unconditional and does not depend
on this decision. Only the Polish/Romanian Extended-A block
(ą ć ę ł ń ó ś ź ż, ă â î ş ţ, etc.) is dropped from the three hero faces.

Measured cost, `lv_font_conv@1.5.3`, `--bpp 4`, IBM Plex Sans Condensed
SemiBold:

| Size | With Latin Extended-A | Without (used for hero) | Saved |
|---:|---:|---:|---:|
| 100 px | 755,930 B | 319,831 B | 436,099 B |
| 76 px | 533,592 B | 226,862 B | 306,730 B |
| 56 px | 367,895 B | 157,786 B | 210,109 B |
| **Subtotal** | **1,657,417 B** | **704,479 B** | **952,938 B (≈ 930 KiB)** |

Keeping Extended-A on the hero faces would raise the full font set from
**1.53 MiB to ≈ 2.43 MiB** — both numbers are "trivial against 16 MB"
flash exactly as DESIGN.md §3 says, but PSRAM is the scarcer, contended
resource on this board (`CONFIG_SPIRAM_RODATA=y`, AGENTS.md §7): 1.53 MiB
is ~19% of the 8 MB PSRAM budget, 2.43 MiB would be ~30%. Given that the
hero faces exist specifically to render short city/aircraft-type strings
in the two sizes that would otherwise be the single biggest PSRAM
consumer in the font set, dropping a script range that most destinations
never touch is the better trade.

**Accepted risk, flagged for M3:** if a Polish or Romanian destination
that has no entry in the German place-name table (DESIGN.md/AGENTS.md
§1, built in PLAN.md M2.5) ever becomes the literal hero text — e.g.
`Poznań` rendered as-is from the API — the `ń` glyph will be missing at
hero size only (it still renders correctly in the body/mono tiers, e.g.
a list row or the compass tape). If this turns out to matter in practice,
either wire an `--lv-fallback` face for the hero font (an app/theme-layer
change, out of this script's scope) or re-add `-r 0x0104-0x017C` to the
hero `gen_font` calls in `build_fonts.sh` at the ~930 KiB cost measured
above.

## Output

`main/ui/fonts/<name>.c`, one file per size, with `--lv-font-name`
matching the file's basename (e.g. `plex_sans_cond_100.c` declares
`plex_sans_cond_100`). These are committed to the repo — they are build
output the firmware needs at compile time, and regenerating them requires
node/npx, which the ESP-IDF build does not otherwise depend on.

| File | Font | Size | Weight |
|---|---|---:|---|
| `plex_sans_cond_100.c` | Sans Condensed | 100 px | SemiBold |
| `plex_sans_cond_76.c` | Sans Condensed | 76 px | SemiBold |
| `plex_sans_cond_56.c` | Sans Condensed | 56 px | SemiBold |
| `plex_sans_cond_34.c` | Sans Condensed | 34 px | Medium |
| `plex_sans_cond_25.c` | Sans Condensed | 25 px | Medium |
| `plex_sans_cond_22.c` | Sans Condensed | 22 px | Medium |
| `plex_mono_32.c` | Mono | 32 px | Medium |
| `plex_mono_17.c` | Mono | 17 px | Medium |
| `plex_mono_13.c` | Mono | 13 px | Medium |
| `plex_mono_12.c` | Mono | 12 px | Medium |

Wiring these into the CMake build and `lv_conf.h` is not part of this
script — that belongs to whoever owns `main/CMakeLists.txt`.
