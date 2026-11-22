"""Task A: is the Harris 2400V (melp2400) payload LPC-10e (FED-STD-1015 v53)?

LPC-10e frame layout, quantiser tables and the unvoiced-frame Hamming parity are read from
the public-domain US government LPC-10e 1996 C code as kept in sox's lpc10/ directory
(chanwr.c, encode.c, decode.c). The tables below are transcribed from there.

Three tests on the stream, each over every frame offset 0..53 and four readings
(as-is, frame reversed, payload bytes read MSB-first, inverted):

  sync     share of frames whose frame bit 54 (index 53) alternates.
  pitch    share of 7-bit pitch/voicing codes the LPC-10e encoder can emit (62 of 128:
           0 unvoiced, 127 transition, 60 voiced codes ENTAU). Chance 62/128 = 0.484.
  parity   among frames whose code is 0 or 127, share whose RC5..RC10 fields hold the
           Hamming(8,4) parity of bits 1-4 of RC1, RC2, RC3, RMS and RC4 that ENCODE writes
           (20 bits; chance about 1e-6).

Then the reading picked by sync is decoded to audio with the LPC-10e decoder (an external
program built from that code, LPC10TOOL) and the speech/time-structure measures of
harris600/timestruct.py are taken in original order against 12 frame shuffles.
Calibration: the same tests on the LPC-10e encoder's own output for 8 kHz s16le speech.

python3 -I -B test/harris/blind/lpc10.py PAYLOAD_DIR LPC10TOOL WORK [SPEECH.raw ...]
"""
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "harris600"))
sys.path.insert(0, os.path.dirname(HERE))
from common import load_bits  # noqa: E402

# chanwr.c IBLIST: parameter carried by frame bits 1..53 (1 pitch, 2 RMS, 4 RC10 ... 13 RC1),
# each parameter's bits sent LSB first.
IBLIST = [13, 12, 11, 1, 2, 13, 12, 11, 1, 2, 13, 10, 11, 2, 1, 10, 13, 12, 11, 10, 2, 13, 12,
          11, 10, 2, 1, 12, 7, 6, 1, 10, 9, 8, 7, 4, 6, 9, 8, 7, 5, 1, 9, 8, 4, 6, 1, 5, 9, 8,
          7, 5, 6]
ENCTAB = [0, 7, 11, 12, 13, 10, 6, 1, 14, 9, 5, 2, 3, 4, 8, 15]
ENTAU = [19, 11, 27, 25, 29, 21, 23, 22, 30, 14, 15, 7, 39, 38, 46, 42, 43, 41, 45, 37, 53, 49,
         51, 50, 54, 52, 60, 56, 58, 26, 90, 88, 92, 84, 86, 82, 83, 81, 85, 69, 77, 73, 75, 74,
         78, 70, 71, 67, 99, 97, 113, 112, 114, 98, 106, 104, 108, 100, 101, 76]
VALID = np.zeros(128, bool)
VALID[[0, 127] + ENTAU] = True

# (param index, rank) for frame bits 1..53
_POS = []
_seen = {}
for p in IBLIST:
    _POS.append((p, _seen.get(p, 0)))
    _seen[p] = _seen.get(p, 0) + 1
WIDTH = dict(_seen)  # bits per parameter


def fields(F):
    """F: (n, 54) frames, frame bit 1 first. Returns dict param -> unsigned field value."""
    out = {p: np.zeros(len(F), np.int64) for p in WIDTH}
    for i, (p, r) in enumerate(_POS):
        out[p] |= F[:, i].astype(np.int64) << r
    return out


def rc(f, k):
    """Unsigned field of reflection coefficient k (1..10): itab index 14 - k."""
    return f[14 - k]


def signature(F):
    f = fields(F)
    pit = f[1]
    pv = float(VALID[pit].mean())
    uv = (pit == 0) | (pit == 127)
    ok =np.ones(len(F), bool)
    ok &= rc(f, 5) == (ENCTAB_ARR[(rc(f, 1) >> 1) & 15] & ((1 << WIDTH[9]) - 1))
    ok &= rc(f, 6) == (ENCTAB_ARR[(rc(f, 2) >> 1) & 15] & ((1 << WIDTH[8]) - 1))
    ok &= rc(f, 7) == (ENCTAB_ARR[(rc(f, 3) >> 1) & 15] & ((1 << WIDTH[7]) - 1))
    ok &= rc(f, 8) == (ENCTAB_ARR[(f[2] >> 1) & 15] & ((1 << WIDTH[6]) - 1))
    e4 = ENCTAB_ARR[(rc(f, 4) >> 1) & 15]
    ok &= rc(f, 9) == ((e4 >> 1) & ((1 << WIDTH[5]) - 1))
    ok &= rc(f, 10) == ((e4 & 1) & ((1 << WIDTH[4]) - 1))
    nuv = int(uv.sum())
    npar = int((ok & uv).sum())
    sync = float((F[1:, 53] != F[:-1, 53]).mean()) if len(F) > 1 else 0.0
    return {"sync": sync, "pitch": pv, "nuv": nuv, "npar": npar, "pitchcodes": pit}


ENCTAB_ARR = np.array(ENCTAB, np.int64)


def readings(bits):
    """Yield (name, bitstream) for the four serialisations."""
    yield "as-is", bits
    n = len(bits) // 8 * 8
    yield "bytes MSB-first", bits[:n].reshape(-1, 8)[:, ::-1].reshape(-1)
    yield "inverted", 1 - bits
    yield "frame reversed", bits  # reversal applied per frame in cut()


def cut(bits, off, name):
    b = bits[off:]
    n = len(b) // 54
    F = b[: n * 54].reshape(n, 54).copy()
    if name == "frame reversed":
        F = F[:, ::-1].copy()
    return F


def scan(bits, label, lo=0, hi=None):
    """Print the signature at every offset and reading; return the best (by sync) choice."""
    best = None
    rows = []
    for name, b in readings(bits):
        for off in range(54):
            F = cut(b[lo:hi] if hi else b[lo:], off, name)
            s = signature(F)
            rows.append((s["sync"], name, off, s))
    rows.sort(key=lambda r: -r[0])
    print("%s: top readings by sync alternation" % label)
    for sy, name, off, s in rows[:4]:
        print("   %-16s off %2d  sync %.3f  pitch-valid %.3f  UV/trans frames %3d  parity ok %3d"
              % (name, off, sy, s["pitch"], s["nuv"], s["npar"]))
    pv = np.array([r[3]["pitch"] for r in rows])
    pp = [r[3]["npar"] for r in rows]
    print("   over all 216 readings: pitch-valid max %.3f, median %.3f; parity-ok max %d"
          % (pv.max(), np.median(pv), max(pp)))
    best = rows[0]
    return best


def joint_constant(F, k=7, skip=(53,)):
    """Permutation-free check: greedily pick k columns that are jointly constant (all 0 or
    all 1) in the most frames. An LPC-10e (or MELP) unvoiced frame sends pitch code 0, so
    under any bit permutation some 7 columns are all zero in every unvoiced frame.
    Returns (columns, share of frames jointly constant) for value 0 and value 1."""
    out = {}
    cols = [c for c in range(F.shape[1]) if c not in skip]
    for v in (0, 1):
        M = F[:, cols] == v
        chosen, alive = [], np.ones(len(F), bool)
        for _ in range(k):
            sc = (M & alive[:, None]).sum(0)
            sc[[cols.index(c) for c in chosen]] = -1
            j = int(np.argmax(sc))
            chosen.append(cols[j]); alive &= M[:, j]
        out[v] = (sorted(chosen), float(alive.mean()))
    return out


def fmt_jc(jc):
    return "; ".join("all-%d %s in %.3f of frames" % (v, jc[v][0], jc[v][1]) for v in (0, 1))


def encode(tool, raw, work):
    out = os.path.join(work, os.path.basename(raw) + ".lpc10.txt")
    subprocess.run([tool, "enc", raw, out], check=True, capture_output=True)
    with open(out) as f:
        s = f.read().strip()
    return np.frombuffer(s.encode(), np.uint8) - ord("0")


def decode(tool, F, work, tag):
    src = os.path.join(work, tag + ".txt")
    dst = os.path.join(work, tag + ".s16")
    with open(src, "w") as f:
        f.write("".join("".join(map(str, r)) for r in F))
    subprocess.run([tool, "dec", src, dst], check=True, capture_output=True)
    return np.fromfile(dst, dtype="<i2").astype(float)


def timetest(tool, F, work, tag, nshuf=12):
    from timestruct import measures
    rng = np.random.default_rng(9)
    m = measures(decode(tool, F, work, tag))
    sh = []
    for _ in range(nshuf):
        S = F[rng.permutation(len(F))].copy()
        S[:, 53] = (np.arange(len(S)) % 2) ^ F[0, 53]
        sh.append(measures(decode(tool, S, work, tag + "_sh")))
    parts = []
    for k in ("track", "ac1E", "pause"):
        v = np.array([s[k] for s in sh])
        parts.append("%s %.3f (sh %.3f, z %+.1f)" % (k, m[k], v.mean(), (m[k] - v.mean()) / (v.std() + 1e-9)))
    print("   decode %-22s %4d frames: %s" % (tag, len(F), "; ".join(parts)))
    return m


def main(pd, tool, work, *raws):
    os.makedirs(work, exist_ok=True)
    print("LPC-10e field widths (bits):", {k: WIDTH[k] for k in sorted(WIDTH)},
          "(1 pitch, 2 RMS, 4..13 = RC10..RC1)")
    for raw in raws:
        b = encode(tool, raw, work)
        tag = "cal_" + os.path.basename(raw).split(".")[0]
        _, name, off, s = scan(b, tag)
        Fc = cut(b, 0, "as-is")
        timetest(tool, Fc, work, tag)
        perm = np.r_[np.random.default_rng(5).permutation(53), 53]
        print("   joint-constant 7 columns, columns permuted:", fmt_jc(joint_constant(Fc[:, perm])))
    rng = np.random.default_rng(3)
    rb = rng.integers(0, 2, 283 * 54).astype(np.uint8)
    scan(rb, "random bits")
    print("   joint-constant 7 columns:", fmt_jc(joint_constant(cut(rb, 0, "as-is"))))
    bits = load_bits(pd, "melp2400")
    # vocoder data from bit 54 to the trailer (15174, docs/harris-600-format.md s2)
    sy, name, off, s = scan(bits[:15174], "melp2400 (bits 0..15173)", lo=54)
    F = cut(bits[54:15174], off, name)
    h, _ = np.histogram(s["pitchcodes"], bins=np.arange(129))
    print("   melp2400 best reading: %d distinct pitch codes; invalid-code frames %d of %d"
          % ((h > 0).sum(), int((~VALID[s["pitchcodes"]]).sum()), len(F)))
    print("   joint-constant 7 columns (sync grid, as-is):", fmt_jc(joint_constant(cut(bits[54:15174], 6, "as-is"))))
    timetest(tool, F, work, "melp2400_lpc10")
    rf = rb[: len(F) * 54].reshape(len(F), 54).copy()
    rf[:, 53] = np.arange(len(rf)) % 2
    timetest(tool, rf, work, "random_lpc10")


if __name__ == "__main__":
    main(*sys.argv[1:])
