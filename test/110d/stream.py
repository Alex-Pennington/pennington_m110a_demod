#!/usr/bin/env python3
"""Streaming test for `m110d_rx --stream`: the interop set as one continuous feed on stdin.

Usage:
  python3 -I test/110d/stream.py --rx build-110d/bin/m110d_rx --set <test-set dir>
      [--resample RATE --workdir DIR] [--pace X] [--gaps 0.5,1,2,3]
      [--noise-snr DB --duration S --every S] [--files 1,2,...] [--seed N]

The files of the set (README order, or --files) are joined into one 16-bit mono WAV stream
whose RIFF and data lengths are 0xFFFFFFFF, as a streaming writer leaves them, and written to
the receiver's stdin in 0.1 s pieces.

  --pace X        feed at X times real time (1 = real time); 0 (default) as fast as the
                  receiver reads.
  --gaps          seconds of silence between files, used in turn (default 0.5,1,2,3).
  --noise-snr DB  add white Gaussian noise to the whole feed; DB is the SNR in 3 kHz against
                  each file's average power over its non-silent samples. With --duration S
                  the feed lasts S seconds: noise throughout, one file every --every seconds
                  (default 60), cycling through the files.
  --resample      resample the files first (score.py's resampler) to RATE Hz under --workdir.

The receiver must print one message block per file, in order: the same WID and interleaver
and decoded text starting with README's text (score.py's prefix rule). Extra, missing or
out-of-order blocks fail. Printed per message: the feed time the file ended and the time the
block arrived (latency, at --pace 1). At the end: the receiver's peak RSS (ru_maxrss of the
child) and its wall time.

Exit status: 0 when every file is decoded once, in order; 1 otherwise.
"""
import argparse
import os
import random
import re
import resource
import struct
import subprocess
import sys
import threading
import time
import wave
import importlib.util

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("score110d", os.path.join(HERE, "score.py"))
score = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(score)


def load(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise ValueError(f"{path}: 16-bit PCM only")
        fs, ch = w.getframerate(), w.getnchannels()
        raw = w.readframes(w.getnframes())
    x = list(struct.unpack("<%dh" % (len(raw) // 2), raw))[::ch]
    return fs, x


def header(fs):
    return (b"RIFF" + struct.pack("<I", 0xFFFFFFFF) + b"WAVE" + b"fmt " +
            struct.pack("<IHHIIHH", 16, 1, 1, fs, fs * 2, 2, 16) + b"data" + struct.pack("<I", 0xFFFFFFFF))


def active_power(x):
    act = [v * v for v in x if abs(v) > 64]
    return sum(act) / len(act) if act else 0.0


def build_feed(clips, fs, gaps, snr, duration, every, seed):
    """Yields (samples list, list of (row, end_time_s)) pieces of the feed, in order."""
    rng = random.Random(seed)
    if snr is None:
        t = 0
        for k, (row, x) in enumerate(clips):
            g = [0] * int(gaps[k % len(gaps)] * fs)
            t += len(x)
            yield x, [(row, t / fs)]
            t += len(g)
            yield g, []
        return
    total = int(duration * fs)
    period = int(every * fs)
    t, k = 0, 0
    while t < total:
        n = min(period, total - t)
        row, x = clips[k % len(clips)]
        p = active_power(x)
        sigma = (p * fs / 6000 / 10 ** (snr / 10)) ** 0.5  # white over fs/2; 3 kHz of it carries p/SNR
        seg = [rng.gauss(0, sigma) for _ in range(n)]
        lead = int(0.2 * period)
        placed = []
        if lead + len(x) <= n:
            for i, v in enumerate(x):
                seg[lead + i] += v
            placed = [(row, (t + lead + len(x)) / fs)]
            k += 1
        seg = [max(-32768, min(32767, int(round(v)))) for v in seg]
        yield seg, placed
        t += n


def parse_blocks(lines):
    blocks, cur = [], None
    for ts, line in lines:
        if line.startswith("message="):
            cur = {"_t": ts}
        if cur is None:
            continue
        if line == "":
            blocks.append(cur)
            cur = None
            continue
        k, _, v = line.partition("=")
        cur[k] = v
        cur["_t"] = ts
    if cur:
        blocks.append(cur)
    return blocks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rx", required=True)
    ap.add_argument("--set", required=True)
    ap.add_argument("--files", help="README row numbers, comma-separated (default: all 60)")
    ap.add_argument("--resample", type=int, default=0)
    ap.add_argument("--workdir")
    ap.add_argument("--pace", type=float, default=0.0)
    ap.add_argument("--gaps", default="0.5,1,2,3")
    ap.add_argument("--noise-snr", type=float)
    ap.add_argument("--duration", type=float, default=3600.0)
    ap.add_argument("--every", type=float, default=60.0)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--rx-args", default="", help="extra receiver arguments, space-separated")
    a = ap.parse_args()

    rows = score.parse_readme(os.path.join(a.set, "README.md"))
    if a.files:
        want = [int(v) for v in a.files.split(",")]
        rows = [r for n in want for r in rows if r["num"] == n]
    clips, fs = [], None
    for r in rows:
        path = os.path.join(a.set, r["file"])
        if a.resample:
            os.makedirs(a.workdir, exist_ok=True)
            dst = os.path.join(a.workdir, r["file"])
            if not os.path.exists(dst):
                score.resample_wav(path, dst, a.resample)
            path = dst
        f, x = load(path)
        if fs is None:
            fs = f
        if f != fs:
            print(f"{r['file']}: {f} Hz, expected {fs}", file=sys.stderr)
            return 2
        clips.append((r, x))
    gaps = [float(v) for v in a.gaps.split(",")]

    cmd = [a.rx, "--stream", *a.rx_args.split()]
    t_start = time.monotonic()
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)
    lines = []

    def reader():
        buf = b""
        for chunk in iter(lambda: p.stdout.read1(65536) if hasattr(p.stdout, "read1") else p.stdout.read(4096), b""):
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                lines.append((time.monotonic() - t_start, line.decode("utf-8", "replace")))

    th = threading.Thread(target=reader, daemon=True)
    th.start()
    expected = []  # (row, feed end time s)
    fed = 0
    try:
        p.stdin.write(header(fs))
        piece = fs // 10
        for samples, placed in build_feed(clips, fs, gaps, a.noise_snr, a.duration, a.every, a.seed):
            expected += placed
            for i in range(0, len(samples), piece):
                part = samples[i:i + piece]
                p.stdin.write(struct.pack("<%dh" % len(part), *part))
                fed += len(part)
                if a.pace > 0:
                    due = fed / fs / a.pace
                    lag = due - (time.monotonic() - t_start)
                    if lag > 0:
                        time.sleep(lag)
        p.stdin.close()
    except BrokenPipeError:
        pass
    rc = p.wait()
    th.join()
    wall = time.monotonic() - t_start
    rss_kb = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss

    blocks = parse_blocks(lines)
    ok = 0
    print(f"feed: {fed / fs:.1f} s at {fs} Hz, {len(expected)} files, pace {a.pace or 'max'}"
          + (f", noise SNR {a.noise_snr:g} dB" if a.noise_snr is not None else f", gaps {a.gaps} s"))
    print(f"{'#':>3} {'file':<46} {'got WID/IL':<16} {'EOM':<4} {'text':<5} {'fed_s':>8} {'out_s':>8}")
    for i, (r, t_end) in enumerate(expected):
        b = blocks[i] if i < len(blocks) else {}
        id_ok = b.get("wid") == str(r["wid"]) and b.get("interleaver") == r["il"] and b.get("wid_checksum") == "ok"
        t_ok = score.unescape(b.get("text", "")).startswith(r["text"])
        good = id_ok and t_ok
        ok += good
        print(f"{i + 1:>3} {r['file'][:46]:<46} {b.get('wid', '-') + '/' + b.get('interleaver', '-'):<16} "
              f"{b.get('eom', '-'):<4} {'PASS' if good else 'FAIL':<5} {t_end:>8.1f} {b.get('_t', float('nan')):>8.1f}")
    extra = len(blocks) - len(expected)
    print(f"messages: {len(blocks)} out, {len(expected)} in; in order and correct: {ok}/{len(expected)}; "
          f"extra: {max(0, extra)}")
    print(f"receiver: rc={rc} wall={wall:.1f} s peak_rss={rss_kb} kB")
    return 0 if ok == len(expected) and extra == 0 and rc == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
