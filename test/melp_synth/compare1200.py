#!/usr/bin/env python3
"""Measure a codebook-free 1200 bit/s decode against the reference coder's decode.

For each input (a Harris payload .bits file, or the reference coder's own 1200 bit/s
.bit output for public speech) it:

  1. cuts the 81-bit superframes (payloads: between the 54-bit header and the 108-bit
     trailer, test/harris/melpe_frames.py) and packs them as RFC 8130 Figure 3;
  2. decodes them with the reference coder (-r 1200 -m S): the target, "ref";
  3. checks the fields melp1200_fields decodes from the printed tables (U/V pattern,
     band-pass cutoff, aperiodic flag) against the reference coder's own reading of
     them, its upward transcode to 2400 bit/s (-m U) read with STANAG 4591 s5.3.1 and
     Table 3. The transcode sets AF to 0 in every voiced frame, so it cannot check the
     aperiodic flags: their agreement is printed and is not a check;
  4. synthesises three outputs and measures each against "ref":
       bits      what the bits give without codebooks: U/V, cutoffs, aperiodic flags
                 from the bits; pitch, gains and LSFs fixed stand-ins
       +prosody  pitch and gains measured from "ref" (stand-in for the pitch and gain
                 codebooks), flat LSFs: what the LSF codebooks would add is missing
       +all      every parameter measured from "ref" (stand-in for all codebooks)
     plus "unrelated": "ref" against itself shifted by 1.5 s, the distance of speech
     that carries none of the target's sounds.

The reference coder is run as an external program only. Nothing is copied from it.

  python3 -I -B test/melp_synth/compare1200.py --melpe <coder> --bin <build>/bin \
      --frames test/harris/melpe_frames.py --work <scratch> <payload.bits|coder.bit> ...
"""
import argparse
import importlib.util
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


V = load("validate", os.path.join(HERE, "validate.py"))

# STANAG 4591 Table 3 (p. 11): frame bits of the pitch code P0..P6, BP0..BP3 and AF
P_BITS = [3, 14, 15, 21, 11, 13, 17]
BP_BITS = [2, 39, 38, 25]
AF_BIT = 47
# The coder's 2400 BP3 BP2 BP1 BP0 read as the voicing of bands 2 3 4 5 (measured,
# docs/melp-synth.md) -> the Table D-2 codeword index
BP_TO_INDEX = {(0, 0, 0, 0): 0, (1, 0, 0, 0): 1, (1, 1, 0, 0): 2, (1, 1, 1, 1): 3}
CUT = {500: 0, 1000: 1, 2000: 2, 4000: 3}


def run(cmd, **kw):
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=600, **kw)
    if p.returncode != 0:
        raise RuntimeError(f"{os.path.basename(cmd[0])} rc={p.returncode}: {p.stderr.strip()[:300]}")
    return p.stdout


def weight(v):
    return bin(v).count("1")


def transcode_fields(mf, melpe, bit, work, tag):
    """The coder's -m U output -> per frame (voiced, D-2 index or None, AF)."""
    up = os.path.join(work, f"{tag}.up2400.bit")
    run([melpe, "-q", "-r", "1200", "-m", "U", "-i", bit, "-o", up])
    fr = mf.unpack_frames(open(up, "rb").read(), 2400)
    out = []
    for f in fr:
        code = sum(int(f[b - 1]) << i for i, b in enumerate(P_BITS))
        voiced = weight(code) >= 3  # s5.3.1: weight 0-1 unvoiced, 2 erasure
        bp = tuple(int(f[b - 1]) for b in BP_BITS[::-1])
        out.append((voiced, BP_TO_INDEX.get(bp) if voiced else None, int(f[AF_BIT - 1])))
    return out


def align_vuv(ref, y, max_lag=1600, step=10):
    """Lag of y behind ref that best matches the voiced/unvoiced tracks (for an output
    whose level carries no information)."""
    fa, act = V.pitch_track(ref)
    best, lag = -1.0, 0
    for L in range(0, max_lag + 1, step):
        fb, _ = V.pitch_track(y[L:])
        n = min(len(fa), len(fb))
        agree = float(((fa[:n] > 0) == (fb[:n] > 0))[act[:n]].mean())
        if agree > best:
            best, lag = agree, L
    return lag


def our_fields(text):
    out = []
    for line in text.splitlines():
        t = line.split()
        uv, prot, cuts, ap = t[4], t[5], t[6:9], t[9]
        out.append((uv, prot, [int(c) for c in cuts], ap))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--melpe", required=True)
    ap.add_argument("--bin", required=True, help="directory holding melp_synth and melp1200_fields")
    ap.add_argument("--frames", required=True, help="test/harris/melpe_frames.py")
    ap.add_argument("--work", required=True)
    ap.add_argument("inputs", nargs="+")
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    mf = load("melpe_frames", a.frames)
    synth = os.path.join(a.bin, "melp_synth")
    fields = os.path.join(a.bin, "melp1200_fields")

    rows = []
    for path in a.inputs:
        tag = os.path.splitext(os.path.basename(path))[0]
        if path.endswith(".bits"):
            s = open(path).read().strip()
            bits, ok = mf.strip_header_trailer(np.frombuffer(s.encode(), np.uint8) - ord("0"))
            if not ok:
                raise SystemExit(f"{path}: no Harris header")
            frames = mf.cut_frames(bits, 1200)
            bit = os.path.join(a.work, f"{tag}.1200.bit")
            open(bit, "wb").write(mf.pack_frames(frames, 1200))
        else:
            bit = path
        ref_raw = os.path.join(a.work, f"{tag}.ref.raw")
        run([a.melpe, "-q", "-r", "1200", "-m", "S", "-i", bit, "-o", ref_raw])
        ref = V.read_raw(ref_raw)

        # 3. fields against the coder's own reading
        params = os.path.join(a.work, f"{tag}.bits.params")
        ours = our_fields(run([fields, bit, params]))
        theirs = transcode_fields(mf, a.melpe, bit, a.work, tag)
        n_uv = n_uv_ok = n_cut = n_cut_ok = n_af = n_af_ok = n_prot = 0
        for i, (uv, prot, cuts, apf) in enumerate(ours):
            n_prot += prot == "ok"
            for j in range(3):
                if 3 * i + j >= len(theirs):
                    break
                tv, tcut, taf = theirs[3 * i + j]
                n_uv += 1
                n_uv_ok += (uv[j] == "V") == tv
                if uv[j] == "V" and tv:
                    n_cut += 1
                    n_cut_ok += CUT[cuts[j]] == tcut
                    n_af += 1
                    n_af_ok += int(apf[j]) == taf
        sf_pat = {}
        for uv, *_ in ours:
            sf_pat[uv] = sf_pat.get(uv, 0) + 1

        # 4. outputs
        outs = {}
        o = os.path.join(a.work, f"{tag}.bits.raw")
        run([synth, params, o])
        outs["bits"] = V.read_raw(o)
        an = V.analyse(ref, 180)
        full = os.path.join(a.work, f"{tag}.all.params")
        V.write_params(an, 180, full)
        flat = [dict(p, lsf=[4000.0 * (k + 1) / 11 for k in range(10)], fm=[1.0] * 10) for p in an]
        pros = os.path.join(a.work, f"{tag}.prosody.params")
        V.write_params(flat, 180, pros)
        for name, pth in (("+prosody", pros), ("+all", full)):
            o = os.path.join(a.work, f"{tag}.{name[1:]}.raw")
            run([synth, pth, o])
            outs[name] = V.read_raw(o)
        sh = 12000
        outs["unrelated"] = np.concatenate([ref[sh:], ref[:sh]])

        res = {"tag": tag, "superframes": len(ours), "patterns": sf_pat,
               "uv": (n_uv_ok, n_uv), "cut": (n_cut_ok, n_cut), "af": (n_af_ok, n_af),
               "prot": (n_prot, len(ours)), "m": {}}
        for name, y in outs.items():
            lag = 0 if name == "unrelated" else align_vuv(ref, y) if name == "bits" else V.align(ref, y)
            yy = y[lag:]
            shp, lvl = V.spectral_distance(ref, yy)
            vuv, within, semis = V.pitch_agreement(ref, yy)
            sm = mf.speech_measure(yy)
            res["m"][name] = (lag, shp, lvl, vuv, within, sm["track"])
        res["m"]["ref"] = (0, 0, 0, 1, 1, mf.speech_measure(ref)["track"])
        rows.append(res)

    for r in rows:
        print(f"{r['tag']}: {r['superframes']} superframes, patterns {r['patterns']}")
        print("  fields vs coder's own reading: U/V %d/%d, cutoff %d/%d, (aperiodic %d/%d, not a "
              "check); mode protection consistent %d/%d" % (*r["uv"], *r["cut"], *r["af"], *r["prot"]))
        for name in ("ref", "bits", "+prosody", "+all", "unrelated"):
            lag, shp, lvl, vuv, within, track = r["m"][name]
            print("  %-10s lag %4d  env %5.2f dB  env+lvl %5.2f dB  V/UV %.2f  f0<10%% %.2f  track %.3f"
                  % (name, lag, shp, lvl, vuv, within, track))
    return 0


if __name__ == "__main__":
    sys.exit(main())
