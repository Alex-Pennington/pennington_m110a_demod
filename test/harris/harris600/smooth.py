"""Smoothness scan: does any column window read as a number that moves slowly?

For window (p, w) read MSB-first ('m') and LSB-first ('l'), ratio = mean |v[n+1]-v[n]|
over consecutive blocks / mean |v[i]-v[j]| over all block pairs of the same message.
An ordered codebook carrying a slow parameter (pitch, gain) gives ratio well below 1;
exact holds lower it too, so the scan also reports the ratio with equal pairs dropped.
The baseline is the same statistic with the blocks of each message in shuffled order.

python3 -I -B test/harris/harris600/smooth.py PAYLOAD_DIR [W ...]
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import H600, load_all
from structure import voice_blocks


def val(R, p, w, order):
    cols = [(p + k) % 60 for k in range(w)]
    if order == "l":
        cols = cols[::-1]
    v = np.zeros(len(R), dtype=np.int64)
    for c in cols:
        v = (v << 1) | R[:, c]
    return v


def ratio(per, p, w, order, drop_equal=False):
    num, nn, den, nd = 0.0, 0, 0.0, 0
    for R in per:
        v = val(R, p, w, order).astype(float)
        d = np.abs(np.diff(v))
        if drop_equal:
            d = d[d > 0]
        num += d.sum(); nn += len(d)
        D = np.abs(v[:, None] - v[None, :])[np.triu_indices(len(v), 1)]
        den += D.sum(); nd += len(D)
    return (num / max(nn, 1)) / (den / max(nd, 1))


def main(pd, widths):
    st = load_all(pd, H600)
    per = [voice_blocks(st[s]) for s in H600 if s != "melp600"]
    rng = np.random.default_rng(5)
    base = []
    for k in range(30):
        sh = [R[rng.permutation(len(R))] for R in per]
        base.append(min(ratio(sh, p, 7, o, True) for p in range(60) for o in "ml"))
    print("shuffled baseline, min ratio over all 7-bit windows, equal pairs dropped: "
          "median %.3f, min %.3f" % (np.median(base), min(base)))
    for w in widths:
        res = []
        for p in range(60):
            for o in "ml":
                res.append((ratio(per, p, w, o, True), ratio(per, p, w, o), p, o))
        res.sort()
        print("width %d, lowest ratios (equal pairs dropped / with them):" % w)
        for r, r0, p, o in res[:8]:
            print("   cols %2d..%2d %s  %.3f / %.3f" % (p, (p + w - 1) % 60, o, r, r0))


if __name__ == "__main__":
    a = sys.argv[1:]
    main(a[0], [int(x) for x in a[1:]] or [7])
