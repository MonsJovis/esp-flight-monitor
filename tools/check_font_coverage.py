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

SCAN_DIRS = ["main/ui", "main/data"]
STRING_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


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
                for m in STRING_RE.finditer(line):
                    for ch in m.group(1):
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
