"""Measure reencode_check's verdicts on right and wrong bytes through gale-rx's audio path, for one or more
versions of the check on identical audio, and tabulate them (docs/m110a-timing-steps.md, "Carrier offset").

  run     per input: ModemTX MODE, a random payload of 1.1 to 2.5 interleaver blocks; the station's carrier
          FOFF_MIN to FOFF_MAX Hz off, either sign; AWGN in 3 kHz at SNR, or hfchansim --preset PRESET at SNR;
          0.5 s of noise before it, at 48 kHz. Then gale-rx's path: 48 -> 8 kHz S16, cut 0.1 s before the
          signal, and m110a_rx at the dial offsets of gale-rx's search in order (0, -50, 50, -100, 100), the
          first with a success kept (else the first with bytes). Each version of the check (in process, the
          decode shared) is run on:
            R      the sent bytes
            W1-W3  the sent bytes with 1, 2 or 3 bytes changed
            WB     the sent bytes with one whole interleaver block's bytes random
            RND    random bytes, the sent length
            N      the receiver's own bytes, when they differ from the sent ones
          One JSON line per input and variant: the block verdicts per version, the blocks holding wrong bytes,
          the receiver's success / eom / acquired offset.
  report  the tables: per channel, mode and SNR and per version, right messages refused (any block NOT REAL)
          and their ok / NOT VERIFIABLE split; wrong messages accepted (every block real); wrong blocks
          called real; with Clopper-Pearson 95 % bounds. success=1 messages only (gale-rx checks those),
          unless --all.

usage: python3 -I reencode_calib.py run --rx M110A_RX --tx MODEM_TX_WAV --out OUT.jsonl --mode M --snr S [S ..]
           --seeds A B --versions NAME=PATH [NAME=PATH ..] --work DIR [--chan HFCHANSIM --preset P]
           [--foff-min 0] [--foff-max 110] [--offsets 0,-50,50,-100,100]
       python3 -I reencode_calib.py report FILE.jsonl [..] [--all] [--by mode|variant]
"""
import argparse
import contextlib
import importlib.util
import io
import json
import os
import re
import subprocess
import sys
import types
from collections import Counter, defaultdict

import numpy as np
from scipy.io import wavfile
from scipy.signal import hilbert, resample_poly

SR = 48000
VARIANTS = ('R', 'W1', 'W2', 'W3', 'WB', 'RND', 'N')


def block_bytes(mode):
    return int(int(mode[:-1]) * (4.8 if mode[-1] == 'L' else 0.6) / 8)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def tx_audio(txp, mode, payload, work):
    b, w = os.path.join(work, 'c.bin'), os.path.join(work, 'c.wav')
    with open(b, 'wb') as f:
        f.write(payload)
    subprocess.run([txp, '--mode', mode, '--lead', '0', '--tail', '0', b, w], check=True, capture_output=True)
    sr, x = wavfile.read(w)
    os.remove(b)
    os.remove(w)
    assert sr == SR
    return x.astype(float)


def synth(x, snr, foff, seed, work, chan=None, preset=None, lead=0.5, tail=1.0):
    """The station's audio foff Hz off, through AWGN (SNR in 3 kHz) or hfchansim, then gale-rx's 8 kHz cut."""
    if foff:
        x = np.real(hilbert(x) * np.exp(2j * np.pi * foff * np.arange(len(x)) / SR))
    y = np.concatenate([np.zeros(int(lead * SR)), x, np.zeros(int(tail * SR))])
    if chan:
        y *= 3000 / np.sqrt(np.mean(x * x))
        pin, pout = os.path.join(work, 'ch.in.pcm'), os.path.join(work, 'ch.out.pcm')
        np.clip(np.round(y), -32768, 32767).astype('<i2').tofile(pin)
        subprocess.run([chan, pin, pout, '--preset', preset, '--snr', str(snr), '--seed', str(seed)], check=True,
                       capture_output=True)
        y = np.fromfile(pout, '<i2').astype(float)
        os.remove(pin)
        os.remove(pout)
    else:
        rng = np.random.default_rng([seed, 7])
        ps = np.mean(x * x)
        y = y + rng.normal(0, np.sqrt(8 * ps / 10 ** (snr / 10)), len(y))  # 3 kHz of the 24 kHz band
    y *= 3000 / np.sqrt(np.mean(y * y))
    y48 = np.clip(np.round(y), -32768, 32767)
    y8 = np.clip(np.round(resample_poly(y48, 1, 6)), -32768, 32767).astype('<i2')  # gale-rx's to_8k_mono
    return y8[int((lead - 0.1) * 8000):]  # gale-rx's cut, 0.1 s before the signal


def verdicts(out):
    return {int(b): v for b, v in
            re.findall(r'^  block (\d+) window check: .*; verdict (real|NOT REAL|NOT VERIFIABLE)', out, re.M)}


def run(a):
    os.makedirs(a.work, exist_ok=True)
    vers = {}
    for nv in a.versions:
        n, p = nv.split('=', 1)
        vers[n] = load('rc_' + n, p)
    ref = next(iter(vers.values()))
    orig_decode = ref.decode
    txcache = {}

    def txs(txp, mode, pl, work, m):
        if (mode, pl) not in txcache:
            if len(txcache) > 64:
                txcache.clear()
            txcache[(mode, pl)] = m._orig_tx(txp, mode, pl, work)
        return txcache[(mode, pl)]
    for m in vers.values():
        m._orig_tx = m.tx_symbols
        m.tx_symbols = (lambda mm: (lambda txp, mode, pl, work: txs(txp, mode, pl, work, mm)))(m)
    offsets = [float(v) for v in a.offsets.split(',')]
    bb = block_bytes(a.mode)
    chan = f'{a.preset}' if a.chan else 'awgn'
    with open(a.out, 'a') as fo:
        for snr in a.snr:
            for seed in range(a.seeds[0], a.seeds[1]):
                rng = np.random.default_rng([seed, int(snr * 10) + 1000, sum(map(ord, a.mode)),
                                             sum(map(ord, chan))])
                n = int(rng.integers(int(1.1 * bb), int(2.5 * bb) + 1))
                payload = rng.integers(0, 256, n, dtype=np.uint8).tobytes()
                foff = float(rng.uniform(a.foff_min, a.foff_max)) * (1 if rng.integers(2) else -1)
                x8 = synth(tx_audio(a.tx, a.mode, payload, a.work), snr, foff, seed, a.work, a.chan, a.preset)
                cut = os.path.join(a.work, 'cut.wav')
                wavfile.write(cut, 8000, x8)
                variants = {'R': payload}
                for k in (1, 2, 3):
                    w = bytearray(payload)
                    for i in rng.choice(n, k, replace=False):
                        w[i] = (w[i] + int(rng.integers(1, 256))) % 256
                    variants[f'W{k}'] = bytes(w)
                w = bytearray(payload)
                b0 = int(rng.integers(0, max(1, n // bb))) * bb
                w[b0:b0 + bb] = rng.integers(0, 256, len(w[b0:b0 + bb]), dtype=np.uint8).tobytes()
                variants['WB'] = bytes(w)
                variants['RND'] = rng.integers(0, 256, n, dtype=np.uint8).tobytes()
                sr, xr = wavfile.read(cut)
                xf = xr.astype(float) / 32768.0
                pick = None
                for off in offsets:  # gale-rx's decode_cut: the first success, else the first with bytes
                    r = orig_decode(a.rx, xf, sr, off, a.work, dump=True)
                    ms = list(ref.messages(r[0]))
                    ok = bool(ms and ms[0][1] and ms[0][0].get('success') == '1')
                    if ms and ms[0][1] and (pick is None or (ok and not pick[2])):
                        pick = (off, r, ok)
                    if ok:
                        break
                head = {'chan': chan, 'mode': a.mode, 'snr': snr, 'seed': seed, 'foff': round(foff, 2), 'nbytes': n}
                if pick is None:
                    fo.write(json.dumps(dict(head, msg=False)) + '\n')
                    continue
                off, dec, _ = pick
                kv, got = next(ref.messages(dec[0]))
                rxw = sum(p != q for p, q in zip(got, payload)) + abs(len(got) - len(payload))
                if rxw:
                    variants['N'] = got
                base = dict(head, msg=True, dial=off, success=int(kv.get('success', 0)), eom=int(kv.get('eom', 0)),
                            rx_bytes=len(got), rx_wrong=rxw, acq_foff=kv.get('acq_foff_hz'))
                for name, claimed in variants.items():
                    wrongpos = [i for i in range(len(claimed)) if i >= len(payload) or claimed[i] != payload[i]]
                    if len(claimed) < len(payload):
                        wrongpos.append(len(claimed))
                    bfile = os.path.join(a.work, 'claimed.bin')
                    with open(bfile, 'wb') as f:
                        f.write(claimed)
                    row = dict(base, variant=name, wrong_bytes=len(wrongpos),
                               wrong_blocks=sorted({i // bb + 1 for i in wrongpos}))
                    for vn, m in vers.items():
                        m.decode = lambda *args, _d=dec, **kw: _d
                        ns = types.SimpleNamespace(rx=a.rx, tx=a.tx, offset_hz=off, bytes=bfile, frames_json=None,
                                                   capture=cut)
                        buf = io.StringIO()
                        try:
                            with contextlib.redirect_stdout(buf):
                                m.check(ns, xf, sr, a.work)
                            err = None
                        except Exception as e:  # noqa: BLE001
                            err = f'{type(e).__name__}: {str(e)[:200]}'
                        hd = re.search(r'data start ([0-9.]+) s \(probe coherence ([0-9.]+)', buf.getvalue())
                        row[vn] = {'v': {str(k): s for k, s in verdicts(buf.getvalue()).items()}, 'err': err,
                                   'd0': float(hd.group(1)) if hd else None}
                    fo.write(json.dumps(row) + '\n')
                    fo.flush()
                for f in os.listdir(a.work):
                    if f.startswith(('payload', 'tx', 'in.', 'rx48k', 'claimed', 'cut')):
                        os.remove(os.path.join(a.work, f))


def cp(k, n):
    from scipy.stats import beta
    if not n:
        return float('nan'), float('nan')
    return (0.0 if k == 0 else float(beta.ppf(0.025, k, n - k + 1)),
            1.0 if k == n else float(beta.ppf(0.975, k + 1, n - k)))


def fmt(k, n):
    lo, hi = cp(k, n)
    return f'{k}/{n} ({100 * lo:.1f}-{100 * hi:.1f} %)' if n else '0/0'


def msg_verdict(r, ver):
    d = r.get(ver) or {}
    v = list((d.get('v') or {}).values())
    if d.get('err') or not v:
        return 'not checked'
    return 'wrong' if 'NOT REAL' in v else 'unverifiable' if 'NOT VERIFIABLE' in v else 'ok'


def report(a):
    rows = [json.loads(ln) for f in a.files for ln in open(f)]
    rows = [r for r in rows if r.get('msg') and (a.all or r.get('success') == 1)]
    vers = [k for k in ('before', 'after_carrier', 'after_power', 'after') if any(k in r for r in rows)]
    vers += sorted({k for r in rows for k, v in r.items() if isinstance(v, dict) and 'v' in v} - set(vers))
    groups = defaultdict(list)
    for r in rows:
        key = r.get('chan', 'awgn'), r['mode'], r['snr'] if a.by == 'mode' else r['variant']
        groups[key].append(r)
        groups[(r.get('chan', 'awgn'), 'all', 'all')].append(r)
        groups[('all', 'all', 'all')].append(r)
    print('| Channel | Mode | ' + ('SNR' if a.by == 'mode' else 'Variant') + ' | Version | Right refused | '
          'Right ok / not verifiable | Wrong messages accepted | Wrong caught (NOT REAL) | Wrong blocks real |')
    print('|---|---|---|---|---|---|---|---|---|')
    order = lambda k: tuple((x == 'all', str(x) if not isinstance(x, float) else f'{x:08.1f}') for x in k)
    for key in sorted(groups, key=order):
        g = groups[key]
        for ver in vers:
            R = [r for r in g if r['variant'] == 'R' and ver in r]
            W = [r for r in g if r['variant'] != 'R' and ver in r]
            rv = Counter(msg_verdict(r, ver) for r in R)
            wv = Counter(msg_verdict(r, ver) for r in W)
            wb = [(r[ver].get('v') or {}).get(str(b)) for r in W for b in r['wrong_blocks']
                  if str(b) in (r[ver].get('v') or {})]
            print(f"| {key[0]} | {key[1]} | {key[2]} | {ver} | {fmt(rv['wrong'], len(R))} | "
                  f"{rv['ok']} / {rv['unverifiable']} | {fmt(wv['ok'], len(W))} | {fmt(wv['wrong'], len(W))} | "
                  f"{fmt(wb.count('real'), len(wb))} |")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('run')
    r.add_argument('--rx', required=True)
    r.add_argument('--tx', required=True)
    r.add_argument('--out', required=True)
    r.add_argument('--mode', required=True)
    r.add_argument('--snr', type=float, nargs='+', required=True)
    r.add_argument('--seeds', type=int, nargs=2, required=True)
    r.add_argument('--versions', nargs='+', required=True)
    r.add_argument('--work', required=True)
    r.add_argument('--chan', default=None)
    r.add_argument('--preset', default='moderate')
    r.add_argument('--foff-min', type=float, default=0.0)
    r.add_argument('--foff-max', type=float, default=110.0)
    r.add_argument('--offsets', default='0,-50,50,-100,100')
    p = sub.add_parser('report')
    p.add_argument('files', nargs='+')
    p.add_argument('--all', action='store_true')
    p.add_argument('--by', default='mode', choices=('mode', 'variant'))
    a = ap.parse_args()
    run(a) if a.cmd == 'run' else report(a)


if __name__ == '__main__':
    main()
