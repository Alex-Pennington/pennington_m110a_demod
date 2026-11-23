"""hfchansim --freq is a frequency shift, not a double-sideband mix (ctest hfchansim_freq_*).

Two checks, both through the hfchansim binary:

--tones: a tone at each of 300..3300 Hz goes through hfchansim --freq F for each F. The
  output must hold one tone at f + F; the image at f - F must be at least --image-db
  below it. The spectrum is taken over the middle of the file (Blackman-Harris, 0.25 Hz
  bins), away from the start and end of the record.
--loopback: payload -> modem_tx_wav (ModemTX::encode, 48 kHz) -> hfchansim --freq F
  [--snr S] -> I/Q (test/harris/modem.py's route) -> m110a_rx (AUTO). Each case passes
  when m110a_rx prints one message, success=1, eom=1, the mode from D1/D2 and the payload
  bytes exactly. A case with |F| above --afc is printed and does not fail the run unless
  --strict-afc. m110a_rx's preamble search acquires to +/-37 Hz, and the acquisition
  frequency is right there, but the decode after it holds to +/-28 Hz in every standard
  mode (at +/-30 Hz it fails at 10 dB, from +/-32 Hz it fails clean), so --afc is 28.

hfchansim's --snr is its own measure: AWGN against the whole-file power over the whole
0..24 kHz band, so --snr 10 is about 19 dB in 3 kHz.

Usage: python3 -I -B test/chansim/freq_offset.py --chansim HFCHANSIM --work DIR
           [--tones] [--offsets 5,-5,20,-20,75,-75] [--image-db 60]
           [--loopback --tool MODEM_TX_WAV --rx M110A_RX --modes 600S,...
            --snr inf,10 --afc 28]
"""
import argparse
import os
import random
import subprocess
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal.windows import blackmanharris

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harris"))
import modem  # noqa: E402

FS = 48000


def chansim(a, inp, out, freq, snr=None, seed=1):
    cmd = [a.chansim, inp, out, "--freq", f"{freq:g}", "--sample-rate", str(FS), "--seed", str(seed)]
    if snr is not None and np.isfinite(snr):
        cmd += ["--snr", f"{snr:g}"]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(f"hfchansim failed: {p.stderr.strip()}")


def band_db(spec, df, f, half=1.5):
    lo, hi = int(max(0, (f - half) / df)), int((f + half) / df) + 1
    return 10 * np.log10(np.max(spec[lo:hi]) + 1e-30)


def tones(a):
    secs, win_s = 6.0, 4.0
    n = int(secs * FS)
    t = np.arange(n) / FS
    m = int(win_s * FS)
    s0 = (n - m) // 2
    w = blackmanharris(m)
    df = FS / m
    worst = None
    ok = True
    pcm_in, pcm_out = os.path.join(a.work, "tone.pcm"), os.path.join(a.work, "tone_out.pcm")
    for f0 in range(300, 3301, 300):
        np.round(0.5 * 32767 * np.cos(2 * np.pi * f0 * t)).astype("<i2").tofile(pcm_in)
        for off in a.offsets:
            chansim(a, pcm_in, pcm_out, off)
            y = np.fromfile(pcm_out, dtype="<i2").astype(float)
            spec = np.abs(np.fft.rfft(y[s0:s0 + m] * w)) ** 2
            want, image, orig = band_db(spec, df, f0 + off), band_db(spec, df, f0 - off), band_db(spec, df, f0, 0.5)
            # anything else: the strongest bin more than 3 Hz from the wanted tone
            k = np.arange(len(spec)) * df
            rest = 10 * np.log10(np.max(spec[np.abs(k - (f0 + off)) > 3]) + 1e-30)
            rej = want - max(image, rest)
            good = rej >= a.image_db
            ok &= good
            if worst is None or rej < worst[0]:
                worst = (rej, f0, off)
            print(f"tone {f0:4d} Hz {off:+6g} Hz: wanted {want:6.1f} dB, image {image - want:7.1f} dBc, "
                  f"at f0 {orig - want:7.1f} dBc, worst spur {rest - want:7.1f} dBc -> {'PASS' if good else 'FAIL'}",
                  flush=True)
    os.remove(pcm_in)
    os.remove(pcm_out)
    print(f"tones: worst rejection {worst[0]:.1f} dB (tone {worst[1]} Hz, offset {worst[2]:+g} Hz), "
          f"need {a.image_db:g}")
    return ok


def wav_to_pcm(wav, pcm):
    sr, x = wavfile.read(wav)
    assert sr == FS, sr
    x.astype("<i2").tofile(pcm)


def pcm_to_wav(pcm, wav):
    wavfile.write(wav, FS, np.fromfile(pcm, dtype="<i2"))


def loop_case(a, mode, off, snr, seed=1, nbytes=120):
    tag = f"{mode}_{off:+g}Hz_{snr:g}"
    rng = random.Random(f"{mode}/{seed}/{nbytes}")
    data = bytes(rng.randrange(256) for _ in range(nbytes))
    binf, clean, pin, pout, wav, iq = (os.path.join(a.work, tag + e) for e in
                                       (".bin", ".clean.wav", ".in.pcm", ".out.pcm", ".wav", ".cf32"))
    with open(binf, "wb") as f:
        f.write(data)
    p = subprocess.run([a.tool, "--mode", mode, binf, clean], capture_output=True, text=True)
    if p.returncode != 0:
        print(f"{tag}: modem_tx_wav failed: {p.stderr.strip()}")
        return False, False
    wav_to_pcm(clean, pin)
    chansim(a, pin, pout, off, snr, seed)
    pcm_to_wav(pout, wav)
    modem.to_iq(wav, iq)
    msgs = modem.run_rx(a.rx, iq)
    for f in (binf, clean, pin, pout, wav, iq):
        os.remove(f)
    m = msgs[0] if len(msgs) == 1 else {}
    got = bytes.fromhex(m.get("hex", ""))
    ok = (len(msgs) == 1 and m.get("success") == "1" and m.get("eom") == "1"
          and m.get("mode") == mode and got == data)
    in_range = abs(off) <= a.afc
    print(f"{tag}: messages={len(msgs)} mode={m.get('mode')} success={m.get('success')} eom={m.get('eom')} "
          f"q={m.get('quality_db')} acq_foff={m.get('acq_foff_hz')} bytes {len(got)}/{nbytes} -> "
          f"{'PASS' if ok else 'FAIL'}{'' if in_range else ' (outside AFC range)'}", flush=True)
    return ok, in_range


def loopback(a):
    ok = True
    for mode in a.modes.split(","):
        for snr in (float(s) for s in a.snr.split(",")):
            for off in a.offsets:
                good, in_range = loop_case(a, mode, off, snr)
                if in_range or a.strict_afc:
                    ok &= good
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--chansim", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--offsets", default="5,-5,20,-20,75,-75")
    ap.add_argument("--tones", action="store_true")
    ap.add_argument("--image-db", type=float, default=60.0)
    ap.add_argument("--loopback", action="store_true")
    ap.add_argument("--tool")
    ap.add_argument("--rx")
    ap.add_argument("--modes", default="150S,150L,300S,300L,600S,600L,1200S,1200L,2400S,2400L")
    ap.add_argument("--snr", default="inf,10")
    ap.add_argument("--afc", type=float, default=28.0)
    ap.add_argument("--strict-afc", action="store_true")
    a = ap.parse_args()
    a.offsets = [float(s) for s in a.offsets.split(",")]
    os.makedirs(a.work, exist_ok=True)
    ok = True
    if a.tones:
        ok &= tones(a)
    if a.loopback:
        ok &= loopback(a)
    print("freq_offset: PASS" if ok else "freq_offset: FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
