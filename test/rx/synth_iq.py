"""Synthesise what an RSP2 at 2 MS/s would record for a 110A USB transmission.

usage: python3 -I synth_iq.py IN.pcm OUT [--offset HZ] [--snr DB] [--seed N]
           [--foff HZ] [--lead S] [--tail S] [--rate HZ] [--format cs16|cf32]

IN is 48 kHz int16 audio with the 110A carrier at 1800 Hz (for example the
MS-DMT reference transmissions in release/examples/refrence_pcm). The audio is
band-limited to 300-3300 Hz, as an SSB transmitter would, made analytic (USB:
audio f -> dial + f), resampled 48 kHz -> 2 MS/s, and moved to +offset Hz
(+ foff Hz of mistuning). Complex white noise is added over the full input
band at the density that gives SNR dB in 3 kHz (signal power measured over
the transmission). lead / tail seconds of noise only go before and after.
Output is interleaved I/Q, int16 (total RMS ~3000 per rail, like
rsp2_stream.py --record PREFIX.cs16) or float32.
"""
import argparse
import sys

import numpy as np
from scipy.signal import firwin, filtfilt, hilbert, resample_poly

ap = argparse.ArgumentParser()
ap.add_argument('inp')
ap.add_argument('out')
ap.add_argument('--offset', type=float, default=100000.0)
ap.add_argument('--snr', type=float, default=20.0)
ap.add_argument('--seed', type=int, default=1)
ap.add_argument('--foff', type=float, default=0.0)
ap.add_argument('--lead', type=float, default=0.1)
ap.add_argument('--tail', type=float, default=0.3)
ap.add_argument('--rate', type=float, default=2e6)
ap.add_argument('--format', choices=('cs16', 'cf32'), default='cs16')
a = ap.parse_args()

FS_A = 48000
if a.rate % 1000 or int(a.rate) % 16:
    sys.exit('rate must be a multiple of 16 kHz... use 2e6, 1e6, 96e3, ...')
up, down = int(a.rate) // 16000, 3          # rate / 48000 = (rate/16000) / 3
x = np.fromfile(a.inp, dtype='<i2').astype(np.float64)
x = filtfilt(firwin(511, [300, 3300], pass_zero=False, fs=FS_A), 1, x)
n_lead, n_sig = int(a.lead * FS_A), len(x)
x = np.concatenate([np.zeros(n_lead), x, np.zeros(int(a.tail * FS_A))])
z = resample_poly(hilbert(x).astype(np.complex64), up, down)   # complex64: long modes at 2 MS/s
del x
s0 = int(n_lead * a.rate / FS_A)
s1 = s0 + int(n_sig * a.rate / FS_A)
ps = float(np.mean(np.abs(z[s0:s1]).astype(np.float64) ** 2))
sigma = np.sqrt(ps / 10 ** (a.snr / 10) * (a.rate / 3000.0) / 2) if np.isfinite(a.snr) else 0.0
# Output level: total RMS ~3000 per rail on the int16 scale.
g = 3000.0 / np.sqrt((float(np.mean(np.abs(z).astype(np.float64) ** 2)) + 2 * sigma ** 2) / 2)
rng = np.random.default_rng(a.seed)
w = 2 * np.pi * (a.offset + a.foff) / a.rate
CH = int(a.rate)  # 1 s chunks keep memory flat
with open(a.out, 'wb') as fo:
    for i in range(0, len(z), CH):
        n = np.arange(i, min(i + CH, len(z)))
        c = z[i:i + CH].astype(np.complex128) * np.exp(1j * (w * n))
        if sigma > 0:
            c = c + sigma * (rng.standard_normal(len(c)) + 1j * rng.standard_normal(len(c)))
        c *= g
        if a.format == 'cs16':
            iq = np.empty(2 * len(c), dtype='<i2')
            iq[0::2] = np.clip(np.round(c.real), -32768, 32767)
            iq[1::2] = np.clip(np.round(c.imag), -32768, 32767)
        else:
            iq = np.empty(2 * len(c), dtype='<f4')
            iq[0::2] = c.real / 32768
            iq[1::2] = c.imag / 32768
        iq.tofile(fo)
print(f"{a.out}: {len(z)} I/Q @ {a.rate:.0f} ({len(z) / a.rate:.2f} s), dial +{a.offset:.0f} Hz "
      f"{a.foff:+g} Hz, SNR(3k)={a.snr} dB, signal at {a.lead:.2f} s", file=sys.stderr)
