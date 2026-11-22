"""Permutation-free test for unvoiced-frame parity in 54-bit, 22.5 ms vocoder frames.

LPC-10e (FED-STD-1015) and MELP 2400 (STANAG 4591 / MIL-STD-3005) both send pitch code 0
in unvoiced frames and both replace spectral bits in those frames with Hamming parity over
other bits. Hamming parity is linear over GF(2), so in the unvoiced frames some columns are
exact XORs of others, whatever order the bits are sent in.

Steps, per stream (frames cut on the 54-bit grid, sync column excluded):
 1. Greedily pick the 7 columns jointly all-0 in the most frames (lpc10.joint_constant):
    the pitch code of unvoiced frames, under any permutation.
 2. Over those frames, count independent affine GF(2) relations among the non-constant
    columns (columns + 1 - rank of [columns | ones]).
 3. List the relations of weight <= 4 (a Hamming(8,4) parity bit is the XOR of 3 data
    bits: weight 4).
Controls: the same count over the same number of the other frames, and over random bits.
Calibration: LPC-10e and MELPe 2400 encoder output of speech, columns permuted.

python3 -I -B test/harris/blind/gf2.py PAYLOAD_DIR LPC10TOOL MELPE WORK SPEECH.raw [...]
"""
import itertools
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "harris600"))
sys.path.insert(0, os.path.dirname(HERE))
from common import load_bits  # noqa: E402
import lpc10  # noqa: E402
import melpe_frames as mf  # noqa: E402


def rank_gf2(M):
    M = (np.asarray(M) & 1).astype(np.uint8).copy()
    r = 0
    rows, cols = M.shape
    for c in range(cols):
        piv = np.nonzero(M[r:, c])[0]
        if len(piv) == 0:
            continue
        p = r + piv[0]
        M[[r, p]] = M[[p, r]]
        nz = np.nonzero(M[:, c])[0]
        nz = nz[nz != r]
        M[nz] ^= M[r]
        r += 1
        if r == rows:
            break
    return r


def relations(S):
    """S: frames x columns subset. Returns (nonconstant cols, independent affine relations)."""
    nc = [c for c in range(S.shape[1]) if 0 < S[:, c].sum() < len(S)]
    if not nc:
        return nc, 0
    A = np.c_[S[:, nc], np.ones(len(S), np.uint8)]
    return nc, len(nc) + 1 - rank_gf2(A)


def low_weight(S, cols, wmax=4):
    """Affine relations of weight <= wmax among cols (XOR of the columns is constant)."""
    out = []
    P = [int("".join(map(str, S[:, c])), 2) for c in cols]
    full = (1 << len(S)) - 1
    for w in range(2, wmax + 1):
        for comb in itertools.combinations(range(len(cols)), w):
            x = 0
            for i in comb:
                x ^= P[i]
            if x == 0 or x == full:
                out.append(tuple(cols[i] for i in comb))
    return out


def robust_count(S, cols, thr=0.9, wmax=4):
    """Number of affine relations of weight 2..wmax among cols that hold in >= thr of the
    frames of S. Tolerates a subset contaminated by frames that are not unvoiced."""
    n = len(S)
    P = [int("".join(map(str, S[:, c])), 2) for c in cols]
    need = int(np.ceil(thr * n))
    cnt = 0
    for w in range(2, wmax + 1):
        for comb in itertools.combinations(range(len(cols)), w):
            x = 0
            for i in comb:
                x ^= P[i]
            ones = x.bit_count()
            if ones <= n - need or ones >= need:
                cnt += 1
    return cnt


def analyse(F, label, rng, list_rel=True):
    F = np.asarray(F)
    jc = lpc10.joint_constant(F)
    zc, share = jc[0]
    uv = np.all(F[:, zc] == 0, axis=1)
    keep = [c for c in range(F.shape[1]) if c not in zc and c != 53]
    S = F[uv][:, keep]
    nc, rel = relations(S)
    # control: same number of other frames
    other = F[~uv][:, keep]
    crel = []
    for _ in range(5):
        if len(other) >= len(S):
            idx = rng.choice(len(other), len(S), replace=False)
            crel.append(relations(other[idx])[1])
    print("%-30s %4d frames; 7 cols all-0 %s in %3d (%.3f); over them %d non-constant cols, "
          "%d affine relations; other frames: %s"
          % (label, len(F), zc, uv.sum(), share, len(nc), rel, crel if crel else "too few"))
    if len(S) >= 40:
        rc = robust_count(S, nc)
        cc = robust_count(other[rng.choice(len(other), len(S), replace=False)], nc) \
            if len(other) >= len(S) else None
        print("    weight<=4 relations holding in >= 90%% of those frames: %d (same count of other "
              "frames: %s)" % (rc, cc))
    if list_rel and rel and len(S) > len(nc) + 10:
        lw = low_weight(S, nc)
        print("    weight<=4 relations (frame columns):", [tuple(keep[c] for c in r) for r in lw][:30])
    return rel


def melpe_encode(melpe, raw, work):
    dst = os.path.join(work, os.path.basename(raw) + ".2400.bit")
    subprocess.run([melpe, "-q", "-r", "2400", "-m", "A", "-i", raw, "-o", dst], check=True,
                   capture_output=True)
    with open(dst, "rb") as f:
        return mf.unpack_frames(f.read(), 2400)


def main(pd, tool, melpe, work, *raws):
    os.makedirs(work, exist_ok=True)
    rng = np.random.default_rng(11)
    perm = np.r_[np.random.default_rng(5).permutation(53), 53]
    for raw in raws:
        tag = os.path.basename(raw).split(".")[0]
        b = lpc10.encode(tool, raw, work)
        analyse(lpc10.cut(b, 0, "as-is")[:, perm], "LPC-10e %s, permuted" % tag, rng, False)
        M = np.asarray(melpe_encode(melpe, raw, work))
        analyse(M[:, perm], "MELPe2400 %s, permuted" % tag, rng, False)
    rb = rng.integers(0, 2, (279, 54)).astype(np.uint8)
    rb[:, 53] = np.arange(279) % 2
    analyse(rb, "random frames", rng)
    bits = load_bits(pd, "melp2400")
    F = lpc10.cut(bits[54:15174], 6, "as-is")
    analyse(F, "melp2400 (frame = bits 60+54k)", rng)


if __name__ == "__main__":
    main(*sys.argv[1:])
