"""Task C: blind partial decode of 600H under the layout of US 6,917,914.

Layout (read from US 6,917,914 B2, decoder Block 203; every index LSB first, bitbuf[k] is
block column k):
  spect1_stage1  cols  0- 9  LSF_CB1 (1024), frames 0-1
  spect1_stage2  cols 10-18  LSF_CB2 (512),  frames 0-1
  bpv_index      cols 19-22  BPV_CB (16)
  pitch_index    cols 23-29  PITCH_CB (128), 4 frames
  gain_index     cols 30-40  GAIN_CB (2048), G1/G2 of 4 frames
  spect2_stage1  cols 41-50  LSF_CB1, frames 2-3
  spect2_stage2  cols 51-59  LSF_CB2, frames 2-3

Measures, over the six 600_x messages (and melp600 reported apart):
  shared codebook  spect1 and spect2 index the same LSF_CB1/LSF_CB2: equal indices inside a
                   block, and spect2[n] == spect1[n+1], against shuffled blocks.
  ordering         mean |d index| over consecutive non-pause blocks (equal pairs dropped)
                   divided by the mean over all pairs of the message, for four maps
                   (binary/Gray x LSB-first/MSB-first); baseline: blocks shuffled.
  split tree       mean Hamming distance of consecutive indices against shuffled
                   (map-free: LBG splitting puts similar entries at nearby bit patterns).
  pitch scale      if PITCH_CB were sorted and log-uniform over MELP lags 20-160 samples,
                   the f0 the used indices imply, against the 1200H talker (93-145 Hz).
  pause/voicing    bpv and gain indices in pause blocks (cols 22-29 = 01100010) and others.

python3 -I -B test/harris/blind/fields600.py PAYLOAD_DIR
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "harris600"))
from common import H600, load_all  # noqa: E402
from structure import voice_blocks  # noqa: E402

FIELDS = {"s1a": (0, 10), "s2a": (10, 9), "bpv": (19, 4), "pitch": (23, 7), "gain": (30, 11),
          "s1b": (41, 10), "s2b": (51, 9)}
PAUSE = [0, 1, 1, 0, 0, 0, 1, 0]  # cols 22..29


def index(R, name, order="lsb"):
    p, w = FIELDS[name]
    cols = list(range(p, p + w))
    if order == "msb":
        cols = cols[::-1]
    v = np.zeros(len(R), np.int64)
    for k, c in enumerate(cols):
        v |= R[:, c].astype(np.int64) << k
    return v


def gray_decode(g):
    b = g.copy()
    s = g >> 1
    while s.any():
        b ^= s
        s >>= 1
    return b


def is_pause(R):
    return np.all(R[:, 22:30] == PAUSE, axis=1)


def popcount(x):
    return np.array([bin(int(v)).count("1") for v in x])


def shared_codebook(per, rng, nshuf=200):
    def stat(per):
        same = nxt = n = m = 0
        for R in per:
            for a, b in (("s1a", "s1b"), ("s2a", "s2b")):
                A, B = index(R, a), index(R, b)
                same += (A == B).sum()
                nxt += (B[:-1] == A[1:]).sum()
            n += 2 * len(R); m += 2 * (len(R) - 1)
        return same / n, nxt / m
    obs = stat(per)
    sh = []
    for _ in range(nshuf):
        sp = []
        for R in per:
            R2 = R.copy()
            R2[:, 41:60] = R[rng.permutation(len(R)), 41:60]
            sp.append(R2)
        sh.append(stat(sp))
    sh = np.array(sh)
    print("shared LSF codebooks: P(spect1 == spect2 in a block) %.3f (shuffled %.3f +- %.3f); "
          "P(spect2[n] == spect1[n+1]) %.3f (shuffled %.3f +- %.3f)"
          % (obs[0], sh[:, 0].mean(), sh[:, 0].std(), obs[1], sh[:, 1].mean(), sh[:, 1].std()))


def stage_equal(per, rng, nshuf=200):
    """Equality of chunk-A and chunk-B sub-windows, split at each candidate boundary:
    the stage boundary is where both halves show excess equality."""
    for split in (8, 9, 10, 11, 12):
        res = []
        for lo, hi in ((0, split), (split, 19)):
            def stat(per):
                n = m = 0
                for R in per:
                    A = R[:, lo:hi]; B = R[:, 41 + lo:41 + hi]
                    n += np.all(B[:-1] == A[1:], axis=1).sum() + np.all(A == B, axis=1).sum()
                    m += 2 * len(R) - 1
                return n / m
            obs = stat(per)
            sh = []
            for _ in range(nshuf):
                sp = []
                for R in per:
                    R2 = R.copy(); R2[:, 41:60] = R[rng.permutation(len(R)), 41:60]; sp.append(R2)
                sh.append(stat(sp))
            res.append("cols %d-%d: %.3f (sh %.3f, z %+.1f)" % (lo, hi - 1, obs, np.mean(sh),
                       (obs - np.mean(sh)) / (np.std(sh) + 1e-9)))
        print("   stage split at %2d: %s" % (split, "; ".join(res)))


def level_proxy(per, rng, nshuf=200):
    """Popcount of the gain index as a level proxy: its lag-1 correlation against shuffled
    blocks, and its mean in pause and bpv=0 blocks."""
    def ac(per, name, nonpause):
        x = []; y = []
        for R in per:
            p = popcount(index(R, name)).astype(float)
            k = ~is_pause(R) if nonpause else np.ones(len(R), bool)
            kk = k[1:] & k[:-1]
            x += list(p[:-1][kk]); y += list(p[1:][kk])
        return np.corrcoef(x, y)[0, 1]
    for name in ("gain", "pitch", "bpv", "s1a", "s2a", "s1b", "s2b"):
        parts = []
        for npo in (False, True):
            obs = ac(per, name, npo)
            sh = [ac([R[rng.permutation(len(R))] for R in per], name, npo) for _ in range(nshuf)]
            parts.append("%s %.3f (shuffled %.3f, z %+.1f)" % ("non-pause" if npo else "all", obs,
                         np.mean(sh), (obs - np.mean(sh)) / (np.std(sh) + 1e-9)))
        print("%-5s popcount lag-1 correlation: %s" % (name, "; ".join(parts)))


def ordering(per, name, rng, nshuf=40):
    def ratio(vals_per):
        num = nn = den = nd = 0.0
        for v, keep in vals_per:
            v = v.astype(float)
            d = np.abs(np.diff(v))[keep[1:] & keep[:-1]]
            d = d[d > 0]
            num += d.sum(); nn += len(d)
            vv = v[keep]
            D = np.abs(vv[:, None] - vv[None, :])[np.triu_indices(len(vv), 1)]
            D = D[D > 0]
            den += D.sum(); nd += len(D)
        return (num / max(nn, 1)) / (den / max(nd, 1))

    def ham(vals_per):
        a = b = 0.0
        for v, keep in vals_per:
            k = keep[1:] & keep[:-1]
            d = popcount(v[1:] ^ v[:-1])[k]
            d = d[d > 0]
            a += d.sum(); b += len(d)
        return a / max(b, 1)

    res = {}
    for order in ("lsb", "msb"):
        for code in ("binary", "gray"):
            vp = []
            for R in per:
                v = index(R, name, order)
                if code == "gray":
                    v = gray_decode(v)
                vp.append((v, ~is_pause(R)))
            obs = ratio(vp)
            sh = []
            for _ in range(nshuf):
                sp = []
                for v, k in vp:
                    p = rng.permutation(len(v)); sp.append((v[p], k[p]))
                sh.append(ratio(sp))
            res[(order, code)] = (obs, np.mean(sh), np.std(sh))
    vp = [(index(R, name), ~is_pause(R)) for R in per]
    hobs = ham(vp)
    hsh = []
    for _ in range(nshuf):
        sp = []
        for v, k in vp:
            p = rng.permutation(len(v)); sp.append((v[p], k[p]))
        hsh.append(ham(sp))
    print("%s ordering (ratio of consecutive to all-pair |d|, equal pairs dropped, non-pause):" % name)
    for (o, c), (r, m, s) in res.items():
        print("   %-3s %-6s %.3f (shuffled %.3f +- %.3f, z %+.1f)" % (o, c, r, m, s, (r - m) / (s + 1e-9)))
    print("   Hamming distance of consecutive unequal indices %.3f (shuffled %.3f +- %.3f, z %+.1f)"
          % (hobs, np.mean(hsh), np.std(hsh), (hobs - np.mean(hsh)) / (np.std(hsh) + 1e-9)))
    # per-bit stability
    st = []
    for b in range(FIELDS[name][1]):
        same = tot = 0
        for v, k in vp:
            kk = k[1:] & k[:-1]
            x = ((v >> b) & 1)
            same += (x[1:] == x[:-1])[kk].sum(); tot += kk.sum()
        st.append(same / max(tot, 1))
    print("   per-bit hold rate (bit 0 = LSB, col %d): %s" % (FIELDS[name][0], " ".join("%.2f" % s for s in st)))
    return res


def pitch_scale(per, order, code):
    vals = []
    for R in per:
        v = index(R, "pitch", order)
        if code == "gray":
            v = gray_decode(v)
        vals.append(v[~is_pause(R)])
    v = np.concatenate(vals)
    lag = 20.0 * 8.0 ** (v / 127.0)
    f0 = 8000.0 / lag
    print("pitch scale, sorted log-uniform 20-160 sample hypothesis (%s %s): f0 median %.0f Hz, "
          "10-90%% %.0f-%.0f Hz; indices used %d of 128, median %d, 10-90%% %d-%d"
          % (order, code, np.median(f0), np.percentile(f0, 10), np.percentile(f0, 90),
             len(set(v.tolist())), np.median(v), np.percentile(v, 10), np.percentile(v, 90)))


def pause_profile(per):
    P = np.concatenate([is_pause(R) for R in per])
    for name in ("bpv", "gain", "pitch"):
        v = np.concatenate([index(R, name) for R in per])
        hp = np.bincount(v[P], minlength=1 << FIELDS[name][1])
        ho = np.bincount(v[~P], minlength=1 << FIELDS[name][1])
        top_p = np.argsort(-hp)[:5]
        top_o = np.argsort(-ho)[:6]
        w = FIELDS[name][1]
        print("%s index (LSB-first): pause blocks %d, top %s; other blocks %d, top %s; "
              "mean popcount pause %.2f, other %.2f (of %d)"
              % (name, P.sum(), [(int(i), int(hp[i])) for i in top_p if hp[i]], (~P).sum(),
                 [(int(i), int(ho[i])) for i in top_o if ho[i]],
                 popcount(v[P]).mean(), popcount(v[~P]).mean(), w))


def bpv_gain(per):
    """Gain bits against voicing: if bpv index 0 is the all-unvoiced block, gain should be
    lower there. Report per-gain-bit share of ones by bpv value class."""
    v = np.concatenate([index(R, "bpv") for R in per])
    G = np.concatenate([R[:, 30:41] for R in per])
    for cls, sel in (("bpv=0", v == 0), ("bpv!=0", v != 0)):
        print("   gain cols 30-40 share of ones, %-7s (%3d blocks): %s"
              % (cls, sel.sum(), " ".join("%.2f" % x for x in G[sel].mean(0))))


def main(pd):
    st = load_all(pd, H600)
    rng = np.random.default_rng(7)
    groups = {"600_x (six messages)": [voice_blocks(st[s]) for s in H600 if s != "melp600"],
              "melp600": [voice_blocks(st["melp600"])]}
    for g, per in groups.items():
        print("==== %s: %d blocks" % (g, sum(len(R) for R in per)))
        shared_codebook(per, rng)
        stage_equal(per, rng)
        level_proxy(per, rng)
        for name in ("pitch", "gain", "bpv", "s1a", "s1b"):
            ordering(per, name, rng)
        for o in ("lsb", "msb"):
            for c in ("binary", "gray"):
                pitch_scale(per, o, c)
        pause_profile(per)
        bpv_gain(per)


if __name__ == "__main__":
    main(*sys.argv[1:])
