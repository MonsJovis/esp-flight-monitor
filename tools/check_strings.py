#!/usr/bin/env python3
"""Fail if a German word he reads is written anywhere but main/strings_de.h.

docs/PLAN.md M7 asks for two things that are really the same thing: every
user-facing string in one translation unit, and then a pass where every screen
is read aloud with a native Austrian speaker. The second one is what the first
one is FOR. A label built with lv_label_set_text(l, "Kein Netz") compiles,
renders, and is invisible to the person doing the reading-aloud pass — it is not
in the header they were handed, so it never gets read, never gets corrected, and
the device ships saying something slightly wrong to the one man who uses it.
Nobody will ever file a bug: he will assume that is how it is meant to read.

So this script enforces the rule mechanically. Every string literal in
main/ui/*.c and main/data/view_build.c must either be structurally meaningless
(empty, pure punctuation, a bare printf format) or appear verbatim in
main/strings_de.h.

It also has a second job. `--list` prints the complete lexicon of German the
device can display — decoded to real UTF-8, no macro names, no C — so that the
reading-aloud pass can be done by someone who does not read code. That lexicon
lives in three files, and all three are parsed, never copied: a checker with its
own private copy of the strings is a checker that goes stale and then lies.
"""
import pathlib
import re
import sys
import unicodedata

ROOT = pathlib.Path(__file__).resolve().parent.parent

STRINGS_H = ROOT / "main" / "strings_de.h"
FMT_DE    = ROOT / "main" / "data" / "fmt_de.c"
SETTINGS  = ROOT / "main" / "data" / "settings.c"
VIEW_BUILD = ROOT / "main" / "data" / "view_build.c"

# ---------------------------------------------------------------------------
# C source lexing
# ---------------------------------------------------------------------------

_IDENT_RE = re.compile(r"[A-Za-z_]\w*")
_DIRECTIVE_RE = re.compile(r"#[ \t]*(\w*)")

# The printf conversion specifications, so that "%s %s" and "%02d:00" can stay
# inline where they belong. Note what is NOT in the terminator class: '%'.
# A "%%" prints a literal percent sign, and a percent sign on the settings
# screen is a unit he reads ("80 %"), not punctuation — so it must be named in
# the header like every other word.
_SPEC_RE = re.compile(r"%[-+ #0-9.*]*(?:hh|h|ll|l|j|z|t|L)?[diouxXeEfgGaAcspn]")

# ESP-IDF's log tag. It is only ever passed to ESP_LOG*(), so it is developer-
# facing English by construction, but it is declared on its own line and so does
# not sit inside a call this scanner would otherwise skip.
_TAG_DECL_RE = re.compile(r"^\s*static\s+const\s+char\s*\*\s*TAG\s*=")

_SIMPLE_ESCAPES = {
    "a": 7, "b": 8, "f": 12, "n": 10, "r": 13, "t": 9, "v": 11,
    "\\": 0x5C, "'": 0x27, '"': 0x22, "?": 0x3F,
}
_HEX = "0123456789abcdefABCDEF"


def scan_literals(text):
    """Every string literal in C source `text`, as (lineno, source_spelling).

    `source_spelling` is the raw text between the quotes, exactly as written —
    "\\xE2\\x80\\x94" comes back as those twelve characters, not as an em dash,
    because that is what has to be matched against the header.

    Four things are deliberately not returned:

      * comments, which may say whatever they like;
      * preprocessor lines other than #define — an #include's "theme.h" is a
        path, not prose, and #if / #pragma / #error carry no UI text either.
        #define IS scanned, because a local #define of a German sentence in a
        screen file is precisely the leak this script exists to catch;
      * the arguments of ESP_LOG*() and ESP_ERROR_CHECK(), developer-facing
        English that never reaches the panel. Tracked by paren depth rather
        than by line, so a log call wrapped over three lines is still skipped;
      * character literals, which cannot hold a sentence.
    """
    out = []
    i, n, line = 0, len(text), 1
    bol = True      # at the start of a line, ignoring leading whitespace
    armed = False   # the token just read was ESP_LOG* / ESP_ERROR_CHECK
    depth = 0       # paren depth inside such a call; 0 means outside one

    while i < n:
        c = text[i]

        # Whitespace never disarms: "ESP_LOGI (TAG, ...)" is still a log call.
        if c == "\n":
            line += 1
            i += 1
            bol = True
            continue
        if c in " \t\r\f\v":
            i += 1
            continue

        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            end = n if j < 0 else j + 2
            line += text.count("\n", i, end)
            i = end
            bol = False
            continue

        if bol and c == "#":
            m = _DIRECTIVE_RE.match(text, i)
            if m.group(1) == "define":
                i = m.end()
                bol = False
                armed = False
                continue
            # Skip the whole directive, backslash continuations included.
            while True:
                j = text.find("\n", i)
                if j < 0:
                    i = n
                    break
                k = j - 1 if j > i and text[j - 1] == "\r" else j
                if k > i and text[k - 1] == "\\":
                    i = j + 1
                    line += 1
                    continue
                i = j     # leave the newline for the branch above
                break
            continue

        bol = False

        if c == "'":
            i += 1
            while i < n and text[i] != "'":
                if text[i] == "\\":
                    i += 1
                if i < n and text[i] == "\n":
                    line += 1
                i += 1
            i += 1
            armed = False
            continue

        if c == '"':
            start = line
            i += 1
            buf = []
            while i < n and text[i] != '"':
                if text[i] == "\\" and i + 1 < n:
                    if text[i + 1] == "\n":
                        line += 1
                    buf.append(text[i])
                    buf.append(text[i + 1])
                    i += 2
                    continue
                if text[i] == "\n":
                    line += 1
                buf.append(text[i])
                i += 1
            i += 1
            if depth == 0:
                out.append((start, "".join(buf)))
            armed = False
            continue

        m = _IDENT_RE.match(text, i)
        if m:
            i = m.end()
            name = m.group(0)
            armed = depth == 0 and (name.startswith("ESP_LOG")
                                    or name == "ESP_ERROR_CHECK")
            continue

        if c == "(":
            if armed:
                depth = 1
            elif depth:
                depth += 1
            armed = False
            i += 1
            continue
        if c == ")":
            if depth:
                depth -= 1
            armed = False
            i += 1
            continue

        armed = False
        i += 1

    return out


def c_unescape(raw):
    """Decode a C string literal's source spelling into the text it renders as.

    The escapes are decoded to BYTES first and the result decoded as UTF-8 at
    the end, because that is how the codebase writes non-ASCII: "\\xE2\\x80\\x94"
    is three bytes that together are one em dash, not three characters.
    """
    out = bytearray()
    i, n = 0, len(raw)
    while i < n:
        ch = raw[i]
        if ch != "\\":
            out += ch.encode("utf-8")
            i += 1
            continue
        i += 1
        if i >= n:
            out += b"\\"
            break
        e = raw[i]
        i += 1
        if e in _SIMPLE_ESCAPES:
            out.append(_SIMPLE_ESCAPES[e])
        elif e == "x":
            # C's \x is greedy over hex digits; this codebase always writes
            # exactly two, and stopping at two keeps "\xE2ab" from swallowing
            # the 'a' and 'b' into one absurd codepoint.
            hx = ""
            while i < n and len(hx) < 2 and raw[i] in _HEX:
                hx += raw[i]
                i += 1
            if hx:
                out.append(int(hx, 16))
        elif e in "01234567":
            oc = e
            while i < n and len(oc) < 3 and raw[i] in "01234567":
                oc += raw[i]
                i += 1
            out.append(int(oc, 8) & 0xFF)
        elif e in "uU":
            width = 4 if e == "u" else 8
            hx = ""
            while i < n and len(hx) < width and raw[i] in _HEX:
                hx += raw[i]
                i += 1
            if hx:
                out += chr(int(hx, 16)).encode("utf-8")
        else:
            out += e.encode("utf-8")
    return out.decode("utf-8", errors="replace")


def is_structural(raw):
    """True if this literal carries no German — punctuation, a format, nothing.

    Such a literal may stay inline. Everything else has to be named in the
    header. A literal is structural only if ALL of these hold:

      * it has no \\x / \\u escape. Those are only ever written here to spell a
        glyph — the em dash, the arrow, the degree sign — and a glyph on the
        panel is something he reads;
      * nothing in it renders outside printable ASCII, for the same reason and
        to catch "%d°" written with a literal degree sign rather than an escape.
        str.isalpha() is False for '°', so the letter test alone lets it pass;
      * no percent sign survives once real conversion specifications are
        removed. A lone "%" is the unit on the settings screen;
      * no letter survives either.
    """
    if raw == "":
        return True
    if re.search(r"\\[xuU]", raw):
        return False
    value = c_unescape(raw)
    if any(ord(ch) > 126 for ch in value):
        return False
    rest = _SPEC_RE.sub("", value)
    if "%" in rest:
        return False
    return not any(ch.isalpha() for ch in rest)


# ---------------------------------------------------------------------------
# main/strings_de.h
# ---------------------------------------------------------------------------

_SECTION_RE = re.compile(r"^\s*/\*\s*-{2,}\s*(.*?)\s*-{2,}\s*\*/\s*$")
_STR_DEFINE_RE = re.compile(r"^\s*#\s*define\s+((?:STR|FMT)_\w+)\s+(.*)$")
_ANY_DEFINE_RE = re.compile(r"^\s*#\s*define\s+(\w+)\s+(.*)$")
_LITERAL_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')

DEFAULT_GROUP = "Bildschirmtexte"


def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", " ", s, flags=re.S)
    s = re.sub(r"//[^\n]*", "", s)
    return re.sub(r"/\*.*$", "", s, flags=re.S)   # an unterminated opener


def parse_strings_h():
    """Parse main/strings_de.h into (groups, allowed).

    `groups` is an ordered list of (section, [spelling, ...]) for the listing,
    split by the /* --- Section --- */ comments if the header has them and one
    group if it does not. `allowed` is the set of spellings a .c file may
    contain inline.

    Adjacent-literal concatenation is handled. A define written as "a" "b"
    LISTS as one string, "ab", because that is what renders — but both halves
    join `allowed` as well, since the same sentence may legitimately be broken
    at a different point where it is used.
    """
    text = STRINGS_H.read_text(encoding="utf-8")
    text = re.sub(r"\\\n", " ", text)         # join line continuations

    groups = []           # [(name, [spelling, ...])]
    index = {}
    strays = []
    allowed = set()

    def bucket(name):
        if name not in index:
            index[name] = []
            groups.append((name, index[name]))
        return index[name]

    current = DEFAULT_GROUP
    for raw_line in text.splitlines():
        sect = _SECTION_RE.match(raw_line)
        if sect and sect.group(1):
            current = sect.group(1)
            bucket(current)
            continue
        m = _STR_DEFINE_RE.match(raw_line)
        if not m:
            other = _ANY_DEFINE_RE.match(raw_line)
            if other and '"' in strip_comments(other.group(2)):
                strays.append(other.group(1))
            continue
        pieces = _LITERAL_RE.findall(strip_comments(m.group(2)))
        if not pieces:
            continue
        b = bucket(current)
        joined = "".join(pieces)
        if joined not in b:
            b.append(joined)
        allowed.add(joined)
        allowed.update(pieces)

    if strays:
        print("warning: main/strings_de.h defines %s — not named STR_* or "
              "FMT_*, so it is invisible to this check and to --list"
              % ", ".join(sorted(set(strays))), file=sys.stderr)

    return groups, allowed


def load_header():
    """The header's groups, or a precise refusal to run without it."""
    if not STRINGS_H.exists():
        sys.exit(
            "main/strings_de.h does not exist. Every string the man reads "
            "belongs in it (docs/PLAN.md M7); with no header there is nothing "
            "to check against, and a check that passes because it found "
            "nothing is worse than no check at all."
        )
    groups, allowed = parse_strings_h()
    if not allowed:
        sys.exit(
            "main/strings_de.h parses to zero '#define STR_…' / '#define "
            "FMT_…' lines. Either the file is still empty or the naming "
            "convention changed — either way this check cannot run, and it "
            "will not pretend to have run."
        )
    return groups, allowed


# ---------------------------------------------------------------------------
# The other two homes of German: fmt_de.c and settings.c
# ---------------------------------------------------------------------------

_ARRAY_RE = re.compile(
    r"static\s+const\s+char\s*\*\s*(?:const\s+)?(\w+)\s*\[[^\]]*\]\s*=\s*\{(.*?)\}\s*;",
    re.S,
)

# Plain-language headings for the tables in fmt_de.c. Only the HEADINGS are
# written here — the words themselves are always parsed out of the file, the
# same way check_font_coverage.py parses the ranges out of build_fonts.sh. An
# unknown table still gets listed, just under a duller name.
_FMT_HEADINGS = {
    "COMPASS_ABBR": "Himmelsrichtungen, abgekürzt",
    "COMPASS_WORD": "Himmelsrichtungen, ausgeschrieben",
    "COMPASS_ADV":  "Himmelsrichtungen, als Richtungswort",
    "WEEKDAY_DE":   "Wochentage",
    "MONTH_DE":     "Monate",
}

_PRESET_TABLE_RE = re.compile(r"k_presets\s*\[\s*\]\s*=\s*\{(.*?)^\s*\}\s*;", re.S | re.M)
_ROW_RE = re.compile(r"\{([^{}]*)\}")


def fmt_de_groups():
    """The German lexicon tables in fmt_de.c, as ordered (heading, [raw])."""
    if not FMT_DE.exists():
        print("warning: %s is missing — its tables are not in the listing"
              % FMT_DE.relative_to(ROOT), file=sys.stderr)
        return []
    text = FMT_DE.read_text(encoding="utf-8")
    groups = []
    for m in _ARRAY_RE.finditer(text):
        name = m.group(1)
        pieces = _LITERAL_RE.findall(strip_comments(m.group(2)))
        if pieces:
            groups.append((_FMT_HEADINGS.get(name, "Tabelle %s" % name), pieces))
    if not groups:
        print("warning: no string tables found in %s — has the declaration "
              "style changed?" % FMT_DE.relative_to(ROOT), file=sys.stderr)
    return groups


def settings_groups():
    """The location preset display names in settings.c.

    The first literal of each row only. The second is a POSIX TZ string
    ("CET-1CEST,M3.5.0,M10.5.0/3"), which nobody ever sees and which would be
    noise of the worst kind in a list meant to be read aloud.
    """
    if not SETTINGS.exists():
        print("warning: %s is missing — the location names are not in the "
              "listing" % SETTINGS.relative_to(ROOT), file=sys.stderr)
        return []
    m = _PRESET_TABLE_RE.search(SETTINGS.read_text(encoding="utf-8"))
    if not m:
        print("warning: the k_presets table was not found in %s — the "
              "location names are not in the listing"
              % SETTINGS.relative_to(ROOT), file=sys.stderr)
        return []
    names = []
    for row in _ROW_RE.finditer(strip_comments(m.group(1))):
        pieces = _LITERAL_RE.findall(row.group(1))
        if pieces:
            names.append(pieces[0])
    return [("Orte, zwischen denen er umschalten kann", names)] if names else []


# ---------------------------------------------------------------------------
# The two things this script does
# ---------------------------------------------------------------------------

def enforced_files():
    """Every file that puts text in front of him.

    main/ui/fonts/*.c is generated glyph data, not prose, and is excluded by
    not recursing into it.
    """
    files = sorted((ROOT / "main" / "ui").glob("*.c"))
    if not VIEW_BUILD.exists():
        sys.exit("%s does not exist — has it been renamed? This check will not "
                 "quietly skip it." % VIEW_BUILD.relative_to(ROOT))
    return files + [VIEW_BUILD]


def do_check():
    _, allowed = load_header()

    problems = []
    for path in enforced_files():
        text = path.read_text(encoding="utf-8")
        lines = text.splitlines()
        for lineno, raw in scan_literals(text):
            if is_structural(raw) or raw in allowed:
                continue
            if 0 < lineno <= len(lines) and _TAG_DECL_RE.match(lines[lineno - 1]):
                continue          # the ESP-IDF log tag
            shown = '"%s"' % raw
            rendered = c_unescape(raw)
            if rendered != raw:
                shown += " (%s)" % rendered
            problems.append("%s:%d: user-facing literal not in "
                            "main/strings_de.h: %s"
                            % (path.relative_to(ROOT), lineno, shown))

    if problems:
        print("user-facing strings: %d leak(s)\n" % len(problems))
        for p in problems:
            print("  " + p)
        print("\nMove each one into main/strings_de.h and use the macro "
              "(docs/PLAN.md M7).")
        return 1
    print("user-facing strings: every German word on the panel comes from "
          "main/strings_de.h (%d spelling(s))" % len(allowed))
    return 0


def sort_key(value):
    """Sort the way a German dictionary does: ä with a, ö with o, ß as ss.

    Plain code-point order files "Jänner" after "Juni" and "März" after "Mai",
    which looks like a bug to the person reading the list.
    """
    folded = value.replace("ß", "ss").replace("SS", "ss")
    folded = unicodedata.normalize("NFD", folded.lower())
    return "".join(c for c in folded if not unicodedata.combining(c))


def do_list():
    if not STRINGS_H.exists():
        print("warning: main/strings_de.h does not exist yet — the screen "
              "texts are missing from this listing", file=sys.stderr)
        groups = []
    else:
        groups, _ = parse_strings_h()
    groups = groups + fmt_de_groups() + settings_groups()

    print("Alles, was auf dem Bildschirm stehen kann.")
    print("Vorlesen, Wort für Wort. Was falsch klingt, ist falsch.")

    total = 0
    for name, items in groups:
        values = sorted({c_unescape(s) for s in items}, key=sort_key)
        if not values:
            continue
        print()
        print("== %s ==" % name)
        for v in values:
            print(v)
        total += len(values)

    print()
    print("(%d Einträge)" % total)
    return 0


def main(argv):
    args = argv[1:]
    if args == ["--list"]:
        return do_list()
    if args:
        sys.exit("usage: check_strings.py [--list]")
    return do_check()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
