#!/usr/bin/env python3
"""Build the timetable the panel matches alphanumeric callsigns against (D94).

The transponder says AUA25L. The ticket says OS 254. Nothing the panel polls
knows the second, because ADS-B only carries the first and every community
route database stores the callsign suffix as the "number". The one free way
to the real number is a timetable: airline, route and time. This script makes
one for Vienna, and main/data/timetable.c matches against it on the device.

Where the data comes from: the Lufthansa Open API, Public Plan, "Flight Status
at Arrival/Departure Airport". It covers Austrian, Lufthansa, Swiss, Brussels
and the rest of the group, which is more than half of the traffic at VIE. The
key lives in this repo's Actions secrets (LH_CLIENT_ID, LH_CLIENT_SECRET) and
nowhere else: not on a device, not in the public tree.

What it writes, one flight per line, for main/data/timetable.c:

    # esp-flight-monitor timetable 1
    # generated 1792411200
    OS,254,EDDH,LOWW,1792415100,1792420200

carrier (operating, IATA), number, from and to (ICAO, because that is what
adsb.im's routeset gives the panel), departure and arrival in UTC seconds.
The times are the best known ones: actual, else estimated, else scheduled.
Airports come back from Lufthansa as IATA codes and are turned into ICAO with
OurAirports (public domain). An airport it cannot place drops the flight;
an unplaceable flight is no worse than a missing one.

How it asks: windows of four hours (the API's own size, at most 80 flights
each), started every hour from --hours-back to --hours-ahead, arrivals and
departures. Starting each window an hour after the last means every flight
falls in the first hour of some window, so the 80-flight cap cannot drop it.
That is 2 x 32 = 64 calls a run at the defaults, a few per second.

    tools/build_timetable.py --out vie.csv          # live, needs the two secrets
    tools/build_timetable.py --selftest             # offline, no network

The selftest runs with the host suite (test/host/Makefile). It feeds a canned
answer through the whole pipeline, including the API's habit of sending a
single flight as an object instead of a one-element list, and compares the
result with test/fixtures/timetable_sample.csv, which test_timetable.c parses
with the device's own parser. So the writer and the reader are checked against
the same bytes.
"""

import argparse
import csv
import io
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timedelta, timezone

HEADER = "# esp-flight-monitor timetable 1"
API = "https://api.lufthansa.com/v1"
AIRPORTS_CSV = "https://davidmegginson.github.io/ourairports-data/airports.csv"
USER_AGENT = "esp-flight-monitor-timetable (+https://github.com/monsjovis/esp-flight-monitor)"

WINDOW_LIMIT = 100        # the API's maximum page size
CALL_GAP_S = 0.25         # stays under the Public Plan's 5 calls a second
SKIP_STATUS = {"CD", "RT"}  # cancelled, rerouted: not the flight on the route

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLE_CSV = os.path.join(HERE, "..", "test", "fixtures", "timetable_sample.csv")


def as_list(x):
    """The API sends one element as an object and several as a list."""
    if x is None:
        return []
    return x if isinstance(x, list) else [x]


def utc_seconds(s):
    """'2026-10-19T14:30Z' -> Unix seconds."""
    return int(datetime.strptime(s, "%Y-%m-%dT%H:%MZ")
               .replace(tzinfo=timezone.utc).timestamp())


def best_time(side):
    """Actual, else estimated, else scheduled. Returns (best, scheduled)."""
    sched = None
    best = None
    for key in ("ActualTimeUTC", "EstimatedTimeUTC", "ScheduledTimeUTC"):
        dt = (side.get(key) or {}).get("DateTime")
        if dt:
            t = utc_seconds(dt)
            if best is None:
                best = t
            if key == "ScheduledTimeUTC":
                sched = t
    return best, sched


# ---- airports -------------------------------------------------------------

_TYPE_RANK = {"large_airport": 3, "medium_airport": 2, "small_airport": 1}


def iata_to_icao(text):
    """OurAirports' airports.csv -> {IATA: ICAO}. Where two places share an
    IATA code, the bigger airport with scheduled service wins."""
    best = {}
    for row in csv.DictReader(io.StringIO(text)):
        iata = (row.get("iata_code") or "").strip().upper()
        if len(iata) != 3:
            continue
        icao = ""
        for key in ("icao_code", "gps_code", "ident"):
            v = (row.get(key) or "").strip().upper()
            if len(v) == 4 and v.isalpha():
                icao = v
                break
        if not icao:
            continue
        rank = (_TYPE_RANK.get(row.get("type"), 0),
                row.get("scheduled_service") == "yes")
        if iata not in best or rank > best[iata][0]:
            best[iata] = (rank, icao)
    return {k: v[1] for k, v in best.items()}


# ---- the API --------------------------------------------------------------

def http(url, data=None, headers=None, timeout=30):
    req = urllib.request.Request(url, data=data, headers=dict(headers or {}))
    req.add_header("User-Agent", USER_AGENT)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read().decode("utf-8")


def token(client_id, client_secret):
    body = urllib.parse.urlencode({
        "client_id": client_id,
        "client_secret": client_secret,
        "grant_type": "client_credentials",
    }).encode()
    answer = json.loads(http(API + "/oauth/token", data=body, headers={
        "Content-Type": "application/x-www-form-urlencoded"}))
    return answer["access_token"]


def fetch_window(tok, direction, airport, start_local):
    """Every flight of one four-hour window, all pages. A 404 is the API's
    way of saying the window is empty."""
    flights = []
    offset = 0
    for _ in range(5):
        url = "%s/operations/flightstatus/%s/%s/%s?serviceType=passenger&limit=%d&offset=%d" % (
            API, direction, airport, start_local.strftime("%Y-%m-%dT%H:%M"),
            WINDOW_LIMIT, offset)
        doc = None
        for attempt in range(3):
            time.sleep(CALL_GAP_S)
            try:
                doc = json.loads(http(url, headers={
                    # "Bearer" with a capital B, or the API refuses it (their
                    # Known Issues page).
                    "Authorization": "Bearer " + tok,
                    "Accept": "application/json"}))
                break
            except urllib.error.HTTPError as e:
                if e.code == 404:
                    return flights
                if e.code == 429 and attempt < 2:
                    time.sleep(2 + 2 * attempt)
                    continue
                raise
        if doc is None:
            break
        res = doc.get("FlightStatusResource") or {}
        page = as_list((res.get("Flights") or {}).get("Flight"))
        flights.extend(page)
        total = (res.get("Meta") or {}).get("TotalCount")
        offset += len(page)
        if len(page) < WINDOW_LIMIT or (isinstance(total, int) and offset >= total):
            break
    return flights


# ---- the table ------------------------------------------------------------

def rows_from(flights, icao):
    """API flights -> sorted, de-duplicated CSV rows. Returns (rows, dropped)."""
    seen = {}
    dropped = 0
    for f in flights:
        status = ((f.get("FlightStatus") or {}).get("Code") or "").upper()
        if status in SKIP_STATUS:
            continue
        op = f.get("OperatingCarrier") or {}
        carrier = str(op.get("AirlineID") or "").upper()
        number = str(op.get("FlightNumber") or "")
        if len(carrier) != 2 or not number.isdigit() or not 1 <= int(number) <= 9999:
            dropped += 1
            continue
        dep_side = f.get("Departure") or {}
        arr_side = f.get("Arrival") or {}
        dep, dep_sched = best_time(dep_side)
        arr, _ = best_time(arr_side)
        frm = icao.get(str(dep_side.get("AirportCode") or "").upper())
        to = icao.get(str(arr_side.get("AirportCode") or "").upper())
        if not (frm and to and dep and arr) or arr <= dep or arr - dep > 20 * 3600:
            dropped += 1
            continue
        key = (carrier, int(number), frm, to, dep_sched or dep)
        # The same flight comes back from several overlapping windows, and
        # from both the arrivals and the departures side when both ends are
        # in scope. The later answer is the fresher one.
        seen[key] = (carrier, int(number), frm, to, dep, arr)
    rows = sorted(seen.values(), key=lambda r: (r[4], r[0], r[1]))
    return rows, dropped


def render(rows, generated, airport_iata, airport_icao):
    out = [HEADER,
           "# generated %d" % generated,
           "# %s (%s): %d flights, Lufthansa Open API flight status" % (
               airport_iata, airport_icao, len(rows))]
    out += ["%s,%d,%s,%s,%d,%d" % r for r in rows]
    return "\n".join(out) + "\n"


def build(args):
    cid = os.environ.get("LH_CLIENT_ID")
    secret = os.environ.get("LH_CLIENT_SECRET")
    if not cid or not secret:
        sys.exit("LH_CLIENT_ID and LH_CLIENT_SECRET must be set (Actions secrets)")

    icao = iata_to_icao(http(AIRPORTS_CSV, timeout=120))
    airport_icao = icao.get(args.airport)
    if not airport_icao:
        sys.exit("OurAirports does not know %s" % args.airport)

    # Here, not at the top: the selftest runs in the IDF container's Python,
    # and it needs no time zones.
    from zoneinfo import ZoneInfo

    tok = token(cid, secret)
    tz = ZoneInfo(args.tz)
    now = datetime.now(tz).replace(minute=0, second=0, microsecond=0)
    flights = []
    for direction in ("arrivals", "departures"):
        for h in range(-args.hours_back, args.hours_ahead):
            flights.extend(fetch_window(tok, direction, args.airport,
                                        now + timedelta(hours=h)))

    rows, dropped = rows_from(flights, icao)
    if not rows:
        # Never publish an empty table over a good one: the panel would keep
        # working off the old file's flights until they run out, which is
        # better than nothing at all.
        sys.exit("no flights came back; not writing a table")
    text = render(rows, int(time.time()), args.airport, airport_icao)
    with open(args.out, "w", newline="\n") as fh:
        fh.write(text)
    print("%s: %d flights from %d answers (%d dropped), %d bytes" % (
        args.out, len(rows), len(flights), dropped, len(text.encode())))


# ---- selftest -------------------------------------------------------------

def _flight(op, num, frm, to, dep, arr, status="NA", est_arr=None, mkt=None):
    f = {
        "Departure": {"AirportCode": frm, "ScheduledTimeUTC": {"DateTime": dep}},
        "Arrival": {"AirportCode": to, "ScheduledTimeUTC": {"DateTime": arr}},
        "MarketingCarrier": {"AirlineID": mkt or op, "FlightNumber": str(num)},
        "OperatingCarrier": {"AirlineID": op, "FlightNumber": str(num)},
        "Equipment": {"AircraftCode": "321"},
        "FlightStatus": {"Code": status},
    }
    if est_arr:
        f["Arrival"]["EstimatedTimeUTC"] = {"DateTime": est_arr}
    return f


def selftest():
    icao = iata_to_icao(
        '"ident","type","scheduled_service","icao_code","iata_code","gps_code"\n'
        '"LOWW","large_airport","yes","LOWW","VIE","LOWW"\n'
        '"EDDH","large_airport","yes","EDDH","HAM","EDDH"\n'
        '"EDDS","large_airport","yes","EDDS","STR","EDDS"\n'
        '"LFSB","large_airport","yes","LFSB","BSL","LFSB"\n'
        '"XXBS","small_airport","no","","BSL","XXBS"\n')
    assert icao["BSL"] == "LFSB", icao       # the big airport wins a shared code

    # One window as the API sends it: several flights, a list.
    many = {"FlightStatusResource": {"Flights": {"Flight": [
        _flight("OS", 254, "HAM", "VIE", "2026-10-19T13:05Z", "2026-10-19T14:30Z",
                est_arr="2026-10-19T14:40Z"),
        _flight("OS", 266, "STR", "VIE", "2026-10-19T13:30Z", "2026-10-19T14:40Z"),
        _flight("OS", 252, "HAM", "VIE", "2026-10-19T05:45Z", "2026-10-19T07:15Z",
                status="CD"),                                 # cancelled: out
        _flight("OS", 9999, "XXX", "VIE", "2026-10-19T10:00Z", "2026-10-19T11:00Z"),
        _flight("OS", 254, "HAM", "VIE", "2026-10-19T13:05Z", "2026-10-19T14:30Z",
                est_arr="2026-10-19T14:43Z"),                 # fresher repeat
    ]}, "Meta": {"TotalCount": 5}}}
    # And one as it sends a single flight: an object, not a list.
    one = {"FlightStatusResource": {"Flights": {"Flight":
        _flight("OS", 127, "VIE", "BSL", "2026-10-19T15:05Z", "2026-10-19T16:30Z",
                mkt="LX")}}}

    flights = []
    for doc in (many, one):
        res = doc["FlightStatusResource"]
        flights.extend(as_list(res["Flights"]["Flight"]))
    rows, dropped = rows_from(flights, icao)
    text = render(rows, 1792411200, "VIE", "LOWW")

    with open(SAMPLE_CSV, newline="") as fh:
        want = fh.read()
    if text != want:
        sys.stderr.write("selftest: output differs from %s\n--- got\n%s--- want\n%s"
                         % (SAMPLE_CSV, text, want))
        return 1
    assert dropped == 1, dropped
    print("timetable writer: %d rows, matches test/fixtures/timetable_sample.csv" % len(rows))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--airport", default="VIE", help="IATA code to ask about")
    ap.add_argument("--tz", default="Europe/Vienna", help="the airport's time zone")
    ap.add_argument("--hours-back", type=int, default=4)
    ap.add_argument("--hours-ahead", type=int, default=28)
    ap.add_argument("--out", default="vie.csv")
    ap.add_argument("--selftest", action="store_true",
                    help="run the offline check against test/fixtures and exit")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    build(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
