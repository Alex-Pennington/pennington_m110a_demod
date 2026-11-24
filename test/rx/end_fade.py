"""Message end in m110a_rx: fades, lead-in, a real end, a cut message (ctest rx_end_fade).

ModemTX (modem_tx_wav, 300L) -> a gain envelope and white noise at --snr dB in 3 kHz, the
SNR of the unfaded signal -> I/Q (test/harris/modem.py) -> m110a_rx (AUTO).

  fade     a slow fade to -12 dB and back (4 s down, 2 s at the bottom, 3 s up) and a 0.5 s
           dropout to -20 dB inside the data: the exact payload with EOM.
  leadin   the fade case after 0.05, 0.3, 1.0 and 3.0 s of noise: the same end, counted
           from the message start (within 2 ms), and the same bytes, exact with EOM.
  end      the message, then 15 s of noise: exact with EOM, and the window ends within
           1 s after the signal does.
  cut      the message stopped halfway through its third interleaver block (no EOM), then
           noise: success, a whole-block prefix of the payload, nothing after it.
  two      two messages 1 s apart: both exact with EOM.
  split    two 0.5 s dropouts inside the 24-frame preamble, which split it into three
           preamble chains: one message, exact with EOM, preamble_chains=3.
  paths    two paths 2 ms (4.8 symbols) apart whose gains swap every preamble frame, so
           the correlation peak moves 4.8 symbols from frame to frame and every frame is a
           chain of its own: one message, exact with EOM, from at least 12 chains.
  lowsnr   the message at 0 dB, then 40 s of noise: the end is a 3 dB step the step test
           misses, so the window would run to the end of the input; its first 30 s are
           decoded first and kept, since the signal ends inside them: one message, exact
           with EOM, the window ending at most 31 s after the start.

Usage: python3 -I -B test/rx/end_fade.py --tool MODEM_TX_WAV --rx M110A_RX --work DIR
           [--snr 18] [--seed 1] [--cases fade,leadin,end,cut,two,split,paths,lowsnr]
The payload text starts "DE AAM402".
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

MODE = "300L"
PREAMBLE_S = 4.8            # 24 frames of 0.2 s
BLOCK_S = 4.8               # 300L interleaver block
BLOCK_BYTES = 180           # 300 bit/s * 4.8 s / 8


def payload(n, seed):
    rng = random.Random(f"end_fade/{seed}/{n}")
    head = b"DE AAM402 end-of-message test "
    return head + bytes(rng.randrange(256) for _ in range(n - len(head)))


def tx(a, data, tag):
    binf, wav = (os.path.join(a.work, tag + e) for e in (".bin", ".tx.wav"))
    with open(binf, "wb") as f:
        f.write(data)
    subprocess.run([a.tool, "--mode", MODE, "--lead", "0", "--tail", "0", binf, wav],
                   check=True, capture_output=True)
    sr, x = wavfile.read(wav)
    os.remove(binf)
    os.remove(wav)
    return sr, x.astype(float)


def sigma_for(x, sr, snr):
    ps = np.mean(x[np.abs(x) > 0] ** 2)
    return np.sqrt(ps / 10 ** (snr / 10) * (sr / 2) / 3000.0)


def fade_envelope(n, sr):
    """Gain over the message: a slow fade from 3 s into the data, a dropout later."""
    t = np.arange(n) / sr - PREAMBLE_S       # seconds into the data
    db = np.zeros(n)
    f0 = 3.0
    db = np.where((t >= f0) & (t < f0 + 4), -12 * (t - f0) / 4, db)
    db = np.where((t >= f0 + 4) & (t < f0 + 6), -12, db)
    db = np.where((t >= f0 + 6) & (t < f0 + 9), -12 * (1 - (t - f0 - 6) / 3), db)
    db = np.where((t >= 14.0) & (t < 14.5), -20, db)
    return 10 ** (db / 20)


def rx(a, x, sr, tag):
    wav, iq = (os.path.join(a.work, tag + e) for e in (".wav", ".cf32"))
    wavfile.write(wav, sr, np.clip(np.round(x), -32768, 32767).astype(np.int16))
    modem.to_iq(wav, iq)
    msgs = modem.run_rx(a.rx, iq)
    os.remove(wav)
    os.remove(iq)
    for m in msgs:
        m["data"] = bytes.fromhex(m.get("hex", ""))
    return msgs


def exact(m, data):
    return m.get("success") == "1" and m.get("eom") == "1" and m.get("mode") == MODE and m["data"] == data


def show(tag, msgs, ok, extra=""):
    parts = [f"start={m.get('start_s')} end={m.get('end_s')} mode={m.get('mode')} success={m.get('success')} "
             f"eom={m.get('eom')} bytes={len(m['data'])}" for m in msgs]
    print(f"{tag}: messages={len(msgs)} " + "; ".join(parts) + (f" {extra}" if extra else "")
          + f" -> {'PASS' if ok else 'FAIL'}", flush=True)
    return ok


def noise(sr, seconds, sigma, seed):
    return np.random.default_rng(seed).normal(0, sigma, int(round(seconds * sr)))


def faded_message(a, data, tag):
    sr, s = tx(a, data, tag)
    sigma = sigma_for(s, sr, a.snr)
    body = s * fade_envelope(len(s), sr)
    tail = np.zeros(int(3 * sr))
    body = np.concatenate([body, tail])
    return sr, body + noise(sr, len(body) / sr, sigma, a.seed), sigma, len(s) / sr


def case_fade(a):
    data = payload(600, a.seed)
    sr, body, sigma, _ = faded_message(a, data, "fade")
    x = np.concatenate([noise(sr, 1.0, sigma, a.seed + 100), body])
    msgs = rx(a, x, sr, "fade")
    return show("fade", msgs, len(msgs) == 1 and exact(msgs[0], data))


def case_leadin(a):
    data = payload(600, a.seed)
    sr, body, sigma, _ = faded_message(a, data, "leadin")
    lead_all = noise(sr, 3.0, sigma, a.seed + 200)
    ends, ok = [], True
    for lead in (0.05, 0.3, 1.0, 3.0):
        x = np.concatenate([lead_all[len(lead_all) - int(round(lead * sr)):], body])
        msgs = rx(a, x, sr, f"leadin_{lead:g}")
        if len(msgs) == 1:
            ends.append(float(msgs[0]["end_s"]) - float(msgs[0]["start_s"]))
        ok &= show(f"leadin {lead:g} s", msgs, len(msgs) == 1 and exact(msgs[0], data))
    spread = max(ends) - min(ends) if len(ends) == 4 else float("inf")
    ok &= spread <= 0.002
    print(f"leadin: end - start over the 4 lead-ins spreads {spread * 1000:.1f} ms (at most 2 ms)"
          f" -> {'PASS' if spread <= 0.002 else 'FAIL'}", flush=True)
    return ok


def case_end(a):
    data = payload(400, a.seed)
    sr, s = tx(a, data, "end")
    sigma = sigma_for(s, sr, a.snr)
    lead = 1.0
    x = np.concatenate([np.zeros(int(lead * sr)), s, np.zeros(int(15 * sr))])
    x = x + noise(sr, len(x) / sr, sigma, a.seed + 300)
    msgs = rx(a, x, sr, "end")
    true_end = lead + len(s) / sr
    late = float(msgs[0]["end_s"]) - true_end if msgs else float("inf")
    ok = len(msgs) == 1 and exact(msgs[0], data) and 0.0 <= late <= 1.0
    return show("end", msgs, ok, f"signal end {true_end:.3f} s, window end {late:+.3f} s from it")


def case_cut(a):
    data = payload(900, a.seed)                  # 5 blocks; stop in the third
    sr, s = tx(a, data, "cut")
    sigma = sigma_for(s, sr, a.snr)
    stop = int((PREAMBLE_S + 2.5 * BLOCK_S) * sr)
    x = np.concatenate([np.zeros(int(1.0 * sr)), s[:stop], np.zeros(int(10 * sr))])
    x = x + noise(sr, len(x) / sr, sigma, a.seed + 400)
    msgs = rx(a, x, sr, "cut")
    m = msgs[0] if msgs else {"data": b""}
    got = m["data"]
    ok = (len(msgs) == 1 and m.get("success") == "1" and m.get("eom") == "0"
          and 0 < len(got) <= 2 * BLOCK_BYTES and len(got) % BLOCK_BYTES == 0 and got == data[:len(got)])
    return show("cut", msgs, ok, f"bytes {len(got)} of the {2 * BLOCK_BYTES} the 2 whole blocks carry,"
                f" prefix of the payload {got == data[:len(got)]}")


def case_two(a):
    d1, d2 = payload(300, a.seed), payload(300, a.seed + 1)
    sr, s1 = tx(a, d1, "two_1")
    _, s2 = tx(a, d2, "two_2")
    sigma = sigma_for(s1, sr, a.snr)
    x = np.concatenate([np.zeros(int(1.0 * sr)), s1, np.zeros(int(1.0 * sr)), s2, np.zeros(int(5 * sr))])
    x = x + noise(sr, len(x) / sr, sigma, a.seed + 500)
    msgs = rx(a, x, sr, "two")
    ok = len(msgs) == 2 and exact(msgs[0], d1) and exact(msgs[1], d2)
    return show("two", msgs, ok)


def case_split(a):
    data = payload(300, a.seed)
    sr, s = tx(a, data, "split")
    sigma = sigma_for(s, sr, a.snr)
    t = np.arange(len(s)) / sr
    g = np.where(((t >= 1.0) & (t < 1.5)) | ((t >= 2.6) & (t < 3.1)), 10 ** (-30 / 20), 1.0)
    x = np.concatenate([np.zeros(int(1.0 * sr)), s * g, np.zeros(int(6 * sr))])
    x = x + noise(sr, len(x) / sr, sigma, a.seed + 600)
    msgs = rx(a, x, sr, "split")
    ok = len(msgs) == 1 and exact(msgs[0], data) and msgs[0].get("preamble_chains") == "3"
    return show("split", msgs, ok, f"preamble chains {msgs[0].get('preamble_chains') if msgs else None}")


def case_paths(a):
    data = payload(300, a.seed)
    sr, s = tx(a, data, "paths")
    sigma = sigma_for(s, sr, a.snr)
    d = int(round(0.002 * sr))
    late = np.concatenate([np.zeros(d), s[:-d]])
    t = np.arange(len(s)) / sr
    g = np.where((t // 0.2) % 2 == 0, 1.0, 0.3)      # frame k: path 1 at 1 or 0.3, path 2 the other
    y = g * s + (1.3 - g) * late
    x = np.concatenate([np.zeros(int(1.0 * sr)), y, np.zeros(int(6 * sr))])
    x = x + noise(sr, len(x) / sr, sigma, a.seed + 700)
    msgs = rx(a, x, sr, "paths")
    ch = int(msgs[0].get("preamble_chains", 0)) if msgs else 0
    ok = len(msgs) == 1 and exact(msgs[0], data) and ch >= 12
    return show("paths", msgs, ok, f"preamble chains {ch}")


def case_lowsnr(a):
    data = payload(300, a.seed)
    sr, s = tx(a, data, "lowsnr")
    sigma = sigma_for(s, sr, 0.0)
    x = np.concatenate([np.zeros(int(1.0 * sr)), s, np.zeros(int(40 * sr))])
    x = x + noise(sr, len(x) / sr, sigma, a.seed + 800)
    msgs = rx(a, x, sr, "lowsnr")
    win = float(msgs[0]["end_s"]) - float(msgs[0]["start_s"]) if msgs else float("inf")
    ok = len(msgs) == 1 and exact(msgs[0], data) and win <= 31.0
    return show("lowsnr", msgs, ok, f"window {win:.1f} s")


CASES = {"fade": case_fade, "leadin": case_leadin, "end": case_end, "cut": case_cut, "two": case_two, "split": case_split,
         "paths": case_paths, "lowsnr": case_lowsnr}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True)
    ap.add_argument("--rx", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--snr", type=float, default=18.0)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--cases", default=",".join(CASES))
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    ok = True
    for c in a.cases.split(","):
        ok &= CASES[c](a)
    print("end_fade: all cases pass" if ok else "end_fade: FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
