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
    to +-30 Hz) and removed. A frame's data are then phased from the probes around them (the
    frame before's and its own), its probes from the probes of the frames either side (never
    their own: under multipath a gain measured on a frame's own fixed probe pattern decides
    those probes better than the data), and each symbol is decided among the mode's points
    (BPSK, QPSK, 8PSK) turned by its scrambler value. Without the offset removed, the data, 20
    symbols ahead of the probes, turned 35 degrees against them at +11.6 Hz on air, and right
    bytes scored as wrong.
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

That holds where the channel is even over the block. A fade inside a block lowers probe and
data agreement together, and there the wrong codeword the Viterbi decoder chose agrees with
the received symbols as well as the right one would, so a block can "fit" with wrong bytes
(300L at 10 dB with a -15 dB/3 s fade: 12 wrong bytes, data 0.013 below probes; 1200L with a
-15 dB/1 s fade: 1 to 9 wrong bytes, within 0.01). So after each block line come new lines
(the block line is unchanged; gale-rx parses it):
  - a row per 8-frame window: probes right, data agreeing, and far-off probes and data;
  - "block N window check: ...; verdict real" or "verdict NOT REAL: ...".
A symbol is far off when it is decided as another point than the one sent (for data, the
re-encoded one) and lies more than TAU noise powers from where that one should land, the noise
and the channel's gain measured on the 2 x k probes around it. The interleaver spreads a wrong
codeword's symbols over the block, so some land where the signal is clean, and there a wrong
symbol sits on the point that was sent, a whole constellation step (tens of noise powers) from
the re-encoded one. Right bytes put data symbols far off only as noise or a crash puts probes
there. The verdict is NOT REAL when the block does not fit, or when its far-off data symbols
are less likely than ALPHA for right bytes (window_check), and real otherwise. For the verdict,
"fit" is read over the frames window_check reads (the block's last TAIL frames, whose probes
are D1/D2, left out), and data below the probes by more than FIT is "not fit" only when right
bytes leave such a gap with odds under ALPHA (fits(): a short block's agreements are samples).
A block that passes is "real" only where one wrong byte would have shown, by the far-off test or
the gap (one_byte_power(), measured on the block); elsewhere the verdict is "NOT VERIFIABLE".

With eom=1, the blocks after the block lines (the data end, EOM, flush and zero padding) are
compared too, as "tail block N" lines with their own window check; when the block lines already
reach the message's last block, one "tail check" line says so.

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
DMIN2 = {r: abs(1 - PSK8[s]) ** 2 for r, s in STEP8.items()}  # squared distance to the nearest other point
TAUS = (6, 8, 10, 12, 15, 20, 30)  # soft thresholds counted per frame, in units of the noise power


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


def frame_agreement(bb, d0, tb, t0, rate, nf, off, soft=False, fpb=None):
    """Per frame: (probe symbols decided right, data symbols equal to the re-encoded ones),
    each out of the frame's probe and data symbol counts. fpb: frames per interleaver block."""
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
    # Phase references, none holding the symbol it phases. A frame's data lie between the probes of the
    # frame before and its own: they are phased from those. A frame's probes are phased from the probes of
    # the frames either side, not their own: the scrambler repeats every 160 symbols, so each frame's probes
    # are a fixed pattern, and under multipath the gain measured on them takes in their own echo (inter-symbol
    # interference) and decides them better than the data, whose echo is random. Phased from their own frame,
    # probes on a Watterson channel scored 0.02-0.05 above the data of right bytes, past FIT.
    # A block's last TAIL frames carry D1/D2 probes, not the scrambler: they phase nothing (fpb, the frames
    # per block, marks them); a side without a reference frame is left out, and with neither side a frame
    # falls back to the next frame's probes, then to its own.
    zv = zs if not fpb else np.where(np.arange(nf) % fpb >= fpb - TAIL, 0, zs)
    prev, nxt = np.append(0, zv[:-1]), np.append(zv[1:], 0)
    gp = prev + nxt
    gp = np.where(np.abs(gp) > 0, gp, zs)
    gd = prev + zv
    gd = np.where(np.abs(gd) > 0, gd, nxt)
    gd = np.where(np.abs(gd) > 0, gd, zs)
    # decide each symbol among the mode's points (BPSK, QPSK or 8PSK) turned by its scrambler value
    st = STEP8[rate]
    dec = (np.round(((np.angle(r * np.conj(np.where(isp, gp[f], gd[f]))) / (np.pi / 4) - scr) % 8) / st).astype(int)
           * st + scr) % 8
    t = t * np.conj(np.sum(t[isp] * np.conj(ref[isp])))  # the re-encoded signal's own phase
    tsym = np.round(np.angle(t) / (np.pi / 4)).astype(int) % 8
    pok = np.where(isp, dec == scr, False).reshape(nf, -1).sum(1)
    dok = np.where(~isp, dec == tsym, False).reshape(nf, -1).sum(1)
    if not soft:
        return pok, dok
    # Soft: each symbol's distance from where the sent symbol would land, in units of the noise
    # (for data, the re-encoded symbol).
    # A frame's data lies between the probes of the frame before and its own: G, the channel's
    # gain and phase, comes from those 2k probes, and the noise power from the median of their
    # residuals (median / ln 2 is the mean of an exponential; one crash does not move it).
    rp, refp = r[isp].reshape(nf, k), ref[isp].reshape(nf, k)
    rd, td = r[~isp].reshape(nf, u), PSK8[tsym[~isp]].reshape(nf, u)
    zp = rp * np.conj(refp)
    gsum = zp.sum(1) + np.append(zp[0].sum(), zp[:-1].sum(1))
    g2 = (gsum / (2 * k))[:, None]
    prev_rp = np.vstack([rp[:1], rp[:-1]])
    prev_ref = np.vstack([refp[:1], refp[:-1]])
    res_p = np.abs(rp - g2 * refp) ** 2
    s2 = np.median(np.hstack([np.abs(prev_rp - g2 * prev_ref) ** 2, res_p]), 1) / np.log(2) + 1e-30
    eps_p = res_p / s2[:, None]
    eps_d = np.abs(rd - g2 * td) ** 2 / s2[:, None]
    # far off: decided as another point, and further than tau noise powers from the sent one
    miss_p = (dec[isp] != scr[isp]).reshape(nf, k)
    miss_d = (dec[~isp] != tsym[~isp]).reshape(nf, u)
    taus = np.array(TAUS)
    deep_p = ((eps_p[:, :, None] > taus) & miss_p[:, :, None]).sum(1)
    deep_d = ((eps_d[:, :, None] > taus) & miss_d[:, :, None]).sum(1)
    sep = DMIN2[rate] * np.abs(g2[:, 0]) ** 2 / s2  # the nearest other point, in units of the noise
    return pok, dok, deep_p, deep_d, sep


def agreement(bb, d0, tb, t0, rate, nf, blocks, off, fpb=None):
    """Per block: (probe agreement, data agreement with the re-encoded symbols)."""
    u, k = FRAME[rate]
    pok, dok = frame_agreement(bb, d0, tb, t0, rate, nf, off, fpb=fpb)
    fpb = nf // blocks
    b = np.arange(nf) // fpb
    return [(float(pok[b == i].sum() / (k * np.sum(b == i))), float(dok[b == i].sum() / (u * np.sum(b == i))))
            for i in range(blocks)]


WIN = 8       # frames per window in the printed rows, as track() follows the grid
TAIL = 2      # a block's last frames, whose probes differ from the scrambler's (left out)
TAU = 15      # a symbol is far off when it lies more than TAU noise powers from where it should
RHO = 2.0     # right bytes: data symbols far off at most RHO times as often as probes
ALPHA = 1e-6  # a block is not real when its far-off data symbols are this unlikely for right bytes


def windows(n, win=WIN):
    """Window edges over n frames; a last window under win/2 frames joins the one before."""
    e = list(range(0, n, win)) + [n]
    if len(e) > 2 and e[-1] - e[-2] < win // 2:
        e.pop(-2)
    return list(zip(e[:-1], e[1:]))


def poisson_sf(d, lam):
    """P(X >= d) for X ~ Poisson(lam)."""
    if d <= 0:
        return 1.0
    from scipy.stats import poisson
    return float(poisson.sf(d - 1, lam))


def window_check(pok, dok, deep_p, deep_d, u, k):
    """The far-off test of one block from its per-frame counts (its last TAIL frames left out).

    A fade inside a block lowers probe and data agreement together, and there the wrong
    codeword the Viterbi decoder chose agrees with the received symbols as well as the right
    one would. Averaged over the block, the faded frames hide the disagreements the wrong
    bytes leave in the rest of it: the interleaver spreads a wrong codeword's symbols over the
    whole block, so some land where the signal is clean. There a wrong symbol is not a near
    miss: the received symbol sits on the point that was sent, a whole constellation step from
    the re-encoded one, tens of noise powers away. So each symbol's distance from where the
    re-encoded symbol should land is measured in units of the noise the probes around it show
    (frame_agreement, soft), and a symbol decided as another point and further than TAU is far
    off. (A symbol near the decision boundary is a near miss, not far off, however small the
    noise: the clean frames of right bytes on air show such outliers at 12 to 20 noise powers.) With the right bytes a
    data symbol is far off only as noise or a crash puts a probe there: at most RHO times the
    probes' rate in the block, plus the Gaussian tail exp(-TAU) per symbol. The block is
    'not real' when its far-off data symbols are less likely than ALPHA under that, else 'real'.
    Returns (rows, summary): rows per window (first frame, last frame, probe errors, probes,
    data errors, data symbols, far-off probes, far-off data); summary with the counts, the
    expected far-off data symbols, p and the verdict."""
    n = max(1, len(pok) - TAIL)
    rows = []
    for a, z in windows(n):
        rows.append((a, z - 1, int(k * (z - a) - pok[a:z].sum()), k * (z - a), int(u * (z - a) - dok[a:z].sum()),
                     u * (z - a), int(deep_p[a:z].sum()), int(deep_d[a:z].sum())))
    npr, nd = k * n, u * n
    fp, fd = int(deep_p[:n].sum()), int(deep_d[:n].sum())
    expected = RHO * fp * nd / npr + nd * np.exp(-TAU)
    p = poisson_sf(fd, expected)
    return rows, dict(probes=npr, data=nd, probe_ok=int(pok[:n].sum()), data_ok=int(dok[:n].sum()),
                      far_probes=fp, far_data=fd, expected=expected, p=p, verdict='not real' if p < ALPHA else 'real')


EOM_BITS = 32 + 144  # EOM 4B65A5B2 and the coder flush, after the data (M110ACodec)


def tail(a, bb, kv, payload, mode, rate, label, start, coh, d0, d, tbs, dumps, first):
    """With eom=1, the blocks the per-block check leaves out: the data's last, partial block, the
    EOM and flush, and zero padding to the end of the interleaver block, all of which the
    re-encoded reference carries (ModemTX, include_eom). Printed as 'tail block N' lines with
    their window rows and window checks. d: the data start the whole blocks chose, or None.
    first: the blocks the block lines printed. Those count a block in whole bytes (22 B for
    300S's 22.5, 11 B for 150S's 11.25), so they can already reach the EOM's block; the blocks
    here are counted in bits, and when the block lines reach the last block one line says so."""
    if kv.get('eom') != '1':
        return
    u, k = FRAME[rate]
    p = u + k
    pre_s = 4.8 if mode[-1] == 'L' else 0.6
    bits = int(round(BPS[rate] * pre_s))            # data bits per interleaver block
    total = -(-(8 * len(payload) + EOM_BITS) // bits)
    if total <= first:
        data_end = max(1, -(-8 * len(payload) // bits))
        print(f'  tail check (eom=1, {total} block{"s" if total > 1 else ""}): the block lines cover the data end '
              f'(block {data_end}), EOM, flush and zero padding (block {total})'
              + ('' if first == total else f'; block line {first} lies past the message end' if first == total + 1
                 else f'; block lines {total + 1}-{first} lie past the message end'))
        return
    fpb = int(round(pre_s * 2400 / p))
    nframes = total * fpb
    tb, t0 = tbs['decoded']
    period = int(np.lcm(160, p)) * SPS
    room = lambda dd: min((len(bb) - dd) // (p * SPS) - 1, (len(tb) - t0) // (p * SPS) - 1)
    if d is None:  # no whole block chose the start: choose it over all the blocks, as check() does
        best = None
        for m in range(-((d0 - start) // period), 2):
            dd = d0 + m * period
            if room(dd) < nframes:
                continue
            off = track(bb, dd, u, k, nframes)
            score = np.mean([r[1] for r in agreement(bb, dd, tb, t0, rate, nframes, total, off, fpb)])
            if best is None or score > best[0]:
                best = (score, dd)
        if best is None:
            print('  tail not checked (the signal ends before the last block does)')
            return
        d = best[1]
        steps = np.flatnonzero(np.abs(np.diff(track(bb, d, u, k, nframes))) >= 8 * SPS)
    if room(d) < nframes:
        print('  tail not checked (the signal ends before the last block does)')
        return
    off = track(bb, d, u, k, nframes)
    ctb, ct0 = tbs['control']
    _, cdok = frame_agreement(bb, d, ctb, ct0, rate, min(nframes, (len(ctb) - ct0) // (p * SPS) - 1), off,
                              fpb=fpb)
    pok, dok, deep_p, deep_d, sep = frame_agreement(bb, d, tb, t0, rate, nframes, off, soft=True, fpb=fpb)
    if first == 0:
        steps = np.flatnonzero(np.abs(np.diff(off)) >= 8 * SPS)
        # indented and without the label: a second head line would read as a second message
        print(f'  tail check (eom=1, {total} block{"s" if total > 1 else ""}): data start {d / FS:.4f} s '
              f'(probe coherence {coh:.2f}), grid steps '
              + (', '.join(f'frame {s + 1} {(off[s + 1] - off[s]) / SPS:+.1f} sym' for s in steps) or 'none'))
    if a.frames_json:
        dumps.append({'mode': mode, 'start_s': kv.get('start_s'), 'bytes': len(payload),
                      'sha256': hashlib.sha256(payload).hexdigest()[:12], 'blocks': total, 'tail_from': first,
                      'frames_per_block': fpb, 'u': u, 'k': k,
                      'probe_ok': pok.tolist(), 'data_ok': dok.tolist(), 'taus': list(TAUS),
                      'deep_probe': deep_p.tolist(), 'deep_data': deep_d.tolist(),
                      'sep': [round(float(v), 2) for v in sep]})
    # The tail block's figures and fit come from the frames the window check reads (its last TAIL
    # frames left out), as the window check's verdict does: there the probes are D1/D2, not the
    # scrambler, and the data of the message's last frames is phased from them alone.
    n = max(1, fpb - TAIL)
    for b in range(first, total):
        f0 = b * fpb
        pa, da = pok[f0:f0 + n].sum() / (k * n), dok[f0:f0 + n].sum() / (u * n)
        ca = cdok[f0:f0 + n].sum() / (u * n) if f0 + n <= len(cdok) else float('nan')
        verdict = 'fit the signal' if fits(pa, da, k * n, u * n) else 'DO NOT FIT the signal (bit errors)'
        print(f'  tail block {b + 1} (data end, EOM, flush, zero padding): probe agreement {pa:.3f}, data agreement '
              f'{da:.3f}, random-bytes control {ca:.3f} (chance {CHANCE[rate]:.3f}): the bytes {verdict}')
        print_window_check(b, f0, fpb, pok, dok, deep_p, deep_d, u, k, sep, rate)


def fit_allowance(pa, npr, nd):
    """How far data agreement may sit below probe agreement with right bytes: FIT, or what right
    bytes leave with odds of ALPHA where that is more. The probe and data agreements of a short block
    are samples: a 2400S block at 8 dB (448 probes, 896 data symbols over its window frames) spreads
    their difference by about 0.02 with right bytes."""
    from scipy.stats import norm
    return max(FIT, float(norm.isf(ALPHA)) * np.sqrt(max(pa * (1 - pa), 0.0) * (1 / npr + 1 / nd)))


def fits(pa, da, npr, nd):
    """Data agreement within fit_allowance() of the probe agreement, or above it."""
    return da >= pa - fit_allowance(pa, npr, nd)


CODE_WEIGHT = 10  # coded bits one wrong data bit changes: the K=7 code's generators 133 and 171 (octal), 5 + 5
REPEAT = {'150': 4, '300': 2, '600': 1, '1200': 1, '2400': 1}  # data symbols per coded bit
POWER = 0.99      # a one-byte error 'would show' when the window check catches it this often


def one_byte_power(s, sep, rate):
    """Whether the window check would have caught one wrong byte in this block, measured on the block.
    One wrong data bit changes CODE_WEIGHT coded bits, so CODE_WEIGHT x REPEAT data symbols of the
    re-encoded reference: d symbols on another point than the one sent (the nearest one, a Gray
    neighbour or BPSK's other point). Two ways to see them:
      - far off: a symbol on the sent point lies sep noise powers from the re-encoded one (sep, the
        frame's measured distance to the nearest other point), so it is far off when |s + n|^2 > TAU,
        noise of unit power: a non-central chi-square, 2 degrees of freedom. The test fires when the
        far-off count reaches what makes p < ALPHA for right bytes; power is the chance d symbols
        bring that many more.
      - the gap: the d symbols lower data agreement by d (p - c (1 - p)) / data symbols (c the share
        of wrong decisions that land on the re-encoded point: 1 for BPSK, 1/2 otherwise), against
        fit_allowance().
    Returns (verifiable, summary)."""
    from scipy.stats import binom, ncx2
    d = CODE_WEIGHT * REPEAT[rate]
    pf = float(np.mean(ncx2.sf(2 * TAU, 2, 2 * np.asarray(sep, float)))) if len(sep) else 0.0
    need = 1
    while poisson_sf(s['far_data'] + need, s['expected']) >= ALPHA and need <= d:
        need += 1
    far_power = float(binom.sf(need - 1, d, pf)) if need <= d else 0.0
    pa = s['probe_ok'] / s['probes']
    c = 1.0 if STEP8[rate] == 4 else 0.5
    shift = d * max(pa - c * (1 - pa), 0.0) / s['data']
    allow = fit_allowance(pa, s['probes'], s['data'])
    ok = far_power >= POWER or shift > allow
    return ok, (f"a one-byte error would not show at this block's SNR ({d} data symbols moved, far-off power "
                f'{far_power:.2f} with {need} more far off needed, gap shift {shift:.3f} against {allow:.3f})')


def print_window_check(b, f0, fpb, pok, dok, deep_p, deep_d, u, k, sep, rate):
    """The new lines after block b's line: its window rows and its window check verdict. The verdict's
    probe and data agreements come from the window frames (window_check: the block's last TAIL
    frames left out), not from the block line's figures over every frame. 'real' only where one wrong
    byte would have shown (one_byte_power); where it would not, NOT VERIFIABLE."""
    ti = TAUS.index(TAU)
    wrows, s = window_check(pok[f0:f0 + fpb], dok[f0:f0 + fpb], deep_p[f0:f0 + fpb, ti],
                            deep_d[f0:f0 + fpb, ti], u, k)
    for i, (w0, w1, pe, pn, de, dn, fp, fd) in enumerate(wrows):
        print(f'    block {b + 1} window {i + 1} frames {f0 + w0 + 1}-{f0 + w1 + 1}: probes {1 - pe / pn:.3f} '
              f'({pn - pe}/{pn} right), data {1 - de / dn:.3f} ({dn - de}/{dn} agree), '
              f'far off {fp} probes, {fd} data')
    n = max(1, min(fpb, len(sep) - f0) - TAIL)
    if not fits(s['probe_ok'] / s['probes'], s['data_ok'] / s['data'], s['probes'], s['data']):
        final = 'NOT REAL: data below the probes over the block (bit errors)'
    elif s['verdict'] == 'not real':
        final = 'NOT REAL: data symbols far off where the probes are not (bit errors)'
    else:
        ok, why = one_byte_power(s, sep[f0:f0 + n], rate)
        final = 'real' if ok else f'NOT VERIFIABLE: {why}'
    print(f"  block {b + 1} window check: far off (over {TAU} noise powers) {s['far_probes']}/{s['probes']} "
          f"probes, {s['far_data']}/{s['data']} data, expected for right bytes {s['expected']:.2f}, "
          f"p {s['p']:.2g} (not real under {ALPHA:g}); verdict {final}")


def check(a, x, sr, work):
    out, rx = decode(a.rx, x, sr, a.offset_hz, work, dump=True)
    bb = baseband(rx)
    found = False
    dumps = []
    claimed = open(a.bytes, 'rb').read() if a.bytes else None
    for kv, payload in messages(out):
        if claimed is not None and payload:  # --bytes: these bytes in place of the first message's
            payload, claimed = claimed, None
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
            if kv.get('eom') != '1':
                continue
        nframes = blocks * int(round(pre_s * 2400 / p))
        start = int(float(kv['start_s']) * FS)
        coh, d0 = find_data_start(bb, start + int((pre_s - 0.3) * FS), start + int((pre_s + 0.3) * FS), u, k)
        tbs = {}
        rng = np.random.default_rng(1)
        for which, pl in (('decoded', payload), ('control', rng.integers(0, 256, len(payload), dtype=np.uint8).tobytes())):
            tb = tx_symbols(a.tx, mode, pl, work)
            tbs[which] = (tb, find_data_start(tb, 0, len(tb) - 1, u, k)[1])
        if blocks == 0:
            tail(a, bb, kv, payload, mode, rate, label, start, coh, d0, None, tbs, dumps, 0)
            continue
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
            rows = agreement(bb, d, tb, t0, rate, nf, blocks, off, nframes // blocks)
            score = np.mean([r[1] for r in rows])
            if best is None or score > best[0]:
                best = (score, d, nf, off, rows)
        _, d, nf, off, rows = best
        tb, t0 = tbs['control']
        ctl = agreement(bb, d, tb, t0, rate, min(nf, (len(tb) - t0) // (p * SPS) - 1), blocks, off, nframes // blocks)
        pok, dok, deep_p, deep_d, sep = frame_agreement(bb, d, tbs['decoded'][0], tbs['decoded'][1], rate, nf, off,
                                                        soft=True, fpb=nframes // blocks)
        if a.frames_json:
            dumps.append({'mode': mode, 'start_s': kv.get('start_s'), 'bytes': len(payload),
                          'sha256': hashlib.sha256(payload).hexdigest()[:12], 'blocks': blocks,
                          'frames_per_block': nframes // blocks, 'u': u, 'k': k,
                          'probe_ok': pok.tolist(), 'data_ok': dok.tolist(), 'taus': list(TAUS),
                          'deep_probe': deep_p.tolist(), 'deep_data': deep_d.tolist(),
                          'sep': [round(float(v), 2) for v in sep]})
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
            print_window_check(b, b * fpb, fpb, pok, dok, deep_p, deep_d, u, k, sep, rate)
        if blocks:
            tail(a, bb, kv, payload, mode, rate, label, start, coh, d0, d, tbs, dumps, blocks)
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
    ap.add_argument('--bytes', default=None,
                    help="check these bytes in place of the receiver's for the first message with bytes")
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
