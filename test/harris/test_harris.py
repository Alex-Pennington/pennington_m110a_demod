#!/usr/bin/env python3
"""Fast checks for the Harris MELPe scorer that need no MELPe coder.

Usage: python3 -I -B test/harris/test_harris.py <work dir>

- frame packing: round trip at each rate, and the sync bit and B_01 land where the
  reference coder puts them
- bit streams: byte order, offset, reversal and inversion
- sync search: finds the offset of an alternating sync bit; random bits do not reach the
  trust level
- speech measure: a voiced signal with a continuous pitch contour scores high; white noise
  and a buzz whose pitch is drawn at random each frame (what random MELPe bits decode to)
  score low
- proofs: text-field check, status rule, and an end-to-end score.py --proofs run with no
  payloads (every item "none")
"""
import json
import os
import subprocess
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import melpe_frames as mf  # noqa: E402
import proofs  # noqa: E402

FAILS = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        FAILS.append(what)


def test_packing():
    for rate in (2400, 1200, 600):
        f = mf.random_frames(rate, 50, seed=rate)
        b = mf.pack_frames(f, rate)
        check(len(b) == 50 * mf.frame_bytes(rate), f"{rate}: {mf.frame_bytes(rate)} bytes per frame")
        check(np.array_equal(mf.unpack_frames(b, rate), f), f"{rate}: pack/unpack round trip")
    one = np.zeros((1, 54), np.uint8)
    one[0, 53] = 1
    check(mf.pack_frames(one, 2400) == bytes([0, 0, 0, 0, 0, 0, 0x20]), "2400: B_54 (sync) is byte 6 bit 5")
    one = np.zeros((1, 81), np.uint8)
    one[0, 0] = 1
    check(mf.pack_frames(one, 1200)[0] == 0x01, "1200: B_01 (sync) is byte 0 bit 0")
    one = np.zeros((1, 54), np.uint8)
    one[0, 0] = 1
    check(mf.pack_frames(one, 600)[0] == 0x80, "600: B_01 is byte 0 bit 7")
    full = np.ones((1, 54), np.uint8)
    check(mf.pack_frames(full, 600)[6] == 0xFC, "600: low two bits of byte 6 unused")


def test_streams():
    data = bytes([0b10110000, 0x0F])
    check(list(mf.bytes_to_bits(data, "msb")[:4]) == [1, 0, 1, 1], "msb order: bit 7 first")
    check(list(mf.bytes_to_bits(data, "lsb")[:4]) == [0, 0, 0, 0], "lsb order: bit 0 first")
    for o in ("msb", "lsb"):
        check(mf.bits_to_bytes(mf.bytes_to_bits(data, o), o) == data, f"{o}: bytes/bits round trip")
    bits = np.arange(54 * 3 + 5) % 2
    f = mf.cut_frames(bits, 600, 5)
    check(f.shape == (3, 54) and f[0, 0] == 1, "cut_frames: offset and frame count")
    check(np.array_equal(mf.cut_frames(bits, 600, 5, reverse=True)[0], f[0, ::-1]), "cut_frames: reverse")
    check(np.array_equal(mf.cut_frames(bits, 600, 5, invert=True)[0], f[0] ^ 1), "cut_frames: invert")


def test_sync():
    rng = np.random.default_rng(7)
    for rate in (2400, 1200):
        n = mf.frame_bits(rate)
        f = mf.random_frames(rate, 60, seed=3)
        f[:, mf.sync_index(rate)] = np.arange(60) % 2
        lead = 17
        bits = np.concatenate([rng.integers(0, 2, lead, dtype=np.uint8), f.ravel()])
        off, s = mf.find_sync(bits, rate)
        check(off == lead and s == 1.0, f"{rate}: sync search finds offset {lead}")
        rev = mf.cut_frames(bits, rate, lead, reverse=True)
        bits_r = np.concatenate([np.zeros(lead, np.uint8), rev.ravel()])
        off, s = mf.find_sync(bits_r, rate, index=n - 1 - mf.sync_index(rate))
        check(off == lead and s == 1.0, f"{rate}: sync search on reversed frames")
        off, s = mf.find_sync(rng.integers(0, 2, n * 200, dtype=np.uint8), rate)
        check(s < 0.9, f"{rate}: random bits stay below sync trust (best {s:.2f})")
    check(mf.find_sync(np.zeros(1000, np.uint8), 600) == (None, None), "600: no sync bit")


def buzz(f0_per_hop, seconds, seed=0):
    """Pulse train through a two-pole resonance, pitch per 22.5 ms hop, syllable envelope."""
    rng = np.random.default_rng(seed)
    n = int(seconds * mf.FS)
    f0 = np.repeat(f0_per_hop, mf.HOP)[:n]
    phase = np.cumsum(f0 / mf.FS)
    pulses = (np.diff(np.floor(phase), prepend=0) > 0).astype(float)
    y = np.zeros(n)
    a1, a2 = 2 * 0.95 * np.cos(2 * np.pi * 700 / mf.FS), -0.95 ** 2
    for i in range(n):
        y[i] = pulses[i] + a1 * (y[i - 1] if i else 0) + a2 * (y[i - 2] if i > 1 else 0)
    env = 0.55 + 0.45 * np.sin(2 * np.pi * 3 * np.arange(n) / mf.FS)
    y = y * env + 0.01 * rng.standard_normal(n)
    return (8000 * y / np.max(np.abs(y))).astype(np.int16)


def test_measure():
    hops = int(4 * mf.FS / mf.HOP) + 2
    t = np.arange(hops) * mf.HOP / mf.FS
    smooth = 120 + 25 * np.sin(2 * np.pi * 0.7 * t)
    m = mf.speech_measure(buzz(smooth, 4))
    check(m["track"] > 0.6 and 95 < m["f0_median"] < 145,
          f"continuous pitch contour scores high (track {m['track']:.2f}, f0 {m['f0_median']:.0f})")
    import score
    jumpy = np.random.default_rng(5).uniform(60, 400, hops)
    m = mf.speech_measure(buzz(jumpy, 4, seed=1))
    check(m["track"] < score.PASS_TRACK,
          f"pitch drawn at random per frame stays below the pass level (track {m['track']:.2f})")
    noise = (3000 * np.random.default_rng(2).standard_normal(4 * mf.FS)).astype(np.int16)
    m = mf.speech_measure(noise)
    check(m["track"] < 0.05, f"white noise scores low (track {m['track']:.2f})")
    check(mf.speech_measure(np.zeros(100, np.int16))["frames"] == 0, "short input: no frames")


def test_proofs(work):
    check(proofs.check_text({"decoder": "m110a_rx feat/harris-mode @abc1234",
                             "what": proofs.WHAT}) == [], "branch name and AN/PRC-150 pass the text check")
    for bad in ("/home/x/600_1.wav", "see ~/.local/share", "payload/600_1.bin", "saga.internal",
                "on saga", "7.0711 MHz", "pitch 120 Hz", "10.0.0.5"):
        check(proofs.check_text({"note": bad}) != [], f"text check catches {bad!r}")
    check([proofs.status_for(*a) for a in ((False, False, False), (True, False, True),
                                           (True, True, False), (True, False, False))]
          == ["none", "pass", "partial", "fail"], "status rule")
    try:
        proofs.write(work, proofs.document("x", "y", [proofs.item(
            "600 #1", "m", "none", "d", audio=[{"label": "Decoded speech", "file": "t7-audio/nope.wav"}])]))
        check(False, "write refuses a missing audio file")
    except ValueError:
        check(True, "write refuses a missing audio file")

    # end to end: no payloads, a fake test set of short 48 kHz recordings
    pay = os.path.join(work, "payload")
    tset = os.path.join(work, "set")
    pdir = os.path.join(work, "proofs")
    for d in (pay, tset, pdir):
        os.makedirs(d, exist_ok=True)
    have_scipy = True
    try:
        import scipy.signal  # noqa: F401
    except ImportError:
        have_scipy = False
    import score
    if have_scipy:
        for _, name, _ in score.FILES:
            with wave.open(os.path.join(tset, name), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(48000)
                w.writeframes((1000 * np.sin(np.arange(4800) * 0.2)).astype("<i2").tobytes())
    cmd = [sys.executable, "-I", "-B", os.path.join(HERE, "score.py"), "--melpe", "/bin/true",
           "--payload", pay, "--audio", os.path.join(work, "audio"), "--work", work,
           "--proofs", pdir, "--decoder", "m110a_rx master @0000000"]
    if have_scipy:
        cmd += ["--set", tset]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    path = os.path.join(pdir, "t7.json")
    check(os.path.exists(path), f"score.py --proofs writes t7.json (rc {p.returncode})")
    if not os.path.exists(path):
        print(p.stdout, p.stderr)
        return
    with open(path, encoding="utf-8") as f:
        doc = json.load(f)
    check(doc["id"] == "T7" and doc["order"] == 50 and doc["title"] == proofs.TITLE, "T7 header")
    check(len(doc["items"]) == 8 and all(i["status"] == "none" for i in doc["items"]),
          "eight items, all none without payloads")
    names = [i["name"] for i in doc["items"]]
    check(names == [n[:-4] for _, n, _ in score.FILES], "item names are the recording names")
    modes = {i["name"]: i["mode"] for i in doc["items"]}
    check(modes["melp2400"].startswith("D1,D2 7,7") and modes["1200 #2"].startswith("D1,D2 5,6")
          and "4 preambles" in modes["600 #3 multiples"], "mode text from the preamble table")
    check(proofs.check_text(doc) == [], "no paths, hosts or frequencies in the written text")
    ok = True
    for it in doc["items"]:
        for au in it["audio"]:
            fp = os.path.join(pdir, au["file"])
            if not os.path.isfile(fp):
                ok = False
                continue
            with wave.open(fp, "rb") as w:
                ok &= (w.getframerate(), w.getnchannels(), w.getsampwidth()) == (8000, 1, 2)
    check(ok, "every named audio file exists, 8 kHz mono 16-bit")
    if have_scipy:
        check(all(any(a["label"] == "On the channel" for a in i["audio"]) for i in doc["items"]),
              "channel audio named for every item")


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    work = sys.argv[1]
    os.makedirs(work, exist_ok=True)
    test_packing()
    test_streams()
    test_sync()
    test_measure()
    test_proofs(work)
    print(f"{len(FAILS)} failed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
