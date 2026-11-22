#!/usr/bin/env python3
"""Score m110d_rx against the MIL-STD-188-110D Appendix D interop test set.

Usage:
  python3 -I test/110d/score.py --rx build-110d/bin/m110d_rx --set <test-set dir> [--jobs N]
      [--resample RATE --workdir DIR] [--stdin [--stream-sizes keep|zero|ff]]

--resample RATE resamples each test file to RATE Hz (16-bit PCM WAV under --workdir) before
decoding. The resampler is scipy's resample_poly with its own Kaiser FIR, independent of the
receiver's (src/m110d/appd_input.h): passband 0 .. 0.425*min(rate in, rate out) (0 .. 3400 Hz
for 48 -> 8 kHz), stopband from 0.5*min (4000 Hz up) at 100 dB. It needs numpy and scipy.
--stdin feeds each WAV to `m110d_rx -` on stdin instead of naming the file. --stream-sizes
sets the RIFF and data length fields of the piped WAV: as written (keep), 0 (zero) or
0xFFFFFFFF (ff, the default), as a streaming writer leaves them.

The expected WID, interleaver, EOM setting and text are read from README.md in the test-set
directory (its "Files" table); nothing from the test set is copied into this repo. The text
column there is the first ~100 characters of each message, so the text check is a prefix
match: decoded text must start with it. For the no-EOM file the decoded text runs on into
the zero padding of the last interleaver block; the prefix match passes that by design.

Text is scored for every waveform, WID 0-13: WID 0 (Walsh), WID 1-5 (BPSK) and WID 6-13
(QPSK, 8PSK, 16/32/64/256QAM).

Exit status: 0 when every file reports the expected WID and interleaver and every
file passes the text check, 1 otherwise.
"""
import argparse
import concurrent.futures
import math
import os
import re
import struct
import subprocess
import sys
import tempfile
import wave

IL_NAMES = {"ultra-short", "short", "medium", "long"}
TEXT_WIDS = range(0, 14)  # waveforms whose payload text is scored


def parse_readme(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            if not line.startswith("| ") or "`" not in line:
                continue
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if len(cells) < 12 or not cells[0].isdigit():
                continue
            m = re.search(r"`([^`]*)`\s*\|\s*$", line)
            if not m:
                continue
            rows.append({
                "num": int(cells[0]),
                "file": cells[1].strip("`"),
                "wid": int(cells[2]),
                "il": cells[5],
                "eom": cells[8],
                "variant": cells[9],
                "text": m.group(1),
            })
    return rows


def unescape(s):
    return re.sub(r"\\x([0-9a-fA-F]{2})", lambda m: chr(int(m.group(1), 16)), s)


def resample_wav(src, dst, rate):
    """Write src resampled to `rate` Hz as 16-bit mono PCM. Returns the clipped-sample count."""
    import numpy as np
    from scipy import signal
    with wave.open(src, "rb") as w:
        fs, ch, sw = w.getframerate(), w.getnchannels(), w.getsampwidth()
        if sw != 2:
            raise ValueError(f"{src}: only 16-bit input supported")
        x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, ch)[:, 0] / 32768.0
    g = math.gcd(fs, rate)
    up, down = rate // g, fs // g
    fmin, fh = min(fs, rate), fs * up
    fp, fsb, atten = 0.425 * fmin, 0.5 * fmin, 100.0
    ntaps, beta = signal.kaiserord(atten, (fsb - fp) / (0.5 * fh))
    ntaps |= 1
    taps = signal.firwin(ntaps, 0.5 * (fp + fsb), window=("kaiser", beta), fs=fh)
    y = signal.resample_poly(x, up, down, window=taps) * 32768.0
    clipped = int(np.sum(np.abs(y) > 32767))
    with wave.open(dst, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(np.clip(np.round(y), -32768, 32767).astype("<i2").tobytes())
    return clipped


def stream_bytes(path, sizes):
    """The WAV at path as bytes, with its RIFF and data lengths set as a streaming writer would."""
    b = bytearray(open(path, "rb").read())
    if sizes == "keep":
        return bytes(b)
    v = 0 if sizes == "zero" else 0xFFFFFFFF
    struct.pack_into("<I", b, 4, v)
    pos = 12
    while pos + 8 <= len(b):
        cid, sz = bytes(b[pos:pos + 4]), struct.unpack_from("<I", b, pos + 4)[0]
        if cid == b"data":
            struct.pack_into("<I", b, pos + 4, v)
            return bytes(b)
        pos += 8 + sz + (sz & 1)
    raise ValueError(f"{path}: no data chunk")


def run_one(rx, path, stdin=False, sizes="ff"):
    if stdin:
        p = subprocess.run([rx, "-"], input=stream_bytes(path, sizes), capture_output=True, timeout=600)
        p.stdout = p.stdout.decode("utf-8", "replace")
    else:
        p = subprocess.run([rx, path], capture_output=True, text=True, timeout=600)
    kv = {}
    for line in p.stdout.splitlines():
        k, _, v = line.partition("=")
        kv[k] = v
    kv["_rc"] = p.returncode
    return kv


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rx", required=True)
    ap.add_argument("--set", required=True, help="test-set directory holding README.md and the WAVs")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--resample", type=int, default=0, metavar="RATE",
                    help="resample each file to RATE Hz before decoding")
    ap.add_argument("--workdir", help="where --resample writes its WAVs (default: a new dir under TMPDIR)")
    ap.add_argument("--stdin", action="store_true", help="feed each WAV to the receiver on stdin")
    ap.add_argument("--stream-sizes", choices=("keep", "zero", "ff"), default="ff",
                    help="RIFF/data length fields of the WAV sent on stdin (default ff = 0xFFFFFFFF)")
    a = ap.parse_args()

    rows = parse_readme(os.path.join(a.set, "README.md"))
    if len(rows) != 60:
        print(f"README.md: expected 60 rows, parsed {len(rows)}", file=sys.stderr)
        return 2
    for r in rows:
        if r["il"] not in IL_NAMES:
            print(f"README.md row {r['num']}: unknown interleaver {r['il']!r}", file=sys.stderr)
            return 2

    paths = {r["num"]: os.path.join(a.set, r["file"]) for r in rows}
    if a.resample:
        work = a.workdir or tempfile.mkdtemp(prefix="score110d-")
        os.makedirs(work, exist_ok=True)
        clipped = 0
        for r in rows:
            dst = os.path.join(work, r["file"])
            clipped += resample_wav(paths[r["num"]], dst, a.resample)
            paths[r["num"]] = dst
        print(f"resampled to {a.resample} Hz in {work}, clipped samples: {clipped}")
    if a.stdin:
        print(f"input on stdin, length fields: {a.stream_sizes}")

    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        futs = {n: ex.submit(run_one, a.rx, p, a.stdin, a.stream_sizes) for n, p in paths.items()}
        res = {n: f.result() for n, f in futs.items()}

    hdr = f"{'#':>2}  {'exp WID':>7} {'exp IL':<11}  {'got WID':>7} {'got IL':<11}  {'ID':<4}  {'text':<4}  {'EOM exp/got':<11}  {'PN':<10}  note"
    print(hdr)
    print("-" * len(hdr))
    n_id = n_text = n_text_scope = n_text_eom = 0
    n_text_by, n_scope_by = [0, 0, 0], [0, 0, 0]  # [WID 0, WID 1-5, WID 6-13]
    failed = []
    for r in rows:
        k = res[r["num"]]
        gw = k.get("wid", "-")
        gi = k.get("interleaver", "-")
        id_ok = gw == str(r["wid"]) and gi == r["il"] and k.get("wid_checksum") == "ok"
        n_id += id_ok
        if r["wid"] in TEXT_WIDS:
            band = 0 if r["wid"] == 0 else 1 if r["wid"] <= 5 else 2
            n_text_scope += 1
            n_scope_by[band] += 1
            got = unescape(k.get("text", ""))
            t_ok = got.startswith(r["text"])
            n_text += t_ok
            n_text_by[band] += t_ok
            tcol = "PASS" if t_ok else "FAIL"
            eom_got = k.get("eom", "-")
            n_text_eom += t_ok and eom_got == {"on": "1", "off": "0"}.get(r["eom"])
        else:
            tcol = "n/a"
            eom_got = "-"
            t_ok = True
        if not (id_ok and t_ok):
            failed.append(r["num"])
        note = r["variant"] or ""
        if k.get("payload_note") and r["wid"] in TEXT_WIDS:
            note = (note + " " + k["payload_note"]).strip()
        if k.get("_rc"):
            note = (note + f" rc={k['_rc']}").strip()
        print(f"{r['num']:>2}  {r['wid']:>7} {r['il']:<11}  {gw:>7} {gi:<11}  {'PASS' if id_ok else 'FAIL':<4}  "
              f"{tcol:<4}  {r['eom'] + '/' + eom_got:<11}  {k.get('pn', '-'):<10}  {note}")
    print("-" * len(hdr))
    if failed:
        print("detail for files failing ID or text:")
        for n in failed:
            k = res[n]
            print(f"  {n:>2} " + " ".join(f"{f}={k.get(f, '-')}" for f in
                  ("acq_metric", "freq_offset_hz", "dcd", "reencode_agree", "probe_quality", "evm_db",
                   "points_unused", "eom", "bytes", "payload_note")))
        print("-" * len(hdr))
    print(f"WID+interleaver correct: {n_id}/60")
    print(f"text PASS (WID 0):       {n_text_by[0]}/{n_scope_by[0]}")
    print(f"text PASS (WID 1-5):     {n_text_by[1]}/{n_scope_by[1]}")
    print(f"text PASS (WID 6-13):    {n_text_by[2]}/{n_scope_by[2]}")
    print(f"text PASS, EOM as expected (WID 0-13): {n_text_eom}/{n_text_scope}")
    return 0 if n_id == 60 and n_text == n_text_scope else 1


if __name__ == "__main__":
    sys.exit(main())
