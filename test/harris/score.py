#!/usr/bin/env python3
"""Score the Harris MELP test transmissions: payload bits -> MELPe frames -> audio.

Usage:
  python3 -I test/harris/score.py --melpe <MELPe program> [--payload DIR] [--audio DIR]
                                   [--work DIR] [--search | --search-all] [--jobs N]
                                   [--proofs DIR --set DIR [--decoder TEXT]]
  python3 -I test/harris/score.py --melpe <MELPe program> --calibrate RAW [RAW ...]
                                   [--work DIR]

The MELPe program is the STANAG 4591 reference coder's command line
(`-r <rate> -m A|S -i <in> -o <out>`, -b 54 byte frames); it is run as an external
program and is not part of this repo.

Inputs, per file of the test set (slugs in FILES below):
  <payload>/<slug>.bin   payload bytes from the 110A receiver, or, for a recording with
                         several preambles, one <slug>_m<N>.bin per message (N = 1, 2, ...),
                         each scored on its own with its own <slug>_m<N>.json; the proof
                         item stays one per recording, its decoded speech the messages
                         joined in order with 0.5 s of silence between them
  <payload>/<slug>.json  optional: {"preamble": ..., "mode": ..., "bit_order": "msb"|"lsb",
                          "rate": 600|1200|2400, "offset": int, "reverse": bool,
                          "invert": bool, "verified": bool, "decoder": str, "d1": int,
                          "d2": int, "countdown": int, "preambles": int}. Keys present
                          fix that choice. Absent keys take the defaults (rate from the
                          slug, msb, the sync-bit offset or 0, no reversal or inversion);
                          --search tries both byte orders and every frame offset,
                          --search-all also every rate, reversal and inversion.
Outputs: <audio>/<slug>.wav, 8 kHz mono 16-bit.

Speech measure (melpe_frames.speech_measure): `track` is the share of 22.5 ms frames of
the decoded audio that are voiced with pitch in 80-308 Hz and continue the previous
frame's pitch within ~10 %. A file PASSes when track >= PASS_TRACK and beats every other decoding choice
tried by PASS_MARGIN. --calibrate prints the
measure for real speech coded and decoded at each rate, and for random bits decoded at
each rate, which is where PASS_TRACK comes from.

Exit status: 0 when every file with a payload passes, 1 otherwise, 2 on usage errors.
"""
import argparse
import concurrent.futures
import json
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import melpe_frames as mf  # noqa: E402
import proofs  # noqa: E402

JOB = "/home/rayven/.claude/jobs/6e6029c0/tmp/harris"

# slug, file name in the test set, rate implied by the name
FILES = [
    ("600_1", "600 #1.wav", 600),
    ("600_2", "600 #2.wav", 600),
    ("600_3_multiples", "600 #3 multiples.wav", 600),
    ("1200_1", "1200 #1.wav", 1200),
    ("1200_2", "1200 #2.wav", 1200),
    ("1200_3", "1200 #3.wav", 1200),
    ("melp600", "melp600.wav", 600),
    ("melp2400", "melp2400.wav", 2400),
]

PASS_TRACK = 0.25   # coded speech >= 0.30; best wrong choice in --search 0.185 (docs/harris-melpe.md)
PASS_MARGIN = 0.05  # and the winner must beat every other choice tried by this much


def run_coder(melpe, rate, mode, src, dst):
    p = subprocess.run([melpe, "-q", "-r", str(rate), "-m", mode, "-i", src, "-o", dst],
                       capture_output=True, text=True, timeout=600)
    if p.returncode != 0 or not os.path.exists(dst):
        raise RuntimeError(f"{melpe} -r {rate} -m {mode} failed rc={p.returncode}: "
                           f"{(p.stderr or p.stdout).strip()[:200]}")


def decode_frames(melpe, frames, rate, work, tag):
    """Frames (n, n_bits) -> int16 audio through the coder's synthesis."""
    bit = os.path.join(work, tag + ".bit")
    pcm = os.path.join(work, tag + ".pcm")
    with open(bit, "wb") as f:
        f.write(mf.pack_frames(frames, rate))
    run_coder(melpe, rate, "S", bit, pcm)
    x = mf.read_raw(pcm)
    os.remove(bit)
    os.remove(pcm)
    return x


def channel_8k(path):
    """A test-set recording (16-bit PCM WAV, first channel) resampled to 8 kHz int16."""
    import wave
    from math import gcd
    from scipy.signal import resample_poly
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise ValueError(f"{os.path.basename(path)}: expected 16-bit PCM")
        fs, nch = w.getframerate(), w.getnchannels()
        x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, nch)[:, 0]
    g = gcd(mf.FS, fs)
    y = resample_poly(x.astype(np.float64), mf.FS // g, fs // g)
    return np.clip(np.round(y), -32768, 32767).astype("<i2")


def fmt(m):
    return f"voiced {m['voiced']:.3f}  track {m['track']:.3f}  f0 {m['f0_median']:5.1f} Hz"


# ---- calibration -------------------------------------------------------------------

def calibrate(melpe, raws, work):
    speech = np.concatenate([mf.read_raw(r) for r in raws])
    src = os.path.join(work, "cal_in.raw")
    speech.astype("<i2").tofile(src)
    print(f"speech: {len(speech) / mf.FS:.1f} s from {len(raws)} file(s)")
    print(f"  original                {fmt(mf.speech_measure(speech))}")
    rows = []
    for rate in (2400, 1200, 600):
        enc = os.path.join(work, f"cal_{rate}.bit")
        run_coder(melpe, rate, "A", src, enc)
        with open(enc, "rb") as f:
            frames = mf.unpack_frames(f.read(), rate)
        os.remove(enc)
        n = len(frames)
        cases = [("speech round trip", frames)]
        bits = frames.ravel()
        if n > 2:
            cases.append(("speech, offset +17 bits", mf.cut_frames(bits, rate, 17)))
            cases.append(("speech, frame reversed", frames[:, ::-1].copy()))
            cases.append(("speech, bits inverted", frames ^ 1))
        for seed in (1, 2, 3):
            cases.append((f"random bits seed {seed}", mf.random_frames(rate, n, seed)))
        if mf.sync_index(rate) is not None:
            r = mf.random_frames(rate, n, 4)
            r[:, mf.sync_index(rate)] = np.arange(n) % 2
            cases.append(("random bits, sync set", r))
        cases.append(("all zero bits", np.zeros_like(frames)))
        print(f"{rate} bit/s: {n} frames")
        for name, f in cases:
            m = mf.speech_measure(decode_frames(melpe, f, rate, work, f"cal_{rate}"))
            rows.append((rate, name, m))
            print(f"  {name:<24}{fmt(m)}")
    os.remove(src)
    return rows


# ---- scoring -----------------------------------------------------------------------

def candidates(meta, rate_hint, search, wide=False):
    """Yield decoding choices (rate, order, reverse, invert) not fixed by the sidecar.

    search: byte order (frame offset is searched in offsets_for). wide: also rate, frame
    reversal and inversion. Wide search is off by default: the rate is read off the
    preamble, and 600 bit/s decodes of mis-cut bits look speech-like inside each 90 ms
    block, so every extra choice raises the best wrong reading (docs/harris-melpe.md)."""
    rates = [meta["rate"]] if "rate" in meta else ([2400, 1200, 600] if wide else [rate_hint])
    orders = [meta["bit_order"]] if "bit_order" in meta else (["msb", "lsb"] if search else ["msb"])
    revs = [meta["reverse"]] if "reverse" in meta else ([False, True] if wide else [False])
    invs = [meta["invert"]] if "invert" in meta else ([False, True] if wide else [False])
    for r in rates:
        for o in orders:
            for rv in revs:
                for iv in invs:
                    yield r, o, rv, iv


SYNC_TRUST = 0.9  # sync-bit alternation at or above this fixes the frame offset


def offsets_for(bits, rate, rev, meta, search):
    """Frame offsets to try. With a sync bit (2400, 1200) the alternating bit fixes the
    offset; in a reversed frame it sits at the mirrored index. 600 bit/s has no sync bit,
    so --search tries every offset."""
    if "offset" in meta:
        return [int(meta["offset"])]
    n = mf.frame_bits(rate)
    si = mf.sync_index(rate)
    if si is not None:
        off, s = mf.find_sync(bits, rate, index=(n - 1 - si) if rev else si)
        if off is not None and s >= SYNC_TRUST:
            return [off]
    return list(range(n)) if search else [0]


def score_file(melpe, payload, meta, rate_hint, search, work, slug, jobs, wide=False):
    """Decode every choice; return (key, details, audio) of the best by the speech measure,
    with details["second"] = best track of any other choice."""
    tasks = []
    for rate, order, rev, inv in candidates(meta, rate_hint, search, wide):
        bits = mf.bytes_to_bits(payload, order)
        for off in offsets_for(bits, rate, rev, meta, search or wide):
            frames = mf.cut_frames(bits, rate, off, rev, inv)
            if len(frames) >= 2:
                tasks.append((dict(rate=rate, order=order, offset=off, reverse=rev, invert=inv,
                                   frames=len(frames), sync=mf.sync_score(frames, rate)),
                              frames))

    def one(i):
        d, frames = tasks[i]
        x = decode_frames(melpe, frames, d["rate"], work, f"{slug}_{i}")
        return mf.speech_measure(x), x

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        results = list(ex.map(one, range(len(tasks))))
    if not results:
        return None
    keys = [(m["track"], m["voiced"]) for m, _ in results]
    i = max(range(len(keys)), key=lambda k: keys[k])
    d = dict(tasks[i][0], m=results[i][0], tried=len(tasks))
    d["second"] = max((keys[k][0] for k in range(len(keys)) if k != i), default=0.0)
    return keys[i], d, results[i][1]


# ---- per-recording payloads --------------------------------------------------------

GAP_S = 0.5  # silence between messages in a recording's joined decoded speech


def load_meta(payload_dir, part):
    p = os.path.join(payload_dir, part + ".json")
    if not os.path.exists(p):
        return {}
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def payload_parts(payload_dir, slug):
    """A recording's payload files: <slug>.bin, or one <slug>_m<N>.bin per preamble
    (message), in message order. Returns [(part slug, message number or None)]."""
    if os.path.exists(os.path.join(payload_dir, slug + ".bin")):
        return [(slug, None)]
    parts = []
    pre = slug + "_m"
    if os.path.isdir(payload_dir):
        for fn in os.listdir(payload_dir):
            if fn.startswith(pre) and fn.endswith(".bin") and fn[len(pre):-4].isdigit():
                parts.append((fn[:-4], int(fn[len(pre):-4])))
    return sorted(parts, key=lambda p: p[1])


def combine_status(statuses):
    """One status for a recording from its messages' statuses."""
    if not statuses:
        return "none"
    if all(s == "pass" for s in statuses):
        return "pass"
    if all(s in ("pass", "partial") for s in statuses):
        return "partial"
    return "fail"


def score_part(a, part, meta, rate_hint):
    """Score one payload file. Returns a dict with bytes, d (best choice or None), x, status."""
    with open(os.path.join(a.payload, part + ".bin"), "rb") as f:
        payload = f.read()
    r = {"part": part, "bytes": len(payload), "d": None, "x": None, "status": "fail"}
    best = score_file(a.melpe, payload, meta, rate_hint, a.search, a.work, part, a.jobs,
                      a.search_all)
    if best is None:
        return r
    _, d, x = best
    ok = d["m"]["track"] >= PASS_TRACK and d["m"]["track"] - d["second"] >= PASS_MARGIN
    verified = bool(meta.get("verified")) or (d["sync"] is not None and d["sync"] >= SYNC_TRUST)
    r.update(d=d, x=x, ok=ok, status=proofs.status_for(True, verified, ok))
    return r


def print_row(part, meta, r=None):
    pre = str(meta.get("preamble", "-"))[:9]
    mode = str(meta.get("mode", "-"))[:10]
    head = f"{part:<20} {pre:<9} {mode:<10}"
    if r is None:
        print(f"{head} {'-':>6}  {'NO PAYLOAD':>78}")
        return
    d = r["d"]
    if d is None:
        print(f"{head} {r['bytes']:>6}  too short for one frame")
        return
    sync = "-" if d["sync"] is None else f"{d['sync']:.2f}"
    print(f"{head} {r['bytes']:>6}  {d['rate']:>4} {d['order']:<5} "
          f"{d['offset']:>3} {'y' if d['reverse'] else 'n':<3} {'y' if d['invert'] else 'n':<3} "
          f"{d['frames']:>6} {sync:>5}  {d['m']['voiced']:>6.3f} {d['m']['track']:>6.3f} "
          f"{d['second']:>5.3f} {d['m']['f0_median']:>5.0f}  {'PASS' if r['ok'] else 'FAIL'}")


def describe(r):
    """(decoded, detail) text for one message."""
    d = r["d"]
    if d is None:
        return f"{r['bytes']} payload bytes, too short for one MELPe frame", None
    decoded = (f"{r['bytes']} payload bytes, {d['frames']} MELPe {d['rate']} bps frames, "
               f"speech measure {d['m']['track']:.2f} (next best reading {d['second']:.2f})")
    detail = (f"bit order {d['order']}-first, frame offset {d['offset']} bits"
              + (", frames reversed" if d["reverse"] else "")
              + (", bits inverted" if d["invert"] else "")
              + ("" if d["sync"] is None else
                 f", sync bit alternates in {100 * d['sync']:.0f} % of frames"))
    return decoded, detail


def recording_item(label, mode, results, audio, proofs_root, slug):
    """One proof item for a recording from its messages' results; writes its decoded audio."""
    status = combine_status([r["status"] for r in results])
    texts = [describe(r) for r in results]
    if len(results) == 1:
        decoded = texts[0][0] + f"; speech from {PASS_TRACK:.2f}."
        detail = (texts[0][1][0].upper() + texts[0][1][1:] + ".") if texts[0][1] else None
    else:
        decoded = (f"{len(results)} messages, one per preamble; speech from {PASS_TRACK:.2f}. "
                   + " ".join(f"Message {i + 1}: {t[0]}." for i, t in enumerate(texts)))
        detail = " ".join(f"Message {i + 1}: {t[1]}." for i, t in enumerate(texts) if t[1]) or None
    xs = [r["x"] for r in results if r["x"] is not None]
    if xs:
        gap = np.zeros(int(GAP_S * mf.FS), dtype=np.int16)
        joined = xs[0]
        for x in xs[1:]:
            joined = np.concatenate([joined, gap, x])
        rel = f"{proofs.AUDIO_DIR}/{slug}.wav"
        mf.write_wav(os.path.join(proofs_root, rel), joined)
        audio = [{"label": "Decoded speech", "file": rel}] + audio
    return proofs.item(label, mode, status, decoded, detail, audio)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--melpe", required=True, help="MELPe reference coder program")
    ap.add_argument("--payload", default=os.path.join(JOB, "payload"))
    ap.add_argument("--audio", default=os.path.join(JOB, "audio"))
    ap.add_argument("--work", default=os.path.join(JOB, "melpe", "work"))
    ap.add_argument("--jobs", type=int, default=4, help="parallel decodes")
    ap.add_argument("--search", action="store_true",
                    help="search byte order and frame offset at the file's rate")
    ap.add_argument("--search-all", action="store_true",
                    help="also search rate, frame reversal and bit inversion")
    ap.add_argument("--calibrate", nargs="+", metavar="RAW",
                    help="8 kHz s16le speech files: print the measure for coded speech and random bits")
    ap.add_argument("--proofs", metavar="DIR",
                    help="write DIR/t7.json and the audio it names under DIR/t7-audio/")
    ap.add_argument("--set", metavar="DIR",
                    help="test-set directory holding the recordings (for the channel audio)")
    ap.add_argument("--decoder", help='proof "decoder" text, e.g. "m110a_rx master @abc1234"; '
                                      'default: the payload sidecars\' "decoder" values')
    a = ap.parse_args()
    if not os.access(a.melpe, os.X_OK):
        print(f"--melpe {a.melpe}: not an executable", file=sys.stderr)
        return 2
    os.makedirs(a.work, exist_ok=True)

    if a.calibrate:
        calibrate(a.melpe, a.calibrate, a.work)
        return 0

    os.makedirs(a.audio, exist_ok=True)
    pdir = None
    if a.proofs:
        pdir = os.path.join(a.proofs, proofs.AUDIO_DIR)
        os.makedirs(pdir, exist_ok=True)
    items = []
    decoders = set()
    hdr = (f"{'payload':<20} {'preamble':<9} {'mode':<10} {'bytes':>6}  {'rate':>4} {'order':<5} "
           f"{'off':>3} {'rev':<3} {'inv':<3} {'frames':>6} {'sync':>5}  {'voiced':>6} "
           f"{'track':>6} {'2nd':>5} {'f0':>5}  result")
    print(hdr)
    print("-" * len(hdr))
    n_have = n_pass = 0
    for slug, name, rate_hint in FILES:
        parts = payload_parts(a.payload, slug)
        # the recording's sidecar, else its first message's, gives the preamble reading
        meta = load_meta(a.payload, slug) or (load_meta(a.payload, parts[0][0]) if parts else {})
        label = name[:-4]
        audio = []
        if pdir and a.set:
            rel = f"{proofs.AUDIO_DIR}/{slug}_channel.wav"
            mf.write_wav(os.path.join(a.proofs, rel), channel_8k(os.path.join(a.set, name)))
            audio.append({"label": "On the channel", "file": rel})
        if not parts:
            print_row(slug, meta)
            items.append(proofs.item(label, proofs.mode_text(label, meta), "none",
                                     "Not decoded yet: no payload bits from the modem.",
                                     audio=audio))
            continue
        n_have += 1
        results = []
        for part, _ in parts:
            pmeta = load_meta(a.payload, part) if part != slug else meta
            if pmeta.get("decoder"):
                decoders.add(str(pmeta["decoder"]))
            r = score_part(a, part, pmeta, rate_hint)
            print_row(part, pmeta, r)
            if r["x"] is not None:
                mf.write_wav(os.path.join(a.audio, part + ".wav"), r["x"])
            results.append(r)
        item = recording_item(label, proofs.mode_text(label, meta), results, audio,
                              a.proofs, slug) if pdir else None
        if item:
            items.append(item)
        n_pass += combine_status([r["status"] for r in results]) == "pass"
    print("-" * len(hdr))
    print(f"speech PASS (track >= {PASS_TRACK}, {PASS_MARGIN} over the next choice, every "
          f"message): {n_pass}/{n_have} recordings with a payload, {len(FILES)} in the set")
    if pdir:
        decoder = a.decoder or (" ; ".join(sorted(decoders)) if decoders else
                                "m110a_rx: not run on these files yet")
        if n_have == 0:
            note = ("The preamble and mode code are read on all eight; no payload bits have "
                    "been decoded to speech yet.")
        else:
            note = (f"Payload bits for {n_have} of {len(FILES)} files; {n_pass} decode to "
                    f"speech by the speech measure.")
        proofs.write(a.proofs, proofs.document(decoder, note, items))
        print(f"proofs: t7.json with {len(items)} items")
    return 0 if n_have and n_pass == n_have else 1


if __name__ == "__main__":
    sys.exit(main())
