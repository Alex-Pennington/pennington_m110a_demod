"""Per-column contrast between two classes of 600H voice blocks.

Class A: blocks whose columns P..P+W-1 equal V (default the pause value at 22..29).
Prints per column the two-proportion z (A minus the rest) and, for a few candidate
windows, the value histogram inside class A.

python3 -I -B test/harris/harris600/contrast.py PAYLOAD_DIR [P W V]
"""
import os
import sys
from collections import Counter

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import H600, load_all
from structure import voice_blocks


def main(pd, p=22, w=8, v="01100010"):
    p, w = int(p), int(w)
    st = load_all(pd, H600)
    R = np.concatenate([voice_blocks(st[s]) for s in H600 if s != "melp600"])
    key = np.array(["".join(map(str, r[p:p + w])) == v for r in R])
    A, B = R[key].astype(float), R[~key].astype(float)
    pa, pb = A.mean(0), B.mean(0)
    pp = R.mean(0)
    z = (pa - pb) / np.sqrt(pp * (1 - pp) * (1 / len(A) + 1 / len(B)) + 1e-12)
    for c in range(60):
        print("%2d  A %.2f  rest %.2f  z %+5.1f %s" % (c, pa[c], pb[c], z[c], "*" * int(min(abs(z[c]), 8))))
    for (q, n) in ((30, 10), (30, 11), (31, 10), (29, 11), (19, 4), (18, 4), (20, 3)):
        h = Counter("".join(map(str, r[q:q + n])) for r in R[key])
        print("A: cols %d..%d: %s" % (q, q + n - 1, h.most_common(6)))


if __name__ == "__main__":
    main(*sys.argv[1:])
