#!/bin/sh
# build_fonts.sh — generate the LVGL C font files for esp-flight-monitor.
#
# Reads:   docs/DESIGN.md §3 (Typography) for the size ladder and subset
#          ranges; AGENTS.md §7 ("Fonts and text") for the two gotchas this
#          script exists to route around (lv_font_conv ignores OpenType
#          tabular-figure features, and umlauts are outside plain ASCII).
#
# Writes:  tools/fonts/*.ttf       — downloaded source fonts (gitignored)
#          main/ui/fonts/*.c       — generated LVGL font files (committed;
#                                    they are build output the firmware
#                                    needs, and regenerating them requires
#                                    node)
#
# Usage:   ./tools/build_fonts.sh
#
# Idempotent: re-running skips any .ttf already present in tools/fonts/ and
# simply regenerates the .c files (cheap, deterministic, safe to overwrite).
set -eu

# ---------------------------------------------------------------------------
# 0. Locate ourselves regardless of the caller's working directory.
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(CDPATH='' cd -- "$SCRIPT_DIR/.." && pwd)"

FONTS_SRC_DIR="$REPO_ROOT/tools/fonts"
OUT_DIR="$REPO_ROOT/main/ui/fonts"

# ---------------------------------------------------------------------------
# 1. Toolchain checks — fail clearly instead of half-generating the set.
# ---------------------------------------------------------------------------
if ! command -v curl >/dev/null 2>&1; then
    echo "ERROR: curl not found. Install curl and re-run." >&2
    exit 1
fi

if ! command -v unzip >/dev/null 2>&1; then
    echo "ERROR: unzip not found. Install unzip and re-run." >&2
    exit 1
fi

# node/npx: try PATH first, then a couple of nvm-shaped fallbacks, since this
# is typically run on a dev machine where node lives under nvm rather than
# system-wide.
if ! command -v node >/dev/null 2>&1; then
    NVM_DIR="${NVM_DIR:-$HOME/.nvm}"
    if [ -s "$NVM_DIR/nvm.sh" ]; then
        # shellcheck disable=SC1091
        . "$NVM_DIR/nvm.sh"
    fi
fi

if ! command -v node >/dev/null 2>&1; then
    # Known-good install path from the project brief, plus a glob fallback
    # for "any version under ~/.nvm" (left as a literal, harmless no-op
    # pattern if nothing matches — no command substitution, so this stays
    # well-behaved under `set -e`).
    for candidate in \
        "$HOME/.nvm/versions/node/v24.19.0/bin" \
        "$HOME"/.nvm/versions/node/*/bin \
        ; do
        if [ -x "$candidate/node" ]; then
            PATH="$candidate:$PATH"
            export PATH
            break
        fi
    done
fi

if ! command -v node >/dev/null 2>&1; then
    echo "ERROR: node not found on PATH and no nvm install was found." >&2
    echo "       Install node (v18+) or source nvm, e.g.:" >&2
    echo "         export NVM_DIR=\"\$HOME/.nvm\"; . \"\$NVM_DIR/nvm.sh\"; nvm use 24" >&2
    exit 1
fi

if ! command -v npx >/dev/null 2>&1; then
    echo "ERROR: npx not found alongside node ($(command -v node))." >&2
    echo "       npx ships with npm >= 5.2; reinstall node/npm." >&2
    exit 1
fi

echo "Using node: $(command -v node) ($(node --version))"
echo "Using npx:  $(command -v npx)"

LV_FONT_CONV="npx --yes lv_font_conv@1.5.3"

# ---------------------------------------------------------------------------
# 2. Fetch source fonts (idempotent — skip download if the .ttf is present).
#
#    IBM Plex is OFL-1.1 licensed. Source + licence text recorded in
#    tools/README.md.
# ---------------------------------------------------------------------------
mkdir -p "$FONTS_SRC_DIR"

IBM_PLEX_SANS_COND_ZIP_URL="https://github.com/IBM/plex/releases/download/%40ibm/plex-sans-condensed%402.0.0/ibm-plex-sans-condensed.zip"
IBM_PLEX_MONO_ZIP_URL="https://github.com/IBM/plex/releases/download/%40ibm/plex-mono%402.5.0/ibm-plex-mono.zip"

SANS_COND_SEMIBOLD="$FONTS_SRC_DIR/IBMPlexSansCondensed-SemiBold.ttf"
SANS_COND_MEDIUM="$FONTS_SRC_DIR/IBMPlexSansCondensed-Medium.ttf"
MONO_MEDIUM="$FONTS_SRC_DIR/IBMPlexMono-Medium.ttf"

WORKDIR="$(mktemp -d)"
cleanup() { rm -rf "$WORKDIR"; }
trap cleanup EXIT INT TERM

# fetch_ttf ZIP_URL INNER_PATH DEST_TTF ZIP_CACHE_NAME
# Downloads ZIP_URL once per run (cached in $WORKDIR under ZIP_CACHE_NAME so
# pulling two files out of the same release zip only costs one download),
# and extracts a single file straight to DEST_TTF. Skips entirely if
# DEST_TTF already exists, on this run or any previous one.
fetch_ttf() {
    zip_url="$1"
    inner_path="$2"
    dest="$3"
    zip_cache_name="$4"

    if [ -f "$dest" ]; then
        echo "  [skip] $(basename "$dest") — already in tools/fonts/"
        return 0
    fi

    zip_path="$WORKDIR/$zip_cache_name"
    if [ ! -f "$zip_path" ]; then
        echo "  [download] $zip_url"
        if ! curl -fsSL -o "$zip_path" "$zip_url"; then
            echo "ERROR: failed to download $zip_url" >&2
            exit 1
        fi
    fi

    echo "  [extract] $inner_path -> $(basename "$dest")"
    if ! unzip -p "$zip_path" "$inner_path" >"$dest"; then
        rm -f "$dest"
        echo "ERROR: failed to extract '$inner_path' from $zip_cache_name" >&2
        exit 1
    fi
}

echo
echo "=== Fetching source fonts ==="
fetch_ttf "$IBM_PLEX_SANS_COND_ZIP_URL" \
    "ibm-plex-sans-condensed/fonts/complete/ttf/IBMPlexSansCondensed-SemiBold.ttf" \
    "$SANS_COND_SEMIBOLD" "sans-condensed.zip"
fetch_ttf "$IBM_PLEX_SANS_COND_ZIP_URL" \
    "ibm-plex-sans-condensed/fonts/complete/ttf/IBMPlexSansCondensed-Medium.ttf" \
    "$SANS_COND_MEDIUM" "sans-condensed.zip"
fetch_ttf "$IBM_PLEX_MONO_ZIP_URL" \
    "ibm-plex-mono/fonts/complete/ttf/IBMPlexMono-Medium.ttf" \
    "$MONO_MEDIUM" "mono.zip"

# ---------------------------------------------------------------------------
# 3. Subset ranges — verbatim from DESIGN.md §3.
# ---------------------------------------------------------------------------
RANGE_ASCII="0x20-0x7F"                            # ASCII
RANGE_LATIN1_UMLAUTS="0xC4,0xD6,0xDC,0xE4,0xF6,0xFC,0xDF" # Ä Ö Ü ä ö ü ß
RANGE_DEGREE_MIDDOT="0xB0,0xB7"                     # ° ·
RANGE_ARROW_DASH="0x2192,0x2014"                    # → —
RANGE_LATIN_EXT_A="0x0104-0x017C"                   # Polish/Romanian etc.

# "Core" set: guaranteed present on every single face this script produces,
# at every size. This is the non-negotiable minimum from DESIGN.md §3.
CORE_RANGES="-r $RANGE_ASCII -r $RANGE_LATIN1_UMLAUTS -r $RANGE_DEGREE_MIDDOT -r $RANGE_ARROW_DASH"

# "Full" set: core + Latin Extended-A (Polish/Romanian destinations such as
# Poznań, Timişoara). Used on every face EXCEPT the three hero sizes — see
# tools/README.md "Hero subsetting decision" for the measured byte cost and
# the reasoning. THE UMLAUTS AND THE ARROW ARE IN CORE_RANGES, SO THEY ARE
# PRESENT AT EVERY SIZE REGARDLESS OF THIS CHOICE.
FULL_RANGES="$CORE_RANGES -r $RANGE_LATIN_EXT_A"

# ---------------------------------------------------------------------------
# 4. Generate the ladder.
# ---------------------------------------------------------------------------
mkdir -p "$OUT_DIR"

GENERATED_FILES=""

# gen_font SRC_TTF SIZE_PX OUT_NAME RANGE_SET(core|full)
gen_font() {
    src="$1"
    size="$2"
    name="$3"
    range_set="$4"
    out="$OUT_DIR/$name.c"

    if [ "$range_set" = "core" ]; then
        ranges="$CORE_RANGES"
    else
        ranges="$FULL_RANGES"
    fi

    echo "  [gen] $name.c  (${size}px, $range_set ranges, bpp4) <- $(basename "$src")"
    # shellcheck disable=SC2086
    $LV_FONT_CONV \
        --font "$src" \
        --format lvgl \
        --bpp 4 \
        --size "$size" \
        --lv-font-name "$name" \
        -o "$out" \
        $ranges

    GENERATED_FILES="$GENERATED_FILES $out"
}

echo
echo "=== Generating fonts ==="
echo "-- IBM Plex Sans Condensed SemiBold, hero ladder (core ranges only) --"
gen_font "$SANS_COND_SEMIBOLD" 100 plex_sans_cond_100 core
gen_font "$SANS_COND_SEMIBOLD" 76  plex_sans_cond_76  core
gen_font "$SANS_COND_SEMIBOLD" 56  plex_sans_cond_56  core

echo "-- IBM Plex Sans Condensed Medium, secondary hero + body (full ranges) --"
gen_font "$SANS_COND_MEDIUM" 34 plex_sans_cond_34 full
gen_font "$SANS_COND_MEDIUM" 25 plex_sans_cond_25 full
gen_font "$SANS_COND_MEDIUM" 22 plex_sans_cond_22 full

echo "-- IBM Plex Mono Medium, data + chrome (full ranges) --"
gen_font "$MONO_MEDIUM" 32 plex_mono_32 full
gen_font "$MONO_MEDIUM" 17 plex_mono_17 full
gen_font "$MONO_MEDIUM" 13 plex_mono_13 full
gen_font "$MONO_MEDIUM" 12 plex_mono_12 full

# ---------------------------------------------------------------------------
# 5. Size report — PLAN.md's "Flash + PSRAM cost of the full font set" (M1).
# ---------------------------------------------------------------------------
echo
echo "=== Font set size report ==="
total_bytes=0
for f in $GENERATED_FILES; do
    bytes=$(wc -c <"$f" | tr -d ' ')
    total_bytes=$((total_bytes + bytes))
    printf '  %-24s %10d bytes\n' "$(basename "$f")" "$bytes"
done
printf -- '  ------------------------------------------\n'
printf '  %-24s %10d bytes\n' "TOTAL" "$total_bytes"
LC_NUMERIC=C awk -v b="$total_bytes" 'BEGIN { printf "  = %.1f KiB = %.2f MiB\n", b/1024, b/1048576 }'
echo
echo "Recorded for docs/PLAN.md 'Flash + PSRAM cost of the full font set'."
echo "Remember: CONFIG_SPIRAM_RODATA=y (AGENTS.md §7) puts these bytes in"
echo "PSRAM, not flash — they compete with the framebuffer for PSRAM bandwidth."
