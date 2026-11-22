"""Value histogram of one column window over the 600H voice blocks, and its run lengths.

python3 -I -B test/harris/harris600/values.py PAYLOAD_DIR FIRST_COL WIDTH [TOP=15]
"""
import os
import sys
from collections import Counter

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import H600, load_all
from structure import voice_blocks


def main(pd, p, w, top=15):
    st = load_all(pd, H600)
    six = [s for s in H600 if s != "melp600"]
    seqs = {}
    for s in six:
        R = voice_blocks(st[s])
        seqs[s] = ["".join(map(str, r[p:p + w])) for r in R]
    allv = sum(seqs.values(), [])
    c = Counter(allv)
    print("cols %d..%d: %d blocks, %d distinct values (of %d)" % (p, p + w - 1, len(allv), len(c), 2 ** w))
    for v, k in c.most_common(top):
        print("   %s  %3d" % (v, k))
    for s in six:
        print("%-20s %s" % (s, " ".join(seqs[s])))


if __name__ == "__main__":
    a = sys.argv[1:]
    main(a[0], *(int(x) for x in a[1:]))
