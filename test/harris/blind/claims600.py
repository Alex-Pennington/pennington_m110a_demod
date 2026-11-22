"""Test each claim US 6,917,914 B2 and US 8,589,151 B2 make about the 600H block against the
bits m110a_rx recovers from the 600H recordings.

Block columns are 0..59 in stream order from stream bit 54 + 60k; bitbuf[k] in the patent's
Block 110/203 is taken as column k (claim T1 tests that reading against its mirror).

Each test prints one line per stream group: the 199 whole blocks of the six 600_x messages
("600x") and the 81 of melp600 ("m600"). z is against N_SHUF shuffles of the blocks inside
each message (block order destroyed, block contents kept) unless the line says otherwise.

  T1  transmission order: bitbuf[0] first, or the 60-bit block mirrored.
  T2  field widths/order in cols 19-40: every permutation of (4, 7, 11).
  T3  spectrum chunks 0-18 and 41-59 index one codebook pair, same bit orientation.
  T4  spectrum frame pairs: chunk B of block n continues into chunk A of block n+1.
  T5  stage boundary inside each chunk: 10 | 9.
  T6  bit order inside fields (LSB-first): ordering smoothness under each order.
  T7  voicing: bpv_index against the pitch pause code and against gain popcount.
  T8  pitch searched on voiced frames only: pitch index in bpv-0 blocks.
  T9  no sync bit, no jitter/Fourier bits: no alternating column.
  T10 [2] col. 20 "four-stage" reading (10,10,9,9 over four frames) against [1]'s two
      frame-pair two-stage VQs: covered by T3/T4.

python3 -I -B test/harris/blind/claims600.py PAYLOAD_DIR
"""
import itertools
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "harris600"))
from common import H600, load_all  # noqa: E402

N_SHUF = 200
RNG = np.random.default_rng(600)


def blocks_of(payload):
    data = load_all(payload, H600)
    g = {"600x": [], "m600": []}
    for s in H600:
        b = data[s][54:]
        # the trailer starts on the block grid (docs/harris-600-format.md s2); keep the
        # whole blocks before it
        tr = trailer_start(b)
        n = tr // 60
        R = b[: n * 60].reshape(n, 60).astype(np.int64)
        g["m600" if s == "melp600" else "600x"].append(R)
    return g


W = "101001010010100010100000000100010101000101110011111101"


def trailer_start(b):
    flat = "".join(map(str, b.tolist()))
    k = flat.find(W)
    if k < 0:
        k = flat.find("1" * 108)
    if k < 0 or k % 60:
        raise SystemExit("trailer not found on the 60-bit grid")
    return k


def val(R, cols):
    """LSB-first: cols[0] is weight 1."""
    v = np.zeros(len(R), np.int64)
    for k, c in enumerate(cols):
        v |= R[:, c] << k
    return v


def hold_rate(msgs, cols):
    """Consecutive-block repeats of the field value, summed over messages."""
    h = 0
    for R in msgs:
        v = val(R, cols)
        h += int((v[1:] == v[:-1]).sum())
    return h


def shuffled(msgs):
    return [R[RNG.permutation(len(R))] for R in msgs]


def zscore(obs, fn, msgs):
    null = np.array([fn(shuffled(msgs)) for _ in range(N_SHUF)], float)
    sd = null.std() or 1e-9
    return obs, null.mean(), (obs - null.mean()) / sd


def fmt(name, o, m, z):
    return f"{name:<44s} obs {o:8.3f}  shuffled {m:8.3f}  z {z:+6.1f}"


def dependence(msgs, cols):
    """Mean |r| between distinct columns of a field over all blocks."""
    X = np.vstack(msgs)[:, cols].astype(float)
    sd = X.std(axis=0)
    keep = sd > 0
    X = X[:, keep]
    if X.shape[1] < 2:
        return 0.0
    C = np.corrcoef(X.T)
    iu = np.triu_indices(len(C), 1)
    return float(np.abs(C[iu]).mean())


def T1(g):
    print("T1 transmission order (7-bit pitch field holds; mirror = bitbuf[59] sent first)")
    for k, msgs in g.items():
        for name, cols in (("pitch cols 23-29 (patent)", range(23, 30)),
                           ("pitch mirrored cols 30-36", range(30, 37)),
                           ("bpv cols 19-22 (patent)", range(19, 23)),
                           ("bpv mirrored cols 37-40", range(37, 41))):
            cols = list(cols)
            o, m, z = zscore(hold_rate(msgs, cols), lambda M: hold_rate(M, cols), msgs)
            print("  " + k + " " + fmt(name, o, m, z))


def T2(g):
    print("T2 field order in cols 19-40: within-field mean |r| minus cross-field mean |r|")
    for k, msgs in g.items():
        X = np.vstack(msgs)[:, 19:41].astype(float)
        C = np.abs(np.nan_to_num(np.corrcoef(X.T)))
        res = []
        for perm in itertools.permutations((4, 7, 11)):
            lab = np.repeat(np.arange(3), perm)
            same = lab[:, None] == lab[None, :]
            off = ~np.eye(22, dtype=bool)
            res.append((C[same & off].mean() - C[~same].mean(), perm))
        res.sort(reverse=True)
        print("  " + k + " " + "  ".join(f"{p}:{s:+.4f}" for s, p in res))


def T3(g):
    print("T3 spectrum chunks share codebooks and orientation")
    for k, msgs in g.items():
        X = np.vstack(msgs).astype(float)
        p = X.mean(axis=0)
        a, b = p[0:19], p[41:60]
        print(f"  {k} ones-profile r(0-18, 41-59) same orientation {np.corrcoef(a, b)[0,1]:+.3f}"
              f"   reversed {np.corrcoef(a, b[::-1])[0,1]:+.3f}"
              f"   r(0-18, 19-37) {np.corrcoef(a, p[19:38])[0,1]:+.3f}")
        # same-block equality of stage-1 indices
        def eq_same(M):
            return float(np.mean(np.concatenate(
                [val(R, range(0, 10)) == val(R, range(41, 51)) for R in M])))

        def eq_shift(M):
            # chunk B of block n against chunk A of block n+1 (adjacent frame pairs)
            return float(np.mean(np.concatenate(
                [val(R[:-1], range(41, 51)) == val(R[1:], range(0, 10)) for R in M])))

        def eq_far(M):
            # chunk A of block n against chunk A of block n+1 (100 ms apart)
            return float(np.mean(np.concatenate(
                [val(R[:-1], range(0, 10)) == val(R[1:], range(0, 10)) for R in M])))

        def cross_null(M):
            # chunk A against chunk B of a random other block in the same message
            out = []
            for R in M:
                q = RNG.permutation(len(R))
                out.append(val(R, range(0, 10)) == val(R[q], range(41, 51)))
            return float(np.mean(np.concatenate(out)))

        nulls = np.array([cross_null(msgs) for _ in range(N_SHUF)])
        for name, fn in (("stage1 A[n]==B[n]", eq_same), ("stage1 B[n]==A[n+1]", eq_shift),
                         ("stage1 A[n]==A[n+1]", eq_far)):
            o = fn(msgs)
            print("  " + k + " " + fmt(name + " (null: A vs B of random block)", o,
                                        nulls.mean(), (o - nulls.mean()) / (nulls.std() or 1e-9)))
        # stage-1 value sets: Jaccard of values used by chunk A and chunk B
        va = set(np.concatenate([val(R, range(0, 10)) for R in msgs]).tolist())
        vb = set(np.concatenate([val(R, range(41, 51)) for R in msgs]).tolist())
        vm = set(np.concatenate([val(R, range(19, 29)) for R in msgs]).tolist())
        print(f"  {k} stage1 distinct values A {len(va)} B {len(vb)} shared {len(va & vb)};"
              f" control cols 19-28 shared with A {len(va & vm)} (of {len(vm)})")


def T5(g):
    print("T5 stage boundary: equality excess of the first s bits, A[n] vs B[n] and B[n] vs A[n+1]")
    for k, msgs in g.items():
        line = []
        for s in (8, 9, 10, 11, 12):
            def f(M, s=s):
                e = []
                for R in M:
                    a, b = val(R, range(0, s)), val(R, range(41, 41 + s))
                    e.append(a == b)
                    e.append(b[:-1] == a[1:])
                return float(np.mean(np.concatenate(e)))

            def null(M, s=s):
                e = []
                for R in M:
                    q = RNG.permutation(len(R))
                    e.append(val(R, range(0, s)) == val(R[q], range(41, 41 + s)))
                return float(np.mean(np.concatenate(e)))
            o = f(msgs)
            nl = np.array([null(msgs) for _ in range(N_SHUF)])
            line.append(f"{s}:z{(o - nl.mean()) / (nl.std() or 1e-9):+.1f}")
        # suffix test: last 9 bits of each chunk (patent stage 2)
        def suf(M):
            return float(np.mean(np.concatenate(
                [val(R, range(10, 19)) == val(R, range(51, 60)) for R in M])))

        def sufnull(M):
            out = []
            for R in M:
                q = RNG.permutation(len(R))
                out.append(val(R, range(10, 19)) == val(R[q], range(51, 60)))
            return float(np.mean(np.concatenate(out)))
        o = suf(msgs)
        nl = np.array([sufnull(msgs) for _ in range(N_SHUF)])
        print(f"  {k} split " + "  ".join(line) +
              f"   stage2 (cols 10-18 vs 51-59) same block z{(o - nl.mean()) / (nl.std() or 1e-9):+.1f}")


def smooth(msgs, cols, msb):
    c = list(cols)[::-1] if msb else list(cols)
    num, den = [], []
    for R in msgs:
        v = val(R, c).astype(float)
        d = np.abs(np.diff(v))
        d = d[d > 0]
        if len(d) == 0:
            continue
        num.append(d.mean())
        den.append(np.abs(v[:, None] - v[None, :])[np.triu_indices(len(v), 1)].mean())
    return float(np.mean(num) / np.mean(den))


def T6(g):
    print("T6 bit order: mean |d index| of consecutive distinct values / all pairs (lower = ordered)")
    for k, msgs in g.items():
        for name, cols in (("pitch", range(23, 30)), ("gain", range(30, 41)),
                           ("bpv", range(19, 23)), ("stage1 A", range(0, 10))):
            for msb in (False, True):
                o, m, z = zscore(smooth(msgs, cols, msb), lambda M: smooth(M, cols, msb), msgs)
                print("  " + k + " " + fmt(f"{name} {'MSB' if msb else 'LSB'}-first", o, m, z))


def T7_T8(g):
    print("T7/T8 voicing, pitch and gain")
    for k, msgs in g.items():
        X = np.vstack(msgs)
        bpv = val(X, range(19, 23))
        pit = val(X, range(23, 30))
        gpc = X[:, 30:41].sum(axis=1)
        hist = np.bincount(bpv, minlength=16)
        print(f"  {k} bpv histogram {hist.tolist()} (values used {int((hist > 0).sum())}/16)")
        print(f"  {k} pitch values used {len(set(pit.tolist()))}/128; commonest "
              f"{np.bincount(pit).argmax()} ({np.bincount(pit).max()} of {len(pit)})")
        for b in range(16):
            sel = bpv == b
            if sel.sum() >= 3:
                pv = pit[sel]
                top = np.bincount(pv).argmax()
                print(f"  {k}   bpv {b:2d}: n {sel.sum():3d}  pitch=35 {int((pv == 35).sum()):3d}"
                      f"  commonest pitch {top} x{int((pv == top).sum())}"
                      f"  distinct pitch {len(set(pv.tolist())):3d}  gain popcount {gpc[sel].mean():.2f}")
        sel = pit == 35
        print(f"  {k} pitch==35 blocks {int(sel.sum())}: bpv {np.bincount(bpv[sel], minlength=16).tolist()}")


def T9(g):
    print("T9 alternating columns (lag-1 autocorrelation per column)")
    for k, msgs in g.items():
        ac = []
        for c in range(60):
            xs = np.concatenate([R[1:, c] - R[1:, c].mean() for R in msgs]).astype(float)
            ys = np.concatenate([R[:-1, c] - R[:-1, c].mean() for R in msgs]).astype(float)
            ac.append(float((xs * ys).sum() / (np.sqrt((xs ** 2).sum() * (ys ** 2).sum()) or 1)))
        ac = np.array(ac)
        print(f"  {k} min lag-1 {ac.min():+.2f} at col {int(ac.argmin())}; max {ac.max():+.2f} at col {int(ac.argmax())}")


def main():
    g = blocks_of(sys.argv[1])
    print("blocks:", {k: [len(R) for R in v] for k, v in g.items()})
    T1(g)
    T2(g)
    T3(g)
    T5(g)
    T6(g)
    T7_T8(g)
    T9(g)


if __name__ == "__main__":
    main()
