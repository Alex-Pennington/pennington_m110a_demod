"""Does melp600 carry the 600H block layout seen in the 600_x streams, and at what phase?

For each start s (0..59), cut melp600 into 60-bit blocks from bit 54+s and score layout L1
on it: the hold rate of the 7-bit pitch candidate (block cols 23..29) against chance, and
the mean within-group |r| of cols 19..22, 23..29 and 30..40. Prints the top phases. The
same score on the six 600_x streams at s = 0 is the reference.

Also the 20-bit structure of melp600: period_z at 20 and 60 over the whole stream and
over its first and second halves, and the longest run of one bit value.

python3 -I -B test/harris/harris600/melp600.py PAYLOAD_DIR
"""
import os
import sys
from collections import Counter

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import H600, load_all, load_bits
from structure import voice_blocks
import melpe_frames as mf


def score(per):
    same, chance = 0, 0.0
    allv = []
    for R in per:
        v = ["".join(map(str, r[23:30])) for r in R]
        allv += v
        same += sum(a == b for a, b in zip(v[1:], v[:-1]))
    c = np.array(list(Counter(allv).values()), float)
    chance = ((c / c.sum()) ** 2).sum() * sum(len(R) - 1 for R in per)
    R = np.concatenate(per).astype(float)
    C = np.abs(np.nan_to_num(np.corrcoef(R.T)))
    np.fill_diagonal(C, np.nan)
    g = [np.nanmean(C[np.ix_(list(x), list(x))]) for x in (range(19, 23), range(23, 30), range(30, 41))]
    return same, chance, g


def main(pd):
    st = load_all(pd, H600)
    six = [voice_blocks(st[s]) for s in H600 if s != "melp600"]
    s0 = score(six)
    print("600_x reference: hold %d vs %.1f, group |r| %s" % (s0[0], s0[1], ["%.3f" % x for x in s0[2]]))
    b = load_bits(pd, "melp600")
    t = "".join(map(str, b))
    runs = [(len(m), m[0]) for m in __import__("re").findall(r"0+|1+", t)]
    print("melp600: %d bits; longest runs %s; ones-run at end starts at bit %d" % (
        len(b), sorted(runs, reverse=True)[:3], len(t.rstrip("0").rstrip("1"))))
    end = len(t.rstrip("0").rstrip("1"))
    res = []
    for s in range(60):
        n = (end - 54 - s) // 60
        R = b[54 + s:54 + s + 60 * n].reshape(n, 60)
        same, ch, g = score([R])
        res.append((same / ch, same, ch, g, s))
    res.sort(key=lambda x: -x[0])
    for r, same, ch, g, s in res[:5]:
        print("   start 54+%2d: hold %2d vs %.1f, group |r| %s" % (s, same, ch, ["%.3f" % x for x in g]))
    half = (end - 54) // 2
    for name, seg in (("all", b[54:end]), ("first half", b[54:54 + half]), ("second half", b[54 + half:end])):
        print("   %-12s z20 %.1f  z60 %.1f" % (name, mf.period_z(seg, 20), mf.period_z(seg, 60)))


if __name__ == "__main__":
    main(sys.argv[1])
