"""Standard-mode loopback for ModemTX::encode (ctest tx_loopback_*).

payload -> modem_tx_wav (ModemTX::encode, RRC or rectangular pulses, 48 kHz) -> white
noise at --snr dB in 3 kHz (the synth_iq.py measure) -> I/Q (test/harris/modem.py's
route) -> m110a_rx (AUTO) -> the payload bytes again, with success=1, eom=1 and the
mode named from D1/D2.

Usage: python3 -I -B test/tx/tx_loopback.py --tool TOOL --rx RX --work DIR
           [--modes 150S,...] [--snr inf,10] [--rect] [--seeds 1] [--bytes 120[,...]]
           [--ber]
--bytes takes a list of payload lengths; every length runs in every case.
--fp counts false positives instead: a case fails only when m110a_rx says success=1
and the bytes are wrong (the quality gate at low SNR, docs/TX_CHAIN.md).
--ber prints the byte and bit error count of each case and does not fail on errors
(the 4800S SNR survey in docs/TX_CHAIN.md).
"""
import argparse
import os
import random
import subprocess
import sys

import numpy as np
from scipy.io import wavfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harris"))
import modem  # noqa: E402

MODES = "150S,150L,300S,300L,600S,600L,1200S,1200L,2400S,2400L,4800S"


def add_noise(src, dst, snr, seed):
    sr, x = wavfile.read(src)
    x = x.astype(float)
    if np.isfinite(snr):
        act = x[np.abs(x) > 0]
        ps = np.mean(act ** 2)
        # real white noise spreads over 0..sr/2; SNR counts the 3 kHz of it in band
        sigma = np.sqrt(ps / 10 ** (snr / 10) * (sr / 2) / 3000.0)
        x = x + np.random.default_rng(seed).normal(0, sigma, len(x))
    wavfile.write(dst, sr, np.clip(np.round(x), -32768, 32767).astype(np.int16))


def run_case(a, mode, snr, seed, nbytes):
    tag = f"{mode}_{'rect' if a.rect else 'rrc'}_{snr:g}_{seed}_{nbytes}B"
    rng = random.Random(f"{mode}/{seed}/{nbytes}")
    data = bytes(rng.randrange(256) for _ in range(nbytes))
    binf, clean, wav, iq = (os.path.join(a.work, tag + e) for e in (".bin", ".clean.wav", ".wav", ".cf32"))
    with open(binf, "wb") as f:
        f.write(data)
    p = subprocess.run([a.tool, "--mode", mode] + (["--rect"] if a.rect else []) + [binf, clean],
                       capture_output=True, text=True)
    if p.returncode != 0:
        print(f"{tag}: modem_tx_wav failed: {p.stderr.strip()}")
        return False
    add_noise(clean, wav, snr, seed)
    modem.to_iq(wav, iq)
    msgs = modem.run_rx(a.rx, iq)
    for f in (iq, clean, wav, binf):
        os.remove(f)
    m = msgs[0] if len(msgs) == 1 else {}
    got = bytes.fromhex(m.get("hex", ""))
    # ModemTX::encode ends the message with the EOM 4B65A5B2 and m110a_rx cuts there:
    # the bytes must be the payload, no more and no fewer
    ok = (len(msgs) == 1 and m.get("success") == "1" and m.get("eom") == "1"
          and m.get("mode") == mode and got == data)
    head = got[:len(data)]
    berr = sum(x != y for x, y in zip(head, data)) + max(0, len(data) - len(got))
    bits = sum(bin(x ^ y).count("1") for x, y in zip(head, data)) + 8 * max(0, len(data) - len(got))
    print(f"{tag}: messages={len(msgs)} mode={m.get('mode')} success={m.get('success')} "
          f"eom={m.get('eom')} q={m.get('quality_db')} bytes {len(got)}/{len(data)} "
          f"byte_err={berr} bit_err={bits} -> {'PASS' if ok else 'FAIL'}", flush=True)
    if a.fp:
        fp = m.get("success") == "1" and got != data
        if fp:
            print(f"{tag}: FALSE POSITIVE", flush=True)
        return not fp
    return ok or a.ber


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True)
    ap.add_argument("--rx", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--modes", default=MODES)
    ap.add_argument("--snr", default="inf,10")
    ap.add_argument("--rect", action="store_true")
    ap.add_argument("--seeds", default="1")
    ap.add_argument("--bytes", default="120")
    ap.add_argument("--ber", action="store_true")
    ap.add_argument("--fp", action="store_true")
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    ok = True
    for mode in a.modes.split(","):
        for snr in (float(s) for s in a.snr.split(",")):
            for seed in (int(s) for s in a.seeds.split(",")):
                for nbytes in (int(n) for n in a.bytes.split(",")):
                    ok &= run_case(a, mode, snr, seed, nbytes)
    print("tx_loopback: all cases exact" if ok else "tx_loopback: FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
