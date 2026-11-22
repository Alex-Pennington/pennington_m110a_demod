#!/usr/bin/env python3
"""Score the Harris MELP test transmissions: recordings -> payload bits -> MELPe frames -> audio.

Usage:
  python3 -I -B test/harris/score.py --melpe <MELPe program> --payload DIR
        [--rx build/bin/m110a_rx --score build/bin/harris_score] [--set <test-set dir>]
        [--search | --search-all] [--audio DIR] [--work DIR] [--jobs N]
        [--proofs DIR [--decoder TEXT]]
  python3 -I -B test/harris/score.py --melpe <MELPe program> --calibrate RAW [RAW ...]
        [--work DIR]

Two stages, one pass table:

1. Modem (modem.py, with --rx and --score): each recording through m110a_rx AUTO, then each
   message it reports through harris_score twice (equalized and phase-tracked only).
   Writes <payload>/<slug>.bin (LSB first, cut at the EOM), .bits, BIT-ORDER.txt, and a
   <slug>.json sidecar per message: mode, D1, D2, countdown, EOM, verified (the two
   harris_score decodes identical and both found the EOM) and the MELPe rate of the mode.
   Without --rx the MELPe stage reads a payload directory the modem stage wrote before.
2. MELPe: each payload cut into frames, decoded through the MELPe program, and scored as
   speech.

The MELPe program is the STANAG 4591 reference coder's command line
(`-r <rate> -m A|S -i <in> -o <out>`, -b 54 byte frames); it is run as an external
program and is not part of this repo.

MELPe stage inputs, per file of the test set (slugs in FILES below):
  <payload>/<slug>.bin   payload bytes from the 110A receiver, or, for a recording with
                         several preambles, one <slug>_m<N>.bin per message (N = 1, 2, ...),
                         each scored on its own with its own <slug>_m<N>.json; the proof
                         item stays one per recording, its decoded speech the messages
                         joined in order with 0.5 s of silence between them
  <payload>/<slug>.json  optional: {"preamble": ..., "mode": ..., "bit_order": "msb"|"lsb",
                          "rate": 600|1200|2400, "offset": int, "reverse": bool,
                          "invert": bool, "verified": bool, "decoder": str, "d1": int,
                          "d2": int, "countdown": int, "preambles": int}. Keys present
                          fix that choice ("packing": "msb"|"lsb" fixes the coder byte
                          packing). Absent keys take the defaults (rate from the slug, LSB
                          first, the sync-bit offset or 0, the coder's measured packing, no
                          reversal or inversion); --search tries both byte orders, every
                          frame offset and, at 600 bps, both coder packings; --search-all
                          also every rate, reversal and inversion.
Outputs: <audio>/<slug>.wav, 8 kHz mono 16-bit, with --audio.

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


def decode_frames(melpe, frames, rate, work, tag, packing=None):
    """Frames (n, n_bits) -> int16 audio through the coder's synthesis."""
    bit = os.path.join(work, tag + ".bit")
    pcm = os.path.join(work, tag + ".pcm")
    with open(bit, "wb") as f:
        f.write(mf.pack_frames(frames, rate, packing))
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
    rates = [int(meta["rate"])] if "rate" in meta else ([2400, 1200, 600] if wide else [rate_hint])
    # the modem stage's payloads are LSB first (BIT-ORDER.txt); --search tries both
    orders = [meta["bit_order"]] if "bit_order" in meta else (["lsb", "msb"] if search else ["lsb"])
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
        # coder byte packing: as measured from the coder, and at 600 bit/s, where that
        # differs from RFC 8130 Figure 4, also RFC 8130's under --search
        default = mf.RATES[rate][4]
        if "packing" in meta:
            packs = [meta["packing"]]
        elif rate == 600 and (search or wide):
            packs = [default, "lsb" if default == "msb" else "msb"]
        else:
            packs = [default]
        for off in offsets_for(bits, rate, rev, meta, search or wide):
            frames = mf.cut_frames(bits, rate, off, rev, inv)
            if len(frames) < 2:
                continue
            for pk in packs:
                tasks.append((dict(rate=rate, order=order, offset=off, reverse=rev, invert=inv,
                                   packing=pk, frames=len(frames),
                                   sync=mf.sync_score(frames, rate)),
                              frames))

    def one(i):
        d, frames = tasks[i]
        x = decode_frames(melpe, frames, d["rate"], work, f"{slug}_{i}", d["packing"])
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


def mode_key(meta, rate_hint):
    return str(meta.get("mode") or f"{rate_hint} bps")


def period_checks(payload_dir, plan):
    """Frame-period check per mode, pooled over every message of that mode.
    plan: [(slug, name, rate_hint, [(part, meta)])]. Returns {mode: (best, zbest, zn, ok, rate)}."""
    groups = {}
    for _, _, rate_hint, parts in plan:
        for part, meta in parts:
            rate = int(meta.get("rate", rate_hint))
            with open(os.path.join(payload_dir, part + ".bin"), "rb") as f:
                bits = mf.bytes_to_bits(f.read(), meta.get("bit_order", "lsb"))
            groups.setdefault(mode_key(meta, rate_hint), (rate, []))[1].append(bits)
    out = {}
    for mode, (rate, streams) in groups.items():
        best, zb, zn, ok = mf.period_check(streams, rate)
        out[mode] = (best, zb, zn, ok, rate)
    return out


def score_part(a, part, meta, rate_hint, period=None):
    """Score one payload file. Returns a dict with bytes, d (best choice or None), x, status.

    period: this mode's period check; when the stream does not repeat at the rate's frame
    size, it is not decoded (the MELPe program would turn it into noise)."""
    with open(os.path.join(a.payload, part + ".bin"), "rb") as f:
        payload = f.read()
    r = {"part": part, "bytes": len(payload), "d": None, "x": None, "status": "fail",
         "period": period}
    if period is not None and not period[3]:
        verified = bool(meta.get("verified"))
        r.update(ok=False, status="partial" if verified else "fail")
        return r
    best = score_file(a.melpe, payload, meta, rate_hint, a.search, a.work, part, a.jobs,
                      a.search_all)
    if best is None:
        return r
    _, d, x = best
    ok = d["m"]["track"] >= PASS_TRACK and d["m"]["track"] - d["second"] >= PASS_MARGIN
    # the modem stage's verification when the sidecar has one; else the sync bit
    verified = bool(meta["verified"]) if "verified" in meta else \
        (d["sync"] is not None and d["sync"] >= SYNC_TRUST)
    r.update(d=d, x=x, ok=ok, status=proofs.status_for(True, verified, ok))
    return r


HDR = (f"{'message':<20} {'mode':<6} {'D1,D2':<5} {'EOM':<3} {'modem':<8} {'bytes':>5}  "
       f"{'rate':>4} {'order':<5} {'pack':<4} {'off':>3} {'rev':<3} {'inv':<3} {'frames':>6} "
       f"{'sync':>5}  {'voiced':>6} {'track':>6} {'2nd':>5} {'f0':>4}  result")


def print_row(part, meta, r=None):
    d1d2 = f"{meta['d1']},{meta['d2']}" if "d1" in meta and "d2" in meta else "-"
    eom = {True: "yes", False: "no"}.get(meta.get("eom"), "-")
    ver = {True: "verified", False: "NOT"}.get(meta.get("verified"), "-")
    head = f"{part:<20} {str(meta.get('mode', '-'))[:6]:<6} {d1d2:<5} {eom:<3} {ver:<8}"
    if r is None:
        print(f"{head} {'-':>5}  NO PAYLOAD")
        return
    d = r["d"]
    pc = r.get("period")
    if d is None and pc is not None and not pc[3]:
        print(f"{head} {r['bytes']:>5}  {pc[4]:>4}  not decoded: the mode's bits repeat every "
              f"{pc[0]} bits (z {pc[1]:.1f}), not {mf.frame_bits(pc[4])} (z {pc[2]:.1f})  "
              f"NOT 4591")
        return
    if d is None:
        print(f"{head} {r['bytes']:>5}  too short for one frame")
        return
    sync = "-" if d["sync"] is None else f"{d['sync']:.2f}"
    print(f"{head} {r['bytes']:>5}  {d['rate']:>4} {d['order']:<5} {d['packing']:<4} "
          f"{d['offset']:>3} {'y' if d['reverse'] else 'n':<3} {'y' if d['invert'] else 'n':<3} "
          f"{d['frames']:>6} {sync:>5}  {d['m']['voiced']:>6.3f} {d['m']['track']:>6.3f} "
          f"{d['second']:>5.3f} {d['m']['f0_median']:>4.0f}  {'PASS' if r['ok'] else 'FAIL'}")


def describe(r):
    """(decoded, detail) text for one message."""
    d = r["d"]
    pc = r.get("period")
    if d is None and pc is not None and not pc[3]:
        ms = 1000 * pc[0] / pc[4]
        return (f"{r['bytes']} payload bytes, not decoded: the bits repeat every {pc[0]} bits "
                f"({ms:.0f} ms at {pc[4]} bps), not every {mf.frame_bits(pc[4])} as STANAG 4591 "
                f"{pc[4]} bps MELPe frames do"), None
    if d is None:
        return f"{r['bytes']} payload bytes, too short for one MELPe frame", None
    decoded = (f"{r['bytes']} payload bytes, {d['frames']} MELPe {d['rate']} bps frames, "
               f"speech measure {d['m']['track']:.2f} (next best reading {d['second']:.2f})")
    pack = "RFC 8130" if d["packing"] == "lsb" else "MSB-first"
    detail = (f"payload bytes read {d['order'].upper()} first, frame offset {d['offset']} bits, "
              f"coder packing {pack}"
              + (", frames reversed" if d["reverse"] else "")
              + (", bits inverted" if d["invert"] else "")
              + ("" if d["sync"] is None else
                 f", sync bit alternates in {100 * d['sync']:.0f} % of frames"))
    return decoded, detail


def recording_item(label, mode, results, audio, proofs_root, slug):
    """One proof item for a recording from its messages' results; writes its decoded audio."""
    status = combine_status([r["status"] for r in results])
    texts = [describe(r) for r in results]
    skipped = [r for r in results if r["d"] is None and r.get("period") and not r["period"][3]]
    if len(results) > 1 and len(skipped) == len(results):
        pc = results[0]["period"]
        decoded = (f"{len(results)} messages, one per preamble, of "
                   + ", ".join(str(r["bytes"]) for r in results)
                   + f" payload bytes. Not decoded: the bits repeat every {pc[0]} bits "
                   f"({1000 * pc[0] / pc[4]:.0f} ms at {pc[4]} bps), not every "
                   f"{mf.frame_bits(pc[4])} as STANAG 4591 {pc[4]} bps MELPe frames do.")
        return proofs.item(label, mode, status, decoded, None, audio)
    if len(results) == 1 and skipped:
        decoded = texts[0][0] + "."
        return proofs.item(label, mode, status, decoded, None, audio)
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


# MELPe rate carried by each receiver mode (docs/harris-melpe.md, Modem)
MODE_RATE = {"600H": 600, "600S": 600, "600L": 600, "1200H": 1200, "1200S": 1200,
             "1200L": 1200, "2400V": 2400, "2400S": 2400, "2400L": 2400}
SIDECAR_KEYS = ("mode", "d1", "d2", "countdown", "preambles", "bytes", "eom", "verified")


def write_sidecars(payload_dir, records, decoder=None):
    """<slug>.json per message from the modem stage: what the preamble and the modem
    verification said, and the MELPe rate the mode carries."""
    for rec in records:
        meta = {k: rec[k] for k in SIDECAR_KEYS if rec.get(k) is not None}
        for k in ("d1", "d2", "countdown"):
            if k in meta and str(meta[k]).lstrip("-").isdigit():
                meta[k] = int(meta[k])
        if rec.get("mode") in MODE_RATE:
            meta["rate"] = MODE_RATE[rec["mode"]]
        if decoder:
            meta["decoder"] = decoder
        with open(os.path.join(payload_dir, rec["slug"] + ".json"), "w", encoding="utf-8") as f:
            json.dump(meta, f, indent=1)
            f.write("\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--melpe", required=True, help="MELPe reference coder program")
    ap.add_argument("--rx", help="m110a_rx: run the modem stage first (with --score and --set)")
    ap.add_argument("--score", help="harris_score, for the modem stage")
    ap.add_argument("--payload", help="payload directory: the modem stage writes it, "
                                      "the MELPe stage reads it")
    ap.add_argument("--audio", help="also write each message's decoded speech here")
    ap.add_argument("--work", help="scratch directory (default <payload>/work)")
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
    if not a.calibrate and not a.payload:
        print("--payload is required", file=sys.stderr)
        return 2
    if a.rx and not (a.score and a.set):
        print("the modem stage (--rx) needs --score and --set", file=sys.stderr)
        return 2
    a.work = a.work or os.path.join(a.payload or ".", "work")
    os.makedirs(a.work, exist_ok=True)

    if a.calibrate:
        calibrate(a.melpe, a.calibrate, a.work)
        return 0

    if a.rx:
        import modem
        print("modem stage")
        rows, records, _ = modem.decode_set(a.rx, a.score, a.set, a.payload, a.work)
        modem.print_rows(rows)
        print()
        write_sidecars(a.payload, records, a.decoder)

    if a.audio:
        os.makedirs(a.audio, exist_ok=True)
    pdir = None
    if a.proofs:
        pdir = os.path.join(a.proofs, proofs.AUDIO_DIR)
        os.makedirs(pdir, exist_ok=True)
    items = []
    decoders = set()
    plan = []
    for slug, name, rate_hint in FILES:
        parts = payload_parts(a.payload, slug)
        plan.append((slug, name, rate_hint,
                     [(p, load_meta(a.payload, p)) for p, _ in parts]))
    checks = period_checks(a.payload, plan)
    print("frame period per mode (pooled over its messages)")
    for mode, (best, zb, zn, ok, rate) in sorted(checks.items()):
        print(f"  {mode:<8} {rate} bps: strongest period {best} bits (z {zb:.1f}); "
              f"at the {mf.frame_bits(rate)}-bit frame size z {zn:.1f}; "
              f"{'STANAG 4591 frames' if ok else 'NOT STANAG 4591 frames: not decoded'}")
    print()
    hdr = HDR
    print("MELPe stage")
    print(hdr)
    print("-" * len(hdr))
    n_have = n_pass = n_partial = 0
    for slug, name, rate_hint, pmetas in plan:
        parts = [(p, None) for p, _ in pmetas]
        # the recording's sidecar, else its first message's, gives the preamble reading
        meta = load_meta(a.payload, slug) or (pmetas[0][1] if pmetas else {})
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
            r = score_part(a, part, pmeta, rate_hint, checks.get(mode_key(pmeta, rate_hint)))
            print_row(part, pmeta, r)
            if r["x"] is not None and a.audio:
                mf.write_wav(os.path.join(a.audio, part + ".wav"), r["x"])
            results.append(r)
        item = recording_item(label, proofs.mode_text(label, meta), results, audio,
                              a.proofs, slug) if pdir else None
        if item:
            items.append(item)
        st = combine_status([r["status"] for r in results])
        n_pass += st == "pass"
        n_partial += st == "partial"
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
            note = (f"Payload bits for {n_have} of {len(FILES)} recordings; {n_pass} decode to "
                    f"speech by the speech measure, and {n_partial} more have verified bits "
                    f"but no speech yet.")
            odd = sorted(m for m, c in checks.items() if not c[3])
            if odd:
                note += (f" The {', '.join(odd)} payloads are not STANAG 4591 frames: their bits "
                         f"repeat every {checks[odd[0]][0]} bits, so the STANAG coder cannot "
                         f"decode them.")
        proofs.write(a.proofs, proofs.document(decoder, note, items))
        print(f"proofs: t7.json with {len(items)} items")
    return 0 if n_have and n_pass == n_have else 1


if __name__ == "__main__":
    sys.exit(main())
