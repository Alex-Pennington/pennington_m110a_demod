"""Where a given bit pattern occurs in each stream (any bit offset), and the stream tails.

python3 -I -B test/harris/harris600/repeats.py PAYLOAD_DIR PATTERN
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import ALL, load_all


def main(pd, pat):
    st = load_all(pd, ALL)
    for s in ALL:
        t = "".join(map(str, st[s]))
        hits, i = [], t.find(pat)
        while i >= 0:
            hits.append(i)
            i = t.find(pat, i + 1)
        print("%-20s len %5d  hits %s  ((pos-54) mod 60: %s)" % (
            s, len(t), hits, [(h - 54) % 60 for h in hits]))
        print("    last 130 bits: %s" % t[-130:])


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
