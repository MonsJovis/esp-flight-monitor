/*
 * theme.h — esp-flight-monitor design tokens
 *
 * Source of truth: docs/DESIGN.md §2 (Colour) and §4 (Layout).
 *
 * THIS IS THE ONLY FILE IN THE CODEBASE ALLOWED TO CONTAIN A HEX COLOUR
 * LITERAL (AGENTS.md §10). Every screen references the named tokens below,
 * never `lv_color_hex(0x...)` directly. That is the only way the RTCA
 * DO-257A §2.1.6 six-colour ceiling stays enforceable once seven screens
 * exist and more than one person is editing them.
 *
 * ============================================================================
 *  THE HARD CEILING — READ BEFORE ADDING A TOKEN
 * ============================================================================
 * RTCA DO-257A §2.1.6 limits colour coding to six chromatic colours. This
 * file defines exactly six: magenta, green, cyan, amber, white, plus grey
 * as the non-chromatic seventh. DO NOT add a seventh chromatic colour.
 * If a new screen seems to need one, it needs to REPLACE one of the six
 * instead — go change DESIGN.md §2 first, and update this comment block
 * to match. Do not invent a colour here that is not in DESIGN.md §2.
 *
 * Semantics follow FAA AC 25-11A (Electronic Flight Displays), not
 * decoration — see the comment above each colour below.
 *
 * Red is reserved for warnings and is currently UNUSED. Keep it that way:
 * if everything can be red, nothing is.
 *
 * Contrast ratios in the comments are measured against the `ground`
 * background (#0A0B0D), per DESIGN.md §2 "Contrast — measured, not
 * claimed". Two tokens (magenta, text-tertiary) are deliberate AA-only
 * exceptions; every other colour-carrying token reaches AAA (7:1). See
 * DESIGN.md §2 for the reasoning — do not "fix" the two exceptions by
 * brightening them without reading it first.
 *
 * This file must be includable from plain C (LVGL 9.2 / ESP-IDF 5.4).
 * Tokens are function-like macros wrapping `lv_color_hex()`, not static
 * const globals — a C file-scope variable initializer must be a constant
 * expression, and `lv_color_hex()` is a function call, so a macro that
 * expands at the point of use (inside a running function, e.g.
 * `lv_obj_set_style_text_color(obj, THEME_MAGENTA, 0)`) is the form that
 * actually compiles everywhere LVGL styles are set.
 */

#ifndef ESP_FLIGHT_MONITOR_THEME_H
#define ESP_FLIGHT_MONITOR_THEME_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 1. Chromatic semantic colours (six) + grey
 *    DESIGN.md §2, "Colour" table + "Contrast — measured, not claimed" table.
 * ============================================================================
 */

/* AC 25-11A: Pilot-selectable reference / active route — "the thing you are
 * heading toward". Used for: the route, the bearing marker, the selected
 * aircraft.
 * Contrast on ground: 6.5:1 — AA, NOT AAA. Deliberate exception (see
 * DESIGN.md §2 rule 2): magenta-on-black is a documented AC 25-11A
 * high-confusion pair (§31.c(5)(g)), used anyway because the semantic fit
 * is exact, and always adjacent to an AAA element carrying the same
 * meaning (e.g. a magenta arrow next to a white/cyan bearing figure).
 * Magenta-on-black verdict is UNSETTLED pending the PLAN.md M1 panel check.
 */
#define THEME_MAGENTA lv_color_hex(0xFF3FDA)

/* AC 25-11A: Engaged modes, normal conditions. Used for: "ÜBER DIR" status,
 * home marker, saved WiFi.
 * Contrast on ground: 11.8:1 — AAA.
 */
#define THEME_GREEN lv_color_hex(0x00E676)

/* AC 25-11A: Armed modes / secondary data. Used for: altitude, distance,
 * settings values.
 * Contrast on ground: 12.7:1 — AAA.
 */
#define THEME_CYAN lv_color_hex(0x22E3FF)

/* AC 25-11A: Caution, abnormal source. Used for: "KEIN FLUGPLAN", missing
 * values, no network.
 * Contrast on ground: 11.0:1 — AAA.
 */
#define THEME_AMBER lv_color_hex(0xFFB300)

/* AC 25-11A: Scales, figures, units, labels. Used for: the hero destination.
 * Contrast on ground: 19.7:1 — AAA.
 */
#define THEME_WHITE lv_color_hex(0xFFFFFF)

/* Non-chromatic (the "plus grey" in DO-257A's six-plus-grey allowance).
 * Used for: labels. Same value as THEME_TEXT_LABEL below — one physical
 * colour, two names because DESIGN.md documents it in two different
 * tables (the semantic-colour table and the text-tone table).
 * Contrast on ground: 7.8:1 — AAA.
 */
#define THEME_GREY lv_color_hex(0x94A5B2)

/* ============================================================================
 * 2. Text tones (three, and only three)
 *    DESIGN.md §2, "Text tones" table. An earlier draft drifted to five
 *    near-duplicate greys across screens drawn on different days; these
 *    three are the collapsed, final set. Do not reintroduce a fourth.
 * ============================================================================
 */

/* Body text, airline names, sentences.
 * Contrast on ground: 15.6:1 — AAA.
 */
#define THEME_TEXT_PRIMARY lv_color_hex(0xDDE6EC)

/* Labels, chrome, secondary values. Identical value to THEME_GREY.
 * Contrast on ground: 7.8:1 — AAA.
 */
#define THEME_TEXT_LABEL lv_color_hex(0x94A5B2)

/* Units and prepositions ONLY, always placed adjacent to a brighter value
 * carrying the same meaning — e.g. "m hoch" beside a 32 px cyan number,
 * "von" beside a magenta route. Never used for information on its own.
 * Contrast on ground: 5.1:1 — AA, NOT AAA. Deliberate exception (see
 * DESIGN.md §2 rule 2) for the same never-colour/tone-alone reason as
 * THEME_MAGENTA above.
 */
#define THEME_TEXT_TERTIARY lv_color_hex(0x6E8494)

/* ============================================================================
 * 3. Surfaces and structure (nine tokens)
 *    DESIGN.md §2, "Surfaces and structure" table. Non-text, so the AA/AAA
 *    contrast rules do not apply — but they are still part of the system
 *    and belong here, not as literals in screen code.
 * ============================================================================
 */

/* Page background. Deliberately NOT pure black (#000000) — pure black
 * maximises halation, the glow bleed around bright glyphs that aging eyes
 * suffer most from. See DESIGN.md §2 "Ground is not pure black".
 */
#define THEME_GROUND lv_color_hex(0x0A0B0D)

/* Selected list row (DESIGN.md §5.4). */
#define THEME_SURFACE_SEL lv_color_hex(0x0C131A)

/* Saved-network card fill (DESIGN.md §5.7). */
#define THEME_SURFACE_GREEN lv_color_hex(0x08130D)

/* Active-preset card fill (DESIGN.md §5.6). */
#define THEME_SURFACE_MAGENTA lv_color_hex(0x140A15)

/* Section rules, outer range ring. */
#define THEME_HAIRLINE lv_color_hex(0x1A3340)

/* Inner range rings (DESIGN.md §5.5). */
#define THEME_HAIRLINE_DIM lv_color_hex(0x12262F)

/* List row dividers (DESIGN.md §5.4). */
#define THEME_DIVIDER lv_color_hex(0x101C24)

/* Unselected cards, slider tracks. */
#define THEME_BORDER_IDLE lv_color_hex(0x1C2B35)

/* Saved-network card border (DESIGN.md §5.7). */
#define THEME_BORDER_GREEN lv_color_hex(0x1C3B2A)

/* ============================================================================
 * 4. Layout constants
 *    DESIGN.md §4, "Layout". 480 = 60 * 8 — the base unit tiles the screen
 *    exactly, with no remainder.
 * ============================================================================
 */

#define THEME_SCREEN_WIDTH  480
#define THEME_SCREEN_HEIGHT 480
#define THEME_SIDE_PADDING  20
#define THEME_BASE_UNIT     8

#ifdef __cplusplus
}
#endif

#endif /* ESP_FLIGHT_MONITOR_THEME_H */
