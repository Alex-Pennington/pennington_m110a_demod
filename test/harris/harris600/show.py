"""Print the whole 60-bit voice blocks of one stream as rows, columns grouped in tens.

python3 -I -B test/harris/harris600/show.py PAYLOAD_DIR SLUG [START=54]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import load_bits
from structure import voice_blocks


def main(pd, slug, start=54):
    B = voice_blocks(load_bits(pd, slug), start)
    print("      " + " ".join("%-10d" % (10 * k) for k in range(6)))
    for i, r in enumerate(B):
        s = "".join(map(str, r))
        print("%4d  %s" % (i, " ".join(s[10 * k:10 * k + 10] for k in range(6))))


if __name__ == "__main__":
    a = sys.argv[1:]
    main(a[0], a[1], *(int(x) for x in a[2:]))
