"""Which column windows are most constant inside pause blocks, and inside the first blocks.

Pause blocks: cols 22..29 = 01100010 (the windows overlapping those columns are skipped).
For each window (p, w) outside 22..29: the share of pause blocks holding the window's most
common value, against the same share over a random set of the same number of non-pause
blocks (mean of 200 draws).

python3 -I -B test/harris/harris600/pausewin.py PAYLOAD_DIR [W ...]
"""
import os
import sys
from collections import Counter

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import H600, load_all
from structure import voice_blocks


def top_share(rows):
    c = Counter(rows)
    v, k = c.most_common(1)[0]
    return k / len(rows), v


def main(pd, widths):
    st = load_all(pd, H600)
    six = [s for s in H600 if s != "melp600"]
    R = np.concatenate([voice_blocks(st[s]) for s in six])
    key = np.array(["".join(map(str, r[22:30])) == "01100010" for r in R])
    P, O = R[key], R[~key]
    rng = np.random.default_rng(2)
    for w in widths:
        res = []
        for p in range(0, 61 - w):
            if p + w > 22 and p < 30:
                continue
            sp, v = top_share(["".join(map(str, r[p:p + w])) for r in P])
            base = np.mean([top_share(["".join(map(str, r[p:p + w]))
                                       for r in O[rng.choice(len(O), len(P), replace=False)]])[0]
                            for _ in range(200)])
            res.append((sp - base, sp, base, p, v))
        res.sort(reverse=True)
        print("width %d: pause blocks %d" % (w, len(P)))
        for d, sp, base, p, v in res[:6]:
            print("   cols %2d..%2d  top value %s in %.2f of pauses (non-pause %.2f)" % (p, p + w - 1, v, sp, base))


if __name__ == "__main__":
    a = sys.argv[1:]
    main(a[0], [int(x) for x in a[1:]] or [4])
