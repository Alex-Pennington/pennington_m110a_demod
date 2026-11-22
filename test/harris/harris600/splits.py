"""Where do the 19-bit chunks of layout L1 split into codebook indices?

For a chunk of columns and each split point s, the mean |r| between columns on the same
side against across the split (within-block correlations over the 600H voice blocks).
Codebook indices of a trained VQ carry dependences among their own bits, so a true
boundary shows a higher within/across ratio. Also prints the mean |r| between chunks.

python3 -I -B test/harris/harris600/splits.py PAYLOAD_DIR
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import H600, load_all
from structure import voice_blocks


def main(pd):
    st = load_all(pd, H600)
    R = np.concatenate([voice_blocks(st[s]) for s in H600 if s != "melp600"]).astype(float)
    C = np.abs(np.corrcoef(R.T))
    np.fill_diagonal(C, np.nan)
    null = 1 / np.sqrt(len(R)) * np.sqrt(2 / np.pi)
    print("blocks %d; mean |r| of independent columns ~%.3f" % (len(R), null))
    for name, lo, hi in (("lsfA", 0, 19), ("lsfB", 41, 60), ("gain", 30, 41), ("0-22", 0, 23)):
        print("%s cols %d-%d:" % (name, lo, hi - 1))
        for s in range(lo + 3, hi - 2):
            L = list(range(lo, s)); Rt = list(range(s, hi))
            within = np.nanmean(np.concatenate([C[np.ix_(L, L)].ravel(), C[np.ix_(Rt, Rt)].ravel()]))
            across = np.nanmean(C[np.ix_(L, Rt)])
            print("   split before %2d: within %.3f across %.3f ratio %.2f" % (s, within, across, within / across))
    blocks = {"0-9": range(0, 10), "10-18": range(10, 19), "19-22": range(19, 23), "23-29": range(23, 30),
              "30-40": range(30, 41), "41-50": range(41, 51), "51-59": range(51, 60)}
    names = list(blocks)
    print("mean |r| between column groups:")
    print("        " + " ".join("%6s" % n for n in names))
    for a in names:
        print("%6s  " % a + " ".join("%6.3f" % np.nanmean(C[np.ix_(list(blocks[a]), list(blocks[b]))]) for b in names))


if __name__ == "__main__":
    main(sys.argv[1])
