"""The re-encode check's window check (ctest rx_reencode_window): a fade inside a block must not
let wrong bytes pass, and the existing output lines keep their form.

  counts   window_check() on made-up per-frame counts: a faded block of right bytes is real; the
           same block with 7 far-off data symbols in its clean frames, as a wrong codeword leaves
           them, is not; crashes that put probes and data far off alike leave it real.
  clean    ModemTX (300L, 400 bytes) in white noise at 10 dB -> reencode_check.py: every block
           "fit the signal", window check verdict real, and the tail (bytes 360-399, EOM,
           flush, padding) checked as block 3, real; with one byte of the tail wrong, block 3
           is NOT REAL and blocks 1 and 2 stay real.
  fade     the same message with a 3 s -12 dB fade inside block 1 (noise seeds 4 and 6). The receiver's
           bytes are checked, and then two claimed payloads with --bytes: the sent bytes (verdict
           real), and the sent bytes with the run m110a_rx gave for this audio when this test was
           written, one or two wrong bytes (verdict NOT REAL in block 1, though the old per-block
           comparison says it fits). Whatever the receiver decodes now, its verdict must be real
           exactly when its block is exact.
  short    ModemTX 2400S, 40 bytes with EOM, at 15 dB: under one block, so the block lines say
           'not checked (no whole block)'; the tail check calls it real, and with one wrong bit
           NOT REAL.
  tails    300S and 150S (22.5 and 11.25 bytes per block), eom=1, at 15 dB, 300S 1277 B and random
           lengths: every block has a verdict, real for the sent bytes, and the output ends in tail
           block lines or in one 'tail check' line saying the block lines cover the data end, EOM
           and flush, never in silence.
  whole2400  2400S, 540 B (exactly 3 blocks; the tail block is EOM, flush and padding only) at 8, 14
           and 20 dB: the sent bytes real in all 4 blocks; at 20 dB one wrong bit is NOT REAL.
  sweep    (not in the default; --cases sweep) random-length eom=1 messages in the ten probe modes at
           each --sweep-snr: the receiver's bytes, the sent bytes and one flipped bit. Passes when no
           exact block is NOT REAL and every block has a verdict; flipped bits are counted, not required.
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
WCHECK_RE = re.compile(r"^  block (\d+) window check: .*; verdict (real|NOT REAL|NOT VERIFIABLE)", re.M)
TAIL_RE = re.compile(r"^  tail block (\d+) \(", re.M)
# Bytes m110a_rx gave for the fade audio at 10 dB, per noise seed: (first wrong byte, the run).
# One or two wrong bytes in block 1, and the old per-block comparison calls each block fit.
BAD = {4: (62, 'ac'), 5: (112, '04'), 6: (120, '2cbd')}


def payload():
    head = b'DE AAM402 re-encode window check '
    r = random.Random('reencode_window')
    return (head + bytes(r.randrange(256) for _ in range(NBYTES)))[:NBYTES]


def audio(a, data, snr, fade, seed, tag, mode=MODE):
    b, w = (os.path.join(a.work, tag + e) for e in ('.bin', '.tx.wav'))
    with open(b, 'wb') as f:
        f.write(data)
    subprocess.run([a.tool, '--mode', mode, '--lead', '0', '--tail', '0', b, w], check=True, capture_output=True)
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
    cmd = [sys.executable, '-I', getattr(a, 'check', None) or os.path.join(HERE, 'reencode_check.py'), wav, '--rx', a.rx,
           '--tx', a.tool, '--work', a.work]
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
    tails = [ln for ln in lines if re.match(r'^  tail block \d+ \(', ln)]
    found = BLOCK_RE.findall(out)
    ok = len(found) == len(blocks) and len(blocks) + len(tails) == len(verdicts(out)) and len(HEAD_RE.findall(out)) >= 1
    ok = ok and all(BLOCK_RE.search(ln) is None for ln in lines if ln not in blocks)
    ok = ok and all(re.fullmatch(r'  block \d+: probe agreement [0-9.]+, data agreement [0-9.]+, random-bytes control '
                                 r'[0-9.]+ \(chance [0-9.]+\): the bytes (fit the signal|DO NOT FIT the signal '
                                 r'\(bit errors\))', ln) for ln in blocks)
    print(f'{tag} format: {len(blocks)} block lines, {len(tails)} tail block lines, gale-rx parse {len(found)}, window checks '
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
    # one_byte_power() on a 2400S block's window frames (28 frames, 448 probes, 896 data, nothing far off):
    # at 20 dB (nearest other point ~60 noise powers out) one wrong byte would show, so a passing block is
    # real; at 8 dB (~4.6) it would not, so the block is NOT VERIFIABLE, whatever its gap
    blk = lambda pr: dict(far_data=0, expected=896 * np.exp(-rc.TAU), probe_ok=int(pr * 448), probes=448,
                          data_ok=int(pr * 896), data=896)
    hi, why_hi = rc.one_byte_power(blk(0.999), [60.0] * 28, '2400')
    lo, why_lo = rc.one_byte_power(blk(0.87), [4.6] * 28, '2400')
    ok &= hi and not lo
    print(f'counts one-byte power, 2400S at 20 dB: {"verifiable" if hi else "not verifiable"} ({why_hi})')
    print(f'counts one-byte power, 2400S at 8 dB: {"verifiable" if lo else "not verifiable"} ({why_lo})')
    print(f'counts -> {"PASS" if ok else "FAIL"}', flush=True)
    return ok


def case_clean(a, data):
    wav = audio(a, data, 10.0, None, 1, 'clean')
    out = run_check(a, wav)
    v = verdicts(out)
    tails = [int(b) for b in TAIL_RE.findall(out)]
    ok = form_ok(out, 'clean') and len(v) == 3 and tails == [3] and all(x == 'real' for x in v.values()) \
        and all(ln.endswith('fit the signal') for ln in out.splitlines() if BLOCK_RE.search(ln))
    print(f'clean: window check verdicts {v}, tail blocks {tails} -> {"PASS" if ok else "FAIL"}', flush=True)
    # one wrong byte in the tail (bytes 360-399, then EOM, flush and padding in block 3)
    bad = bytearray(data)
    bad[380] ^= 0x5a
    v2 = verdicts(run_check(a, wav, bytes(bad)))
    good = v2.get(1) == v2.get(2) == 'real' and v2.get(3) == 'NOT REAL'
    print(f'clean wrong byte in the tail: verdicts {v2} -> {"PASS" if good else "FAIL"}', flush=True)
    os.remove(wav)
    return ok and good


def case_short(a):
    """A message under one interleaver block (2400S, 40 bytes, eom=1): the block lines leave it
    'not checked (no whole block)'; the tail check covers it."""
    data = (b'DE AAM402 short ' + bytes(random.Random('reencode_window/short').randrange(256) for _ in range(40)))[:40]
    wav = audio(a, data, 15.0, None, 1, 'short', mode='2400S')
    out = run_check(a, wav)
    v = verdicts(out)
    ok = 'not checked (no whole block)' in out and form_ok(out, 'short') and v == {1: 'real'}
    print(f'short: tail verdicts {v} -> {"PASS" if ok else "FAIL"}', flush=True)
    bad = bytearray(data)
    bad[20] ^= 0x01
    v2 = verdicts(run_check(a, wav, bytes(bad)))
    good = v2 == {1: 'NOT REAL'}
    print(f'short one wrong bit: tail verdicts {v2} -> {"PASS" if good else "FAIL"}', flush=True)
    os.remove(wav)
    return ok and good


def case_tails(a):
    """300S and 150S blocks hold 22.5 and 11.25 bytes. Random lengths with eom=1, at 15 dB: each message
    ends in tail block lines, or in one line saying the block lines already cover the data end, EOM
    and flush (300S, 1277 B: the block lines count 58 blocks of 22 B, and the message has 58). Never
    silent; every block has a verdict, real for the sent bytes."""
    r = random.Random('reencode_window/tails')
    ok = True
    for mode, n in (('300S', 1277), ('300S', r.randrange(23, 400)), ('150S', r.randrange(12, 120)),
                    ('150S', r.randrange(120, 400))):
        data = (b'DE AAM402 tails ' + r.randbytes(n))[:n]
        wav = audio(a, data, 15.0, None, 1, f'tails_{mode}_{n}', mode=mode)
        out = run_check(a, wav, data)
        v = verdicts(out)
        bits = block_bits(mode)
        total = -(-(8 * n + rc.EOM_BITS) // bits)
        tails = [int(b) for b in TAIL_RE.findall(out)]
        cover = re.search(r'^  tail check \(eom=1, \d+ blocks?\): the block lines cover', out, re.M)
        good = form_ok(out, f'tails {mode} {n} B') and sorted(v) == list(range(1, total + 1)) \
            and all(x == 'real' for x in v.values()) and bool(tails or cover) and not (tails and cover)
        ok &= good
        print(f'tails {mode} {n} B: {total} blocks, tail blocks {tails or "none"}, '
              f'{"cover line" if cover else "no cover line"}, verdicts NOT REAL '
              f'{[b for b, x in v.items() if x != "real"] or "none"} -> {"PASS" if good else "FAIL"}', flush=True)
        os.remove(wav)
    return ok


def case_whole2400(a):
    """2400S, 540 B: exactly 3 blocks, so the tail block holds EOM, flush and zero padding only. At 8, 14 and
    20 dB the sent bytes are real in every block, the tail included (its last two frames, D1/D2 probes,
    are left out of its comparison as the window check leaves them out), and one flipped bit at 20 dB
    is NOT REAL."""
    data = (b'DE AAM402 whole blocks ' + random.Random('reencode_window/whole2400').randbytes(540))[:540]
    ok = True
    # noise seeds where the check before this case read right bytes NOT REAL: 8 dB 1 and 2 (seed 2 in whole
    # blocks 1 and 3 too), 14 dB 10 and 15 (tail probe 0.965, data 0.929 / 0.927)
    for snr, seed in ((8.0, 1), (8.0, 2), (14.0, 10), (14.0, 15), (20.0, 1)):
        wav = audio(a, data, snr, None, seed, f'whole2400_{snr:g}_{seed}', mode='2400S')
        out = run_check(a, wav, data)
        v = verdicts(out)
        # right bytes are never NOT REAL; at 20 dB one wrong byte would show, so they are real
        good = form_ok(out, f'2400S 540 B {snr:g} dB seed {seed}') and sorted(v) == [1, 2, 3, 4] \
            and 'NOT REAL' not in v.values() and TAIL_RE.findall(out) == ['4'] \
            and (snr < 20 or set(v.values()) == {'real'})
        if snr == 20.0:
            bad = bytearray(data)
            bad[300] ^= 0x10
            v2 = verdicts(run_check(a, wav, bytes(bad)))
            good &= v2.get(2) == 'NOT REAL'
            print(f'2400S 540 B {snr:g} dB one wrong bit in block 2: verdicts {v2}', flush=True)
        ok &= good
        print(f'2400S 540 B {snr:g} dB seed {seed}: verdicts {v} -> {"PASS" if good else "FAIL"}', flush=True)
        os.remove(wav)
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
            good = verdict != 'NOT REAL' if exact else verdict != 'real'
            ok &= good
            print(f'fade seed {seed} receiver block {b}: bytes {"exact" if exact else "wrong"}, verdict {verdict} '
                  f'-> {"PASS" if good else "FAIL"}', flush=True)
    # the sent bytes: real
    v = verdicts(run_check(a, wav, data))
    good = bool(v) and 'NOT REAL' not in v.values()
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


SWEEP_MODES = ('150S', '150L', '300S', '300L', '600S', '600L', '1200S', '1200L', '2400S', '2400L')


def block_bits(mode):
    """Data bits per interleaver block: a block lasts as long as the preamble."""
    return int(round(rc.BPS[mode[:-1]] * (4.8 if mode[-1] == 'L' else 0.6)))


def block_bytes(mode, b, n):
    """Byte range [lo, hi) of a payload of n bytes whose bits block b (1-based) carries."""
    bits = block_bits(mode)
    return min(n, (b - 1) * bits // 8), min(n, -(-b * bits // 8))


def sweep_one(a, mode, snr, i):
    """One random-length eom=1 message: the receiver's bytes (each verdict against whether its block is
    exact), the sent bytes (never NOT REAL) and the sent bytes with one bit flipped (its block never real).
    Verdicts are counted as real / NOT VERIFIABLE / NOT REAL."""
    r = random.Random(f'reencode_window/sweep {mode} {snr} {i}')
    bits = block_bits(mode)
    if mode in ('150S', '300S') and i % 2:  # long enough that the block lines' whole-byte count can reach the EOM
        n = r.randrange(900, 1401)
    else:
        n = r.randrange(8, (3 if mode[-1] == 'S' else 2) * bits // 8 + 1)
    data = (b'DE AAM402 sweep ' + r.randbytes(n))[:n]
    w = argparse.Namespace(**vars(a))
    w.work = os.path.join(a.work, f'sweep_{mode}_{snr:g}_{i}')
    os.makedirs(w.work, exist_ok=True)
    wav = audio(w, data, snr, None, 100 + i, 'msg', mode=mode)
    V3 = ('real', 'NOT VERIFIABLE', 'NOT REAL')
    res = dict(mode=mode, snr=snr, n=n, missing=[], eom=0, **{f'{key} {x}': 0 for key in ('rx exact', 'rx wrong', 'sent', 'flip')
                                                                for x in V3})
    sr, x = wavfile.read(wav)
    msgs = list(rc.messages(rc.decode(a.rx, x.astype(float) / 32768.0, sr, 0.0, w.work)[0]))
    pls = [pl for _, pl in msgs if pl]
    res['eom'] = sum(kv.get('eom') == '1' for kv, _ in msgs)
    out = run_check(w, wav)
    v = verdicts(out)
    pl = pls[0] if pls else b''
    for b, verdict in v.items():
        lo, hi = block_bytes(mode, b, n)
        exact = len(pl) == n and pl[lo:hi] == data[lo:hi]
        res[f"rx {'exact' if exact else 'wrong'} {verdict}"] += 1
    out = run_check(w, wav, data)
    v = verdicts(out)
    total = -(-(8 * n + rc.EOM_BITS) // bits)
    cover = re.search(r'^  tail check \(eom=1, \d+ blocks?\): the block lines cover', out, re.M)
    res['missing'] = [b for b in range(1, total + 1) if b not in v]
    if not (cover or TAIL_RE.search(out)):
        res['missing'].append('silent tail')
    if 'no message' in out or not v:
        res['missing'] = ['no message']
    for x in v.values():
        res[f'sent {x}'] += 1
    at = r.randrange(16, n) if n > 16 else n - 1
    bad = bytearray(data)
    bit = r.randrange(8)
    bad[at] ^= 1 << bit
    blk = (8 * at + bit) // bits + 1  # ModemTX sends each byte LSB first (api/modem_tx.cpp)
    v = verdicts(run_check(w, wav, bytes(bad)))
    if blk in v:
        res[f'flip {v[blk]}'] += 1
    os.remove(wav)
    return res


def case_sweep(a):
    """Random-length eom=1 messages in all ten probe modes at each --sweep-snr: no right bytes NOT REAL, no
    wrong block real (the receiver's or one flipped bit), no block left without a verdict. Counts are
    real / NOT VERIFIABLE / NOT REAL."""
    from concurrent.futures import ThreadPoolExecutor
    jobs = [(m, float(s), i) for s in a.sweep_snr.split(',') for m in SWEEP_MODES for i in range(a.sweep_n)]
    with ThreadPoolExecutor(a.jobs) as ex:
        results = list(ex.map(lambda j: sweep_one(a, *j), jobs))
    ok = True
    print('sweep: mode  SNR  msgs  eom=1  real/NOT VERIFIABLE/NOT REAL for: rx exact blocks | rx wrong blocks | '
          'sent bytes | one flipped bit  ; blocks without a verdict')
    for s in a.sweep_snr.split(','):
        for m in SWEEP_MODES:
            rs = [x for x in results if x['mode'] == m and x['snr'] == float(s)]
            g = lambda key: sum(x[key] for x in rs)
            t = lambda key: '/'.join(str(g(f'{key} {x}')) for x in ('real', 'NOT VERIFIABLE', 'NOT REAL'))
            miss = [(x['n'], x['missing']) for x in rs if x['missing']]
            bad = g('rx exact NOT REAL') or g('sent NOT REAL') or g('rx wrong real') or g('flip real') or miss
            ok &= not bad
            print(f"sweep: {m:5} {float(s):4.0f} {len(rs):5}  {g('eom'):5}  {t('rx exact'):>11} | {t('rx wrong'):>8} | "
                  f"{t('sent'):>11} | {t('flip'):>8}  ; {miss or 'none'}{'  FAIL' if bad else ''}", flush=True)
    print(f'sweep -> {"PASS" if ok else "FAIL"}', flush=True)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tool', required=True)
    ap.add_argument('--rx', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--fade-snr', type=float, default=10.0)
    ap.add_argument('--fade-seeds', default='4,6', help=f'noise seeds of the fade case, of {sorted(BAD)}')
    ap.add_argument('--cases', default='counts,clean,short,tails,whole2400,fade',
                    help='of counts,clean,short,tails,whole2400,fade,sweep (sweep is long and not in the default)')
    ap.add_argument('--sweep-snr', default='8,20')
    ap.add_argument('--sweep-n', type=int, default=4, help='messages per mode and SNR')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--check', default=None, help='the reencode_check.py to run (default: the one beside this file)')
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    data = payload()
    ok = True
    for c in a.cases.split(','):
        ok &= {'counts': lambda: case_counts(), 'clean': lambda: case_clean(a, data),
               'short': lambda: case_short(a), 'tails': lambda: case_tails(a), 'whole2400': lambda: case_whole2400(a),
               'fade': lambda: all([case_fade(a, data, int(sd)) for sd in a.fade_seeds.split(',')]),
               'sweep': lambda: case_sweep(a)}[c]()
    print('reencode_window: all cases pass' if ok else 'reencode_window: FAILED')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
