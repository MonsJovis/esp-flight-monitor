#!/usr/bin/env python3
"""Refuse to publish a firmware image that is not what it claims to be.

This is the last gate before a build leaves for a device that may be 9,000 km
away, so it reads the FINISHED ARTIFACT rather than the build system that
produced it. AGENTS.md §11 rule 2 is that a gate can quietly stop checking:
`check_font_coverage.py` went on passing for a day with nothing left in its
scan path. A gate that asks CMake what version CMake was told to build is that
same hole — it would agree with itself whatever happened in between.

So everything here comes out of the .bin:

  * the app descriptor, at file offset 0x20. ESP-IDF lays it immediately after
    the 24-byte image header and the first 8-byte segment header, magic
    0xABCD5432, then version at +0x10 and project name at +0x30. That is the
    struct esp_app_get_description() hands to main/net/ota.c at runtime, so it
    is literally the string the device will compare against the manifest.

  * the Secure Boot V2 signature block appended after the image, verified
    against tools/ota_signing_key.pub.pem. An unsigned build of this firmware
    does not merely fail to update — it ABORTS ON BOOT ("No signatures were
    found for the running app"), so publishing one bricks the panel until
    somebody opens the case. That is not a thing to find out afterwards.

  * the size, against the real ota_0 entry in partitions.csv. The device
    already refuses an over-large image (main/net/ota.c:288), but it learns
    the size from the manifest, and discovering it at 3 a.m. on the device is
    the expensive way to find out.

The version comparison is the one that matters most and reads like the least.
main/net/ota_policy.c compares the manifest against what the app descriptor
carries; if the two ever disagree the device concludes it is already up to
date and declines the fix it was sent, silently, for the rest of its life.

It also WRITES the manifest, with --manifest. That is deliberate: the manifest
and the checks above are the same handful of facts — version, byte count, the
image's own name — and two copies of them, one in Python and one in workflow
YAML, is exactly the arrangement AGENTS.md §11 rule 1 is about. Here the
numbers that go into the manifest are the ones that were just verified, and
there is nowhere for a second opinion to live.

Usage:
    python3 tools/check_release.py <image.bin> <expected-version> [--key PEM]
    python3 tools/check_release.py <image.bin> <tag> --manifest out.json \
            --url-base https://github.com/OWNER/REPO/releases/download/<tag>

    <expected-version> may carry a leading "v" — the release workflow passes
    the git tag straight through, and v0.2.0 and 0.2.0 are the same release.

Exits non-zero, loudly, on any disagreement.
"""
import argparse
import json
import pathlib
import re
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_KEY = ROOT / "tools" / "ota_signing_key.pub.pem"
PARTITIONS = ROOT / "partitions.csv"

# esp_app_desc_t, from esp_app_format.h. The offsets are asserted against the
# magic word below rather than trusted, because a layout change upstream must
# fail here rather than silently read the wrong 32 bytes.
DESC_OFFSET = 0x20
DESC_MAGIC = 0xABCD5432
VERSION_OFFSET = DESC_OFFSET + 0x10
PROJECT_OFFSET = DESC_OFFSET + 0x30
FIELD_LEN = 32

PROJECT_NAME = "esp-flight-monitor"

# Mirrored from main/net/ota_policy.h and main/net/ota.c. Asserted against
# those headers by manifest_limits_match() below, so this copy cannot drift
# away from the firmware in silence.
OTA_URL_LEN = 192
MANIFEST_MAX = 4096


def manifest_limits_match():
    """Fail if the firmware's limits have moved out from under this file."""
    want = {"OTA_URL_LEN": OTA_URL_LEN, "MANIFEST_MAX": MANIFEST_MAX}
    src = ((ROOT / "main" / "net" / "ota_policy.h").read_text(encoding="utf-8")
           + (ROOT / "main" / "net" / "ota.c").read_text(encoding="utf-8"))
    for name, expected in want.items():
        m = re.search(rf"^#define\s+{name}\s+(\d+)", src, re.MULTILINE)
        if m is None:
            raise Failed(f"{name} is no longer defined in the firmware; this "
                         f"checker is validating against a number that has "
                         f"gone away")
        if int(m.group(1)) != expected:
            raise Failed(f"the firmware says {name} is {m.group(1)}, this "
                         f"checker assumes {expected}. Update tools/check_release.py.")


class Failed(Exception):
    pass


def cstr(blob, offset):
    """The NUL-terminated string in a fixed-width field."""
    raw = blob[offset:offset + FIELD_LEN]
    end = raw.find(b"\0")
    if end == -1:
        raise Failed(f"the field at 0x{offset:X} is not NUL-terminated")
    try:
        return raw[:end].decode("utf-8")
    except UnicodeDecodeError as exc:
        raise Failed(f"the field at 0x{offset:X} is not UTF-8: {exc}") from None


def normalise(v):
    """'v0.2.0' and '0.2.0' name the same release."""
    return v[1:] if v.startswith(("v", "V")) else v


def ota_slot_size():
    """The smaller of the two app slots, read from partitions.csv.

    Read rather than hardcoded: the layout is frozen once a device is in the
    field (a partition table cannot be replaced over OTA), so the number in
    that file is the real constraint and a second copy of it here would be one
    more thing to drift.
    """
    smallest = None
    for line in PARTITIONS.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        cols = [c.strip() for c in line.split(",")]
        if len(cols) < 5 or cols[1] != "app":
            continue
        size = int(cols[4], 0)
        smallest = size if smallest is None else min(smallest, size)
    if smallest is None:
        raise Failed(f"no app partition found in {PARTITIONS.name}")
    return smallest


def check_descriptor(blob, expected):
    magic, = struct.unpack_from("<I", blob, DESC_OFFSET)
    if magic != DESC_MAGIC:
        raise Failed(
            f"no app descriptor at 0x{DESC_OFFSET:X}: magic is 0x{magic:08X}, "
            f"expected 0x{DESC_MAGIC:08X}. Either this is not an ESP-IDF app "
            f"image, or esp_app_desc_t has moved and this checker is now "
            f"reading the wrong bytes.")

    project = cstr(blob, PROJECT_OFFSET)
    if project != PROJECT_NAME:
        raise Failed(f"this image is {project!r}, not {PROJECT_NAME!r}")

    version = cstr(blob, VERSION_OFFSET)
    if normalise(version) != normalise(expected):
        raise Failed(
            f"the image says it is {version!r} but it is being published as "
            f"{expected!r}.\n"
            f"    A device compares the manifest against the string IN THE "
            f"IMAGE, so publishing this would either offer an update that "
            f"installs and then reports the wrong version, or one the device "
            f"never accepts at all.\n"
            f"    Check that PROJECT_VER was exported before idf.py build.")
    return version


def check_size(blob):
    slot = ota_slot_size()
    if len(blob) > slot:
        raise Failed(
            f"the image is {len(blob):,} B and the app slot is {slot:,} B — "
            f"it cannot be installed")
    return len(blob), slot


def check_signature(path, key):
    if not key.exists():
        raise Failed(
            f"no public key at {key}. Extract one from the signing key:\n"
            f"    espsecure.py extract_public_key --version 2 "
            f"--keyfile secure_boot_signing_key.pem {key}")
    proc = subprocess.run(
        [sys.executable, "-m", "espsecure", "verify_signature",
         "--version", "2", "--keyfile", str(key), str(path)],
        capture_output=True, text=True)
    if proc.returncode != 0:
        detail = (proc.stdout + proc.stderr).strip()
        raise Failed(
            f"the image is not signed by {key.name}:\n"
            f"    {detail}\n"
            f"    An unsigned build of this firmware ABORTS ON BOOT, and a "
            f"build signed by a different key is one no device already in the "
            f"field will ever accept.")


def write_manifest(path, version, url_base, image_name, size):
    """The three fields main/net/ota_policy.c requires, and nothing else.

    `url` names THIS release's asset rather than a "latest" alias, so a
    manifest can never end up pointing at an image other than the one it was
    measured from. The device caps the whole document at 4 KB
    (MANIFEST_MAX) and the URL at OTA_URL_LEN-1, and refuses rather than
    truncates, so both are checked here where it is cheap.
    """
    url = f"{url_base.rstrip('/')}/{image_name}"
    if len(url) > OTA_URL_LEN - 1:
        raise Failed(f"the image URL is {len(url)} B and the device's "
                     f"OTA_URL_LEN allows {OTA_URL_LEN - 1}")
    body = json.dumps({"version": normalise(version), "url": url,
                       "size": size}, indent=2) + "\n"
    if len(body.encode("utf-8")) >= MANIFEST_MAX:
        raise Failed(f"the manifest is {len(body)} B and the device refuses "
                     f"anything at or over {MANIFEST_MAX}")
    path.write_text(body, encoding="utf-8")
    return body


def main():
    ap = argparse.ArgumentParser(
        description="Refuse to publish a firmware image that is not what it "
                    "claims to be.")
    ap.add_argument("image", type=pathlib.Path)
    ap.add_argument("version")
    ap.add_argument("--key", type=pathlib.Path, default=DEFAULT_KEY,
                    help="public key to verify the signature against")
    ap.add_argument("--no-signature", action="store_true",
                    help="skip the signature check (local builds only — a "
                         "release must never use this)")
    ap.add_argument("--manifest", type=pathlib.Path,
                    help="also write the update manifest here")
    ap.add_argument("--url-base",
                    help="where this release's assets will be served from; "
                         "required with --manifest")
    ap.add_argument("--image-name",
                    help="the filename the image is published under "
                         "(default: esp-flight-monitor-<version>.bin)")
    args = ap.parse_args()

    if args.manifest and not args.url_base:
        ap.error("--manifest needs --url-base")

    try:
        if not args.image.exists():
            raise Failed(f"no such image: {args.image}")
        blob = args.image.read_bytes()
        if len(blob) < PROJECT_OFFSET + FIELD_LEN:
            raise Failed(f"{args.image} is {len(blob)} B — far too small to "
                         f"be a firmware image")

        version = check_descriptor(blob, args.version)
        size, slot = check_size(blob)
        if args.no_signature:
            signed = "SKIPPED — not publishable"
        else:
            check_signature(args.image, args.key)
            signed = f"verified against {args.key.name}"

        manifest = None
        if args.manifest:
            manifest_limits_match()
            name = args.image_name or f"esp-flight-monitor-{normalise(version)}.bin"
            manifest = write_manifest(args.manifest, version, args.url_base,
                                      name, size)
    except Failed as exc:
        print(f"release check FAILED: {exc}", file=sys.stderr)
        return 1

    print(f"release check: {args.image.name}")
    print(f"  version    {version}  (published as {args.version})")
    print(f"  project    {PROJECT_NAME}")
    print(f"  size       {size:,} B of {slot:,} B slot "
          f"({100.0 * size / slot:.0f}% full)")
    print(f"  signature  {signed}")
    if manifest is not None:
        print(f"  manifest   {args.manifest}")
        for line in manifest.rstrip("\n").splitlines():
            print(f"    {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
