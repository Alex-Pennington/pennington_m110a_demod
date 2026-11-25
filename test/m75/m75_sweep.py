"""75 bps robustness sweep: exact-message rate and BER against SNR, AWGN and Watterson.

payload ("DE AAM402 " + random bytes) -> modem_tx_wav (ModemTX::encode, 48 kHz)
  -> [hfchansim --model watterson: two equal Rayleigh paths, fading only, no noise]
  -> white noise at --snr dB in 3 kHz (signal power over the keyed part, as
     test/tx/tx_loopback.py measures it; MIL-STD-188-110D Table XVI note: both powers in
     a 3 kHz bandwidth) -> I/Q (test/harris/modem.py) -> m110a_rx (AUTO)
  -> per case: exact (success=1, eom=1, mode named, bytes equal), success, false success
     (success=1 with wrong bytes), bytes_ok (the payload bytes exact whatever success says)
     and bit errors over the payload (bytes not delivered
     count as all bits wrong); one_msg: the transmission came out as exactly one message.

Usage: python3 -I -B test/m75/m75_sweep.py --tool MODEM_TX_WAV (--rx M110A_RX | --wav-rx M75_WAV_RX) --work DIR
           [--chansim HFCHANSIM] [--modes 75S,75L,150S,300S] [--channel awgn|moderate|poor|D,T]
           [--snr -6,-4,...] [--seeds 1,2,...] [--bytes 64] [--jobs 4]
           [--rx-args "--mode 75S"] [--require MODE:SNR:MIN_EXACT ...]
--wav-rx decodes the WAV with ModemRX directly (test/m75/wav_rx.cpp), without m110a_rx's
preamble scanner, which finds no 110A preamble below about -7 dB (docs/m110a-75bps.md).
--channel moderate = Watterson 1 Hz / 1 ms, poor = 2 Hz / 2 ms (CCIR, hfchansim's presets),
D,T = Doppler spread D Hz, delay T ms. --require makes the run fail unless MODE at SNR
decoded exact in at least MIN_EXACT of the seeds (the ctest gate); without it the run only
prints the table.
"""
import argparse
import concurrent.futures as cf
import os
import random
import subprocess
import sys

import numpy as np
from scipy.io import wavfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harris"))
import modem  # noqa: E402

CHANNELS = {"awgn": None, "moderate": (1.0, 1.0), "poor": (2.0, 2.0)}


def channel(a, src, dst, snr, seed):
    sr, x = wavfile.read(src)
    x = x.astype(float)
    ch = CHANNELS.get(a.channel) if a.channel in CHANNELS else tuple(float(v) for v in a.channel.split(","))
    if ch is not None:
        pin, pout = dst + ".in.pcm", dst + ".out.pcm"
        np.clip(np.round(x), -32768, 32767).astype("<i2").tofile(pin)
        p = subprocess.run([a.chansim, pin, pout, "--model", "watterson", "--doppler", str(ch[0]),
                            "--delay", str(ch[1]), "--seed", str(1000 + seed)],
                           capture_output=True, text=True)
        if p.returncode != 0:
            raise RuntimeError(f"hfchansim: {p.stderr.strip()} {p.stdout.strip()[-300:]}")
        x = np.fromfile(pout, dtype="<i2").astype(float)
        os.remove(pin)
        os.remove(pout)
    if np.isfinite(snr):
        act = x[np.abs(x) > 0]
        ps = np.mean(act ** 2)
        sigma = np.sqrt(ps / 10 ** (snr / 10) * (sr / 2) / 3000.0)
        x = x + np.random.default_rng(seed).normal(0, sigma, len(x))
    # keep headroom: a deep-faded or noisy signal must not clip at int16
    peak = np.max(np.abs(x))
    if peak > 32000:
        x *= 32000 / peak
    wavfile.write(dst, sr, np.round(x).astype(np.int16))


def run_rx(a, iq):
    # test/harris/modem.py's run_rx, with --rx-args added
    p = subprocess.run([a.rx, "--format", "cf32", "--rate", str(modem.RATE), "--offset", str(int(modem.OFFSET)),
                        "--dial", "0"] + a.rx_args.split() + [iq], capture_output=True, text=True, timeout=3600)
    return parse(p.stdout)


def parse(text):
    msgs, cur = [], None
    for line in text.splitlines():
        if line.startswith("msg="):
            cur = {k: v.strip('"') for k, v in modem.KV.findall(line)}
            msgs.append(cur)
        elif cur is not None and line.startswith("  hex="):
            cur["hex"] = line.split("=", 1)[1].strip()
    return msgs


def run_wav_rx(a, wav):
    p = subprocess.run([a.wav_rx] + a.rx_args.split() + [wav], capture_output=True, text=True, timeout=3600)
    return parse(p.stdout)


def run_case(a, mode, snr, seed):
    tag = f"{mode}_{a.channel.replace(',', '-')}_{snr:g}_{seed}"
    rng = random.Random(f"{mode}/{seed}/{a.bytes}")
    data = b"DE AAM402 " + bytes(rng.randrange(256) for _ in range(max(0, a.bytes - 10)))
    binf, clean, wav, iq = (os.path.join(a.work, tag + e) for e in (".bin", ".clean.wav", ".wav", ".cf32"))
    with open(binf, "wb") as f:
        f.write(data)
    p = subprocess.run([a.tool, "--mode", mode, binf, clean], capture_output=True, text=True)
    if p.returncode != 0:
        return dict(mode=mode, snr=snr, seed=seed, exact=False, success=False, fp=False,
                    bits=len(data) * 8, err=len(data) * 8, note="tx failed: " + p.stderr.strip())
    channel(a, clean, wav, snr, seed)
    if a.wav_rx:
        msgs = run_wav_rx(a, wav)
    else:
        modem.to_iq(wav, iq)
        msgs = run_rx(a, iq)
        os.remove(iq)
    for f in (clean, wav, binf):
        os.remove(f)
    m = msgs[0] if msgs else {}
    got = bytes.fromhex(m.get("hex", ""))
    exact = (len(msgs) == 1 and m.get("success") == "1" and m.get("eom") == "1"
             and m.get("mode") == mode and got == data)
    head = got[:len(data)]
    err = sum(bin(x ^ y).count("1") for x, y in zip(head, data)) + 8 * max(0, len(data) - len(got))
    succ = m.get("success") == "1"
    q = float(m["quality_db"]) if m.get("quality_db") not in (None, "") else None
    return dict(mode=mode, snr=snr, seed=seed, exact=exact, success=succ, fp=succ and got != data,
                bits=len(data) * 8, err=err, q=q, right=got == data and len(got) > 0,
                byte_err=sum(x != y for x, y in zip(got, data)) + abs(len(data) - len(got)),
                wrong=len(got) > 0 and got != data, msgs=len(msgs))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True)
    ap.add_argument("--rx", help="m110a_rx: the I/Q route (default)")
    ap.add_argument("--wav-rx", help="m75_wav_rx: WAV straight into ModemRX instead of the I/Q route")
    ap.add_argument("--chansim")
    ap.add_argument("--work", required=True)
    ap.add_argument("--modes", default="75S,75L,150S,300S")
    ap.add_argument("--channel", default="awgn")
    ap.add_argument("--snr", default="-6,-4,-2,0,2")
    ap.add_argument("--seeds", default="1,2,3,4,5")
    ap.add_argument("--bytes", type=int, default=64)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--require", action="append", default=[])
    ap.add_argument("--rx-args", default="", help="extra m110a_rx arguments, e.g. '--mode 75S'")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    if not a.rx and not a.wav_rx:
        ap.error("give --rx or --wav-rx")
    os.makedirs(a.work, exist_ok=True)
    if a.channel != "awgn" and not a.chansim:
        ap.error("--channel other than awgn needs --chansim")
    modes = a.modes.split(",")
    snrs = [float(s) for s in a.snr.split(",")]
    seeds = [int(s) for s in a.seeds.split(",")]
    cases = [(m, s, k) for m in modes for s in snrs for k in seeds]
    res = []
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        futs = [ex.submit(run_case, a, *c) for c in cases]
        for f in futs:
            r = f.result()
            res.append(r)
            if a.verbose:
                print(r, flush=True)
    route = "WAV -> ModemRX (m75_wav_rx)" if a.wav_rx else "I/Q -> m110a_rx"
    print(f"channel={a.channel} bytes={a.bytes} seeds={len(seeds)} route={route} rx_args='{a.rx_args}' (SNR in 3 kHz)")
    print(f"{'mode':6} {'snr_db':>7} {'exact':>7} {'bytes_ok':>8} {'success':>8} {'false_ok':>8} {'BER':>10}"
          f" {'q_min_right':>11} {'q_max_wrong':>11} {'one_msg':>8}")
    table = {}
    for m in modes:
        for s in snrs:
            rs = [r for r in res if r["mode"] == m and r["snr"] == s]
            ex_ = sum(r["exact"] for r in rs)
            su = sum(r["success"] for r in rs)
            fp = sum(r["fp"] for r in rs)
            ber = sum(r["err"] for r in rs) / max(1, sum(r["bits"] for r in rs))
            table[(m, s)] = ex_
            qr = [r["q"] for r in rs if r.get("right") and r["q"] is not None]
            qw = [r["q"] for r in rs if r.get("wrong") and r["q"] is not None]
            fq = lambda v: f"{v:11.1f}" if v is not None else f"{'-':>11}"
            rt = sum(bool(r.get("right")) for r in rs)
            one = sum(r.get("msgs") == 1 for r in rs)
            print(f"{m:6} {s:7g} {ex_:>3}/{len(rs):<3} {rt:>4}/{len(rs):<3} {su:>4}/{len(rs):<3} {fp:>4}/{len(rs):<3} {ber:10.2e}"
                  f" {fq(min(qr) if qr else None)} {fq(max(qw) if qw else None)} {one:>4}/{len(rs):<3}", flush=True)
    ok = True
    for req in a.require:
        m, s, n = req.split(":")
        got = table.get((m, float(s)), 0)
        good = got >= int(n)
        ok &= good
        print(f"require {m} at {s} dB: exact {got} >= {n} -> {'PASS' if good else 'FAIL'}")
    for r in sorted((r for r in res if r.get("wrong")), key=lambda r: -(r["q"] or -99)):
        print(f"  wrong bytes: {r['mode']} {r['snr']:g} dB seed {r['seed']}: success={int(r['success'])} "
              f"q={r['q']} byte_err={r['byte_err']} bit_err={r['err']}")
    fps = sum(r["fp"] for r in res)
    print(f"false successes: {fps}")
    return 0 if ok and fps == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
