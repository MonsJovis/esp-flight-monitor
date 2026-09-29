#!/usr/bin/env python3
"""Recompress one of sim_common.h's PNGs for the repo.

png_write() stores its pixels uncompressed (~690 KB a frame, zero
dependencies); a flat UI frame deflates to a few percent of that. Only the
IDAT is touched: the pixels are the simulator's, byte for byte.

  python3 png_squeeze.py in.png out.png
"""
import struct, sys, zlib


def chunks(data):
    pos = 8
    while pos < len(data):
        n, = struct.unpack(">I", data[pos:pos + 4])
        yield data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        pos += 12 + n


def chunk(tag, body):
    return (struct.pack(">I", len(body)) + tag + body
            + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))


def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()
    ihdr, idat = None, b""
    for tag, body in chunks(data):
        if tag == b"IHDR":
            ihdr = body
        elif tag == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    out = (data[:8] + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    open(dst, "wb").write(out)
    print(f"  {dst}: {len(out) // 1024} KB")


if __name__ == "__main__":
    main()
