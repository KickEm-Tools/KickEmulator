#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""row_highlight.py SHOT.png X0 X1 Y0 PITCH N

Which of N list rows is drawn highlighted in a 1024x600 screen shot: the rows are PITCH pixels
apart, the first centred at Y0, between X0 and X1. Prints the mean brightness of each row's band
and "selected <i>" for the brightest row when it stands out from the others by 8 or more.
Python standard library only (zlib PNG decoding: 8-bit RGB / RGBA, non-interlaced)."""
import struct, sys, zlib


def read_png(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', 'not a PNG'
    pos, idat, w = 8, b'', 0
    while pos < len(d):
        n, kind = struct.unpack('>I4s', d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', body)
            assert depth == 8 and ctype in (2, 6) and inter == 0, 'unsupported PNG'
            bpp = 3 if ctype == 2 else 4
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw, stride = zlib.decompress(idat), w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def main():
    path, x0, x1, y0, pitch, n = sys.argv[1], *map(int, sys.argv[2:7])
    w, h, bpp, rows = read_png(path)
    sx = w / 1024.0
    means = []
    for r in range(n):
        yc = y0 + r * pitch
        tot = cnt = 0
        for y in range(int((yc - pitch // 3) * sx), int((yc + pitch // 3) * sx)):
            line = rows[y]
            for x in range(int(x0 * sx), int(x1 * sx), 4):
                tot += sum(line[x * bpp:x * bpp + 3]) / 3
                cnt += 1
        means.append(tot / max(cnt, 1))
    best = max(range(n), key=lambda i: means[i])
    others = [m for i, m in enumerate(means) if i != best]
    print('rows ' + ' '.join('%d:%.1f' % (i, m) for i, m in enumerate(means)))
    if means[best] - max(others) >= 8:
        print('selected %d' % best)
    else:
        print('selected none')


main()
