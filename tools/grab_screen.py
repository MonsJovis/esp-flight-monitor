#!/usr/bin/env python3
"""Pull the panel's framebuffer over USB and write it out as a PNG.

The build host cannot see the device, so this is how UI work gets verified:
it reads the actual RGB565 framebuffer being scanned out, not a re-render.

  python3 tools/grab_screen.py out.png [--port /dev/cu.usbmodem1101]
"""
import argparse, base64, re, struct, sys, time, zlib
import serial

MARK_START = re.compile(rb"<<<SHOT w=(\d+) h=(\d+) fmt=(\w+) bytes=(\d+) crc=([0-9a-f]+)>>>")
MARK_END   = b"<<<ENDSHOT>>>"
B64_LINE   = re.compile(rb"^[A-Za-z0-9+/]+={0,2}$")


def write_png(path, w, h, rgb):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = bytearray()
    for y in range(h):                      # PNG wants a filter byte per scanline
        raw.append(0)
        raw += rgb[y * w * 3:(y + 1) * w * 3]
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 6))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def rgb565_to_rgb888(buf, w, h):
    out = bytearray(w * h * 3)
    for i in range(w * h):
        v = buf[i * 2] | (buf[i * 2 + 1] << 8)      # little-endian
        r = (v >> 11) & 0x1F
        g = (v >> 5) & 0x3F
        b = v & 0x1F
        out[i * 3]     = (r << 3) | (r >> 2)        # expand, preserving white
        out[i * 3 + 1] = (g << 2) | (g >> 4)
        out[i * 3 + 2] = (b << 3) | (b >> 2)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--port", default="/dev/cu.usbmodem1101")
    ap.add_argument("--timeout", type=float, default=40.0)
    a = ap.parse_args()

    with serial.Serial(a.port, 115200, timeout=0.3) as s:
        s.reset_input_buffer()
        s.write(b"s")
        s.flush()

        buf, t0 = bytearray(), time.time()
        while time.time() - t0 < a.timeout:
            buf += s.read(65536)
            if MARK_END in buf and MARK_START.search(buf):
                break
        else:
            sys.exit("timed out waiting for a frame (is the firmware running?)")

    m = MARK_START.search(buf)
    w, h, fmt, nbytes, crc_hex = (int(m.group(1)), int(m.group(2)),
                                  m.group(3).decode(), int(m.group(4)), m.group(5).decode())
    body = buf[m.end():buf.index(MARK_END, m.end())]

    # Log lines can interleave with the payload; keep only pure base64 lines.
    payload = b"".join(ln for ln in body.split(b"\n") if B64_LINE.match(ln.strip()))
    raw = base64.b64decode(payload)

    if len(raw) != nbytes:
        sys.exit(f"size mismatch: got {len(raw)}, header said {nbytes}")

    want = int(crc_hex, 16)
    got, got_inv = zlib.crc32(raw) & 0xFFFFFFFF, (zlib.crc32(raw) ^ 0xFFFFFFFF) & 0xFFFFFFFF
    if want not in (got, got_inv):
        sys.exit(f"CRC mismatch: device {want:08x}, host {got:08x}/{got_inv:08x} — corrupt transfer")

    write_png(a.out, w, h, rgb565_to_rgb888(raw, w, h))
    print(f"wrote {a.out}  {w}x{h} {fmt}  {nbytes} bytes  crc ok")


if __name__ == "__main__":
    main()
