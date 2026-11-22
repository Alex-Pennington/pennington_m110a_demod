"""Column profile and repeats of the 600H streams on a 60-bit grid from a given start.

Prints, per column of the block, the share of ones and its z (pooled 600_x streams, and
melp600 alone), then how often whole blocks and 20-bit thirds repeat exactly.

python3 -I -B test/harris/harris600/profile.py PAYLOAD_DIR [START=54] [MELP600_START=54]
"""
import os
import sys
from collections import Counter

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import H600, load_all, blocks, column_z


def main(pd, start=54, mstart=54):
    st = load_all(pd, H600)
    six = [s for s in H600 if s != "melp600"]
    R = np.concatenate([blocks(st[s], start, 60) for s in six])
    M = blocks(st["melp600"], mstart, 60)
    zr, mr = column_z(R)
    zm, mm = column_z(M)
    print("blocks: 600_x %d, melp600 %d" % (len(R), len(M)))
    print("col  600_x:p1   z    melp600:p1   z")
    for c in range(60):
        print("%3d   %5.2f %6.1f      %5.2f %6.1f %s" % (c, mr[c], zr[c], mm[c], zm[c],
              "*" if abs(zr[c]) > 3 else ""))
    for name, X in (("600_x", R), ("melp600", M)):
        rows = ["".join(map(str, r)) for r in X]
        cnt = Counter(rows)
        rep = sum(v for v in cnt.values() if v > 1)
        print("%s: %d blocks, %d distinct, %d in repeated blocks; top %s"
              % (name, len(rows), len(cnt), rep, cnt.most_common(3)))
        for t in range(3):
            th = Counter(r[20 * t:20 * t + 20] for r in rows)
            print("   third %d: %d distinct of %d; top %s" % (t, len(th), len(rows), th.most_common(2)))


if __name__ == "__main__":
    a = sys.argv[1:]
    main(a[0], *(int(x) for x in a[1:]))
