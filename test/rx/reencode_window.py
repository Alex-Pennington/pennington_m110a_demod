"""The re-encode check's window check (ctest rx_reencode_window): a fade inside a block must not
let wrong bytes pass, and the existing output lines keep their form.

  counts   window_check() on made-up per-frame counts: a faded block of right bytes is real; the
           same block with 7 far-off data symbols in its clean frames, as a wrong codeword leaves
           them, is not; crashes that put probes and data far off alike leave it real.
  clean    ModemTX (300L, 400 bytes) in white noise at 10 dB -> reencode_check.py: every block
           "fit the signal" and window check verdict real.
  fade     the same message with a 3 s -12 dB fade inside block 1 (noise seeds 4 and 6). The receiver's
           bytes are checked, and then two claimed payloads with --bytes: the sent bytes (verdict
           real), and the sent bytes with the run m110a_rx gave for this audio when this test was
           written, one or two wrong bytes (verdict NOT REAL in block 1, though the old per-block
           comparison says it fits). Whatever the receiver decodes now, its verdict must be real
           exactly when its block is exact.
  format   gale-rx's parse of the output (its BLOCK_RE and HEAD_RE) finds exactly one block line
           per block and the same numbers as the block lines, and no window line matches them.

Usage: python3 -I -B test/rx/reencode_window.py --tool MODEM_TX_WAV --rx M110A_RX --work DIR
The payload text starts "DE AAM402".
"""
import argparse
import os
import random
import re
import subprocess
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import reencode_check as rc  # noqa: E402

MODE, NBYTES, BLOCK_BYTES, SR = '300L', 400, 180, 48000
FADE = (6.8, 3.0, -12.0)  # start (s from the message start, 2.0 s into the data), length (s), depth (dB)
# gale-rx (greybeard-ale tools/gale_rx.py) reads the block lines with these
BLOCK_RE = re.compile(r"block \d+: probe agreement ([0-9.]+), data agreement ([0-9.]+)")
HEAD_RE = re.compile(r": (\S+) start \S+ s success=(\d) bytes=(\d+)(?: sha256 ([0-9a-f]+))?")
WCHECK_RE = re.compile(r"^  block (\d+) window check: .*; verdict (real|NOT REAL)", re.M)
# Bytes m110a_rx gave for the fade audio at 10 dB, per noise seed: (first wrong byte, the run).
# One or two wrong bytes in block 1, and the old per-block comparison calls each block fit.
BAD = {4: (62, 'ac'), 5: (112, '04'), 6: (120, '2cbd')}


def payload():
    head = b'DE AAM402 re-encode window check '
    r = random.Random('reencode_window')
    return (head + bytes(r.randrange(256) for _ in range(NBYTES)))[:NBYTES]


def audio(a, data, snr, fade, seed, tag):
    b, w = (os.path.join(a.work, tag + e) for e in ('.bin', '.tx.wav'))
    with open(b, 'wb') as f:
        f.write(data)
    subprocess.run([a.tool, '--mode', MODE, '--lead', '0', '--tail', '0', b, w], check=True, capture_output=True)
    _, x = wavfile.read(w)
    os.remove(b)
    os.remove(w)
    x = x.astype(float)
    ps = np.mean(x ** 2)
    if fade:
        f0, dur, db = fade
        x[int(f0 * SR):int((f0 + dur) * SR)] *= 10 ** (db / 20)
    y = np.concatenate([np.zeros(3 * SR), x, np.zeros(4 * SR)])
    y = y + np.random.default_rng(seed).normal(0, np.sqrt(ps / 10 ** (snr / 10) * (SR / 2) / 3000.0), len(y))
    y8 = resample_poly(np.clip(np.round(y), -32768, 32767), 1, 6)
    out = os.path.join(a.work, tag + '.wav')
    wavfile.write(out, 8000, np.clip(np.round(y8), -32768, 32767).astype(np.int16))
    return out


def run_check(a, wav, claimed=None):
    cmd = [sys.executable, '-I', os.path.join(HERE, 'reencode_check.py'), wav, '--rx', a.rx, '--tx', a.tool,
           '--work', a.work]
    if claimed is not None:
        cb = os.path.join(a.work, 'claimed.bin')
        with open(cb, 'wb') as f:
            f.write(claimed)
        cmd += ['--bytes', cb]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if claimed is not None:
        os.remove(cb)
    return p.stdout


def decoded(a, wav):
    sr, x = wavfile.read(wav)
    out, _ = rc.decode(a.rx, x.astype(float) / 32768.0, sr, 0.0, a.work)
    return [pl for _, pl in rc.messages(out) if pl]


def verdicts(out):
    return {int(b): v for b, v in WCHECK_RE.findall(out)}


def form_ok(out, tag):
    """gale-rx's parse: one block line per window check, the same numbers, no window line matched."""
    lines = out.splitlines()
    blocks = [ln for ln in lines if re.match(r'^  block \d+: probe agreement ', ln)]
    found = BLOCK_RE.findall(out)
    ok = len(found) == len(blocks) == len(verdicts(out)) and len(HEAD_RE.findall(out)) >= 1
    ok = ok and all(BLOCK_RE.search(ln) is None for ln in lines if ln not in blocks)
    ok = ok and all(re.fullmatch(r'  block \d+: probe agreement [0-9.]+, data agreement [0-9.]+, random-bytes control '
                                 r'[0-9.]+ \(chance [0-9.]+\): the bytes (fit the signal|DO NOT FIT the signal '
                                 r'\(bit errors\))', ln) for ln in blocks)
    print(f'{tag} format: {len(blocks)} block lines, gale-rx parse {len(found)}, window checks '
          f'{len(verdicts(out))} -> {"PASS" if ok else "FAIL"}', flush=True)
    return ok


def case_counts():
    u = k = 20
    nf = 288
    pok, dok = np.full(nf, k), np.full(nf, u)
    pok[130:280] = dok[130:280] = 17           # a fade: 15 % of the symbols wrong, probes and data alike
    fp, fd = np.zeros(nf, int), np.zeros(nf, int)
    ok = True
    _, s = rc.window_check(pok, dok, fp, fd, u, k)
    ok &= s['verdict'] == 'real'
    print(f"counts right bytes: far off {s['far_data']} data, expected {s['expected']:.3f}, p {s['p']:.2g} -> {s['verdict']}")
    bad, badf = dok.copy(), fd.copy()
    at = np.linspace(0, 129, 7).astype(int)    # a wrong codeword: 7 symbols on other points where the signal is clean
    bad[at] -= 1
    badf[at] += 1
    _, s = rc.window_check(pok, bad, fp, badf, u, k)
    ok &= s['verdict'] == 'not real'
    print(f"counts wrong bytes: far off {s['far_data']} data, expected {s['expected']:.3f}, p {s['p']:.2g} -> {s['verdict']}")
    crash_p, crash_d = fp.copy(), fd.copy()   # crashes: far-off probes and data alike
    crash_p[[10, 50, 90, 200]] = 3
    crash_d[[10, 50, 90, 200]] = 4
    _, s = rc.window_check(pok, dok, crash_p, crash_d, u, k)
    ok &= s['verdict'] == 'real'
    print(f"counts crashes: far off {s['far_probes']} probes, {s['far_data']} data, expected {s['expected']:.2f}, "
          f"p {s['p']:.2g} -> {s['verdict']}")
    print(f'counts -> {"PASS" if ok else "FAIL"}', flush=True)
    return ok


def case_clean(a, data):
    wav = audio(a, data, 10.0, None, 1, 'clean')
    out = run_check(a, wav)
    os.remove(wav)
    v = verdicts(out)
    ok = form_ok(out, 'clean') and len(v) >= 2 and all(x == 'real' for x in v.values()) \
        and all(ln.endswith('fit the signal') for ln in out.splitlines() if BLOCK_RE.search(ln))
    print(f'clean: window check verdicts {v} -> {"PASS" if ok else "FAIL"}', flush=True)
    return ok


def case_fade(a, data, seed):
    wav = audio(a, data, a.fade_snr, FADE, seed, 'fade')
    ok = True
    # the receiver's own bytes: real only where the block is exact
    pls = decoded(a, wav)
    out = run_check(a, wav)
    ok &= form_ok(out, f'fade seed {seed}')
    v = verdicts(out)
    if pls:
        pl = pls[0]
        for b, verdict in v.items():
            exact = pl[(b - 1) * BLOCK_BYTES:b * BLOCK_BYTES] == data[(b - 1) * BLOCK_BYTES:b * BLOCK_BYTES]
            good = (verdict == 'real') == exact
            ok &= good
            print(f'fade seed {seed} receiver block {b}: bytes {"exact" if exact else "wrong"}, verdict {verdict} '
                  f'-> {"PASS" if good else "FAIL"}', flush=True)
    # the sent bytes: real
    v = verdicts(run_check(a, wav, data))
    good = bool(v) and all(x == 'real' for x in v.values())
    ok &= good
    print(f'fade seed {seed} sent bytes: verdicts {v} -> {"PASS" if good else "FAIL"}', flush=True)
    # bytes a receiver gave for this audio: wrong in block 1, which the old comparison passes
    at, run = BAD[seed]
    run = bytes.fromhex(run)
    bad = data[:at] + run + data[at + len(run):]
    out = run_check(a, wav, bad)
    v = verdicts(out)
    bl = BLOCK_RE.findall(out)
    old_fit = bool(bl) and float(bl[0][1]) >= float(bl[0][0]) - rc.FIT
    good = v.get(1) == 'NOT REAL'
    ok &= good
    print(f'fade seed {seed} wrong bytes ({sum(x != y for x, y in zip(bad, data))} in block 1): old comparison '
          f'{"fits" if old_fit else "does not fit"}, window check {v.get(1)} -> {"PASS" if good else "FAIL"}',
          flush=True)
    os.remove(wav)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tool', required=True)
    ap.add_argument('--rx', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--fade-snr', type=float, default=10.0)
    ap.add_argument('--fade-seeds', default='4,6', help=f'noise seeds of the fade case, of {sorted(BAD)}')
    ap.add_argument('--cases', default='counts,clean,fade')
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    data = payload()
    ok = True
    for c in a.cases.split(','):
        ok &= {'counts': lambda: case_counts(), 'clean': lambda: case_clean(a, data),
               'fade': lambda: all([case_fade(a, data, int(sd)) for sd in a.fade_seeds.split(',')])}[c]()
    print('reencode_window: all cases pass' if ok else 'reencode_window: FAILED')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
