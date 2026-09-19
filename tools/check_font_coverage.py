#!/usr/bin/env python3
"""Fail if any UI string needs a glyph the generated fonts do not contain.

LVGL renders a missing glyph as NOTHING. No error, no placeholder, no log line —
the text is simply shorter than you wrote it, and on a panel nobody is reading
character by character that can survive a long time. AGENTS.md §7 calls this out
for umlauts; the same trap catches a real "…", a non-breaking space, or a typo-
graphic quote pasted in from a document.

The subset is defined in docs/DESIGN.md §3 and implemented in
tools/build_fonts.sh. This script is the enforcement.

HOW IT READS C, AND WHY IT NO LONGER DOES IT ITSELF. This file used to find
literals with one regex per line and decode them with its own escape table.
A review put a "…" through it four different ways in a few minutes:

  * a string broken over two lines with a backslash, because the regex wanted
    both quotes on one line;
  * a line containing the character literal '"', because the regex paired that
    quote with the next one and read the label's text as "the bit in between";
  * "\\U00002026", because the private escape table knew \\u and not \\U — while
    check_strings.py's decoder knew both. Two decoders that disagree are a hole
    by construction, which is why there is now one, imported;
  * any line that also mentioned ESP_LOG, because the skip was per LINE rather
    than per CALL, so `ESP_LOGI(TAG, "x"); lv_label_set_text(l, "Flüge …");`
    skipped both halves.

All four are gone because the lexing and the decoding are now check_strings.py's
scan_literals() and c_unescape(): one implementation, fixed in one place, used
by both gates.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from check_strings import c_unescape, scan_literals   # noqa: E402

# The ranges are PARSED OUT OF tools/build_fonts.sh rather than copied here.
# A checker with its own private copy of the subset is a checker that goes stale
# and then lies — which happened on the first draft of this file, within minutes.
SCRIPT = (ROOT / "tools" / "build_fonts.sh").read_text(encoding="utf-8")


def _range_var(name):
    m = re.search(rf'^{name}="([^"]+)"', SCRIPT, re.M)
    if not m:
        sys.exit(f"{name} not found in tools/build_fonts.sh — did it get renamed?")
    out = []
    for part in m.group(1).split(","):
        part = part.strip()
        if "-" in part:
            lo, hi = part.split("-", 1)
            out.append((int(lo, 16), int(hi, 16)))
        else:
            cp = int(part, 16)
            out.append((cp, cp))
    return out


def _ranges_for(var):
    """Expand a *_RANGES variable (a string of '-r $VAR' fragments)."""
    m = re.search(rf'^{var}="([^"]+)"', SCRIPT, re.M)
    if not m:
        sys.exit(f"{var} not found in tools/build_fonts.sh")
    out = []
    for name in re.findall(r'\$(\w+)', m.group(1)):
        if name.endswith("_RANGES"):
            out += _ranges_for(name)
        else:
            out += _range_var(name)
    return out


# Every face gets CORE; the three hero sizes get only CORE, everything else
# also gets Latin Extended-A. See tools/README.md for why.
HERO_RANGES = _ranges_for("CORE_RANGES")
FULL_RANGES = _ranges_for("FULL_RANGES")

# "main", not ["main/ui", "main/data"]. The narrow list was a silent hole: when
# PLAN.md M7 gathered every user-facing string into main/strings_de.h — one
# directory level up from both scanned dirs — this checker went on passing
# because there was nothing left in its scan path to check. A gate that reports
# success after its subject has moved out from under it is worse than no gate,
# so it now scans the whole component and names what it skips.
#
# Unlike check_strings.py, nothing here is exempt but the generated glyph
# tables: a missing glyph is missing on a developer screen too, and the city
# and aircraft tables are precisely where a stray "ā" or "ș" would come from.
SCAN_DIRS = ["main"]

# The one hatch, and the reason it needs anchoring.
#
# A font subset is about what LVGL has to DRAW. Developer output does not go
# through LVGL — the serial terminal has every glyph there is — and the
# arguments of ESP_LOG*/printf are already skipped by scan_literals(). What
# that cannot see is a literal ASSIGNED to a variable that is only ever logged,
# so LOG-ONLY exists to let the author say so by hand: the claim then lives in
# the diff instead of in a heuristic here. main/debug/dbg_fixture.c:49-66 is
# the real case — four strings carrying "§" (U+00A7), which is genuinely in
# neither font range and genuinely never drawn.
#
# It has to be a COMMENT WHOSE WHOLE BODY IS "LOG-ONLY" and nothing else. As a
# bare substring it granted itself to anything that merely mentioned it,
# including the negation of its own claim: `/* NOT LOG-ONLY: this really does
# reach a label */` was an exemption, and so were `/* see docs/ANALOG-ONLY.md
# */` and `/* LOG-ONLY but actually drawn */`. Anchoring on the body rather
# than on the end of the line means a trailing brace does not silently cancel
# the marker, while every one of those three still fails to earn it.
#
# The marker covers the literals that START on its own line — not the file, not
# the enclosing block, not the next line.
_LOG_ONLY_RE = re.compile(r"/\*[ \t]*LOG-ONLY[ \t]*\*/|//[ \t]*LOG-ONLY[ \t]*$")

# One decoder, two gates — asserted, not assumed. Every spelling of U+2026 that
# a C compiler accepts has to arrive here as U+2026, because the one that did
# not (\U, eight digits) is how a horizontal ellipsis got past this check.
_ELLIPSIS_SPELLINGS = (r"…", r"\U00002026", r"\xE2\x80\xA6",
                       r"\342\200\246", "…")
for _spelling in _ELLIPSIS_SPELLINGS:
    if c_unescape(_spelling) != "…":
        sys.exit("tools/check_strings.py's c_unescape() decodes %r to %r, not "
                 "U+2026. The two gates share that decoder precisely so they "
                 "cannot disagree about an escape; fix it there."
                 % (_spelling, c_unescape(_spelling)))


def covered(cp, ranges):
    return any(lo <= cp <= hi for lo, hi in ranges)


def main():
    problems = []
    for d in SCAN_DIRS:
        root = ROOT / d
        for path in sorted(set(root.rglob("*.c")) | set(root.rglob("*.h"))):
            # Generated font tables are data, not prose.
            if "ui/fonts/" in path.as_posix():
                continue
            text = path.read_text(encoding="utf-8")
            lines = text.splitlines()
            for lineno, raw in scan_literals(text):
                src = lines[lineno - 1] if 0 < lineno <= len(lines) else ""
                if _LOG_ONLY_RE.search(src):
                    continue
                for ch in c_unescape(raw):
                    cp = ord(ch)
                    if cp < 0x80:
                        continue
                    if not covered(cp, FULL_RANGES):
                        problems.append(
                            f"{path.relative_to(ROOT)}:{lineno}: U+{cp:04X} "
                            f"{ch!r} is in NO generated face — it will render "
                            f"as nothing"
                        )
                    elif not covered(cp, HERO_RANGES):
                        problems.append(
                            f"{path.relative_to(ROOT)}:{lineno}: U+{cp:04X} "
                            f"{ch!r} is missing from the 100/76/56 px hero "
                            f"faces — fine in body text, blank if it reaches "
                            f"a hero"
                        )

    if problems:
        print("font coverage: %d problem(s)\n" % len(problems))
        for p in problems:
            print("  " + p)
        print("\nEither add the range in tools/build_fonts.sh and regenerate, "
              "or spell the text with a glyph the subset has. If the literal "
              "genuinely never reaches a widget, put an exact /* LOG-ONLY */ "
              "comment — those nine characters and nothing else — on its line.")
        return 1
    print("font coverage: every non-ASCII character in UI strings has a glyph")
    return 0


if __name__ == "__main__":
    sys.exit(main())
