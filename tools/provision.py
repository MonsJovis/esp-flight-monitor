#!/usr/bin/env python3
"""Put WiFi credentials onto the device, without them touching the repo.

Credentials live in the device's NVS and nowhere else (AGENTS.md §10). This
script prompts for them locally, sends them straight down the serial link, and
keeps nothing: the password is never echoed, never written to a file, and never
appears in any log or transcript.

    python3 tools/provision.py [--port /dev/cu.usbmodem1101] [--slot 0]

Slot 0 is the Austrian network, slot 1 the Thai one — the device stores several
and connects to whichever is in range, because it travels (AGENTS.md §6).

    python3 tools/provision.py --timetable-key

stores the key that opens the callsign pairs CI publishes (D95): the same 64
hex digits as the repo's TIMETABLE_KEY secret, kept in the personal 1Password
next to the signing key. The same rules as the password: prompted for, never
echoed, never written anywhere, filtered out of everything printed back.
"""
import argparse, getpass, json, subprocess, sys, time
import serial

def _default_port():
    """The board re-enumerates under a different node after a replug
    (usbmodem1101 -> usbmodem101), so discover it rather than hardcode it."""
    import glob
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    return ports[0] if ports else "/dev/cu.usbmodem1101"



def _osascript(prompt, hidden):
    """Native macOS dialog. Used when stdin is not a TTY — which is the case
    when this is launched from a tool runner rather than a shell. The answer is
    captured here and never echoed, so it cannot reach a terminal or a log."""
    hide = " with hidden answer" if hidden else ""
    script = (f'display dialog {json.dumps(prompt)} default answer ""'
              f'{hide} with title "Flugradar — WLAN einrichten"')
    r = subprocess.run(["osascript", "-e", script],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("cancelled")
    marker = "text returned:"
    out = r.stdout.strip()
    return out[out.index(marker) + len(marker):] if marker in out else ""


def prompt_credentials():
    if sys.stdin.isatty():
        return input("SSID: ").strip(), getpass.getpass("Password (not echoed): ")
    print("no TTY here — opening a dialog on your desktop...", flush=True)
    return _osascript("WLAN-Name (SSID):", False).strip(), \
           _osascript("WLAN-Passwort:", True)


def provision_timetable_key(port):
    if sys.stdin.isatty():
        key = getpass.getpass("Timetable key, 64 hex digits (not echoed): ").strip()
    else:
        print("no TTY here — opening a dialog on your desktop...", flush=True)
        key = _osascript("Fahrplan-Schlüssel (64 Hex-Zeichen):", True).strip()
    if len(key) != 64 or any(c not in "0123456789abcdefABCDEF" for c in key):
        sys.exit("not 64 hex digits; nothing sent")

    with serial.Serial(port, 115200, timeout=0.5) as s:
        s.reset_input_buffer()
        s.write(b"T")                      # firmware prompts for one line
        s.flush()
        time.sleep(0.5)
        s.write(key.encode() + b"\n")
        s.flush()
        deadline = time.time() + 10
        while time.time() < deadline:
            line = s.readline().decode("utf-8", "replace").rstrip()
            # The console echoes what it reads; never print a line with the key.
            if not line or key.lower() in line.lower():
                continue
            print("  " + line)
            if "timetable key" in line and ("stored" in line or "rejected" in line
                                            or "could not" in line):
                return
    print("no answer seen — press 'n' on the device to check the timetable line")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=_default_port())
    ap.add_argument("--slot", type=int, default=0, choices=range(4))
    ap.add_argument("--timetable-key", action="store_true",
                    help="store the timetable key instead of a WiFi network (D95)")
    a = ap.parse_args()
    if a.timetable_key:
        provision_timetable_key(a.port)
        return

    ssid, password = prompt_credentials()
    if not ssid:
        sys.exit("no SSID given, nothing sent")
    if "\t" in ssid or "\t" in password:
        sys.exit("SSID/password must not contain a tab — that is the field separator")

    with serial.Serial(a.port, 115200, timeout=0.5) as s:
        s.reset_input_buffer()
        s.write(b"w")                      # firmware prompts for one line
        s.flush()
        time.sleep(0.5)
        s.write(f"{ssid}\t{password}\n".encode())
        s.flush()

        print(f"\nsent to slot {a.slot}; watching for the device to join...\n")
        deadline = time.time() + 45
        joined = False
        while time.time() < deadline:
            line = s.readline().decode("utf-8", "replace").rstrip()
            if not line:
                continue
            # Never print a line that could contain what we just typed.
            if password and password in line:
                continue
            print("  " + line)
            if "got ip" in line.lower() or "connected to" in line.lower():
                joined = True
            if joined and "flight_source" in line:
                break

    print("\njoined." if joined else
          "\nno join seen yet — check the SSID, or run tools/grab_screen.py and press 'n'.")


if __name__ == "__main__":
    main()
