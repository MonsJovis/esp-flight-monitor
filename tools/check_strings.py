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

So this script enforces the rule mechanically. Every string literal under
main/ — every .c and every .h, recursively — must either be structurally
meaningless (empty, pure punctuation, a bare format string), be a protocol
identifier rather than prose, or appear verbatim in main/strings_de.h.

WHY THE SCAN IS RECURSIVE AND WHY IT HAS A FLOOR. It used to be
`(main/ui).glob("*.c")` plus view_build.c, and a review evaded it four ways in
a few minutes: a German sentence in main/ui/anything/screen.c passed, a German
`#define` in main/ui/screen_settings.h passed, a German label in main/main.c
passed, and so did anything in main/data, main/net or main/debug. The glob was
the symptom. The disease is that a scan which silently shrinks to nothing still
prints "success", so enforced_files() now also refuses to run if the file set
falls below MIN_ENFORCED_FILES. A reorganisation that empties the scan has to
come and edit that number, in the diff, where someone can see it.

It also has a second job. `--list` prints the complete lexicon of German the
device can display — decoded to real UTF-8, no macro names, no C — so that the
reading-aloud pass can be done by someone who does not read code. That lexicon
lives in five files, and all five are parsed, never copied: a checker with its
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
TBL_AIRPORT = ROOT / "main" / "data" / "tbl_airport.c"
TBL_ACTYPE  = ROOT / "main" / "data" / "tbl_actype.c"
TBL_AIRLINE = ROOT / "main" / "data" / "tbl_airline.c"

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

# strftime's conversions, which are a different alphabet from printf's: the
# clock is formatted with strftime(b, n, "%H:%M", &tm) and nothing in printf's
# terminator class covers %H or %M. Without this the one format string on the
# most-looked-at line of the product was reported as a leak — a false positive
# in a gate is an invitation to weaken the gate, so it is worth the extra
# regex. The E and O modifiers are C99's locale-alternative forms.
_STRFTIME_SPEC_RE = re.compile(r"%[-_0^#]*[0-9]*[EO]?[aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ]")

# ESP-IDF's log tag. It is only ever passed to ESP_LOG*(), so it is developer-
# facing English by construction, but it is declared on its own line and so does
# not sit inside a call this scanner would otherwise skip. Both spellings are
# in the tree — `const char *TAG` and `const char *const TAG` — and only the
# first used to be recognised, so adding the second `const` (which is strictly
# better C) turned a clean file into a reported leak.
_TAG_DECL_RE = re.compile(
    r"^\s*static\s+const\s+char\s*\*\s*(?:const\s+)?TAG\s*="
)

# A `#define` whose NAME says the value is a machine identifier: an NVS
# namespace or key, an endpoint, a POSIX TZ string, a User-Agent. Every
# string-valued #define outside main/strings_de.h is one of these, and they are
# recognised by the naming convention rather than listed one by one — the first
# draft of this check listed them, and the very first run after that was broken
# by a new `#define OTA_KEY_BADVER "ota_badver"` landing in main/net/ota.c.
# A gate that has to be edited every time someone adds an NVS key is a gate
# people learn to route around.
#
# Note how narrow this is: the leading `\w+_` means the name must genuinely END
# in one of these words, so SCREEN_WIFI_TITLE is not exempt and a German
# sentence cannot hide behind it.
_MACHINE_DEFINE_RE = re.compile(
    r"^\s*#\s*define\s+"
    r"(?:TZ_\w+|\w+_(?:NS|NAMESPACE|URL|UA|USER_AGENT|KEY)(?:_\w+)?)\s"
)

# ---------------------------------------------------------------------------
# Literals that are identifiers, not prose
# ---------------------------------------------------------------------------
#
# Widening the scan from main/ui to all of main/ brings in the network and
# parsing layers, where most string literals are machine-facing: a JSON field
# name, an NVS key, a task name, a URL. None of them can reach a label, and
# demanding that "alt_baro" or "rb" be defined in strings_de.h would turn the
# German lexicon into a pile of protocol trivia and the gate into something
# people route around. The rule is therefore: a literal is exempt when it is
# an ARGUMENT TO A CALL that is known never to draw anything. The call is
# named here, in one visible list, rather than guessed at from the literal's
# shape — "Netzwerke" and "settings" look alike to a regex.

_NON_UI_CALL_PREFIXES = (
    "ESP_LOG",        # developer console, every level
    "ESP_EARLY_LOG",
    "nvs_",           # namespace and key names, never displayed
)

_NON_UI_CALLS = frozenset({
    # Developer console. The panel is not a terminal; none of this is drawn.
    "ESP_ERROR_CHECK",
    "printf", "fprintf", "vprintf", "vfprintf", "puts", "fputs", "perror",
    "log_memory_budget",          # main.c: formats one ESP_LOGI line
    "lvgl_mem_report",            # main.c: formats one ESP_LOGW line
    "dbg_bench_run",              # main/debug: a developer benchmark screen

    # Byte comparison and search. The argument is a value being matched, not a
    # value being shown: strcmp(ssid, "Gloggnitz-WLAN") compares an SSID.
    "strcmp", "strncmp", "strcasecmp", "strncasecmp",
    "strstr", "strchr", "strrchr", "strtok", "strtok_r", "strspn", "strcspn",
    "memcmp",

    # JSON field names, from cJSON and from the two local helpers that wrap it.
    "cJSON_GetObjectItem", "cJSON_GetObjectItemCaseSensitive",
    "cJSON_HasObjectItem", "str_field", "copy_field",

    # Operating-system and protocol identifiers: file names and modes, env
    # variables, host names, HTTP headers, FreeRTOS task names, netif keys.
    "fopen", "freopen", "remove", "rename",
    "setenv", "getenv", "unsetenv", "putenv",
    "getaddrinfo",
    "xTaskCreate", "xTaskCreatePinnedToCore",
    "esp_netif_get_handle_from_ifkey",
    "esp_http_client_set_header", "esp_http_client_set_url",
    "ESP_NETIF_SNTP_DEFAULT_CONFIG",
    "http_get", "http_post",

    # Screen identifiers. nav_open_overlay()'s second argument is the page id
    # that nav.c stores and compares — "wlan", not "WLAN". The heading the man
    # actually reads is built inside the screen, from strings_de.h.
    "nav_open_overlay",
})


def _is_non_ui_call(name):
    return name in _NON_UI_CALLS or name.startswith(_NON_UI_CALL_PREFIXES)


# A URI scheme separator does not occur in anything he reads. This covers the
# endpoint #defines, the snprintf'd query URLs and the User-Agent string in one
# rule instead of one allowlist entry each.
_URI_RE = re.compile(r"[a-zA-Z][a-zA-Z0-9+.\-]*://")

# The last narrow escape hatch: literals that are neither structural, nor in
# strings_de.h, nor an argument to one of the calls above, but that still never
# reach a label. Listed per file and spelled out in full, so that adding a
# GERMAN sentence to any of these files still fails — this exempts the literal,
# never the file. Keep it short; if a group of them grows, it wants a rule (or
# a home in strings_de.h), not more entries here.
_NON_UI_LITERALS = {
    # The two status words that only ever reach the ESP_LOGI line at the
    # bottom of report_nearest(); the German he reads for the same states is
    # STR_NO_FLIGHT_PLAN / STR_ROUTE_SEARCHING.
    "main/net/flight_source.c": (
        "no route", "route pending",
    ),
    # The routeset POST body is assembled by hand; this is JSON syntax.
    "main/net/route_parse.c": (
        "{\\\"planes\\\":[",
        "%s{\\\"callsign\\\":\\\"%s\\\",\\\"lat\\\":%.6f,\\\"lng\\\":%.6f}",
    ),
    # Source names and the English compass used in the developer log line;
    # the German compass he reads is compass_de_abbr() in fmt_de.c.
    "main/net/source_logic.c": (
        "adsb.lol (point)", "adsb.lol (lat/lon/dist)",
        "N", "NE", "E", "SE", "S", "SW", "W", "NW",
    ),
    # NVS key templates, one per stored network slot.
    "main/net/wifi.c": (
        "ssid%d", "pass%d",
    ),
    # main/main.c is a mixture of product bootstrap and developer diagnostics.
    #
    # The eight specimens below are font_card(), the M1 font gate: their job is
    # to make glyphs visible, not to say anything, and they are drawn on a
    # screen only a developer reaches. They belong in main/debug/ beside the
    # other developer screens (which this checker exempts as a directory), or
    # in strings_de.h if the card ever becomes something he sees. Until then
    # they are named here rather than exempting the whole of main.c, so that a
    # German label added anywhere else in main.c is still caught.
    "main/main.c": (
        # The font-specimen strings that used to be listed here moved to
        # main/debug/dbg_fontcard.c, which is exempt as a directory. Eight
        # entries fewer, and the exemption is now "developer diagnostics live
        # in main/debug" rather than eight spellings someone has to maintain.
        # network_status(): both go into one ESP_LOGW argument via snprintf.
        "never", "%lld ms ago",
        # nav page ids, stored and compared — see nav_open_overlay above.
        "ueber-dir", "liste", "radar",
    ),
}

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

    Five things are deliberately not returned:

      * comments, which may say whatever they like;
      * preprocessor lines other than #define — an #include's "theme.h" is a
        path, not prose, and #if / #pragma / #error carry no UI text either.
        #define IS scanned, because a local #define of a German sentence in a
        screen file is precisely the leak this script exists to catch;
      * the arguments of the calls in _NON_UI_CALLS — logging, byte
        comparison, JSON field names, OS identifiers. Tracked by paren depth
        rather than by line, so a log call wrapped over three lines is still
        skipped, and so a drawing call that happens to SHARE a line with a log
        call is still checked;
      * character literals, which cannot hold a sentence;
      * the "C" of `extern "C" {`, which is a linkage specifier.

    This is also what tools/check_font_coverage.py lexes with — one decoder and
    one lexer for both gates, because when they were separate they disagreed
    about \\U and the disagreement was itself a hole.
    """
    out = []
    i, n, line = 0, len(text), 1
    bol = True         # at the start of a line, ignoring leading whitespace
    armed = False      # the token just read is a call whose args are skipped
    depth = 0          # paren depth inside such a call; 0 means outside one
    prev_ident = None  # the identifier immediately before, for extern "C"

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
                prev_ident = None
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
            prev_ident = None
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
            if depth == 0 and prev_ident != "extern":
                out.append((start, "".join(buf)))
            armed = False
            prev_ident = None
            continue

        m = _IDENT_RE.match(text, i)
        if m:
            i = m.end()
            name = m.group(0)
            armed = depth == 0 and _is_non_ui_call(name)
            prev_ident = name
            continue

        if c == "(":
            if armed:
                depth = 1
            elif depth:
                depth += 1
            armed = False
            prev_ident = None
            i += 1
            continue
        if c == ")":
            if depth:
                depth -= 1
            armed = False
            prev_ident = None
            i += 1
            continue

        armed = False
        prev_ident = None
        i += 1

    return out


def c_unescape(raw):
    """Decode a C string literal's source spelling into the text it renders as.

    The escapes are decoded to BYTES first and the result decoded as UTF-8 at
    the end, because that is how the codebase writes non-ASCII: "\\xE2\\x80\\x94"
    is three bytes that together are one em dash, not three characters.

    Both \\uXXXX and \\UXXXXXXXX are handled. That is not pedantry: for a while
    check_font_coverage.py had its own decoder that knew \\u and not \\U, so
    "\\U00002026" read to it as the ASCII letters U, 0, 0, ... and a horizontal
    ellipsis — a glyph in none of the generated faces — walked straight past
    the font gate. Two decoders that disagree are a hole by construction, so
    there is now one, here, and the font checker imports it.
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
                try:
                    out += chr(int(hx, 16)).encode("utf-8")
                except (ValueError, OverflowError):
                    out += b"\xef\xbf\xbd"     # U+FFFD, so the caller complains
        else:
            out += e.encode("utf-8")
    return out.decode("utf-8", errors="replace")


def is_structural(raw):
    """True if this literal carries no German — punctuation, a format, nothing.

    Such a literal may stay inline. Everything else has to be named in the
    header. A literal is structural only if ALL of these hold:

      * it has no \\x / \\u / \\U escape. Those are only ever written here to
        spell a glyph — the em dash, the arrow, the degree sign — and a glyph
        on the panel is something he reads;
      * nothing in it renders outside printable ASCII, for the same reason and
        to catch "%d°" written with a literal degree sign rather than an escape.
        str.isalpha() is False for '°', so the letter test alone lets it pass;
      * no percent sign survives once real conversion specifications are
        removed. A lone "%" is the unit on the settings screen;
      * no letter survives either.

    Both printf's and strftime's conversions count as real, because the clock —
    the one thing on screen at every hour of the day — is built with strftime.
    """
    if raw == "":
        return True
    if re.search(r"\\[xuU]", raw):
        return False
    value = c_unescape(raw)
    if any(ord(ch) > 126 for ch in value):
        return False
    rest = _STRFTIME_SPEC_RE.sub("", _SPEC_RE.sub("", value))
    if "%" in rest:
        return False
    return not any(ch.isalpha() for ch in rest)


def is_non_ui(rel, raw):
    """True if this literal is machine-facing: a URI, or a listed exception."""
    if _URI_RE.search(c_unescape(raw)):
        return True
    return raw in _NON_UI_LITERALS.get(rel, ())


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
# The other homes of German: fmt_de.c, settings.c, tbl_airport.c, tbl_actype.c
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

_AIRPORT_TABLE_RE = re.compile(r"AIRPORTS\s*\[\s*\]\s*=\s*\{(.*?)^\s*\}\s*;", re.S | re.M)
_ACTYPE_TABLE_RE = re.compile(r"ACTYPES\s*\[\s*\]\s*=\s*\{(.*?)^\s*\}\s*;", re.S | re.M)
_AIRLINE_TABLE_RE = re.compile(r"AIRLINES\s*\[\s*\]\s*=\s*\{(.*?)^\s*\}\s*;", re.S | re.M)

# {"A20N", {"Airbus", "A320neo", "Airbus A320neo", "Mittelstreckenjet", AC_CAT…}}
_ACTYPE_ROW_RE = re.compile(r'\{\s*"(?:[^"\\]|\\.)*"\s*,\s*\{(.*?)\}\s*,?\s*\}', re.S)
_ACTYPE_SIZE_CLASS_INDEX = 3      # ac_type_t: manufacturer, model, full_name, size_class


def _table_body(path, table_re, what):
    """The initialiser body of one generated-shaped lookup table, or None."""
    if not path.exists():
        print("warning: %s is missing — %s are not in the listing"
              % (path.relative_to(ROOT), what), file=sys.stderr)
        return None
    m = table_re.search(path.read_text(encoding="utf-8"))
    if not m:
        print("warning: the lookup table in %s was not found — %s are not in "
              "the listing. Has the declaration style changed?"
              % (path.relative_to(ROOT), what), file=sys.stderr)
        return None
    return strip_comments(m.group(1))


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
    body = _table_body(SETTINGS, _PRESET_TABLE_RE, "the location names")
    if body is None:
        return []
    names = []
    for row in _ROW_RE.finditer(body):
        pieces = _LITERAL_RE.findall(row.group(1))
        if pieces:
            names.append(pieces[0])
    return [("Orte, zwischen denen er umschalten kann", names)] if names else []


def airport_groups():
    """The German city names in tbl_airport.c.

    AGENTS.md §1 makes these a language decision, not data: the route headline
    rendered as *Wien → London* is "the single most important thing on screen",
    and whether Cluj-Napoca reads as Klausenburg is exactly the kind of call
    only he can make. 556 of them is a long section to read aloud; reading a
    short one and shipping the other 555 unread is worse.

    The second literal of each row — the first is the ICAO code.
    """
    body = _table_body(TBL_AIRPORT, _AIRPORT_TABLE_RE, "the city names")
    if body is None:
        return []
    names = []
    for row in _ROW_RE.finditer(body):
        pieces = _LITERAL_RE.findall(row.group(1))
        if len(pieces) >= 2:
            names.append(pieces[1])
    return [("Städtenamen in der Routenzeile", names)] if names else []


def actype_groups():
    """The size_class field of tbl_actype.c — a closed set of ~22 German words.

    Everything else in that table is a manufacturer or a model designator
    ("Airbus A320neo"), which is a name and not German. size_class is not: it
    is the word the panel uses to say WHAT the thing overhead is, chosen from
    a small vocabulary, and every one of those choices is his to approve.
    """
    body = _table_body(TBL_ACTYPE, _ACTYPE_TABLE_RE, "the aircraft classes")
    if body is None:
        return []
    classes = []
    for row in _ACTYPE_ROW_RE.finditer(body):
        pieces = _LITERAL_RE.findall(row.group(1))
        if len(pieces) > _ACTYPE_SIZE_CLASS_INDEX:
            classes.append(pieces[_ACTYPE_SIZE_CLASS_INDEX])
    return [("Flugzeugklassen", classes)] if classes else []


def airline_note():
    """One line about tbl_airline.c, which is a list of names, not of German.

    "Austrian Airlines", "Lufthansa", "Turkish Airlines" — an airline's name is
    its name in every language, so printing 206 of them into a document meant
    for reading German aloud would bury the words that do need a decision. The
    count is parsed, not typed, so it cannot quietly drift.
    """
    body = _table_body(TBL_AIRLINE, _AIRLINE_TABLE_RE, "the airline names")
    if body is None:
        return None
    n = sum(1 for row in _ROW_RE.finditer(body)
            if len(_LITERAL_RE.findall(row.group(1))) >= 2)
    if not n:
        return None
    return ("Dazu kommen %d Namen von Fluggesellschaften (main/data/"
            "tbl_airline.c). Die stehen so auf dem Flugzeug — "
            "\"Austrian Airlines\", \"Lufthansa\" — und werden nicht "
            "übersetzt, darum sind sie hier nicht aufgezählt." % n)


# ---------------------------------------------------------------------------
# The two things this script does
# ---------------------------------------------------------------------------

# Whole files that are exempt, each because the German in it legitimately lives
# there rather than in main/strings_de.h. This is a list of FILES on purpose:
# an exemption anyone can read in one screen beats loosening the rule that
# applies to the other forty.
EXEMPT_FILES = {
    # The source of truth itself.
    "main/strings_de.h",
    # The two exceptions main/strings_de.h declares in its own header comment:
    # indexed tables whose entries are addressed by number, not by name.
    "main/data/fmt_de.c",       # weekdays, months, the three compass tables
    "main/data/settings.c",     # the location preset display names
    # Generated-shaped lookup tables, thousands of rows of key/value data.
    # They are not exempt from REVIEW — airport_groups() and actype_groups()
    # put their German into `--list`, which is where it gets read.
    "main/data/tbl_airport.c",
    "main/data/tbl_actype.c",
    "main/data/tbl_airline.c",
}

EXEMPT_DIRS = (
    "main/ui/fonts/",           # generated glyph data, not prose
    "main/debug/",              # developer screens; he never reaches them
)

# The floor. If the scan ever covers fewer files than this, something moved and
# the gate is no longer looking at the product — which is exactly the failure
# it exists to prevent, and exactly the failure that reports "success". There
# were 44 enforced files when this number was set; it is deliberately a little
# below that, so that deleting one file is not a build break, and deliberately
# not far below, so that a reorganisation has to come and edit it here.
MIN_ENFORCED_FILES = 38


def enforced_files():
    """Every file under main/ that can put text in front of him.

    Recursive, and both .c and .h, because every non-recursive or .c-only
    version of this function has been evaded within minutes of someone
    looking: a screen one directory down, a #define in a screen's header.
    """
    files = []
    for path in sorted(set((ROOT / "main").rglob("*.c"))
                       | set((ROOT / "main").rglob("*.h"))):
        rel = path.relative_to(ROOT).as_posix()
        if rel in EXEMPT_FILES:
            continue
        if any(rel.startswith(d) for d in EXEMPT_DIRS):
            continue
        files.append(path)

    if len(files) < MIN_ENFORCED_FILES:
        sys.exit(
            "user-facing strings: this check found only %d file(s) to scan "
            "under main/, and it is written to cover at least %d.\n\n"
            "Either a lot of code moved out of main/, or an exemption in "
            "tools/check_strings.py now matches more than it was meant to. "
            "Whichever it is, a gate that scans almost nothing and then "
            "prints 'success' is worse than no gate — so this one stops "
            "instead. Fix the scan, or lower MIN_ENFORCED_FILES in the same "
            "commit that moves the files, where a reviewer can see it."
            % (len(files), MIN_ENFORCED_FILES)
        )
    return files


def do_check():
    _, allowed = load_header()

    files = enforced_files()
    problems = []
    used_exemptions = set()
    for path in files:
        rel = path.relative_to(ROOT).as_posix()
        text = path.read_text(encoding="utf-8")
        lines = text.splitlines()
        for lineno, raw in scan_literals(text):
            if is_structural(raw) or raw in allowed:
                continue
            if is_non_ui(rel, raw):
                used_exemptions.add((rel, raw))
                continue
            src = lines[lineno - 1] if 0 < lineno <= len(lines) else ""
            if _TAG_DECL_RE.match(src):
                continue          # the ESP-IDF log tag
            if _MACHINE_DEFINE_RE.match(src):
                continue          # an NVS key, an endpoint, a TZ string
            shown = '"%s"' % raw
            rendered = c_unescape(raw)
            if rendered != raw:
                shown += " (%s)" % rendered
            problems.append("%s:%d: user-facing literal not in "
                            "main/strings_de.h: %s" % (rel, lineno, shown))

    # An exemption nobody needs any more is an exemption that will one day
    # cover something new by accident. Warning rather than failing, because
    # the literal it named may be in a file someone else is editing right now.
    stale = [(rel, raw) for rel, lits in sorted(_NON_UI_LITERALS.items())
             for raw in lits if (rel, raw) not in used_exemptions]
    if stale:
        print("warning: %d entr(ies) in _NON_UI_LITERALS no longer match "
              "anything and should be deleted from tools/check_strings.py: %s"
              % (len(stale), ", ".join("%s %r" % e for e in stale)),
              file=sys.stderr)

    if problems:
        print("user-facing strings: %d leak(s)\n" % len(problems))
        for p in problems:
            print("  " + p)
        print("\nMove each one into main/strings_de.h and use the macro "
              "(docs/PLAN.md M7). If it is machine-facing — a JSON key, an "
              "NVS key, a URL, a log line — pass it to one of the calls "
              "listed in _NON_UI_CALLS, or name it in _NON_UI_LITERALS with "
              "a reason.")
        return 1
    print("user-facing strings: every German word on the panel comes from "
          "main/strings_de.h (%d spelling(s), %d file(s) scanned)"
          % (len(allowed), len(files)))
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
    groups = (groups + fmt_de_groups() + settings_groups()
              + actype_groups() + airport_groups())

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
    note = airline_note()
    if note:
        print()
        print(note)
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
