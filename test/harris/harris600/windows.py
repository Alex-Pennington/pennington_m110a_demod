"""Field-window statistics over the 600H 60-bit blocks.

For every window (start p, width w) of contiguous columns (circular within the block):
  rep   share of block pairs (n, n+1) where the window's value is the same, divided by the
        share expected from the value distribution (sum of squared value probabilities,
        estimated with the pairs' own marginals). >1: the field holds still from block to
        block, as pitch, gain and voicing do in steady speech and in pauses.
  mi    multi-information: sum of the bit entropies minus the joint entropy (bits), minus
        the same for a column-shuffled copy (each column permuted on its own across blocks,
        which keeps every bit's share of ones and destroys dependences). >0: the window's
        bits depend on each other, as the bits of one codebook index do.

python3 -I -B test/harris/harris600/windows.py PAYLOAD_DIR [W ...]
"""
import os
import sys
from collections import Counter

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import H600, load_all
from structure import voice_blocks


def ent(vals):
    c = np.array(list(Counter(vals).values()), dtype=float)
    p = c / c.sum()
    return float(-(p * np.log2(p)).sum())


def win(R, p, w):
    cols = [(p + k) % R.shape[1] for k in range(w)]
    v = np.zeros(len(R), dtype=np.int64)
    for c in cols:
        v = (v << 1) | R[:, c]
    return v


def stats(per, p, w, rng):
    same, exp = 0.0, 0.0
    allv = []
    for R in per:
        v = win(R, p, w)
        allv.append(v)
        same += (v[1:] == v[:-1]).sum()
    v = np.concatenate(allv)
    c = np.array(list(Counter(v.tolist()).values()), dtype=float)
    q = (c / c.sum()) ** 2
    npairs = sum(len(R) - 1 for R in per)
    exp = q.sum() * npairs
    R = np.concatenate(per)
    hb = sum(ent(R[:, (p + k) % 60].tolist()) for k in range(w))
    mi = hb - ent(v.tolist())
    S = R.copy()
    for c_ in range(60):
        S[:, c_] = rng.permutation(S[:, c_])
    vs = win(S, p, w)
    mis = hb - ent(vs.tolist())
    return same / max(exp, 1e-9), same, exp, mi - mis


def main(pd, widths):
    st = load_all(pd, H600)
    per = [voice_blocks(st[s]) for s in H600 if s != "melp600"]
    rng = np.random.default_rng(3)
    for w in widths:
        print("width %d" % w)
        rows = []
        for p in range(60):
            r, same, exp, mi = stats(per, p, w, rng)
            rows.append((p, r, same, exp, mi))
        for p, r, same, exp, mi in rows:
            print("  p%2d rep %5.2f (%3d vs %5.1f)  mi %+5.2f %s" % (
                p, r, same, exp, mi, "#" * int(max(0, mi) * 4)))


if __name__ == "__main__":
    a = sys.argv[1:]
    main(a[0], [int(x) for x in a[1:]] or [3])
