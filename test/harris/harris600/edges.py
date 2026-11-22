"""Bits around the header end and around the end-of-voice trailer, per stream.

TRAILER is the first 54-bit end-of-voice word of every 600_x and 1200_x stream. It is
followed by the same word with its last bit cleared, then zeros.

python3 -I -B test/harris/harris600/edges.py PAYLOAD_DIR
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import ALL, HEADER, load_all

TRAILER = "101001010010100010100000000100010101000101110011111101"


def main(pd):
    st = load_all(pd, ALL)
    print("header   %s" % HEADER)
    print("trailer  %s" % TRAILER)
    print("header xor trailer %s" % "".join(str(int(a) ^ int(b)) for a, b in zip(HEADER, TRAILER)))
    for s in ALL:
        t = "".join(map(str, st[s]))
        assert t.startswith(HEADER), s
        i = t.find(TRAILER)
        tail = ""
        if i >= 0:
            tail = "trailer at %d; 12 bits before: %s; after 2 copies: %s" % (
                i, t[i - 12:i], t[i + 108:])
        print("%-20s bits 54-71: %s  %s" % (s, t[54:72], tail))


if __name__ == "__main__":
    main(sys.argv[1])
