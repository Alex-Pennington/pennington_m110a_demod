"""Per-column profile of Harris 1200H / 2400V frames against the reference coder's own.

For each frame column: share of ones (p1) and lag-1 autocorrelation (ac1, the column
against itself one frame later). A stream in the standard layout carries the codec's
per-position biases (codebook MSBs, mode bits, spare zeros) at the same columns as the
coder's own output on other speech, so the two profiles correlate; a permuted or foreign
layout does not. Random frames give the null.

python3 -I -B test/harris/harris600/colprofile.py PAYLOAD_DIR MELPE WORK SPEECH.raw [...]
"""
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import load_bits
from edges import TRAILER
import melpe_frames as mf


def prof(F):
    F = np.asarray(F, dtype=float)
    p1 = F.mean(0)
    ac = np.array([np.corrcoef(F[:-1, c], F[1:, c])[0, 1] if F[:, c].std() > 0 else 0.0
                   for c in range(F.shape[1])])
    return p1, np.nan_to_num(ac)


def encode(melpe, rate, raw, work):
    dst = os.path.join(work, os.path.basename(raw) + ".%d.bit" % rate)
    subprocess.run([melpe, "-q", "-r", str(rate), "-m", "A", "-i", raw, "-o", dst], check=True,
                   capture_output=True)
    with open(dst, "rb") as f:
        return mf.unpack_frames(f.read(), rate)


def voice_frames(bits, rate, start):
    t = "".join(map(str, bits))
    end = t.find(TRAILER)
    end = len(t) if end < 0 else end
    n = mf.frame_bits(rate)
    k = (end - start) // n
    return np.asarray(bits[start:start + k * n]).reshape(k, n)


def main(pd, melpe, work, *raws):
    os.makedirs(work, exist_ok=True)
    for rate, slugs, start in ((1200, ["1200_1", "1200_2", "1200_3"], 54), (2400, ["melp2400"], 6)):
        cal = np.concatenate([encode(melpe, rate, r, work) for r in raws])
        cp, ca = prof(cal)
        H = np.concatenate([voice_frames(load_bits(pd, s), rate, start) for s in slugs])
        hp, ha = prof(H)
        rp, ra = prof(mf.random_frames(rate, len(H), seed=4))
        si = mf.sync_index(rate)
        keep = [c for c in range(mf.frame_bits(rate)) if c != si]
        def r(a, b):
            return float(np.corrcoef(a[keep], b[keep])[0, 1])
        print("%d bit/s: Harris %d frames, coder %d frames (sync column left out)" % (rate, len(H), len(cal)))
        print("   corr of p1 profiles:  Harris-coder %+.2f   random-coder %+.2f" % (r(hp, cp), r(rp, cp)))
        print("   corr of ac1 profiles: Harris-coder %+.2f   random-coder %+.2f" % (r(ha, ca), r(ra, ca)))
        if len(raws) > 1:
            a = np.concatenate([encode(melpe, rate, x, work) for x in raws[:-1]])
            b = encode(melpe, rate, raws[-1], work)
            ap_, aa_ = prof(a)
            bp_, ba_ = prof(b)
            print("   coder on different speech (%d vs %d frames): p1 %+.2f, ac1 %+.2f"
                  % (len(a), len(b), r(ap_, bp_), r(aa_, ba_)))
        print("   coder columns with p1 < 0.1 or > 0.9: %s" % [(c + 1, round(cp[c], 2)) for c in keep if cp[c] < .1 or cp[c] > .9])
        print("   Harris p1 at those columns:          %s" % [(c + 1, round(hp[c], 2)) for c in keep if cp[c] < .1 or cp[c] > .9])
        print("   Harris columns with p1 < 0.1 or > 0.9: %s" % [(c + 1, round(hp[c], 2)) for c in keep if hp[c] < .1 or hp[c] > .9])


if __name__ == "__main__":
    main(*sys.argv[1:])
