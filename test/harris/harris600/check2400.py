"""Is melp2400 standard STANAG 4591 2400 bit/s MELPe after its header? A bit-level test.

STANAG 4591 s5.4 and Annex A.5.2.19: in an unvoiced frame (pitch code all zero or one bit
set) the 13 bandpass-voicing, Fourier-magnitude and aperiodic-flag bits are Hamming parity
over the first LSF stage index and the gain indices. So in a standard stream each parity
group is a fixed function of the bits it protects. The test is convention-free: per group,
over the unvoiced frames, it counts protected values seen more than once and how many of
those always carry the same parity. Bit positions are RFC 8130 Table 1 (B_01..B_54).

Also: the share of frames whose 7-bit pitch code has exactly two bits set. STANAG 4591
reserves those codes as erasures, so an encoder never sends them (random bits: 21/128).

The same tests run on the reference coder's own 2400 bit/s encoding of calibration speech,
and on random frames.

python3 -I -B test/harris/harris600/check2400.py PAYLOAD_DIR MELPE WORK SPEECH.raw [...]
"""
import os
import subprocess
import sys
from collections import defaultdict

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import load_bits
import melpe_frames as mf

B = lambda k: k - 1  # RFC 8130 B_k -> 0-based column
PITCH = [B(3), B(14), B(15), B(21), B(11), B(13), B(17)]           # P0..P6
GROUPS = {
    "FEC1 over LSF1 b6..b3": ([B(2), B(39), B(38), B(25)], [B(19), B(22), B(23), B(26)]),
    "FEC2 over LSF1 b2..b0": ([B(35), B(34), B(33)], [B(27), B(31), B(18)]),
    "FEC3 over G2 b4..b1": ([B(51), B(50), B(49)], [B(7), B(6), B(10), B(9)]),
    "FEC4 over g20,G1": ([B(30), B(52), B(47)], [B(1), B(53), B(36), B(37)]),
}


def tests(F):
    F = np.asarray(F)
    w = F[:, PITCH].sum(axis=1)
    unv = F[w <= 1]
    out = {"frames": len(F), "unvoiced": len(unv), "erasure codes": float((w == 2).mean())}
    for name, (par, prot) in GROUPS.items():
        seen = defaultdict(set)
        cnt = defaultdict(int)
        for r in unv:
            k = tuple(r[prot]); seen[k].add(tuple(r[par])); cnt[k] += 1
        multi = [k for k in cnt if cnt[k] > 1]
        same = sum(1 for k in multi if len(seen[k]) == 1)
        out[name] = "%d of %d repeated values carry one parity" % (same, len(multi))
    return out


def encode(melpe, raw, work):
    dst = os.path.join(work, os.path.basename(raw) + ".2400.bit")
    subprocess.run([melpe, "-q", "-r", "2400", "-m", "A", "-i", raw, "-o", dst], check=True,
                   capture_output=True)
    with open(dst, "rb") as f:
        return mf.unpack_frames(f.read(), 2400)


def words_reversed(bits, start, n_frames, m=6):
    """Frames read as 54/m words of m bits sent last word first, each word LSB first:
    stream column j of a frame holds B_k with word (53//m - j//m), bit j%m."""
    nw = 54 // m
    F = np.asarray(bits[start:start + 54 * n_frames]).reshape(n_frames, 54)
    out = np.zeros_like(F)
    for j in range(54):
        k = (nw - 1 - j // m) * m + j % m
        out[:, k] = F[:, j]
    return out


def main(pd, melpe, work, *raws):
    os.makedirs(work, exist_ok=True)
    bits = load_bits(pd, "melp2400")
    cases = {"melp2400 from bit 54, 6-bit words last-first": words_reversed(bits, 54, 280),
             "melp2400 from bit 6": mf.cut_frames(bits, 2400, 6),
             "melp2400 from bit 5, each frame bit-reversed (sync first)":
                 mf.cut_frames(bits, 2400, 5, reverse=True),
             "melp2400 from bit 6, inverted": mf.cut_frames(bits, 2400, 6, invert=True),
             "melp2400 from bit 33 (wrong cut)": mf.cut_frames(bits, 2400, 33)}
    cal = np.concatenate([encode(melpe, r, work) for r in raws])
    cases["reference coder on calibration speech"] = cal
    cases["random frames"] = mf.random_frames(2400, 300, seed=11)
    for name, F in cases.items():
        print(name)
        for k, v in tests(F).items():
            print("   %-24s %s" % (k, v))


if __name__ == "__main__":
    main(*sys.argv[1:])
