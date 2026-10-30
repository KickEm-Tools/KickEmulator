#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Draws the panel's app icon, KickEm.icns: a dark rounded square with a 4x4 grid of pads, a few
of them lit. No logo, no lettering, nothing taken from the unit's branding.

    python3 panel/macos/make_icon.py [OUT.icns] [--png OUT.png]

Python 3 standard library only (zlib for PNG, struct for the icns container). The output is
deterministic: running it reproduces the committed panel/macos/KickEm.icns byte for byte.
"""
import os
import struct
import sys
import zlib

BG_TOP, BG_BOTTOM = (46, 50, 58), (20, 22, 26)
PAD_OFF = (58, 63, 72)
# Lit pads (row, column): colours in the spirit of a drum machine's pads, not the unit's.
LIT = {(0, 0): (255, 120, 40), (0, 2): (255, 120, 40), (1, 1): (60, 200, 120), (2, 3): (70, 150, 255),
       (3, 0): (255, 70, 90), (3, 2): (255, 200, 60), (1, 3): (60, 200, 120), (2, 0): (170, 110, 255)}


def rrect_sdf(px, py, cx, cy, hw, hh, r):
    """Signed distance from (px, py) to a rounded rectangle centred at (cx, cy)."""
    qx, qy = abs(px - cx) - hw + r, abs(py - cy) - hh + r
    ox, oy = max(qx, 0.0), max(qy, 0.0)
    return (ox * ox + oy * oy) ** 0.5 + min(max(qx, qy), 0.0) - r


def over(dst, src, a):
    return tuple(d + (s - d) * a for d, s in zip(dst, src))


def render(n):
    """RGBA rows of an n x n icon, on Apple's grid: the body is 824/1024 of the canvas."""
    k = n / 1024.0
    body_c, body_h, body_r = 512 * k, 412 * k, 185 * k
    grid0, cell, pad_h, pad_r = 512 * k - 4 * 82 * k, 164 * k, 66 * k, 22 * k
    rows = []
    for y in range(n):
        py = y + 0.5
        row = bytearray()
        t = min(max((py - (body_c - body_h)) / (2 * body_h), 0.0), 1.0)
        bg = tuple(a + (b - a) * t for a, b in zip(BG_TOP, BG_BOTTOM))
        for x in range(n):
            px = x + 0.5
            d = rrect_sdf(px, py, body_c, body_c, body_h, body_h, body_r)
            cov = min(max(0.5 - d, 0.0), 1.0)
            if cov <= 0.0:
                row += b"\0\0\0\0"
                continue
            col = bg
            ci, cj = int((py - grid0) // cell), int((px - grid0) // cell)
            if 0 <= ci < 4 and 0 <= cj < 4:
                cx, cy = grid0 + (cj + 0.5) * cell, grid0 + (ci + 0.5) * cell
                pd = rrect_sdf(px, py, cx, cy, pad_h, pad_h, pad_r)
                lit = LIT.get((ci, cj))
                if lit:  # a soft glow around a lit pad
                    g = max(0.0, 1.0 - max(pd, 0.0) / (18 * k + 0.5)) ** 2 * 0.45
                    col = over(col, lit, g)
                pc = min(max(0.5 - pd, 0.0), 1.0)
                if pc > 0:
                    face = lit or PAD_OFF
                    # A little light from the top of each pad.
                    shade = 1.0 + 0.12 * (1.0 - min(max((py - (cy - pad_h)) / (2 * pad_h), 0.0), 1.0))
                    face = tuple(min(255.0, c * shade) for c in face)
                    col = over(col, face, pc)
            row += bytes((int(round(col[0])), int(round(col[1])), int(round(col[2])), int(round(cov * 255))))
        rows.append(bytes(row))
    return rows


def png(rows):
    n = len(rows)
    raw = b"".join(b"\0" + r for r in rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", n, n, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


# icns element types holding PNG data, by pixel size (the @2x types reuse a size).
TYPES = [(b"icp4", 16), (b"icp5", 32), (b"icp6", 64), (b"ic07", 128), (b"ic08", 256), (b"ic09", 512),
         (b"ic10", 1024), (b"ic11", 32), (b"ic12", 64), (b"ic13", 256), (b"ic14", 512)]


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "KickEm.icns")
    png_out = None
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == "--png":
            png_out = args.pop(0)
        else:
            out = a
    pngs = {}
    for _, n in TYPES:
        if n not in pngs:
            pngs[n] = png(render(n))
    body = b"".join(t + struct.pack(">I", 8 + len(pngs[n])) + pngs[n] for t, n in TYPES)
    with open(out, "wb") as f:
        f.write(b"icns" + struct.pack(">I", 8 + len(body)) + body)
    if png_out:
        with open(png_out, "wb") as f:
            f.write(pngs[512])
    print("%s (%d bytes)" % (out, 8 + len(body)))


if __name__ == "__main__":
    main(sys.argv[1:])
