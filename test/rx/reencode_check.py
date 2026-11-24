"""Check decoded 110A bytes against the received signal: decode, re-encode, compare symbols.

usage: python3 -I reencode_check.py CAPTURE.wav --rx M110A_RX --tx MODEM_TX_WAV [--offset-hz F]
       python3 -I reencode_check.py CAPTURE.wav --rx M110A_RX --sweep T0 T1 STEP MS

110A carries no CRC, and m110a_rx's success=1 comes from probe quality, not from the
bytes. This is the payload check that needs no CRC.

Check (--tx): run m110a_rx (AUTO) over CAPTURE (mono audio, any rate, carrier at 1800 Hz
USB, as m110a_check.py feeds it). For each standard-mode message with whole interleaver
blocks, re-encode its bytes with this repo's ModemTX (modem_tx_wav) and compare symbols
with the received ones:
  - The receive grid comes from the probes, not from the receiver, tracked over 8-frame
    windows +-50 symbols so a timing step does not break it. The probes repeat every
    lcm(160, frame) symbols, so the data start is tried at each such period from the
    preamble on, and the one the decoded bytes agree with best is kept.
  - The carrier offset is measured from the probes (their phase step from frame to frame,
    to +-30 Hz) and removed. Each frame is then phased from its own probes and the next
    frame's, and each symbol is decided among the mode's points (BPSK, QPSK, 8PSK) turned by
    its scrambler value. Without the offset removed, the data, 20 symbols ahead of the probes,
    turned 35 degrees against them at +11.6 Hz on air, and right bytes scored as wrong.
  - probe agreement: probe symbols decided right, the channel's raw symbol accuracy.
  - data agreement: data symbols equal to the re-encoded ones.
  - random-bytes control: the same for random bytes of the same length, i.e. chance.
Right bytes give data agreement at the probe agreement or above. Wrong bytes give less,
though more than chance, because the Viterbi decoder returns the codeword nearest the
received symbols. So the verdict compares data with probe, not with chance: the bytes fit
when data agreement is within FIT (0.03) of the probe agreement or above. Measured on this
modem's own 300L TX:
  - exact decodes (21: AWGN, Watterson moderate and poor, 15 ms steps): gap 0.023 at most;
  - the right payload with bits flipped, probe agreement 0.93: gap 0.019 at 0.3 % bit
    errors, 0.048 at 1 %, 0.120 at 3 %, 0.301 at 10 %.
So "fit" means under about 0.5 % bit errors, and the gap says roughly how many there are.

Sweep (--sweep T0 T1 STEP MS): delete MS ms of audio at each position from T0 to T1 s,
STEP s apart, decode each and count the different payloads. A cut that repairs a timing
step gives the same bytes at every position near the step. Different bytes at each
position mean the decode depends on where the cut falls, so the bytes are not evidence.

The payload is never printed, only its length and a sha256 prefix. 75 bps (no probes) and
Harris modes are skipped by the check.
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys
import tempfile

import numpy as np
from scipy.io import wavfile
from scipy.signal import hilbert, resample_poly

FS, SPS, FC = 48000, 20, 1800.0
FRAME = {'150': (20, 20), '300': (20, 20), '600': (20, 20), '1200': (20, 20), '2400': (32, 16)}
BPS = {'150': 150, '300': 300, '600': 600, '1200': 1200, '2400': 2400}
CHANCE = {'150': 0.5, '300': 0.5, '600': 0.5, '1200': 0.25, '2400': 0.125}
FIT = 0.03  # a right payload's data agreement is within this of the probes' or above
STEP8 = {'150': 4, '300': 4, '600': 4, '1200': 2, '2400': 1}  # 8PSK index step of the data constellation
PSK8 = np.exp(1j * np.pi / 4 * np.arange(8))


def scrambler():
    s, out = [1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1], []
    for _ in range(160):
        for _ in range(8):
            c = s[11]
            s[11], s[10], s[9], s[8], s[7] = s[10], s[9], s[8], s[7], s[6]
            s[6], s[5], s[4], s[3], s[2] = s[5] ^ c, s[4], s[3] ^ c, s[2], s[1]
            s[1], s[0] = s[0] ^ c, c
        out.append((s[2] << 2) | (s[1] << 1) | s[0])
    return np.array(out)


SCR = scrambler()


def rrc(alpha=0.35, span=6):
    t = np.arange(-span * SPS, span * SPS + 1) / SPS
    h = np.empty_like(t)
    for i, x in enumerate(t):
        if abs(x) < 1e-9:
            h[i] = 1 - alpha + 4 * alpha / np.pi
        elif abs(abs(x) - 1 / (4 * alpha)) < 1e-9:
            h[i] = alpha / np.sqrt(2) * ((1 + 2 / np.pi) * np.sin(np.pi / (4 * alpha)) +
                                         (1 - 2 / np.pi) * np.cos(np.pi / (4 * alpha)))
        else:
            h[i] = (np.sin(np.pi * x * (1 - alpha)) + 4 * alpha * x * np.cos(np.pi * x * (1 + alpha))) / \
                   (np.pi * x * (1 - (4 * alpha * x) ** 2))
    return h / np.sqrt(np.sum(h ** 2))


def baseband(a):
    return np.convolve(a * np.exp(-2j * np.pi * FC * np.arange(len(a)) / FS) * 2, rrc(), mode='same')


def probes(u, k, n0, n1):
    """Probe symbol indices in [n0, n1) and their reference points."""
    n = np.arange(n0, n1)
    n = n[(n % (u + k)) >= u]
    return n, PSK8[SCR[n % 160]]


def frame_coherence(bb, pos, ref, per):
    """Mean over frames of |sum x conj(ref)|^2 / (sum |x|^2 K): frequency-insensitive."""
    pos = np.clip(pos, 0, len(bb) - 1)
    z = (bb[pos] * np.conj(ref)).reshape(*pos.shape[:-1], -1, per)
    p = (np.abs(bb[pos]) ** 2).reshape(*pos.shape[:-1], -1, per)
    return np.mean(np.abs(z.sum(-1)) ** 2 / (p.sum(-1) * per + 1e-30), -1)


def find_data_start(bb, lo, hi, u, k):
    """First data sample: the best probe grid in [lo, hi), then earlier by one period,
    lcm(160, frame) symbols (the probe grid repeats with the 160-symbol scrambler), while
    the frames of that period carry probes: their 75th-percentile frame coherence reaches
    half the median of the first 16 data frames. The percentile keeps a block's last two
    frames (D1/D2 on some transmitters) from stopping it; preamble frames stay low."""
    n, ref = probes(u, k, 0, 30 * (u + k))
    best = (0.0, lo)
    for step, a, b in ((4, lo, hi), (1, None, None)):
        if a is None:
            a, b = best[1] - 4, best[1] + 5
        cand = np.arange(max(a, 0), b, step)
        cand = cand[cand + n[-1] * SPS < len(bb)]
        if not len(cand):
            break
        for c0 in range(0, len(cand), 512):
            cs = cand[c0:c0 + 512]
            c = frame_coherence(bb, cs[:, None] + n[None, :] * SPS, ref, k)
            i = int(np.argmax(c))
            if c[i] > best[0]:
                best = (float(c[i]), int(cs[i]))
    coh, d0 = best
    p = u + k
    period = int(np.lcm(160, p))

    def frames(d, f0, f1):
        return np.array([frame_coherence(bb, (d + probes(u, k, i * p, (i + 1) * p)[0] * SPS)[None, :],
                                         probes(u, k, i * p, (i + 1) * p)[1], k)[0] for i in range(f0, f1)])

    level = np.median(frames(d0, 0, 16))
    while d0 - period * SPS - p * SPS >= 0:
        if np.percentile(frames(d0, -period // p, 0), 75) < 0.5 * level:
            break
        d0 -= period * SPS
    return coh, d0


def track(bb, d0, u, k, nframes, win=8, reach=50):
    """Per-frame grid offset (samples) from the probes over win-frame windows."""
    off, cur = np.zeros(nframes, dtype=int), 0
    cand = np.arange(-reach * SPS, reach * SPS + 1, 2)
    for w0 in range(0, nframes, win):
        w1 = min(nframes, w0 + win)
        n, ref = probes(u, k, w0 * (u + k), w1 * (u + k))
        c = frame_coherence(bb, d0 + cur + cand[:, None] + n[None, :] * SPS, ref, k)
        cur = cur + int(cand[int(np.argmax(c))])
        off[w0:w1] = cur
    return off


def tx_symbols(txp, mode, payload, work):
    b, w = os.path.join(work, 'payload.bin'), os.path.join(work, 'tx.wav')
    with open(b, 'wb') as f:
        f.write(payload)
    subprocess.run([txp, '--mode', mode, '--lead', '0.5', '--tail', '0.5', b, w], check=True,
                   capture_output=True)
    os.remove(b)
    _, x = wavfile.read(w)
    os.remove(w)
    return baseband(x.astype(float))


def decode(rx, x, sr, offset_hz, work, dump=False):
    """m110a_rx (AUTO) over audio x; returns (stdout, the 48 kHz audio it saw or None)."""
    iq, d = os.path.join(work, 'in.cf32'), os.path.join(work, 'rx48k.f32')
    z = resample_poly(hilbert(x), 96000, sr)
    z *= np.exp(2j * np.pi * 10000.0 * np.arange(len(z)) / 96000)
    np.stack([z.real, z.imag], 1).astype('<f4').tofile(iq)
    cmd = [rx, '--format', 'cf32', '--rate', '96000', '--offset', f'{10000 + offset_hz:.1f}', '--dial', '0']
    out = subprocess.run(cmd + (['--dump-audio', d] if dump else []) + [iq], capture_output=True, text=True).stdout
    os.remove(iq)
    audio = None
    if dump:
        audio = np.fromfile(d, '<f4').astype(float)
        os.remove(d)
    return out, audio


def messages(out):
    for head, body in re.findall(r'^msg=\d+ (.*)$((?:\n  .*)*)', out, re.M):
        kv = dict(re.findall(r'(\w+)=("[^"]*"|\S+)', head))
        hx = re.search(r'^  hex=(.*)$', body, re.M)
        yield kv, bytes.fromhex(hx.group(1).replace(' ', '')) if hx else b''


def frame_agreement(bb, d0, tb, t0, rate, nf, off):
    """Per frame: (probe symbols decided right, data symbols equal to the re-encoded ones),
    each out of the frame's probe and data symbol counts."""
    u, k = FRAME[rate]
    n = np.arange(nf * (u + k))
    f = n // (u + k)
    r = bb[np.clip(d0 + n * SPS + off[f], 0, len(bb) - 1)]
    t = tb[np.clip(t0 + n * SPS, 0, len(tb) - 1)]
    isp = (n % (u + k)) >= u
    scr = SCR[n % 160]
    ref = PSK8[scr]
    z = r * np.conj(ref)
    zs = np.where(isp, z, 0).reshape(nf, -1).sum(1)
    # the carrier offset, from the probes' phase step frame to frame (unambiguous to +-30 Hz)
    z = z * np.exp(-1j * np.angle(np.sum(zs[1:] * np.conj(zs[:-1]))) / (u + k) * n)
    zs = np.where(isp, z, 0).reshape(nf, -1).sum(1)
    r = z * ref
    g = zs + np.append(zs[1:], 0)  # each frame phased from its own probes and the next frame's
    # decide each symbol among the mode's points (BPSK, QPSK or 8PSK) turned by its scrambler value
    st = STEP8[rate]
    dec = (np.round(((np.angle(r * np.conj(g[f])) / (np.pi / 4) - scr) % 8) / st).astype(int) * st + scr) % 8
    t = t * np.conj(np.sum(t[isp] * np.conj(ref[isp])))  # the re-encoded signal's own phase
    tsym = np.round(np.angle(t) / (np.pi / 4)).astype(int) % 8
    pok = np.where(isp, dec == scr, False).reshape(nf, -1).sum(1)
    dok = np.where(~isp, dec == tsym, False).reshape(nf, -1).sum(1)
    return pok, dok


def agreement(bb, d0, tb, t0, rate, nf, blocks, off):
    """Per block: (probe agreement, data agreement with the re-encoded symbols)."""
    u, k = FRAME[rate]
    pok, dok = frame_agreement(bb, d0, tb, t0, rate, nf, off)
    fpb = nf // blocks
    b = np.arange(nf) // fpb
    return [(float(pok[b == i].sum() / (k * np.sum(b == i))), float(dok[b == i].sum() / (u * np.sum(b == i))))
            for i in range(blocks)]


WIN = 8          # frames per window, as track() follows the grid
CLEAN = 0.90     # a window is clean when its probe agreement, chance removed, reaches this
RATIO = 2.0      # right bytes: data errors in clean windows at most RATIO times the probes' rate
Z, C0 = 3.0, 3   # plus Z sigma of a Poisson count, plus C0 symbols


def windows(n, win=WIN):
    """Window edges over n frames; a last window under win/2 frames joins the one before."""
    e = list(range(0, n, win)) + [n]
    if len(e) > 2 and e[-1] - e[-2] < win // 2:
        e.pop(-2)
    return list(zip(e[:-1], e[1:]))


def window_check(pok, dok, rate, u, k):
    """The window check of one block from its per-frame counts.

    A fade inside a block lowers probe and data agreement together, and there the wrong
    codeword the Viterbi decoder chose agrees with the received symbols as well as the right
    one would. Averaged over the block, the faded frames then hide the disagreement the wrong
    bytes leave in the rest of the block: the interleaver spreads a wrong codeword's symbols
    over the whole block, so some land where the signal is clean and the received symbols
    are the sent ones. So the block is cut into WIN-frame windows; a window is clean when its
    probe agreement, chance removed, is CLEAN or more; and over the clean windows the data
    symbols may disagree with the re-encoded ones at most RATIO times as often as the probes
    are decided wrong there, plus Z sigma of a Poisson count and C0 symbols.
    Returns (rows, summary): rows per window (first frame, last frame, probes right, probes,
    data agreeing, data symbols, clean); summary with the counts, the allowance and the verdict
    'real', 'not real' or 'unverifiable' (no clean window)."""
    c = CHANCE[rate]
    rows = []
    for a, z in windows(len(pok)):
        pr, pn, dr, dn = int(pok[a:z].sum()), k * (z - a), int(dok[a:z].sum()), u * (z - a)
        rows.append((a, z - 1, pr, pn, dr, dn, (pr / pn - c) / (1 - c) >= CLEAN))
    cl = [r for r in rows if r[6]]
    pr, pn = sum(r[2] for r in cl), sum(r[3] for r in cl)
    dr, dn = sum(r[4] for r in cl), sum(r[5] for r in cl)
    expected = RATIO * (pn - pr) / pn * dn if pn else 0.0
    allowed = int(np.floor(expected + Z * np.sqrt(expected) + C0))
    verdict = 'unverifiable' if not cl else ('real' if dn - dr <= allowed else 'not real')
    return rows, dict(windows=len(rows), clean=len(cl), probes_right=pr, probes=pn, data_agree=dr, data=dn,
                      data_errors=dn - dr, expected=expected, allowed=allowed, verdict=verdict)


def check(a, x, sr, work):
    out, rx = decode(a.rx, x, sr, a.offset_hz, work, dump=True)
    bb = baseband(rx)
    found = False
    dumps = []
    for kv, payload in messages(out):
        found = True
        mode, rate = kv.get('mode', ''), kv.get('mode', '')[:-1]
        label = (f"{a.capture}: {mode} start {kv.get('start_s')} s success={kv.get('success')} bytes={len(payload)}"
                 + (f" sha256 {hashlib.sha256(payload).hexdigest()[:12]}" if payload else ""))
        if rate not in FRAME or mode[-1] not in 'SL' or not payload:
            print(f'{label}: not checked (no bytes, or a mode without probes / not ModemTX)')
            continue
        u, k = FRAME[rate]
        p = u + k
        pre_s = 4.8 if mode[-1] == 'L' else 0.6  # a block lasts as long as the preamble
        blocks = len(payload) // int(BPS[rate] * pre_s / 8)
        if blocks == 0:
            print(f'{label}: not checked (no whole block)')
            continue
        nframes = blocks * int(round(pre_s * 2400 / p))
        start = int(float(kv['start_s']) * FS)
        coh, d0 = find_data_start(bb, start + int((pre_s - 0.3) * FS), start + int((pre_s + 0.3) * FS), u, k)
        tbs = {}
        rng = np.random.default_rng(1)
        for which, pl in (('decoded', payload), ('control', rng.integers(0, 256, len(payload), dtype=np.uint8).tobytes())):
            tb = tx_symbols(a.tx, mode, pl, work)
            tbs[which] = (tb, find_data_start(tb, 0, len(tb) - 1, u, k)[1])
        # The probe grid repeats every period, so the probes alone leave the data start open
        # by whole periods: try each from the found preamble's start on, keep the one the
        # decoded bytes agree with best; the control is scored at that same start.
        period = int(np.lcm(160, p)) * SPS
        best = None
        for m in range(-((d0 - start) // period), 2):
            d = d0 + m * period
            nf = min(nframes, (len(bb) - d) // (p * SPS) - 1)
            if nf < nframes // blocks:
                continue
            off = track(bb, d, u, k, nf)
            tb, t0 = tbs['decoded']
            nf = min(nf, (len(tb) - t0) // (p * SPS) - 1)
            rows = agreement(bb, d, tb, t0, rate, nf, blocks, off)
            score = np.mean([r[1] for r in rows])
            if best is None or score > best[0]:
                best = (score, d, nf, off, rows)
        _, d, nf, off, rows = best
        tb, t0 = tbs['control']
        ctl = agreement(bb, d, tb, t0, rate, min(nf, (len(tb) - t0) // (p * SPS) - 1), blocks, off)
        pok, dok = frame_agreement(bb, d, tbs['decoded'][0], tbs['decoded'][1], rate, nf, off)
        if a.frames_json:
            dumps.append({'mode': mode, 'start_s': kv.get('start_s'), 'bytes': len(payload),
                          'sha256': hashlib.sha256(payload).hexdigest()[:12], 'blocks': blocks,
                          'frames_per_block': nframes // blocks, 'u': u, 'k': k,
                          'probe_ok': pok.tolist(), 'data_ok': dok.tolist()})
        steps = np.flatnonzero(np.abs(np.diff(off)) >= 8 * SPS)
        print(f'{label}: data start {d / FS:.4f} s (probe coherence {coh:.2f}), grid steps '
              + (', '.join(f'frame {s + 1} {(off[s + 1] - off[s]) / SPS:+.1f} sym' for s in steps) or 'none'))
        for b in range(blocks):
            (pa, da), ca = rows[b], ctl[b][1]
            verdict = 'fit the signal' if da >= pa - FIT else 'DO NOT FIT the signal (bit errors)'
            print(f'  block {b + 1}: probe agreement {pa:.3f}, data agreement {da:.3f}, random-bytes control '
                  f'{ca:.3f} (chance {CHANCE[rate]:.3f}): the bytes {verdict}')
            # New lines (the block line above is unchanged): the window check of this block.
            fpb = nf // blocks
            f0 = b * fpb
            wrows, s = window_check(pok[f0:f0 + fpb], dok[f0:f0 + fpb], rate, u, k)
            for i, (w0, w1, pr, pn, dr, dn, cl) in enumerate(wrows):
                print(f'    block {b + 1} window {i + 1} frames {f0 + w0 + 1}-{f0 + w1 + 1}: probes {pr / pn:.3f} '
                      f'({pr}/{pn} right), data {dr / dn:.3f} ({dr}/{dn} agree)' + (', clean' if cl else ''))
            if da < pa - FIT:
                final = 'NOT REAL: data below the probes over the block (bit errors)'
            elif s['verdict'] == 'not real':
                final = 'NOT REAL: data disagree where the probes agree (bit errors)'
            elif s['verdict'] == 'unverifiable':
                final = 'UNVERIFIABLE: no clean window'
            else:
                final = 'real'
            c = CHANCE[rate]
            print(f"  block {b + 1} window check: clean windows {s['clean']} of {s['windows']} (probes "
                  f"{c + CLEAN * (1 - c):.3f} or more right), clean probes {s['probes_right']}/{s['probes']} right, "
                  f"clean data {s['data_errors']}/{s['data']} disagree, allowed {s['allowed']} "
                  f"(expected {s['expected']:.1f}); verdict {final}")
    if not found:
        print(f'{a.capture}: no message')
    if a.frames_json:
        import json
        with open(a.frames_json, 'w') as fh:
            json.dump(dumps, fh)


def sweep(a, x, sr, work):
    """Delete a.sweep[3] ms at each position from a.sweep[0] to a.sweep[1] s, a.sweep[2] s
    apart, decode each, and count the different payloads."""
    t0, t1, dt, ms = a.sweep
    cut = int(round(ms * sr / 1000))
    seen = {}
    for t in np.arange(t0, t1 + dt / 2, dt):
        i = int(round(t * sr))
        out, _ = decode(a.rx, np.concatenate([x[:i], x[i + cut:]]), sr, a.offset_hz, work)
        res = [(kv.get('mode'), kv.get('success'), pl) for kv, pl in messages(out)]
        txt = '; '.join(f'{m} success={s} bytes={len(pl)} sha256 {hashlib.sha256(pl).hexdigest()[:12] if pl else "-"}'
                        for m, s, pl in res) or 'no message'
        for _, _, pl in res:
            if pl:
                seen[pl] = seen.get(pl, 0) + 1
        print(f'{a.capture}: {cut} samples ({1000 * cut / sr:.2f} ms) cut at {t:.3f} s: {txt}')
    n = sum(seen.values())
    print(f'{a.capture}: {n} payloads over the sweep, {len(seen)} different'
          + (f', the commonest {max(seen.values())} times' if seen else ''))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('capture')
    ap.add_argument('--rx', required=True)
    ap.add_argument('--tx')
    ap.add_argument('--offset-hz', type=float, default=0.0)
    ap.add_argument('--sweep', type=float, nargs=4, metavar=('T0', 'T1', 'STEP', 'MS'))
    ap.add_argument('--work', default=None)
    ap.add_argument('--frames-json', default=None, help='write per-frame probe/data counts here (calibration)')
    a = ap.parse_args()
    work = a.work or tempfile.mkdtemp(prefix='reencode.')
    sr, x = wavfile.read(a.capture)
    if x.ndim > 1:
        x = x[:, 0]
    x = x.astype(float) / (32768.0 if x.dtype == np.int16 else 1.0)
    if a.sweep:
        sweep(a, x, sr, work)
    elif not a.tx:
        ap.error('--tx is needed unless --sweep')
    else:
        check(a, x, sr, work)


if __name__ == '__main__':
    sys.exit(main())
