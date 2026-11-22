"""Step-1 check for the Harris-mode transmitter: re-make recorded RT-1694D messages.

For each 600H / 1200H / 2400V message in the test set:

1. m110a_rx finds the message in the recording (modem.py's I/Q route) and its start time.
2. m110a_tx re-transmits the message's trusted payload bits (<payload>/<slug>.bits, as the
   modem stage writes them) to <work>/<slug>.wav, level matched to the recording, and
   writes its symbol stream (preamble first) to <work>/<slug>.sym.
3. (a) m110a_rx decodes our WAV: it must report success=1 eom=1 and the payload bytes.
4. (b) harris_score --compare takes the receiver's own hard decisions on the recording's
   symbols and on ours and counts agreement with our symbol stream, by region.

Usage: python3 -I -B test/harris/tx_step1.py --rx RX --tx TX --score SCORE --set DIR
           --payload DIR --work DIR [--only SLUG ...] [--eq L]
The recordings and the payloads stay outside git; so must --work.
Exit 0 when every (a) holds; (b) is a measure and is printed, not judged.
"""
import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modem  # noqa: E402

KV = re.compile(r'(\w+)=(\S+)')
TX_MODES = {"600H", "1200H", "2400V"}


def compare(score, wav, t0, t1, sym, eq):
    p = subprocess.run([score, wav, f"{t0:.3f}", f"{t1:.3f}", "--eq", str(eq), "0.02", "0.005",
                        "--compare", sym], capture_output=True, text=True, timeout=1800)
    summ, last = None, ""
    for line in p.stdout.splitlines():
        if line.startswith("compare_summary "):
            summ = dict(KV.findall(line))
        elif line.startswith("  last 6 symbols"):
            last = line.split(":", 1)[1].strip()
    return summ, last


def frac(s, k):
    a, b = s[k].split("/")
    return f"{a}/{b}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rx", required=True)
    ap.add_argument("--tx", required=True)
    ap.add_argument("--score", required=True)
    ap.add_argument("--set", required=True, help="directory holding the recordings")
    ap.add_argument("--payload", required=True, help="trusted <slug>.bits / <slug>.bin")
    ap.add_argument("--work", required=True)
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--eq", type=int, default=0, help="harris_score --eq L for the compare")
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    ok_all = True
    for name, slug in modem.FILES:
        wav = os.path.join(a.set, name + ".wav")
        iq = os.path.join(a.work, slug + ".cf32")
        dur = modem.to_iq(wav, iq)
        msgs = modem.run_rx(a.rx, iq)
        os.remove(iq)
        for i, m in enumerate(msgs):
            s = slug if len(msgs) == 1 else f"{slug}_m{i + 1}"
            if m.get("mode") not in TX_MODES or (a.only and s not in a.only):
                continue
            t0 = float(m["start_s"])
            t1 = float(msgs[i + 1]["start_s"]) - 0.05 if i + 1 < len(msgs) else dur
            bits = os.path.join(a.payload, s + ".bits")
            ref = open(os.path.join(a.payload, s + ".bin"), "rb").read()
            ours, sym = os.path.join(a.work, s + ".wav"), os.path.join(a.work, s + ".sym")
            # the level of this message's stretch of the recording
            seg = os.path.join(a.work, s + ".seg.wav")
            sr, x = modem.wavfile.read(wav)
            modem.wavfile.write(seg, sr, x[int(t0 * sr):int(t1 * sr)])
            subprocess.run([a.tx, "--mode", m["mode"], "--bits", bits, "--match", seg,
                            "--symbols", sym, ours], check=True, capture_output=True, text=True)
            os.remove(seg)
            oiq = os.path.join(a.work, s + ".ours.cf32")
            modem.to_iq(ours, oiq)
            om = modem.run_rx(a.rx, oiq)
            os.remove(oiq)
            got = bytes.fromhex(om[0].get("hex", "")) if len(om) == 1 else b""
            dec_ok = (len(om) == 1 and om[0].get("success") == "1" and om[0].get("eom") == "1"
                      and om[0].get("mode") == m["mode"] and got == ref)
            ok_all &= dec_ok
            rec, last = compare(a.score, wav, t0, t1, sym, a.eq)
            own, _ = compare(a.score, ours, float(om[0]["start_s"]) if om else 2.0, 1e9, sym, a.eq)
            print(f"{s}: mode {m['mode']}, {len(ref)} bytes; ours decoded by m110a_rx: "
                  f"success={om[0].get('success') if om else '-'} eom={om[0].get('eom') if om else '-'} "
                  f"bytes {'identical' if got == ref else 'DIFFERENT'} -> {'ok' if dec_ok else 'FAIL'}")
            for label, r in (("recording", rec), ("our WAV", own)):
                if not r:
                    print(f"  {label}: compare failed")
                    continue
                print(f"  {label} vs our symbols ({r['taps']} taps, lag {r['lag']}): "
                      f"{r['agree']}/{r['total']}; preamble {frac(r, 'preamble')}, "
                      f"data {frac(r, 'data')}, zero probes {frac(r, 'zero_probes')}, "
                      f"D1/D2 probes {frac(r, 'd_probes')}")
            print(f"  recording, last 6 symbols (ours/decision/|y|): {last}")
    print("step 1 (a): all re-made messages decode exactly" if ok_all else "step 1 (a): FAILED")
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
