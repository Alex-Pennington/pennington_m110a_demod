"""Structured hypotheses about the 2400V (melp2400) payload layout, tested on the bits
m110a_rx recovers. Pure numpy; no coder runs.

The voice bits are stream bits 54 .. 15173: after the 54-bit header, before the trailer of
108 ones that starts at 15174 (docs/harris-600-format.md s2; m110a_tx --frame rebuilds the
stream from them, docs/harris-tx.md). F is those bits cut into 54-bit rows from bit 54.

  V1  frame length: period_z at every period 4..400, and lag autocorrelation of the bits.
  V2  frame grid from stream bit 54 (header end and trailer start on it; the alternating
      column runs through the first and the last frame).
  V3  standard sync position: the alternating column is the frame's last bit (B_54).
  V4  superframe: column shares differ by frame index mod k (k = 2, 3, 4, 6), outside the
      alternating column, against frame-order shuffles.
  V5  contiguous fields: adjacent columns depend on each other more than random column
      pairs do (z against random column orders). Calibrated on 600H, whose fields are
      contiguous (US 6,917,914; docs/harris-600-format.md s3), and 1200H, standard MELPe
      1200 in its standard serialisation.
  V6  field boundaries by bit statistics: segments where adjacent dependence breaks, and
      whether their widths are MELPe 2400's parameter widths.
  V7  standard MELPe 2400 under a rotation or reversal of the frame (54 x 2 readings):
      share of pitch codes with exactly two bits set, which STANAG 4591 never sends.
  V8  a 7-bit all-zero code (gf2.py found cols 4, 6, 7, 8, 16, 17, 18 jointly zero in
      41 % of frames) behaves as an unvoiced pitch code: zero-code frames come in runs
      (z against shuffles) and shift the other columns.

python3 -I -B test/harris/blind/claims2400.py PAYLOAD_DIR
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "harris600"))
from common import HEADER, H600, H1200, load_bits  # noqa: E402
import melpe_frames as mf  # noqa: E402

N_SHUF = 400
RNG = np.random.default_rng(2400)
W = "101001010010100010100000000100010101000101110011111101"


def voice_frames(pd):
    s = load_bits(pd, "melp2400")
    t = "".join(map(str, s.tolist()))
    if t[:54] != HEADER:
        raise SystemExit("melp2400: header not found")
    end = len(t.rstrip("0"))
    run = end - len(t[:end].rstrip("1"))
    tr = end - 108
    print("stream %d bits; final run of ones %d bits (%d..%d); trailer taken at %d = 54 + %.3f x 54"
          % (len(t), run, end - run, end - 1, tr, (tr - 54) / 54))
    if (tr - 54) % 54:
        raise SystemExit("trailer not on the 54-bit grid from bit 54")
    V = s[54:tr].astype(np.int64)
    return s, V, V.reshape(-1, 54)


def phi_matrix(R):
    R = np.asarray(R, float)
    sd = R.std(axis=0)
    C = np.corrcoef(R.T)
    C[np.ix_(sd == 0, sd == 0)] = 0
    C = np.nan_to_num(C)
    np.fill_diagonal(C, 0)
    return np.abs(C)


def adjacency_z(R, n_perm=1000, seed=1):
    """Mean |phi| of adjacent columns against random column orders."""
    C = phi_matrix(R)
    n = C.shape[0]
    a = float(np.mean([C[i, i + 1] for i in range(n - 1)]))
    rng = np.random.default_rng(seed)
    null = []
    for _ in range(n_perm):
        p = rng.permutation(n)
        null.append(np.mean([C[p[i], p[i + 1]] for i in range(n - 1)]))
    null = np.array(null)
    return a, float(null.mean()), float((a - null.mean()) / null.std())


def blocks_until(bits, start, size, trailer):
    t = "".join(map(str, bits.tolist()))
    k = t.find(trailer, start)
    if k < 0:
        k = t.find("1" * 108, start)
    n = (k - start) // size
    return bits[start:start + n * size].reshape(n, size)


def v1(V):
    print("\nV1 frame length")
    Ps = list(range(4, 401))
    z = np.array([mf.period_z(V, P) for P in Ps])
    order = np.argsort(z)[::-1]
    print("  strongest periods: " + ", ".join("%d (z %.1f)" % (Ps[i], z[i]) for i in order[:10]))
    # null for "54-bit frames with independent columns": bits drawn from melp2400's own
    # 54-column share of ones. A period sharing a divisor with 54 picks up z from that
    # profile alone; only z beyond this null is structure of its own.
    F = V.reshape(-1, 54)
    prof = F.mean(axis=0)
    alt = [c for c in range(54) if (F[1:, c] != F[:-1, c]).mean() >= 0.9]

    def draw():
        R = (RNG.random(F.shape) < prof).astype(np.int64)
        for c in alt:  # the alternating column alternates in the null too
            R[:, c] = (F[0, c] + np.arange(len(R))) % 2
        return R.ravel()

    sim = np.array([[mf.period_z(x, P) for P in Ps] for x in (draw() for _ in range(20))])
    sm, ss = sim.mean(axis=0), sim.std(axis=0)
    ex = (z - sm) / np.where(ss > 0, ss, 1)
    print("  null: independent bits with melp2400's 54-column share of ones, column(s) %s alternating" % alt)
    print("  P     z   54-profile null (mean +- sd)   excess z")
    for P in (18, 27, 36, 48, 54, 60, 72, 81, 90, 108, 162, 216):
        i = Ps.index(P)
        print("  %3d %6.1f   %6.1f +- %4.1f   %+7.1f" % (P, z[i], sm[i], ss[i], ex[i]))
    print("  largest excess over all 397 periods: %+.1f at %d" % (ex.max(), Ps[int(ex.argmax())]))
    non = [(Ps[i], round(float(ex[i]), 1)) for i in np.argsort(ex)[::-1] if Ps[i] % 54 and ex[i] >= 5]
    print("  periods not a multiple of 54 with excess z >= 5 over the 54-profile null: %s"
          % (non[:10] if non else "none"))
    mult = [(Ps[i], round(float(ex[i]), 1)) for i in np.argsort(ex)[::-1] if Ps[i] % 54 == 0 and ex[i] >= 4]
    print("  multiples of 54 with excess z >= 4 (structure across frames): %s" % mult[:8])
    zr = []
    for _ in range(20):
        r = RNG.permutation(V)
        zr.append(max(mf.period_z(r, P) for P in Ps))
    print("  the same bits shuffled, max z over 4..400 (20 draws): median %.1f, max %.1f"
          % (np.median(zr), max(zr)))
    x = 2.0 * V - 1
    x -= x.mean()
    N = len(x)
    ac = np.array([np.dot(x[:-L], x[L:]) / np.dot(x, x) for L in range(1, 401)]) * np.sqrt(N)
    top = np.argsort(np.abs(ac))[::-1][:12] + 1
    print("  autocorrelation, strongest lags (z): " + ", ".join("%d (%+.1f)" % (L, ac[L - 1]) for L in top))
    print("  share of the 12 strongest lags that are multiples of 54: %d/12"
          % sum(1 for L in top if L % 54 == 0))
    ok = z[Ps.index(54)] == z.max() and not non
    print("  -> V1 54-bit frame: %s" % ("SUPPORTED" if ok else "see table"))
    print("  -> V1 other frame lengths (48, 60, 72, 81, 27): %s"
          % ("CONTRADICTED (no excess over the 54-profile null)" if not non else "see table"))


def v2(s, F):
    print("\nV2 frame grid from stream bit 54")
    alt = [c for c in range(54) if (F[1:, c] != F[:-1, c]).mean() >= 0.9]
    a = alt[0] if alt else -1
    print("  header ends at 54; trailer starts at 54 + %d x 54; columns alternating in >= 90%% "
          "of frame steps: %s" % (len(F), alt))
    if a >= 0:
        k = int((F[1:, a] != F[:-1, a]).sum())
        print("  column %d alternates in %d/%d steps, frames 0 and %d included" % (a, k, len(F) - 1, len(F) - 1))
    print("  -> V2: SUPPORTED (header end, trailer start and the alternation all on one grid)")
    return a


def v3(F, a):
    print("\nV3 sync bit in the standard position (B_54, the frame's last column)")
    k53 = int((F[1:, 53] != F[:-1, 53]).sum())
    print("  column 53 alternates in %d/%d steps; the alternating column is %d" % (k53, len(F) - 1, a))
    print("  a grid with the alternating column last starts at bit 54 + %d: its first frame "
          "takes %d header bits, and the trailer falls %d bits into a frame" % (a + 1, 53 - a, 54 - a - 1))
    print("  -> V3: CONTRADICTED on the V2 grid")


def v4(F, a):
    print("\nV4 superframe of k frames (columns other than %d)" % a)
    cols = [c for c in range(54) if c != a]
    R = F[:, cols].astype(float)

    def stat(R, k):
        idx = np.arange(len(R)) % k
        m = R.mean(axis=0)
        s = 0.0
        for r in range(k):
            sub = R[idx == r]
            v = m * (1 - m)
            v[v == 0] = 1
            s += (len(sub) * (sub.mean(axis=0) - m) ** 2 / v).sum()
        return s

    for k in (2, 3, 4, 6):
        o = stat(R, k)
        null = np.array([stat(R[RNG.permutation(len(R))], k) for _ in range(N_SHUF)])
        z = (o - null.mean()) / null.std()
        print("  k=%d: statistic %.1f, shuffles %.1f +- %.1f, z %+.1f%s"
              % (k, o, null.mean(), null.std(), z, "  -> CONTRADICTED" if z < 3 else "  -> SUPPORTED"))


def v5(F, a, pd):
    print("\nV5 contiguous fields: adjacent-column dependence against random column orders")
    cols = [c for c in range(54) if c != a]
    r = adjacency_z(F[:, cols])
    print("  melp2400 (%d frames, col %d dropped): adjacent |phi| %.3f, random orders %.3f, z %+.1f"
          % (len(F), a, r[0], r[1], r[2]))
    B6 = np.concatenate([blocks_until(load_bits(pd, s), 54, 60, W) for s in H600 if s != "melp600"])
    r6 = adjacency_z(B6)
    print("  600H calibration, 6 messages (%d blocks; fields contiguous): %.3f, %.3f, z %+.1f"
          % (len(B6), r6[0], r6[1], r6[2]))
    B6s = B6[:len(F)]
    r6s = adjacency_z(B6s)
    print("  600H, first %d blocks only (same count as melp2400): z %+.1f" % (len(B6s), r6s[2]))
    B12 = np.concatenate([blocks_until(load_bits(pd, s), 54, 81, W) for s in H1200])
    keep = [c for c in range(81) if B12[:, c].std() > 0 and (B12[1:, c] != B12[:-1, c]).mean() < 0.9]
    r12 = adjacency_z(B12[:, keep])
    print("  1200H calibration, standard MELPe 1200 order (%d frames, sync dropped): %.3f, %.3f, z %+.1f"
          % (len(B12), r12[0], r12[1], r12[2]))
    print("  -> V5 dependent bits sit next to each other: %s" % ("SUPPORTED" if r[2] >= 3 else "NOT SUPPORTED"))
    print("  -> V5 every field contiguous: NOT DETERMINABLE (1200H, standard order, also reads z >= 3)"
          if r12[2] >= 3 else "  -> V5 every field contiguous: see calibration")


def segments(R):
    C = phi_matrix(R)
    thr = 3 / np.sqrt(len(R))
    segs, cur = [], [0]
    for c in range(R.shape[1] - 1):
        if C[c, c + 1] >= thr:
            cur.append(c + 1)
        else:
            segs.append(cur)
            cur = [c + 1]
    segs.append(cur)
    return segs, C, thr


def fmt(segs):
    return ["%d-%d" % (g[0], g[-1]) if len(g) > 1 else str(g[0]) for g in segs]


def v6(F, a, pd):
    print("\nV6 field boundaries by bit statistics")
    segs, C, thr = segments(F)
    p1 = F.mean(axis=0)
    ac1 = []
    for c in range(54):
        x, y = F[:-1, c].astype(float), F[1:, c].astype(float)
        ac1.append(0.0 if x.std() == 0 or y.std() == 0 else float(np.corrcoef(x, y)[0, 1]))
    print("  col  p1    ac1    |phi| to next   (|phi| >= %.3f is z >= 3)" % thr)
    for c in range(54):
        nxt = C[c, c + 1] if c < 53 else float("nan")
        print("  %2d  %.2f  %+.2f   %.3f%s" % (c, p1[c], ac1[c], nxt, "  *" if nxt >= thr else ""))
    print("  segments joined by adjacent |phi| >= %.3f: %s" % (thr, fmt(segs)))
    widths = sorted(len(g) for g in segs)
    print("  segment widths: %s (%d segments, %d single columns)" % (widths, len(segs), sum(1 for w in widths if w == 1)))
    melp = sorted([7, 5, 3, 7, 6, 6, 6, 8, 4, 1, 1])
    print("  MELPe 2400 parameter widths: %s" % melp)
    slow = [c for c in range(54) if ac1[c] >= thr]
    print("  columns that persist frame to frame (ac1 >= %.3f): %s" % (thr, slow))
    # calibration: the same segmentation on 600H, whose fields are known (s3:
    # LSF chunk 0-18, pitch/voicing/gain 19-40, LSF chunk 41-59)
    B6 = np.concatenate([blocks_until(load_bits(pd, s), 54, 60, W) for s in H600 if s != "melp600"])
    s6, _, t6 = segments(B6)
    w6 = [len(g) for g in s6]
    print("  600H calibration (%d blocks, |phi| >= %.3f): %d segments, %d single columns: %s"
          % (len(B6), t6, len(s6), sum(1 for w in w6 if w == 1), fmt(s6)))
    found = widths == melp
    weak = sum(1 for w in w6 if w == 1) > len(s6) / 2
    print("  -> V6 MELPe 2400 widths as contiguous fields: %s"
          % ("SUPPORTED" if found else
             "NOT DETERMINABLE (the method splits 600H's known fields into single columns too)" if weak
             else "CONTRADICTED"))


PITCH = [2, 13, 14, 20, 10, 12, 16]  # RFC 8130 B_3, B_14, B_15, B_21, B_11, B_13, B_17


def v7(F):
    print("\nV7 standard MELPe 2400 under each rotation / reversal of the frame")
    res = []
    for rev in (False, True):
        for r in range(54):
            G = F[:, ::-1] if rev else F
            G = np.roll(G, -r, axis=1)
            w = G[:, PITCH].sum(axis=1)
            res.append(((w == 2).mean(), r, rev, (G[1:, 53] != G[:-1, 53]).mean()))
    sync = [x for x in res if x[3] >= 0.9]
    for e, r, rev, _ in sync:
        print("  sync at B_54 (rotation %d%s): two-bit pitch codes %.1f %%"
              % (r, ", reversed" if rev else "", 100 * e))
    best = min(res)
    print("  lowest share over all 108 readings: %.1f %% (rotation %d%s); STANAG coder 0.0 %%, "
          "random bits 16.4 %%" % (100 * best[0], best[1], ", reversed" if best[2] else ""))
    print("  -> V7: %s" % ("CONTRADICTED" if best[0] > 0.02 else "see table"))


def v8(F, a):
    print("\nV8 cols 4, 6, 7, 8, 16, 17, 18 all zero as an unvoiced pitch code")
    Z = [4, 6, 7, 8, 16, 17, 18]
    z = (F[:, Z].sum(axis=1) == 0)
    print("  zero-code frames: %d of %d (%.3f)" % (z.sum(), len(F), z.mean()))

    def mean_run(v):
        runs, k = [], 0
        for b in v:
            if b:
                k += 1
            elif k:
                runs.append(k); k = 0
        if k:
            runs.append(k)
        return np.mean(runs) if runs else 0.0, len(runs)

    o, nr = mean_run(z)
    null = np.array([mean_run(z[RNG.permutation(len(z))])[0] for _ in range(N_SHUF)])
    zs = (o - null.mean()) / null.std()
    print("  mean run of zero-code frames %.2f frames (%d runs); shuffled %.2f +- %.2f; z %+.1f"
          % (o, nr, null.mean(), null.std(), zs))
    others = [c for c in range(54) if c not in Z and c != a]
    d = []
    for c in others:
        p0, p1 = F[z, c].mean(), F[~z, c].mean()
        p = F[:, c].mean()
        se = np.sqrt(p * (1 - p) * (1 / z.sum() + 1 / (~z).sum())) if 0 < p < 1 else 1
        d.append(((p0 - p1) / se, c, p0, p1))
    d.sort(key=lambda x: -abs(x[0]))
    print("  other columns most shifted in zero-code frames (z, col, p1 zero-code / rest): "
          + ", ".join("%+.1f c%d %.2f/%.2f" % x for x in d[:8]))
    print("  columns shifted at |z| >= 3: %d of %d" % (sum(1 for x in d if abs(x[0]) >= 3), len(d)))
    print("  -> V8 zero code clusters in time: %s; shifts other columns: %s"
          % ("SUPPORTED" if zs >= 3 else "NOT SUPPORTED",
             "SUPPORTED" if sum(1 for x in d if abs(x[0]) >= 3) >= 3 else "NOT SUPPORTED"))


def main(pd):
    s, V, F = voice_frames(pd)
    print("voice bits %d, frames %d x 54" % (len(V), len(F)))
    v1(V)
    a = v2(s, F)
    v3(F, a)
    v4(F, a)
    v5(F, a, pd)
    v6(F, a, pd)
    v7(F)
    v8(F, a)


if __name__ == "__main__":
    main(sys.argv[1])
