#!/usr/bin/env python3
"""Build the encrypted callsign pairs the panel names aircraft with (D94, D95).

The transponder says AUA25L. The ticket says OS 254. ADS-B carries only the
first, and every community route database stores the callsign suffix as the
"number". AeroDataBox's airport board is the one free source that lists both
side by side: its ADS-B receivers fill in the callsign of every flight they
saw. Airlines give a flight the same callsign every day of a season, so the
pairs seen yesterday name today's flights. main/data/timetable.c looks them
up on the device.

What it does, twice a day in .github/workflows/timetable.yml:

  1. Opens the previous file (--previous), so pairs accumulate over days.
  2. Asks AeroDataBox for Vienna's departures and arrivals of the last twelve
     hours: one call, 2 of the free plan's 400 monthly units.
  3. Merges. A pair seen again is refreshed; a pair older than six days, or
     seen before the current IATA season began, is dropped. AeroDataBox's
     terms allow keeping their data seven days at most (§5.5).
  4. Encrypts the result and writes it (--out).

THE FILE IS ENCRYPTED because it is AeroDataBox's data and it sits on a
public branch. Their terms forbid passing it to third parties (§5.8); the
panels that hold the key are this application's own users, and nobody else
can read it. The format is in main/net/timetable_crypt.h. AES-256-CTR comes
from the openssl command line, which every runner has, and the HMAC from
Python's own hmac module, so the job that holds both secrets installs nothing.

The plaintext, one pair per line, for main/data/timetable.c:

    # esp-flight-monitor timetable 2
    # generated 1792411200
    AUA25L,OS,254,EDDH,LOWW,1792411200

callsign, carrier (IATA), number, from and to (ICAO, as adsb.im's routeset
gives them to the panel), and when the pair was last seen, in UTC seconds.

    AERODATABOX_KEY=... TIMETABLE_KEY=... tools/build_timetable.py --out vie.tt
    tools/build_timetable.py --selftest        # offline, no network, no secret

The selftest runs with the host suite (test/host/Makefile). It feeds a canned
AeroDataBox answer and a canned previous file through the whole pipeline and
compares the result with test/fixtures/timetable_sample.csv (the plaintext)
and timetable_sample.tt (encrypted with a published test key and a fixed IV).
test_timetable.c opens that .tt with the device's own code. So the writer and
the reader are checked against the same bytes.
"""

import argparse
import hashlib
import hmac
import json
import os
import re
import secrets
import subprocess
import sys
import time
import urllib.request
from datetime import datetime, timedelta, timezone

HEADER = "# esp-flight-monitor timetable 2"
MAGIC = b"ESPFMTT2"
ENC_LABEL = b"esp-flight-monitor timetable 2 enc"
MAC_LABEL = b"esp-flight-monitor timetable 2 mac"

API_HOST = "aerodatabox.p.rapidapi.com"
USER_AGENT = "esp-flight-monitor-timetable (+https://github.com/monsjovis/esp-flight-monitor)"

KEEP_S = 6 * 24 * 3600      # under AeroDataBox's seven-day limit, with a day spare
WINDOW_H = 12               # the longest window the free plan allows per call

CALLSIGN = re.compile(r"^[A-Z]{3}[0-9]{1,4}[A-Z]{0,2}$")
NUMBER = re.compile(r"^([A-Z0-9]{2}) ?0*([0-9]{1,4})$")
ICAO = re.compile(r"^[A-Z]{4}$")
SKIP_STATUS = {"Canceled", "CanceledUncertain", "Diverted"}

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "..", "test", "fixtures")

# Published on purpose, and used for nothing but the fixture: bytes 0x00..0x1f.
TEST_KEY = bytes(range(32))
# Ends in ff ff fe, so the counter carries across three bytes within the
# fixture and a decryptor that only increments the last byte fails the test.
TEST_IV = bytes.fromhex("a0a1a2a3a4a5a6a7a8a9aaabacfffffe")


# ---- crypto ---------------------------------------------------------------

def _keys(master):
    return (hmac.new(master, ENC_LABEL, hashlib.sha256).digest(),
            hmac.new(master, MAC_LABEL, hashlib.sha256).digest())


def _aes_ctr(key, iv, data):
    """OpenSSL's aes-256-ctr: the whole IV is the counter, big-endian."""
    r = subprocess.run(["openssl", "enc", "-aes-256-ctr", "-K", key.hex(), "-iv", iv.hex()],
                       input=data, capture_output=True, check=True)
    return r.stdout


def encrypt(master, plaintext, iv=None):
    enc, mac = _keys(master)
    iv = iv or secrets.token_bytes(16)
    body = MAGIC + iv + _aes_ctr(enc, iv, plaintext)
    return body + hmac.new(mac, body, hashlib.sha256).digest()


def decrypt(master, blob):
    """The plaintext, or None when the file is not ours or not for this key."""
    if len(blob) < len(MAGIC) + 16 + 32 or not blob.startswith(MAGIC):
        return None
    enc, mac = _keys(master)
    body, tag = blob[:-32], blob[-32:]
    if not hmac.compare_digest(hmac.new(mac, body, hashlib.sha256).digest(), tag):
        return None
    return _aes_ctr(enc, body[8:24], body[24:])


# ---- the table --------------------------------------------------------------

def parse(text):
    """Plaintext -> {(callsign, from, to): (carrier, number, seen)}."""
    pairs = {}
    lines = text.splitlines()
    if not lines or lines[0] != HEADER:
        return pairs
    for line in lines[1:]:
        if not line or line.startswith("#"):
            continue
        f = line.split(",")
        if len(f) == 6 and CALLSIGN.match(f[0]) and f[2].isdigit() and f[5].isdigit():
            pairs[(f[0], f[3], f[4])] = (f[1], int(f[2]), int(f[5]))
    return pairs


def render(pairs, generated, airport):
    rows = sorted((cs, c, n, a, b, seen) for (cs, a, b), (c, n, seen) in pairs.items())
    out = [HEADER,
           "# generated %d" % generated,
           "# %s: %d callsign pairs, from AeroDataBox" % (airport, len(rows))]
    out += ["%s,%s,%d,%s,%s,%d" % r for r in rows]
    return "\n".join(out) + "\n"


def season_start(t):
    """The start of the IATA season `t` falls in: the last Sunday of March or
    of October. Callsigns are reassigned then (AUA25L flew FRA-GRZ until
    31 Oct 2025, HAM-VIE after), so nothing seen before it is trusted."""
    def last_sunday(year, month):
        nxt = datetime(year + (month == 12), month % 12 + 1, 1, tzinfo=timezone.utc)
        d = nxt - timedelta(days=1)
        return d - timedelta(days=(d.weekday() + 1) % 7)
    now = datetime.fromtimestamp(t, timezone.utc)
    starts = [last_sunday(y, m) for y in (now.year - 1, now.year) for m in (3, 10)]
    return int(max(s for s in starts if s <= now).timestamp())


def pairs_from_board(doc, airport):
    """AeroDataBox's FIDS answer -> {(callsign, from, to): (carrier, number)},
    with every key that came back with two different numbers left out."""
    found, clash = {}, set()
    for side, outbound in (("departures", True), ("arrivals", False)):
        for f in (doc or {}).get(side) or []:
            if f.get("codeshareStatus") == "IsCodeshared" or f.get("isCargo"):
                continue
            if f.get("status") in SKIP_STATUS:
                continue
            cs = (f.get("callSign") or "").strip().upper()
            m = NUMBER.match((f.get("number") or "").strip().upper())
            other = (((f.get("movement") or {}).get("airport") or {}).get("icao") or "").upper()
            if not (CALLSIGN.match(cs) and m and ICAO.match(other)):
                continue
            n = int(m.group(2))
            if not 1 <= n <= 9999:
                continue
            key = (cs, airport, other) if outbound else (cs, other, airport)
            val = (m.group(1), n)
            if key in found and found[key] != val:
                clash.add(key)
            found[key] = val
    return {k: v for k, v in found.items() if k not in clash}


def merge(old, new, now):
    """Old pairs, refreshed or replaced by what was just seen; anything past
    KEEP_S or from before the season began is gone."""
    floor = max(now - KEEP_S, season_start(now))
    out = {k: v for k, v in old.items() if v[2] >= floor}
    for k, (carrier, number) in new.items():
        out[k] = (carrier, number, now)
    return out


# ---- AeroDataBox ------------------------------------------------------------

def fetch_board(api_key, airport, start, end):
    url = ("https://%s/flights/airports/icao/%s/%s/%s?withLeg=false&direction=Both"
           "&withCancelled=false&withCodeshared=false&withCargo=false"
           "&withPrivate=false&withLocation=false") % (
        API_HOST, airport, start.strftime("%Y-%m-%dT%H:%M"), end.strftime("%Y-%m-%dT%H:%M"))
    req = urllib.request.Request(url, headers={
        "X-RapidAPI-Key": api_key,
        "X-RapidAPI-Host": API_HOST,
        "User-Agent": USER_AGENT,
        "Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        if r.status == 204:                    # no flights in the window
            return {}
        return json.loads(r.read().decode("utf-8"))


def build(args):
    api_key = os.environ.get("AERODATABOX_KEY")
    key_hex = os.environ.get("TIMETABLE_KEY", "")
    if not api_key or len(key_hex) != 64:
        sys.exit("AERODATABOX_KEY and TIMETABLE_KEY (64 hex digits) must be set")
    master = bytes.fromhex(key_hex)

    old = {}
    if args.previous and os.path.exists(args.previous):
        with open(args.previous, "rb") as fh:
            plain = decrypt(master, fh.read())
        if plain is None:
            print("previous file does not open with this key; starting over")
        else:
            old = parse(plain.decode("utf-8"))

    from zoneinfo import ZoneInfo   # only here: the selftest needs no time zones
    now = int(time.time())
    local = datetime.now(ZoneInfo(args.tz)).replace(second=0, microsecond=0)
    doc = fetch_board(api_key, args.airport, local - timedelta(hours=WINDOW_H), local)
    new = pairs_from_board(doc, args.airport)
    pairs = merge(old, new, now)
    if not pairs:
        sys.exit("no pairs at all; not writing a table")

    text = render(pairs, now, args.airport)
    with open(args.out, "wb") as fh:
        fh.write(encrypt(master, text.encode()))
    flights = len((doc or {}).get("departures") or []) + len((doc or {}).get("arrivals") or [])
    print("%s: %d pairs (%d seen now) from %d flights on the board" % (
        args.out, len(pairs), len(new), flights))


# ---- selftest -----------------------------------------------------------------

def _f(number, callsign, other, status="Arrived", codeshare="IsOperator"):
    return {"number": number, "callSign": callsign, "status": status,
            "codeshareStatus": codeshare, "isCargo": False,
            "movement": {"airport": {"icao": other, "iata": "", "name": other}}}


def selftest():
    now = 1792411200                       # Mon 2026-10-19 12:00 UTC
    board = {
        "departures": [
            _f("OS 257", "AUA25M", "EDDH", status="Departed"),
            _f("OS 127", "AUA12L", "LFSB", status="Departed"),
            _f("W6 2274", "WZZ839", "LHBP", status="Departed"),
            _f("LX 1579", "AUA12L", "LFSB", codeshare="IsCodeshared"),   # out
            _f("OS 9", "", "EDDF"),                                      # no callsign
            _f("OS 901", "AUA9AB", "LGAV", status="Canceled"),           # out
        ],
        "arrivals": [
            _f("OS 254", "AUA25L", "EDDH"),
            _f("OS 266", "AUA26V", "EDDS"),
            _f("LH 1341", "DLH7KP", "EDDF"),
            _f("LH 1343", "DLH7KP", "EDDF"),     # the same key, two numbers: out
        ],
    }
    # Summer 2026 began Sun 29 Mar; winter begins Sun 25 Oct, after `now`.
    assert season_start(now) == 1774742400, season_start(now)
    old_text = "\n".join([
        HEADER,
        "AUA25L,OS,254,EDDH,LOWW,%d" % (now - 86400),            # refreshed
        "FIN5H,AY,1476,EFHK,LOWW,%d" % (now - 2 * 86400),         # kept
        "EZY46WE,U2,8225,LGAV,LOWW,%d" % (now - 7 * 86400),       # too old: gone
    ]) + "\n"

    new = pairs_from_board(board, "LOWW")
    assert ("DLH7KP", "EDDF", "LOWW") not in new, new
    pairs = merge(parse(old_text), new, now)
    # A pair from before a season change goes, however recent: one day into
    # winter, a summer pair seen two days ago is not trusted.
    winter = 1792886400 + 86400            # Sun 25 Oct 2026, plus a day
    assert season_start(winter) == 1792886400
    assert merge({("X1A", "A", "B"): ("XX", 1, winter - 2 * 86400)}, {}, winter) == {}
    text = render(pairs, now, "LOWW")

    with open(os.path.join(FIXTURES, "timetable_sample.csv"), newline="") as fh:
        want = fh.read()
    if text != want:
        sys.stderr.write("selftest: plaintext differs from timetable_sample.csv\n"
                         "--- got\n%s--- want\n%s" % (text, want))
        return 1

    blob = encrypt(TEST_KEY, text.encode(), TEST_IV)
    with open(os.path.join(FIXTURES, "timetable_sample.tt"), "rb") as fh:
        want_blob = fh.read()
    if blob != want_blob:
        sys.stderr.write("selftest: ciphertext differs from timetable_sample.tt\n")
        return 1
    assert decrypt(TEST_KEY, blob) == text.encode()
    assert decrypt(bytes(32), blob) is None                                # wrong key
    assert decrypt(TEST_KEY, blob[:-1] + bytes([blob[-1] ^ 1])) is None    # damaged
    print("timetable writer: %d pairs, matches test/fixtures/timetable_sample.csv and .tt"
          % len(pairs))
    return 0


def main():
    ap = argparse.ArgumentParser(description="Build the panel's encrypted callsign pairs (D95).")
    ap.add_argument("--airport", default="LOWW", help="ICAO code of the airport")
    ap.add_argument("--tz", default="Europe/Vienna", help="the airport's time zone")
    ap.add_argument("--previous", help="the last published file, to merge with")
    ap.add_argument("--out", default="vie.tt")
    ap.add_argument("--selftest", action="store_true",
                    help="run the offline check against test/fixtures and exit")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    build(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
