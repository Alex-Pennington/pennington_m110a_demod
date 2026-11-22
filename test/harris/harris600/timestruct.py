"""Time-structure test of decoded MELPe: original frame order against shuffled frames.

Decodes frames with the external reference coder (-m S), then measures on the audio:
  track    melpe_frames.speech_measure track (steady pitch contour)
  ac1E     lag-1 correlation of the 22.5 ms log-energy track (energy moves smoothly)
  mod      share of the log-energy modulation spectrum (0.5-20 Hz) in 2-8 Hz, the
           syllabic rate
  pause    share of 22.5 ms frames more than 25 dB below the 95th-percentile frame
Shuffling whole frames keeps every frame's content and the sync alternation (re-set after
the shuffle) and destroys the order. A real speech decode should beat its shuffles on ac1E
and mod; any steady sound passes track whatever the order.

Writes per-frame tracks (time s, log energy dB, f0 Hz or 0) of each original-order decode
to WORK/<name>.track.txt for the cross-check.

python3 -I -B test/harris/harris600/timestruct.py PAYLOAD_DIR MELPE WORK [SPEECH.raw ...]
"""
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import load_bits
from colprofile import voice_frames, encode
import melpe_frames as mf

HOP, WIN = 180, 320


def decode(melpe, F, rate, work, tag):
    src = os.path.join(work, tag + ".bit")
    dst = os.path.join(work, tag + ".pcm")
    with open(src, "wb") as f:
        f.write(mf.pack_frames(F, rate))
    subprocess.run([melpe, "-q", "-r", str(rate), "-m", "S", "-i", src, "-o", dst], check=True,
                   capture_output=True)
    return mf.read_raw(dst)


def tracks(x):
    x = np.asarray(x, dtype=float)
    n = (len(x) - WIN) // HOP + 1
    idx = np.arange(WIN)[None, :] + HOP * np.arange(n)[:, None]
    fr = x[idx] - x[idx].mean(1, keepdims=True)
    e = 10 * np.log10((fr ** 2).mean(1) + 1.0)
    seg = WIN - mf.LAG_MAX
    a = fr[:, :seg]
    ea = (a ** 2).sum(1)
    rmax = np.zeros(n); lag = np.zeros(n, dtype=int)
    for L in range(mf.LAG_MIN, mf.LAG_MAX + 1):
        b = fr[:, L:L + seg]
        r = (a * b).sum(1) / np.sqrt(ea * (b ** 2).sum(1) + 1e-12)
        k = r > rmax; rmax[k] = r[k]; lag[k] = L
    ref = np.percentile(e, 95)
    f0 = np.where((rmax > mf.VOICED_R) & (e > ref - 40), mf.FS / np.maximum(lag, 1), 0.0)
    return e, f0


def measures(x):
    e, f0 = tracks(x)
    ac1 = float(np.corrcoef(e[:-1], e[1:])[0, 1])
    d = e - e.mean()
    P = np.abs(np.fft.rfft(d * np.hanning(len(d)))) ** 2
    fq = np.fft.rfftfreq(len(d), HOP / mf.FS)
    band = (fq >= 0.5) & (fq <= 20)
    syl = (fq >= 2) & (fq <= 8)
    mod = float(P[syl].sum() / P[band].sum())
    pause = float((e < np.percentile(e, 95) - 25).mean())
    return {"track": mf.speech_measure(x)["track"], "ac1E": ac1, "mod": mod, "pause": pause}


def shuffled(F, rate, rng):
    S = F[rng.permutation(len(F))].copy()
    si = mf.sync_index(rate)
    if si is not None:
        S[:, si] = np.arange(len(S)) % 2 ^ F[0, si]
    return S


def run(melpe, name, F, rate, work, nshuf=12):
    rng = np.random.default_rng(9)
    x = decode(melpe, F, rate, work, name)
    m = measures(x)
    e, f0 = tracks(x)
    with open(os.path.join(work, name + ".track.txt"), "w") as f:
        for i in range(len(e)):
            f.write("%.4f %.2f %.1f\n" % (i * HOP / mf.FS, e[i], f0[i]))
    sh = [measures(decode(melpe, shuffled(F, rate, rng), rate, work, name + "_sh")) for _ in range(nshuf)]
    out = []
    for k in ("track", "ac1E", "mod", "pause"):
        v = np.array([s[k] for s in sh])
        out.append("%s %.3f (shuffled %.3f +- %.3f, z %+.1f)" % (k, m[k], v.mean(), v.std(),
                   (m[k] - v.mean()) / (v.std() + 1e-9)))
    print("%-26s %4d frames, %.1f s\n     %s" % (name, len(F), len(x) / mf.FS, "\n     ".join(out)))


def main(pd, melpe, work, *raws):
    os.makedirs(work, exist_ok=True)
    for s in ("1200_1", "1200_2", "1200_3"):
        run(melpe, s, voice_frames(load_bits(pd, s), 1200, 54), 1200, work)
    run(melpe, "melp2400", voice_frames(load_bits(pd, "melp2400"), 2400, 6), 2400, work)
    for r in raws:
        for rate in (1200, 2400):
            run(melpe, "cal_%s_%d" % (os.path.basename(r).split(".")[0], rate), encode(melpe, rate, r, work), rate, work)
    run(melpe, "random_1200", mf.random_frames(1200, 110, seed=2), 1200, work)
    run(melpe, "random_2400", mf.random_frames(2400, 280, seed=2), 2400, work)


if __name__ == "__main__":
    main(*sys.argv[1:])
