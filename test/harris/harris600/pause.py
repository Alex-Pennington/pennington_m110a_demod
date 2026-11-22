"""Columns that go constant in pause blocks.

A pause block is one whose columns P..P+W-1 hold the value V (default: cols 22..29 =
01100010, the value that runs at message starts). Prints, per column, the share of ones in
pause blocks and in the others, and the pause blocks themselves.

python3 -I -B test/harris/harris600/pause.py PAYLOAD_DIR [P W V]
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import H600, load_all
from structure import voice_blocks


def main(pd, p=22, w=8, v="01100010"):
    p, w = int(p), int(w)
    st = load_all(pd, H600)
    six = [s for s in H600 if s != "melp600"]
    R = np.concatenate([voice_blocks(st[s]) for s in six])
    key = np.array(["".join(map(str, r[p:p + w])) == v for r in R])
    P, O = R[key], R[~key]
    print("pause blocks %d, others %d" % (len(P), len(O)))
    print("col " + " ".join("%2d" % c for c in range(60)))
    print("pau " + " ".join("%2d" % round(9 * x) for x in P.mean(axis=0)) + "   (share of ones x9)")
    print("oth " + " ".join("%2d" % round(9 * x) for x in O.mean(axis=0)))
    for r in P:
        s = "".join(map(str, r))
        print("    " + " ".join(s[10 * k:10 * k + 10] for k in range(6)))


if __name__ == "__main__":
    main(*sys.argv[1:])
