"""Align 600H messages against each other, and per-column agreement in aligned blocks.

Key: the 8-bit value of cols 22..29 per block (pause value 01100010 left out of matching).
For each pair of messages and each block lag, count blocks whose keys are equal. Report
lags with 3 or more matches, against the count expected by chance (from the key
distribution), and the matched block pairs.

Then, over all block pairs matched at those lags (excluding the key columns themselves),
the per-column agreement rate, against the agreement of random block pairs. A column the
speech determines (voicing, gain, pitch, first-stage LSF bits) agrees more often when the
same phrase recurs; a column that carries fine detail (later-stage MSVQ bits) does not.

python3 -I -B test/harris/harris600/align.py PAYLOAD_DIR
"""
import os
import sys
from collections import Counter
from itertools import combinations

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import H600, load_all
from structure import voice_blocks

PAUSE = "01100010"


def keys(R):
    return ["".join(map(str, r[22:30])) for r in R]


def main(pd):
    st = load_all(pd, H600)
    six = [s for s in H600 if s != "melp600"]
    R = {s: voice_blocks(st[s]) for s in six}
    K = {s: keys(R[s]) for s in six}
    allk = [k for s in six for k in K[s] if k != PAUSE]
    c = Counter(allk)
    q = sum((v / len(allk)) ** 2 for v in c.values())
    pairs = []
    for a, b in combinations(six, 2):
        for lag in range(-len(K[b]) + 1, len(K[a])):
            m = [(i, i - lag) for i in range(len(K[a])) if 0 <= i - lag < len(K[b])
                 and K[a][i] == K[b][i - lag] and K[a][i] != PAUSE]
            ov = sum(1 for i in range(len(K[a])) if 0 <= i - lag < len(K[b]))
            if len(m) >= 3:
                print("%-18s %-18s lag %+3d: %d equal keys of %d overlapping (chance %.2f): %s"
                      % (a, b, lag, len(m), ov, ov * q, m))
                pairs += [(R[a][i], R[b][j]) for i, j in m]
    if not pairs:
        return
    A = np.array([x for x, _ in pairs]); B = np.array([y for _, y in pairs])
    agree = (A == B).mean(0)
    allR = np.concatenate([R[s] for s in six])
    rng = np.random.default_rng(1)
    i = rng.integers(0, len(allR), 5000); j = rng.integers(0, len(allR), 5000)
    base = (allR[i] == allR[j]).mean(0)
    print("\nmatched block pairs: %d" % len(A))
    print("col  agree  random   z")
    for col in range(60):
        z = (agree[col] - base[col]) / np.sqrt(base[col] * (1 - base[col]) / len(A))
        print("%3d   %.2f   %.2f  %+5.1f %s" % (col, agree[col], base[col], z,
              "(key)" if 22 <= col <= 29 else "*" * int(max(0, min(z, 8)))))


if __name__ == "__main__":
    main(sys.argv[1])
