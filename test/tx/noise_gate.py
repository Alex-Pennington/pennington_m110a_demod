"""The decode-quality gate refuses noise (ctest tx_noise_gate).

For 4800S and 2400S: a ModemTX message (RRC, 400 bytes) whose data part, everything
after the 3-frame preamble, is replaced by white noise at the signal's RMS. m110a_rx
(AUTO) finds the preamble and decodes the noise; every message it reports must say
success=0.

Usage: python3 -I -B test/tx/noise_gate.py --tool TOOL --rx RX --work DIR [--seeds N]
"""
import argparse
import os
import subprocess
import sys

import numpy as np
from scipy.io import wavfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harris"))
import modem  # noqa: E402

LEAD_S, PREAMBLE_S = 0.5, 0.6  # modem_tx_wav's default lead; 3 frames of 480 symbols


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True)
    ap.add_argument("--rx", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--seeds", type=int, default=10)
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    bad = n = 0
    for mode in ("4800S", "2400S"):
        for seed in range(a.seeds):
            d = os.path.join(a.work, f"{mode}_{seed}")
            rng = np.random.default_rng(seed)
            with open(d + ".bin", "wb") as f:
                f.write(rng.integers(0, 256, 400, dtype=np.uint8).tobytes())
            subprocess.run([a.tool, "--mode", mode, d + ".bin", d + ".c.wav"], check=True,
                           capture_output=True)
            sr, x = wavfile.read(d + ".c.wav")
            x = x.astype(float)
            start = int((LEAD_S + PREAMBLE_S) * sr)
            end = np.nonzero(x)[0][-1]
            sig = np.sqrt(np.mean(x[np.abs(x) > 0] ** 2))
            x[start:end] = rng.normal(0, sig, end - start)
            wavfile.write(d + ".wav", sr, np.clip(np.round(x), -32768, 32767).astype(np.int16))
            modem.to_iq(d + ".wav", d + ".cf32")
            for m in modem.run_rx(a.rx, d + ".cf32"):
                n += 1
                ok = m.get("success") != "1"
                bad += not ok
                print(f"{mode} seed {seed}: mode={m.get('mode')} success={m.get('success')} "
                      f"q={m.get('quality_db')} eom={m.get('eom')} -> {'PASS' if ok else 'FAIL'}")
            for e in (".bin", ".c.wav", ".wav", ".cf32"):
                os.remove(d + e)
    ok = bad == 0 and n > 0
    print(f"tx_noise_gate: {n} messages from preamble + noise, {bad} with success=1 -> "
          f"{'PASS' if ok else 'FAILED'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
