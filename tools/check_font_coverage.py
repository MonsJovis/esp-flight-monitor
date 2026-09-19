#!/usr/bin/env python3
"""Fail if any UI string needs a glyph the generated fonts do not contain.

LVGL renders a missing glyph as NOTHING. No error, no placeholder, no log line —
the text is simply shorter than you wrote it, and on a panel nobody is reading
character by character that can survive a long time. AGENTS.md §7 calls this out
for umlauts; the same trap catches a real "…", a non-breaking space, or a typo-
graphic quote pasted in from a document.

The subset is defined in docs/DESIGN.md §3 and implemented in
tools/build_fonts.sh. This script is the enforcement.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

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
SCAN_DIRS = ["main"]
STRING_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')

# C escape sequences, decoded before the codepoint check. main/strings_de.h
# deliberately writes every non-ASCII glyph as a hex escape ("\xE2\x80\x94")
# so that no editor can silently re-encode it and so a grep finds it — which
# means that without this, the em dash, the arrow, the degree sign and the
# middle dot all read as plain ASCII backslashes here and NONE of them were
# ever checked against the subset.
# Two kinds of line hold text that never reaches a label, and a font subset is
# about what LVGL has to DRAW, not about what the serial terminal prints:
#
#   ESP_LOG*/printf  — developer output. Your terminal has every glyph there is.
#   LOG-ONLY         — an explicit, greppable promise that a literal on this
#                      line is built for a log line and is never passed to a
#                      widget. It has to be written by hand precisely because
#                      the checker cannot follow a variable from its assignment
#                      to its use; putting the burden on the author keeps the
#                      claim visible in the diff instead of buried in a
#                      heuristic here.
SKIP_RE = re.compile(r'\b(ESP_LOG[A-Z]*|ESP_EARLY_LOG[A-Z]*|printf|fprintf)\s*\(|LOG-ONLY')

_ESC_RE = re.compile(r'\\(x[0-9a-fA-F]{1,2}|u[0-9a-fA-F]{4}|[0-7]{1,3}|.)')


def decode_c_string(raw):
    """A C source literal's text -> the characters it actually denotes.

    Hex and octal escapes are BYTES (that is how "\xE2\x80\x94" spells one
    UTF-8 em dash), so they are accumulated as bytes and the whole thing is
    decoded once at the end. Anything that does not decode is returned as
    latin-1 so the caller still sees something to complain about rather than
    crashing on it.
    """
    out = bytearray()
    i = 0
    simple = {"n": b"\n", "t": b"\t", "r": b"\r", "0": b"\0",
              "\\": b"\\", '"': b'"', "'": b"'"}
    while i < len(raw):
        m = _ESC_RE.match(raw, i)
        if m is None:
            out += raw[i].encode("utf-8")
            i += 1
            continue
        body = m.group(1)
        if body[0] in "xX":
            out.append(int(body[1:], 16))
        elif body[0] == "u":
            out += chr(int(body[1:], 16)).encode("utf-8")
        elif body in simple:
            out += simple[body]
        elif body.isdigit():
            out.append(int(body, 8) & 0xFF)
        else:
            out += body.encode("utf-8")
        i = m.end()
    try:
        return out.decode("utf-8")
    except UnicodeDecodeError:
        return out.decode("latin-1")


def covered(cp, ranges):
    return any(lo <= cp <= hi for lo, hi in ranges)


def main():
    problems = []
    for d in SCAN_DIRS:
        for path in sorted((ROOT / d).rglob("*.c")) + sorted((ROOT / d).rglob("*.h")):
            # Generated font tables are data, not prose.
            if "ui/fonts/" in path.as_posix():
                continue
            for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                stripped = line.lstrip()
                if stripped.startswith("*") or stripped.startswith("//"):
                    continue          # comments may say whatever they like
                if SKIP_RE.search(line):
                    continue
                for m in STRING_RE.finditer(line):
                    for ch in decode_c_string(m.group(1)):
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
        return 1
    print("font coverage: every non-ASCII character in UI strings has a glyph")
    return 0


if __name__ == "__main__":
    sys.exit(main())
