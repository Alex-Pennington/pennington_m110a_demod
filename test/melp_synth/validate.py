#!/usr/bin/env python3
"""Measure the MELP parameter-domain synthesiser against original speech and a MELPe coder.

Test-only analysis path (not a MELP encoder: no quantisation, no bitstream), written from
NATO STANAG 4591 Ed. 1 Annex A.5.2: per frame, a 10th-order LPC from a 200-sample Hamming
window centred on the frame's last sample (bandwidth expansion 0.994), LSFs from the
roots of P(z) and Q(z), integer pitch by normalised autocorrelation (Eq. A-1) with a
doubling check, five band voicing strengths (6th-order Butterworth bands 0-500, 500-1000,
1000-2000, 2000-3000, 3000-4000 Hz, band envelope less 0.1), peakiness forcing, the
aperiodic flag, two pitch-adaptive gains (Eq. A-6), and ten Fourier magnitudes of the
residual (A.5.2.18). Parameters are written in melp_synth's text format and synthesised.

Usage:
  python3 -I -B test/melp_synth/validate.py --synth build/bin/melp_synth --work DIR \\
      [--melpe CODER] [--measures melpe_frames.py] RAW [RAW ...]

RAW: 8 kHz s16le speech. --melpe: an external MELPe program taking
`-r 2400 -m C -i in -o out` (run as a program only; its decode is a comparison). Optional
--measures: a path to test/harris/melpe_frames.py (feat/harris-melpe) whose speech
measures are reported too. Needs numpy and scipy. Prints one table; writes nothing
outside --work.

Copyright (C) the pennington_m110a_demod authors. Licensed under the GNU General Public
License v3.0, the same as this repository's LICENSE file. SPDX-License-Identifier: GPL-3.0-only
"""
import argparse
import importlib.util
import os
import subprocess
import sys

import numpy as np
from scipy.signal import butter, lfilter, sosfilt

FS = 8000


# ---- analysis (STANAG 4591 A.5.2, simplified, unquantised) ---------------------------

def levinson(r, p):
    a = np.zeros(p + 1)
    a[0] = 1.0
    e = r[0]
    for m in range(1, p + 1):
        k = -(r[m] + np.dot(a[1:m], r[m - 1:0:-1])) / e
        a[1:m] = a[1:m] + k * a[m - 1:0:-1]
        a[m] = k
        e *= 1 - k * k
        if e <= 0:
            break
    return a  # A(z) = 1 + sum a_i z^-i


def lpc_to_lsf(a):
    ae = np.concatenate([a, [0.0]])
    P = ae + ae[::-1]
    Q = ae - ae[::-1]
    w = []
    for poly in (P, Q):
        ang = np.angle(np.roots(poly))
        w += [x for x in ang if 1e-4 < x < np.pi - 1e-4]
    w = np.sort(np.array(w))
    if len(w) != 10:
        return None
    return w * FS / (2 * np.pi)


def ncorr(x, c, tau):
    """Eq. A-1/A-2: normalised autocorrelation at lag tau, window centred on sample c."""
    lo = c - tau // 2 - 80
    hi = c - tau // 2 + 80
    if lo < 0 or hi + tau > len(x):
        return 0.0
    a = x[lo:hi]
    b = x[lo + tau:hi + tau]
    d = np.sqrt(np.dot(a, a) * np.dot(b, b))
    return float(np.dot(a, b) / d) if d > 0 else 0.0


def best_lag(x, c, lo, hi):
    lags = range(max(lo, 20), min(hi, 160) + 1)
    r = [ncorr(x, c, t) for t in lags]
    i = int(np.argmax(r))
    return list(lags)[i], r[i]


def envelope_filter(x):
    # zero at DC, complex pole pair at 150 Hz, radius 0.97 (A.5.2.4)
    th = 2 * np.pi * 150 / FS
    return lfilter([1, -1], [1, -2 * 0.97 * np.cos(th), 0.97 ** 2], x)


class Analyzer:
    def __init__(self, x, N):
        self.N = N
        hp = butter(2, 60, "highpass", fs=FS, output="sos")
        pad = 400
        self.x = np.concatenate([np.zeros(pad), sosfilt(hp, x.astype(float)), np.zeros(pad)])
        self.pad = pad
        self.lp1k = sosfilt(butter(6, 1000, fs=FS, output="sos"), self.x)
        edges = [(0, 500), (500, 1000), (1000, 2000), (2000, 3000), (3000, 4000)]
        self.bands, self.envs = [], []
        for lo, hi in edges:
            if lo == 0:
                sos = butter(6, hi, fs=FS, output="sos")
            elif hi == 4000:
                sos = butter(6, lo, "highpass", fs=FS, output="sos")
            else:
                sos = butter(3, [lo, hi], "bandpass", fs=FS, output="sos")
            b = sosfilt(sos, self.x)
            self.bands.append(b)
            self.envs.append(envelope_filter(np.abs(b)))
        self.hamming200 = np.hamming(200)
        self.pavg = 50.0
        self.n_frames = (len(x)) // N

    def frame(self, k):
        N, x = self.N, self.x
        c = self.pad + (k + 1) * N - 1  # last sample of frame k
        # LPC
        seg = x[c - 100:c + 100] * self.hamming200
        r = np.array([np.dot(seg[:200 - i], seg[i:]) for i in range(11)])
        r[0] *= 1.0001
        r[0] += 1e-3
        a = levinson(r, 10) * (0.994 ** np.arange(11))
        lsf = lpc_to_lsf(a)
        if lsf is None:
            lsf = np.linspace(1, 10, 10) * 4000 / 11
        # pitch: integer search on the 1 kHz low-passed speech, then a doubling check
        P, rP = best_lag(self.lp1k, c, 40, 160)
        for kk in range(8, 1, -1):
            Pk = int(round(P / kk))
            if Pk >= 20:
                Pk2, rk = best_lag(self.lp1k, c, Pk - 2, Pk + 2)
                if rk > 0.75 * rP:
                    P, rP = Pk2, rk
                    break
        # band voicing at lag P (band 1 refines the lag by +-5)
        P2, v1 = best_lag(self.bands[0], c, P - 5, P + 5)
        vbp = [v1]
        for b in range(1, 5):
            vbp.append(max(ncorr(self.bands[b], c, P2), ncorr(self.envs[b], c, P2) - 0.1))
        # residual and peakiness
        res = lfilter(a, [1.0], x[c - 300:c + 300])[100:500]  # 400 samples centred on c
        rw = res[120:280]
        peak = np.sqrt(np.mean(rw ** 2)) / (np.mean(np.abs(rw)) + 1e-9)
        ap = vbp[0] < 0.5
        if peak > 1.34:
            vbp[0] = 1.0
        if peak > 1.6:
            vbp[0] = vbp[1] = vbp[2] = 1.0
        voiced = vbp[0] > 0.6
        # gains (A.5.2.12)
        if vbp[0] > 0.6:
            L = P2 * int(np.ceil(120.0 / P2 + 1e-9))
            if L <= 120:
                L += P2
            if L > 320:
                L //= 2
        else:
            L = 120
        g = []
        for cc in (c - N // 2, c):
            s = x[cc - L // 2:cc - L // 2 + L]
            g.append(max(0.0, 10 * np.log10(0.01 + np.mean(s ** 2))))
        # Fourier magnitudes (A.5.2.18), voiced only
        fm = np.ones(10)
        if voiced:
            w = res[100:300] * self.hamming200
            S = np.abs(np.fft.fft(w, 512))
            nh = min(10, int(P2 / 4))
            width = int(512 / P2)
            mags = []
            for i in range(1, nh + 1):
                ctr = int(round(512 * i / P2))
                lo, hi = max(0, ctr - width // 2), min(256, ctr + width // 2 + 1)
                mags.append(S[lo:hi].max() if hi > lo else 0.0)
            mags = np.array(mags)
            if len(mags) and np.any(mags > 0):
                mags = mags / np.sqrt(np.mean(mags ** 2))
                fm[:len(mags)] = mags
        if voiced:
            vb = [1] + [1 if v > 0.6 else 0 for v in vbp[1:]]
            if vb[1:] == [0, 0, 0, 1]:
                vb[4] = 0
        else:
            vb = [0] * 5
        return {"pitch": float(P2) if voiced else 0.0, "g": g, "vbp": vb, "lsf": lsf,
                "fm": fm, "ap": int(ap and voiced)}


def analyse(x, N):
    an = Analyzer(x, N)
    return [an.frame(k) for k in range(an.n_frames)]


def write_params(frames, N, path):
    with open(path, "w") as f:
        f.write(f"frame_len {N}\n")
        for p in frames:
            f.write("frame pitch=%g g1=%.3f g2=%.3f vbp=%s lsf=%s fm=%s ap=%d\n" % (
                p["pitch"], p["g"][0], p["g"][1], ",".join(str(v) for v in p["vbp"]),
                ",".join("%.2f" % v for v in p["lsf"]), ",".join("%.4f" % v for v in p["fm"]),
                p["ap"]))


# ---- measures -------------------------------------------------------------------------

def align(ref, y, max_lag=800):
    """Lag (samples) of y behind ref, from the cross-correlation of 10 ms energy envelopes."""
    def env(v):
        e = np.convolve(v.astype(float) ** 2, np.ones(80) / 80, "same")
        return np.log10(e + 1.0)
    a, b = env(ref), env(y)
    n = min(len(a), len(b)) - max_lag
    best, lag = -np.inf, 0
    for L in range(0, max_lag + 1, 2):
        aa, bb = a[:n] - a[:n].mean(), b[L:L + n] - b[L:L + n].mean()
        r = np.dot(aa, bb) / np.sqrt(np.dot(aa, aa) * np.dot(bb, bb) + 1e-12)
        if r > best:
            best, lag = r, L
    return lag


def frames_of(x, hop=180, win=256):
    n = (len(x) - win) // hop + 1
    idx = np.arange(win)[None, :] + hop * np.arange(max(n, 0))[:, None]
    return x[idx].astype(float)


def envelope_db(fr, ncep=20):
    """Smoothed (cepstrally liftered, first ncep coefficients) log spectrum in dB."""
    S = np.abs(np.fft.rfft(fr * np.hanning(fr.shape[1]), 512, axis=1)) ** 2 + 1e-2
    c = np.fft.irfft(np.log(S), axis=1)
    c[:, ncep + 1:-ncep] = 0
    return 10 / np.log(10) * np.fft.rfft(c, axis=1).real


def active_mask(fr):
    e = (fr ** 2).mean(axis=1)
    return e > np.percentile(e, 95) * 10 ** (-35 / 10)


def spectral_distance(ref, y):
    """Mean RMS difference (dB) of the spectral envelopes over 125-3800 Hz, active frames,
    each envelope with its own level removed (shape only), and with the level kept."""
    n = min(len(ref), len(y))
    A, B = frames_of(ref[:n]), frames_of(y[:n])
    m = active_mask(A)
    ea, eb = envelope_db(A[m]), envelope_db(B[m])
    f = np.fft.rfftfreq(512, 1 / FS)
    band = (f >= 125) & (f <= 3800)
    d = ea[:, band] - eb[:, band]
    level = np.sqrt((d ** 2).mean(axis=1)).mean()
    d0 = d - d.mean(axis=1, keepdims=True)
    shape = np.sqrt((d0 ** 2).mean(axis=1)).mean()
    return float(shape), float(level)


def pitch_track(x, hop=180, win=320, lag_min=26, lag_max=100, vr=0.5):
    fr = frames_of(x, hop, win)
    fr = fr - fr.mean(axis=1, keepdims=True)
    e = (fr ** 2).mean(axis=1)
    active = e > max(np.percentile(e, 95) * 1e-4, 1e-3)
    seg = win - lag_max
    a = fr[:, :seg]
    ea = (a ** 2).sum(axis=1)
    rmax = np.zeros(len(fr))
    lag = np.zeros(len(fr), dtype=int)
    for L in range(lag_min, lag_max + 1):
        b = fr[:, L:L + seg]
        r = (a * b).sum(axis=1) / np.sqrt(ea * (b ** 2).sum(axis=1) + 1e-12)
        better = r > rmax
        rmax[better], lag[better] = r[better], L
    voiced = active & (rmax > vr)
    return np.where(voiced, FS / np.maximum(lag, 1), 0.0), active


def pitch_agreement(ref, y):
    n = min(len(ref), len(y))
    fa, act = pitch_track(ref[:n])
    fb, _ = pitch_track(y[:n])
    va, vb = fa > 0, fb > 0
    vuv = float((va == vb)[act].mean())
    both = va & vb
    ratio = np.abs(np.log(np.maximum(fb[both], 1) / np.maximum(fa[both], 1)))
    within = float((ratio < 0.1).mean()) if both.any() else float("nan")
    semis = float(np.median(12 * ratio / np.log(2))) if both.any() else float("nan")
    return vuv, within, semis


def segmental_snr(ref, y):
    n = min(len(ref), len(y))
    A, B = frames_of(ref[:n]), frames_of(y[:n])
    m = active_mask(A)
    s = (A[m] ** 2).sum(axis=1)
    e = ((A[m] - B[m]) ** 2).sum(axis=1) + 1e-9
    return float(np.mean(np.clip(10 * np.log10(s / e), -10, 35)))


# ---- driver ---------------------------------------------------------------------------

def read_raw(p):
    return np.fromfile(p, dtype="<i2").astype(float)


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    if p.returncode != 0:
        raise RuntimeError(f"{cmd[0]} failed: {p.stderr.strip()[:300]}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--synth", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--melpe")
    ap.add_argument("--measures")
    ap.add_argument("raws", nargs="+")
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    mf = None
    if a.measures:
        spec = importlib.util.spec_from_file_location("melpe_frames", a.measures)
        mf = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mf)

    variants = [  # name, frame length, synth options
        ("synth bandpass 22.5", 180, []),
        ("synth bandpass+pf 22.5", 180, ["--postfilter"]),
        ("synth harmonic 22.5", 180, ["--excitation", "harmonic"]),
        ("synth harmonic+pf 22.5", 180, ["--excitation", "harmonic", "--postfilter"]),
        ("synth bandpass 25", 200, []),
        ("synth harmonic 25", 200, ["--excitation", "harmonic"]),
    ]
    out = {}  # name -> list of (ref, y, tag)
    for raw in a.raws:
        tag = os.path.splitext(os.path.basename(raw))[0]
        x = read_raw(raw)
        params = {}
        for N in (180, 200):
            pth = os.path.join(a.work, f"{tag}.{N}.params")
            write_params(analyse(x, N), N, pth)
            params[N] = pth
        out.setdefault("original", []).append((x, x, tag))
        for name, N, opts in variants:
            o = os.path.join(a.work, f"{tag}.{name.replace(' ', '_').replace('+', '_')}.raw")
            run([a.synth] + opts + [params[N], o])
            out.setdefault(name, []).append((x, read_raw(o), tag))
        if a.melpe:
            for name, extra in (("melpe 2400", []), ("melpe 2400 -p", ["-p"])):
                o = os.path.join(a.work, f"{tag}.{name.replace(' ', '_')}.raw")
                run([a.melpe, "-q"] + extra + ["-r", "2400", "-m", "C", "-i", raw, "-o", o])
                out.setdefault(name, []).append((x, read_raw(o), tag))

    def row(name, pairs, versus):
        shp, lvl, vuv, within, semis, snr, lags = [], [], [], [], [], [], []
        for ref, y, tag in pairs:
            # every output is aligned to the original first; a comparison between two
            # outputs uses both aligned copies
            lag = align(ref, y)
            yy = y[lag:]
            if versus is not None:
                r0, y0, _ = [p for p in out[versus] if p[2] == tag][0]
                ref = y0[align(r0, y0):]
            s, l = spectral_distance(ref, yy)
            v, w, st = pitch_agreement(ref, yy)
            shp.append(s); lvl.append(l); vuv.append(v); within.append(w); semis.append(st)
            snr.append(segmental_snr(ref, yy)); lags.append(lag)
        cat = np.concatenate([y for _, y, _ in pairs])
        extra = ""
        if mf is not None:
            m = mf.speech_measure(cat)
            r1, depth = mf.energy_contour(cat, 2400)
            extra = "  voiced %.2f track %.2f f0 %3.0f mod %.2f r1 %.2f depth %4.1f" % (
                m["voiced"], m["track"], m["f0_median"], mf.modulation(cat), r1, depth)
        print("%-24s %-12s lag %-15s env %.2f dB  env+lvl %5.2f dB  V/UV %.2f  f0<10%% %.2f  "
              "|df0| %.2f st  segSNR %5.2f%s" % (
                  name, versus or "original", ",".join(str(v) for v in lags), np.mean(shp),
                  np.mean(lvl), np.mean(vuv), np.nanmean(within), np.nanmean(semis),
                  np.mean(snr), extra))

    print("speech:", ", ".join(os.path.basename(r) for r in a.raws))
    for name in out:
        row(name, out[name], None)
    if a.melpe:
        for name, _, _ in variants:
            row(name, out[name], "melpe 2400")
    return 0


if __name__ == "__main__":
    sys.exit(main())
