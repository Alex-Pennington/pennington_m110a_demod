"""Gate: m110a_rx's I/Q front end must match a scipy reference chain within 1 dB of SNR.

usage: python3 -I frontend_vs_scipy.py --rx PATH/m110a_rx --ref DIR [--work DIR] [--tol 1.0]

Both chains take 2 MS/s I/Q with the dial at +100 kHz to 48 kHz USB audio:
  m110a_rx   NCO, IQSource decimation, 255-tap USB filter (m110a_rx --dump-audio)
  scipy      NCO, resample_poly(3, 125) (Kaiser polyphase), the same USB filter
             designed with firwin
Both chains are linear, so a signal-only input (an MS-DMT 600S reference
transmission, made USB analytic) and a noise-only input (complex white noise
over the full 2 MHz) are run separately, and each chain's output SNR is the
ratio of their powers in 300-3300 Hz. The input SNR is the same for both,
so any gap is the front end's own loss.
"""
import argparse
import glob
import os
import subprocess
import sys
import tempfile

import numpy as np
from scipy.signal import firwin, lfilter, resample_poly

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument('--rx', required=True)
ap.add_argument('--ref', required=True)
ap.add_argument('--work', default='')
ap.add_argument('--tol', type=float, default=1.0)
a = ap.parse_args()
work = a.work or tempfile.mkdtemp(prefix='fe_vs_scipy.', dir=os.getcwd())
os.makedirs(work, exist_ok=True)
FS, OFF = 2_000_000, 100000.0

sig = os.path.join(work, 'sig.cf32')
noise = os.path.join(work, 'noise.cf32')
ref = glob.glob(os.path.join(a.ref, 'tx_600S_*.pcm'))[0]
subprocess.run([sys.executable, '-I', os.path.join(HERE, 'synth_iq.py'), ref, sig, '--snr', 'inf',
                '--format', 'cf32', '--lead', '0.2', '--tail', '0.2'], check=True,
               stderr=subprocess.DEVNULL)
n = os.path.getsize(sig) // 8
rng = np.random.default_rng(5)
(0.01 * (rng.standard_normal(n) + 1j * rng.standard_normal(n))).astype(np.complex64).tofile(noise)


def rx_chain(path):
    out = path + '.f32'
    # --threshold 2: no preamble can match, so this only runs the front end;
    # exit status 1 (nothing decoded) is expected
    p = subprocess.run([a.rx, '--mode', '600S', '--threshold', '2', '--dump-audio', out,
                        '--format', 'cf32', '--offset', repr(OFF), path], capture_output=True)
    if p.returncode not in (0, 1):
        sys.exit(f'm110a_rx failed: {p.stderr.decode()}')
    return np.fromfile(out, dtype='<f4').astype(np.float64)


def scipy_chain(path):
    z = np.fromfile(path, dtype=np.complex64).astype(np.complex128)
    z *= np.exp(-2j * np.pi * OFF * np.arange(len(z)) / FS)
    b = resample_poly(z, 3, 125)
    h = firwin(255, 1500, fs=48000) * np.exp(2j * np.pi * 1800 * (np.arange(255) - 127) / 48000)
    return lfilter(h, 1, b).real


def band_power(y):
    y = y[4800:-4800]  # drop filter transients at both ends
    Y = np.fft.rfft(y)
    f = np.fft.rfftfreq(len(y), 1 / 48000)
    m = (f >= 300) & (f <= 3300)
    return 2 * np.sum(np.abs(Y[m]) ** 2) / len(y) ** 2


res = {}
for name, chain in (('m110a_rx', rx_chain), ('scipy', scipy_chain)):
    ps, pn = band_power(chain(sig)), band_power(chain(noise))
    res[name] = 10 * np.log10(ps / pn)
    print(f'{name:9s} SNR(300-3300 Hz) = {res[name]:.2f} dB')
gap = res['scipy'] - res['m110a_rx']
print(f'gap (scipy - m110a_rx) = {gap:+.2f} dB, tolerance {a.tol} dB')
for p in (sig, noise, sig + '.f32', noise + '.f32'):
    os.remove(p)
sys.exit(0 if abs(gap) <= a.tol else 1)
