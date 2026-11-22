#!/usr/bin/env python3
"""Decode the Harris RT-1694D MELP test set to payload bits, three ways, and compare them.

Usage:
  python3 -I test/harris/score.py --rx build/bin/m110a_rx --score build/bin/harris_score \\
      --set <test-set dir> --out <payload dir> [--work <scratch dir>]

The test set is the eight WAVs described in docs/harris-melpe.md (48 kHz mono 16-bit);
nothing from it is copied into this repo. For each file:

1. The audio becomes I/Q as greybeard-ale's tools/m110a_check.py makes it (analytic signal,
   96 kS/s, dial at +10 kHz, cf32), from the native 48 kHz rather than 8 kHz, and goes
   through `m110a_rx --format cf32 --rate 96000 --offset 10000 --dial 0` in AUTO mode.
2. Each message m110a_rx reports is decoded twice more by harris_score, with the waveform
   AUTO named: once with probe phase tracking only, once with its fractionally spaced
   equalizer (--eq 6 0.02 0.005).
3. <out>/<slug>.bin is the equalized harris_score decode, cut at the EOM; <slug>.bits holds
   the same bits as ASCII 0/1. Bit k of the stream, in the order bits leave the Viterbi
   decoder, is bit (k % 8) of byte k // 8 (LSB first), the packing of m110a_rx hex=.
   <out>/BIT-ORDER.txt says so beside the files.

110A has no CRC. A payload counts as verified when the equalized and the tracked-only
harris_score decodes are identical and both found the EOM 4B65A5B2; the table also says
whether m110a_rx's own (DFE) bytes agree and gives the coded-bit disagreement of the
message blocks (re-encoded decoded bits against the received hard bits).

Exit status: 0 when every message verifies, 1 otherwise.
"""
import argparse
import os
import re
import subprocess
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import hilbert, resample_poly

FILES = [("600 #1", "600_1"), ("600 #2", "600_2"), ("600 #3 multiples", "600_3_multiples"),
         ("1200 #1", "1200_1"), ("1200 #2", "1200_2"), ("1200 #3", "1200_3"),
         ("melp600", "melp600"), ("melp2400", "melp2400")]
CAND = {"600S": "600-short", "600L": "600-long", "600H": "600-short",
        "1200S": "1200-short", "1200L": "1200-long", "1200H": "1200-short",
        "2400S": "2400-short", "2400L": "2400-long", "2400V": "2400-short"}
RATE, OFFSET = 96000, 10000.0
KV = re.compile(r'(\w+)=("[^"]*"|\S+)')
EQ = ["--eq", "6", "0.02", "0.005"]
BIT_ORDER = """Payload bit order

Each <slug>.bin holds the decoded MIL-STD-188-110A payload of one message, up to (not
including) the EOM 4B65A5B2. Bit k of the decoded stream, in the order the bits leave the
K=7 Viterbi decoder (after descrambling and deinterleaving), is bit (k % 8) of byte k // 8:
LSB first, the packing m110a_rx prints as hex=. <slug>.bits holds the same bits as ASCII
'0'/'1' in stream order, one file line.

The first bit of the stream is the first information bit the transmitter's FEC encoder
took after the preamble. None of the streams is a whole number of 54-bit or 81-bit
frames; what the MELPe framing inside them is, is outside the modem.
"""


def to_iq(wav, out):
    sr, x = wavfile.read(wav)
    if x.ndim > 1:
        x = x[:, 0]
    x = x.astype(float) / 32768
    z = resample_poly(hilbert(x), RATE // sr, 1)
    z *= np.exp(2j * np.pi * OFFSET * np.arange(len(z)) / RATE)
    np.stack([z.real, z.imag], 1).astype("<f4").tofile(out)
    return len(x) / sr


def run_rx(rx, iq):
    p = subprocess.run([rx, "--format", "cf32", "--rate", str(RATE), "--offset", str(int(OFFSET)),
                        "--dial", "0", iq], capture_output=True, text=True, timeout=1800)
    msgs, cur = [], None
    for line in p.stdout.splitlines():
        if line.startswith("msg="):
            cur = {k: v.strip('"') for k, v in KV.findall(line)}
            msgs.append(cur)
        elif cur is not None and line.startswith("  hex="):
            cur["hex"] = line.split("=", 1)[1].strip()
    return msgs


def run_score(score, wav, t0, t1, cand, out, eq):
    args = [score, wav, f"{t0:.3f}", f"{t1:.3f}"] + (EQ if eq else []) + ["--dump", cand, out]
    p = subprocess.run(args, capture_output=True, text=True, timeout=1800)
    for line in p.stdout.splitlines():
        if line.startswith("summary "):
            return {k: v for k, v in KV.findall(line)}
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rx", required=True)
    ap.add_argument("--score", required=True)
    ap.add_argument("--set", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--work")
    a = ap.parse_args()
    work = a.work or os.path.join(a.out, "work")
    os.makedirs(a.out, exist_ok=True)
    os.makedirs(work, exist_ok=True)
    with open(os.path.join(a.out, "BIT-ORDER.txt"), "w") as f:
        f.write(BIT_ORDER)

    rows, ok_all = [], True
    for name, slug in FILES:
        wav = os.path.join(a.set, name + ".wav")
        iq = os.path.join(work, slug + ".cf32")
        dur = to_iq(wav, iq)
        msgs = run_rx(a.rx, iq)
        os.remove(iq)
        if not msgs:
            rows.append((slug, "-", "-", "-", "-", "-", "-", "-", "-", "NO PREAMBLE"))
            ok_all = False
        for i, m in enumerate(msgs):
            s = slug if len(msgs) == 1 else f"{slug}_m{i + 1}"
            t0 = float(m["start_s"])
            t1 = float(msgs[i + 1]["start_s"]) - 0.05 if i + 1 < len(msgs) else dur
            cand = CAND.get(m["mode"])
            if cand is None:
                rows.append((s, m["mode"], f"{m.get('d1')},{m.get('d2')}", "-", "-", "-", "-", "-", "-",
                             "mode not scored"))
                ok_all = False
                continue
            out_eq, out_tr = os.path.join(a.out, s + ".bin"), os.path.join(work, s + ".tracked.bin")
            se = run_score(a.score, wav, t0, t1, cand, out_eq, True)
            st = run_score(a.score, wav, t0, t1, cand, out_tr, False)
            b_eq = open(out_eq, "rb").read() if se else b""
            b_tr = open(out_tr, "rb").read() if st else b""
            b_rx = bytes.fromhex(m.get("hex", ""))
            with open(os.path.join(a.out, s + ".bits"), "w") as f:
                f.write("".join(str((c >> j) & 1) for c in b_eq for j in range(8)) + "\n")
            eom_ok = bool(se and st and int(se["eom_bit"]) >= 0 and int(st["eom_bit"]) >= 0)
            verified = eom_ok and b_eq == b_tr and len(b_eq) > 0
            ok_all &= verified
            rx_diff = sum(x != y for x, y in zip(b_eq, b_rx)) + abs(len(b_eq) - len(b_rx))
            pe = int(st["probe_errors"]) / max(1, int(st["probe_total"])) if st else float("nan")
            rows.append((s, m["mode"], f"{m.get('d1')},{m.get('d2')}", str(len(b_eq)),
                         "yes" if eom_ok else "no",
                         f"{float(se['msg_ber']) * 100:.2f}%" if se else "-",
                         f"{float(st['msg_ber']) * 100:.2f}%" if st else "-",
                         f"{pe * 100:.2f}%",
                         f"eom={m['eom']} q={m['quality_db']} dB, {rx_diff} bytes differ",
                         "verified" if verified else "NOT VERIFIED"))
    hdr = ("message", "mode", "D1,D2", "bytes", "EOM", "coded dis. (EQ)", "coded dis. (tracked)",
           "probe SER", "m110a_rx (DFE)", "result")
    w = [max(len(str(r[i])) for r in rows + [hdr]) for i in range(len(hdr))]
    for r in [hdr] + rows:
        print("  ".join(str(c).ljust(w[i]) for i, c in enumerate(r)))
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
