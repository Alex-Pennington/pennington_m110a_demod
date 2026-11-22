"""Speech/pause timelines at 100 ms resolution, for the cross-check between modes.

1200H and 2400V: from the decoded audio's 22.5 ms log-energy track (WORK/<slug>.track.txt
from timestruct.py), a 100 ms cell is '#' when its mean energy is within 20 dB of the
message's 95th-percentile cell, else '.'; voiced cells (median f0 > 0) are 'v'.
600H: one cell per 60-bit block; '.' when cols 22..29 hold the pause value 01100010
(inferred, see docs/harris-600-format.md), else '#'.

Also prints the f0 statistics of each 1200H decode.

python3 -I -B test/harris/harris600/timeline.py PAYLOAD_DIR WORK
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import H600, H1200, load_bits
from structure import voice_blocks


def cells_from_track(path):
    t = np.loadtxt(path)
    cell = (t[:, 0] / 0.1).astype(int)
    n = cell.max() + 1
    e = np.array([t[cell == k, 1].mean() for k in range(n)])
    f = [t[cell == k, 2] for k in range(n)]
    ref = np.percentile(e, 95)
    s = "".join(("v" if np.median(fk) > 0 else "#") if ek > ref - 20 else "." for ek, fk in zip(e, f))
    f0 = t[t[:, 2] > 0, 2]
    return s, f0


def main(pd, work):
    for s in H1200 + ["melp2400"]:
        tl, f0 = cells_from_track(os.path.join(work, s + ".track.txt"))
        print("%-20s %4.1f s  %s" % (s, len(tl) / 10, tl))
        if len(f0):
            print("%-20s f0 voiced frames %d: median %.0f Hz, 10-90 %% %.0f-%.0f Hz"
                  % ("", len(f0), np.median(f0), np.percentile(f0, 10), np.percentile(f0, 90)))
    for s in H600:
        if s == "melp600":
            continue
        R = voice_blocks(load_bits(pd, s))
        tl = "".join("." if "".join(map(str, r[22:30])) == "01100010" else "#" for r in R)
        print("%-20s %4.1f s  %s" % (s, len(tl) / 10, tl))


if __name__ == "__main__":
    main(*sys.argv[1:])
