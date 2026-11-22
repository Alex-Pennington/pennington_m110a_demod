"""Evaluate a candidate field layout of the 600H 60-bit block.

Per field: width, distinct values seen, entropy H of the values (bits), the summed entropy
of its single bits (Hb; Hb - H is the dependence among its bits), the hold rate (value
equal to the previous block's) against chance, and the field's mean value read MSB-first
and LSB-first in pause blocks (cols 22..29 = 01100010) and in the others, as a fraction
of full scale. Then the mutual information between fields (bits), bias-corrected by a
block-shuffled copy.

Layouts:
  L1  lsfA 0-18 | bpv 19-22 | pitch 23-29 | gain 30-40 | lsfB 41-59   (inferred from the data)
  L2  bpv 0-3 | gain 4-14 | pitch 15-21 | lsf 22-31, 32-41, 42-50, 51-59   (MILCOM Table 1 order)

python3 -I -B test/harris/harris600/fields.py PAYLOAD_DIR [L1|L2 ...]
"""
import os
import sys
from collections import Counter

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import H600, load_all
from structure import voice_blocks

LAYOUTS = {
    "L1": [("lsfA", 0, 19), ("bpv", 19, 4), ("pitch", 23, 7), ("gain", 30, 11), ("lsfB", 41, 19)],
    "L2": [("bpv", 0, 4), ("gain", 4, 11), ("pitch", 15, 7), ("lsf1", 22, 10), ("lsf2", 32, 10),
           ("lsf3", 42, 9), ("lsf4", 51, 9)],
}


def H(v):
    c = np.array(list(Counter(v).values()), float)
    p = c / c.sum()
    return float(-(p * np.log2(p)).sum())


def num(R, p, w, lsb=False):
    cols = list(range(p, p + w))
    if lsb:
        cols = cols[::-1]
    v = np.zeros(len(R), dtype=np.int64)
    for c in cols:
        v = (v << 1) | R[:, c]
    return v


def main(pd, *names):
    st = load_all(pd, H600)
    per = [voice_blocks(st[s]) for s in H600 if s != "melp600"]
    R = np.concatenate(per)
    pause = np.array(["".join(map(str, r[22:30])) == "01100010" for r in R])
    rng = np.random.default_rng(8)
    for name in names or ("L1", "L2"):
        L = LAYOUTS[name]
        print("layout %s" % name)
        vals = {}
        for f, p, w in L:
            v = num(R, p, w)
            vals[f] = v
            hb = sum(H(R[:, c].tolist()) for c in range(p, p + w))
            same = sum((num(P, p, w)[1:] == num(P, p, w)[:-1]).sum() for P in per)
            npairs = sum(len(P) - 1 for P in per)
            c = np.array(list(Counter(v.tolist()).values()), float)
            chance = ((c / c.sum()) ** 2).sum() * npairs
            fs = 2 ** w - 1
            m_m = (num(R[pause], p, w).mean() / fs, num(R[~pause], p, w).mean() / fs)
            m_l = (num(R[pause], p, w, True).mean() / fs, num(R[~pause], p, w, True).mean() / fs)
            print("  %-6s cols %2d-%2d w%2d  distinct %3d  H %.2f  Hb-H %.2f  hold %3d vs %.1f"
                  "  mean msb pause/other %.2f/%.2f  lsb %.2f/%.2f"
                  % (f, p, p + w - 1, w, len(c), H(v.tolist()), hb - H(v.tolist()), same, chance,
                     m_m[0], m_m[1], m_l[0], m_l[1]))
        fl = [f for f, _, _ in L]
        print("  MI between fields (bits, minus shuffled):")
        for i, a in enumerate(fl):
            row = []
            for b in fl[: i]:
                mi = H(vals[a].tolist()) + H(vals[b].tolist()) - H(list(zip(vals[a].tolist(), vals[b].tolist())))
                sb = vals[b][rng.permutation(len(R))]
                mis = H(vals[a].tolist()) + H(sb.tolist()) - H(list(zip(vals[a].tolist(), sb.tolist())))
                row.append("%s %+.2f" % (b, mi - mis))
            print("    %-6s %s" % (a, "  ".join(row)))


if __name__ == "__main__":
    main(*sys.argv[1:])
