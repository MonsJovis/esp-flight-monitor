#!/usr/bin/env python3
"""Generate main/net/tz_table.h from the system tzdata.

Open-Meteo's geocoding response names a timezone the way the rest of the world
does — "Europe/Vienna", "Asia/Bangkok" — and ESP-IDF's newlib cannot use that.
It has no zoneinfo database at all; setenv("TZ", ...) understands only a POSIX
TZ string, which is why main/net/timesync.h carries
"CET-1CEST,M3.5.0,M10.5.0/3" spelled out. So something has to map one to the
other, and that something is a table.

The table is GENERATED rather than typed, because every one of those strings is
a fact with a source. A TZif v2+ file ends with the POSIX rule that describes
its zone after the last explicit transition — that is precisely the string
newlib wants, written by the people who maintain the rules for a living. Typing
them by hand means typing "M3.5.0/3" correctly sixty times and then being wrong
about Israel, which changes its DST rule by government decision.

Run it when tzdata moves and the diff is the news:

    python3 tools/build_tz_table.py

WHY THESE ZONES. The device travels between Austria and Thailand (AGENTS.md
§6), and "Eigener Ort" exists for everywhere that is neither. So: all of Europe,
because that is where a car can reach; the parts of Asia within a plausible
flight of Pattaya; the Mediterranean holiday coast; and a thin North American
and Australian set so that a wrong guess there is still only a wrong guess
about a place nobody will go. Everything outside the table falls back to a
whole-hour offset computed from the longitude (geo_parse.c), which is never
more than an hour out and never silently claims a DST rule it does not have.
"""
import datetime
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "main" / "net" / "tz_table.h"
ZONEINFO = pathlib.Path("/usr/share/zoneinfo")

# Curated, not globbed. A glob would pull in 600 zones, most of them aliases or
# places this device will never stand, and the flash cost is the least of it —
# a table nobody has read is a table nobody can check.
ZONES = [
    # --- Europe, all of it. The continent is one drive. -------------------
    "Europe/Vienna", "Europe/Berlin", "Europe/Zurich", "Europe/Paris",
    "Europe/Rome", "Europe/Madrid", "Europe/Lisbon", "Europe/Amsterdam",
    "Europe/Brussels", "Europe/Luxembourg", "Europe/Copenhagen",
    "Europe/Stockholm", "Europe/Oslo", "Europe/Helsinki", "Europe/Prague",
    "Europe/Bratislava", "Europe/Budapest", "Europe/Warsaw", "Europe/Ljubljana",
    "Europe/Zagreb", "Europe/Belgrade", "Europe/Sarajevo", "Europe/Skopje",
    "Europe/Podgorica", "Europe/Tirane", "Europe/Bucharest", "Europe/Sofia",
    "Europe/Athens", "Europe/Chisinau", "Europe/Kyiv", "Europe/Kiev",
    "Europe/Minsk", "Europe/Moscow", "Europe/Kaliningrad", "Europe/Riga",
    "Europe/Tallinn", "Europe/Vilnius", "Europe/London", "Europe/Dublin",
    "Atlantic/Reykjavik", "Europe/Istanbul", "Europe/Malta", "Europe/Monaco",
    "Europe/Andorra", "Europe/Gibraltar", "Europe/San_Marino",
    "Europe/Vatican", "Europe/Guernsey", "Europe/Jersey", "Europe/Isle_of_Man",
    "Atlantic/Canary", "Atlantic/Madeira", "Atlantic/Faroe",

    # --- The Mediterranean rim: a holiday away, a different rule ----------
    "Africa/Casablanca", "Africa/Tunis", "Africa/Algiers", "Africa/Cairo",
    "Asia/Nicosia", "Asia/Jerusalem", "Asia/Beirut", "Asia/Amman",
    "Asia/Damascus",

    # --- Asia, within reach of the other home -----------------------------
    "Asia/Bangkok", "Asia/Ho_Chi_Minh", "Asia/Phnom_Penh", "Asia/Vientiane",
    "Asia/Yangon", "Asia/Jakarta", "Asia/Makassar", "Asia/Singapore",
    "Asia/Kuala_Lumpur", "Asia/Manila", "Asia/Hong_Kong", "Asia/Macau",
    "Asia/Shanghai", "Asia/Taipei", "Asia/Seoul", "Asia/Tokyo",
    "Asia/Kolkata", "Asia/Colombo", "Asia/Kathmandu", "Asia/Dhaka",
    "Asia/Karachi", "Asia/Dubai", "Asia/Qatar", "Asia/Riyadh",
    "Asia/Kuwait", "Asia/Baku", "Asia/Tbilisi", "Asia/Yerevan",
    "Asia/Tehran", "Asia/Tashkent", "Asia/Almaty",

    # --- Thin cover elsewhere --------------------------------------------
    "America/New_York", "America/Toronto", "America/Chicago",
    "America/Denver", "America/Phoenix", "America/Los_Angeles",
    "America/Vancouver", "America/Mexico_City", "America/Sao_Paulo",
    "America/Argentina/Buenos_Aires", "America/Bogota", "America/Lima",
    "Australia/Sydney", "Australia/Melbourne", "Australia/Brisbane",
    "Australia/Perth", "Australia/Adelaide", "Pacific/Auckland",
    "Africa/Johannesburg", "Africa/Nairobi", "Africa/Lagos",
]


def posix_rule(zone):
    """The POSIX TZ footer of a TZif v2+ file, or None.

    Layout (RFC 8536 §3.3): the version-2+ data block is followed by a footer
    that is a newline, the TZ string, and a newline. Reading it back off the
    end of the file is exact — no parsing of the transition table required.
    """
    path = ZONEINFO / zone
    if not path.is_file():
        return None
    data = path.read_bytes()
    if data[:4] != b"TZif" or data[4:5] == b"\x00":
        return None                      # not TZif, or version 1: no footer
    end = data.rfind(b"\n")
    start = data.rfind(b"\n", 0, end)
    if start < 0 or end <= start + 1:
        return None                      # a zone with no rule (e.g. UTC-only)
    return data[start + 1:end].decode("ascii")


def tzdata_version():
    f = ZONEINFO / "+VERSION"
    return f.read_text().strip() if f.is_file() else "unknown"


def main():
    if not ZONEINFO.is_dir():
        print(f"no zoneinfo at {ZONEINFO}", file=sys.stderr)
        return 2

    rows, missing = [], []
    for zone in ZONES:
        rule = posix_rule(zone)
        if rule is None:
            missing.append(zone)
            continue
        rows.append((zone, rule))

    # A miss is a TYPO IN THE LIST ABOVE, not a fact about the world: every
    # zone here was chosen deliberately, so one that does not resolve is a
    # name spelled wrong (it was "Europe/Reykjavik", which is Atlantic/) and
    # the device would quietly fall back to a longitude guess for it forever.
    # AGENTS.md §11 rule 2 — a generator that skips silently is a gate that
    # has stopped checking. So it stops instead.
    if missing:
        print("no POSIX rule for: " + ", ".join(missing), file=sys.stderr)
        print("check the spelling against /usr/share/zoneinfo", file=sys.stderr)
        return 1

    # Distinct rules get their own array: Europe alone collapses to a handful
    # of strings, and storing one pointer per zone instead of one string means
    # the interesting content is visible at a glance rather than repeated
    # forty times.
    rules = sorted({rule for _, rule in rows})
    index = {rule: i for i, rule in enumerate(rules)}

    version = tzdata_version()
    name_w = max(len(z) for z, _ in rows) + 3

    out = [
        "/* GENERATED by tools/build_tz_table.py — do not edit by hand.",
        " *",
        f" * Source: the system tzdata, version {version}.",
        f" * Generated: {datetime.date.today().isoformat()}.",
        " *",
        " * IANA zone name (what Open-Meteo's geocoder returns) -> POSIX TZ string",
        " * (the only thing ESP-IDF's newlib understands — see main/net/timesync.h).",
        " * Each string is the TZif footer the tzdata maintainers wrote for that",
        " * zone, not a transcription of it.",
        " *",
        f" * {len(rows)} zones, {len(rules)} distinct rules. Anything not listed falls back to a",
        " * whole-hour offset from the longitude in geo_tz_posix() (geo_parse.c).",
        " */",
        "#pragma once",
        "",
        "/* The distinct rules. Indexed by tz_zones[].rule. */",
        "static const char *const tz_rules[] = {",
    ]
    for rule in rules:
        out.append(f'    "{rule}",')
    out += [
        "};",
        "",
        "typedef struct {",
        "    const char   *iana;",
        "    unsigned char rule;   /* index into tz_rules[] */",
        "} tz_zone_t;",
        "",
        "/* Sorted by IANA name so geo_tz_posix() can binary-search it. */",
        "static const tz_zone_t tz_zones[] = {",
    ]
    for zone, rule in sorted(rows):
        out.append(f'    {{ "{zone}",'.ljust(name_w + 6) + f"{index[rule]:2d} }},")
    out += ["};", ""]

    OUT.write_text("\n".join(out))
    print(f"{OUT.relative_to(ROOT)}: {len(rows)} zones, {len(rules)} rules, "
          f"tzdata {version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
