#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""png_diff.py A.png B.png [X Y W H]

How different two renderings of the 1024x600 screen are: the app's own (TestControl shot, or a
QMP screendump) and the frame the panel received (kickem_ctl screenshot). Either image may be at
another size; both are sampled on the 1024x600 grid (every second pixel), optionally only within
the rectangle X Y W H. Prints "mean <m> changed <p>%": the mean absolute difference per channel
(0..255) and the share of samples whose largest channel difference is over 32.
Python standard library only."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from row_highlight import read_png  # noqa: E402


def main():
    a, b = read_png(sys.argv[1]), read_png(sys.argv[2])
    x0, y0, w, h = map(int, sys.argv[3:7]) if len(sys.argv) >= 7 else (0, 0, 1024, 600)
    tot = changed = n = 0
    for y in range(y0, y0 + h, 2):
        for x in range(x0, x0 + w, 2):
            pa = []
            for (iw, ih, bpp, rows) in (a, b):
                px, py = min(iw - 1, x * iw // 1024), min(ih - 1, y * ih // 600)
                pa.append(rows[py][px * bpp:px * bpp + 3])
            d = [abs(pa[0][i] - pa[1][i]) for i in range(3)]
            tot += sum(d) / 3.0
            changed += max(d) > 32
            n += 1
    print("mean %.2f changed %.2f%%" % (tot / max(n, 1), 100.0 * changed / max(n, 1)))


if __name__ == '__main__':
    main()
