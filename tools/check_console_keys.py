#!/usr/bin/env python3
"""Fail if the debug console answers to a key nothing tells you about.

The serial console is how every screen on this device gets verified (D4, D41):
a screen that can only be reached by tapping the glass is a screen nobody
checks, so `on_cmd()` in main/main.c has a key for each one. Two places
describe that console — the comment block at the top of main.c, and the
"ready:" line the firmware prints at boot — and NEITHER of them is consulted
by the code, so both drift the moment someone adds a key and forgets.

They had. Two consecutive reviews found it: the first that `LONGPRESS_MS` had
drifted from its comment, the second that the header and the ready line still
said `1-4=fixture` and mentioned none of `K`, `a` or `Y`. And when this script
was first run it turned up four more nobody had reported — `u`, `i`, `p`, `t`
and `v` had never been in the ready line at all, and `u` had never been in the
header either.

AGENTS.md §11 rule 1 is "the comment had drifted from the code, and the review
believed the comment". A key that works and is written down nowhere is the
same failure pointed the other way: the next person cannot use it, so the
screen behind it goes back to being one nobody checks.

Usage:  python3 tools/check_console_keys.py [--list]
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MAIN = ROOT / "main" / "main.c"
DBG  = ROOT / "main" / "debug" / "dbg_screen.c"


def dispatched():
    """Every key the firmware acts on: on_cmd()'s, plus dbg_screen.c's own.

    Read out of the SOURCE rather than listed here, because a checker with its
    own copy of the answer is a checker that goes stale and then lies — the
    same reason check_font_coverage.py parses its ranges out of
    tools/build_fonts.sh instead of keeping a copy.
    """
    src = MAIN.read_text(encoding="utf-8")
    try:
        body = src[src.index("static void on_cmd(char c)"):]
        body = body[:body.index("\nvoid app_main")]
    except ValueError:
        sys.exit("check_console_keys: on_cmd() not found in main/main.c — "
                 "did it get renamed? Fix this script in the same commit.")

    keys = set(re.findall(r"c == '(.)'", body))
    for lo, hi in re.findall(r"c >= '(.)' && c <= '(.)'", body):
        keys |= {chr(x) for x in range(ord(lo), ord(hi) + 1)}

    # dbg_screen.c reads the same stream and takes the screenshot key itself,
    # before on_cmd() ever sees it.
    for lo_hi in re.findall(r"c == '(.)'(?:\s*\|\|\s*c == '(.)')?\s*\)\s*screenshot\(\)",
                            DBG.read_text(encoding="utf-8")):
        keys |= {k for k in lo_hi if k}

    if len(keys) < 20:
        sys.exit("check_console_keys: only found %d key(s), and this console has "
                 "had more than twenty since M8. The dispatch moved or changed "
                 "shape, and a check that finds almost nothing and prints "
                 "'success' is worse than no check." % len(keys))
    return keys


def documented(text):
    """Keys named in a prose blob, expanding `1-5` ranges and `k/K` alternations.

    Deliberately generous about HOW a key is written, because the two blobs are
    written for humans and reformatting them must not fail a build. It is only
    strict about whether the character appears as a key at all.
    """
    found = set()
    for lo, hi in re.findall(r"(?<![A-Za-z0-9])([0-9A-Za-z])\s*-\s*([0-9A-Za-z])(?![A-Za-z0-9])",
                             text):
        if ord(lo) <= ord(hi) and (lo.isdigit() == hi.isdigit()):
            found |= {chr(x) for x in range(ord(lo), ord(hi) + 1)}
    # A bare key: one character with no letter or digit welded to either side.
    # "k/K=wlan" yields k and K; "Einstellungen" yields nothing.
    found |= set(re.findall(r"(?<![A-Za-z0-9])([0-9A-Za-z])(?![A-Za-z0-9])", text))
    return found


def blobs():
    src = MAIN.read_text(encoding="utf-8")
    header = src[:src.index("#include")]
    m = re.search(r'ESP_LOGW\(TAG, "ready: (.*?)"\s*\)\s*;', src, re.S)
    if m is None:
        sys.exit("check_console_keys: the boot 'ready:' line is gone from "
                 "main/main.c. It is one of the two things this checks; "
                 "restore it or fix this script in the same commit.")
    ready = m.group(1).replace('"', " ")
    return {"the comment block at the top of main/main.c": header,
            "the boot 'ready:' line": ready}


def main(argv):
    keys = dispatched()
    if "--list" in argv:
        print("".join(sorted(keys)))
        return 0

    problems = []
    for where, text in blobs().items():
        missing = sorted(keys - documented(text))
        if missing:
            problems.append("%s does not mention: %s"
                            % (where, " ".join(repr(k) for k in missing)))

    if problems:
        print("console keys: %d place(s) out of date\n" % len(problems))
        for p in problems:
            print("  " + p)
        print("\nEvery key on_cmd() answers to has to appear in both, because "
              "they are the only two places anyone will look. A key that works "
              "and is written down nowhere puts the screen behind it back to "
              "being one nobody checks (D4, D41).")
        return 1

    print("console keys: all %d are named in both the header and the ready line"
          % len(keys))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
