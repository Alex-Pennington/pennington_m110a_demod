"""Period and phase of the 600H streams.

1. period_z at candidate periods per stream, header skipped, and pooled.
2. Phase: the 60-column profile of each stream, cut from bit 54 (just after the header),
   circularly cross-correlated against the pooled profile of the others. If the grid is
   anchored to the header, every stream's best shift is 0.

python3 -I -B test/harris/harris600/period.py PAYLOAD_DIR
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import H600, H1200, load_all, blocks
import melpe_frames as mf


def main(pd):
    st = load_all(pd)
    print("period_z, from bit 54")
    Ps = [20, 30, 40, 54, 60, 66, 80, 81, 100, 120]
    print("%-20s %6s " % ("stream", "bits") + " ".join("%6d" % P for P in Ps))
    for s in H600 + H1200:
        b = st[s][54:]
        print("%-20s %6d " % (s, len(b)) + " ".join("%6.1f" % mf.period_z(b, P) for P in Ps))
    for name, grp in (("pooled 600H", H600), ("pooled 1200H", H1200)):
        row = []
        for P in Ps:
            v = [mf.period_z(st[s][54:], P) for s in grp]
            v = [x for x in v if x == x]
            row.append(sum(v) / np.sqrt(len(v)))
        print("%-20s %6s " % (name, "") + " ".join("%6.1f" % x for x in row))

    # random reference
    rng = np.random.default_rng(7)
    zr = [max(mf.period_z(rng.integers(0, 2, 3000), P) for P in range(4, 131)) for _ in range(20)]
    print("random 3000-bit streams, max z over P 4..130: median %.1f max %.1f" % (np.median(zr), max(zr)))

    print()
    print("phase of the 60-bit grid relative to bit 54 (best circular shift vs the others)")
    prof = {}
    for s in H600:
        r = blocks(st[s], 54, 60).astype(float)
        prof[s] = r.mean(axis=0) - r.mean()
    for s in H600:
        others = sum(prof[o] * len(st[o]) for o in H600 if o != s)
        c = [float(np.dot(np.roll(prof[s], k), others)) for k in range(60)]
        order = np.argsort(c)[::-1]
        print("%-20s best shift %2d (corr %.3f), next %2d (%.3f), shift 0 corr %.3f"
              % (s, order[0], c[order[0]], order[1], c[order[1]], c[0]))


if __name__ == "__main__":
    main(sys.argv[1])
