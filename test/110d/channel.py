#!/usr/bin/env python3
"""Channel matrix for m110d_rx: impair test-set files with m110d_chan, decode, score.

Usage:
  python3 -I test/110d/channel.py --rx build-110d/bin/m110d_rx --chan build-110d/bin/m110d_chan \
      --set <test-set dir> [--matrix default] [--files 1,13,53,29,41] [--seeds N] [--jobs 2]
      [--bandpass LO:HI[:N]] [--proofs DIR [--decoder-label TEXT] [--proofs-note TEXT]]

--proofs DIR also writes DIR/t6.json for a report page: one item per condition, summed over
the files and seeds run (status pass when every run decoded the message, partial when some
did, fail when none did). It refuses, as score.py does, a text field holding a path or host
name: nothing is written and the exit status is 3.

Each file is run through every condition of the chosen matrix with N seeds (seed 1..N), each
impaired WAV is decoded by m110d_rx, and the decode passes when its text starts with the
expected text in <set>/README.md: the same prefix rule as score.py. The table has one row per
condition and one column per file; a cell is passes/seeds. An `id` column-suffix `*` marks a
cell where some seed passed the text check but not the WID/interleaver check (should not occur).

Matrices (--matrix, comma-separated; `all` is every one):
  default  a small cross-section: clean, AWGN, frequency offset, clock offset, Poor channel
  awgn     AWGN only, SNR stepped relative to each file's Table D-LXIV AWGN requirement
  foff     constant frequency offset at 30 dB SNR
  drift    linear frequency drift and the D.6.5 triangle sweep, at 30 dB SNR
  ppm      sample-clock offset and drift, at 30 dB SNR
  fading   ITU good / moderate / poor (Watterson) and the D.6.4 +-75 Hz Doppler shift test
  static   Table D-LXV 3 kHz static multipath channels at 50 dB SNR
  band     a radio's SSB audio filter (m110d_chan --bandpass, 8th-order Butterworth edges) on
           its own and at 30 dB: 300-2700, 300-3000, 300-3300 and 200-3000 Hz

--bandpass LO:HI[:N] adds that receiver audio filter to every condition of the chosen matrices
(m110d_chan --bandpass), so `--matrix default --bandpass 300:2700` is the default matrix
heard through a 300-2700 Hz radio. Condition labels gain a ` bp LO-HI` suffix.

SNR is in 3 kHz (m110d_chan --noise-bw default) against the burst's average power (E.5.5).
Impaired WAVs go to a scratch directory under $TMPDIR (or --work) and are deleted after each
decode unless --keep is given.
"""
import argparse
import concurrent.futures
import csv
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True  # importing score.py must not leave __pycache__ in the tree
HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("score110d", os.path.join(HERE, "score.py"))
score = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(score)

# Table D-LXIV: average SNR (dB, 3 kHz) for coded BER <= 1.0E-5, Long interleaver.
# None: the table gives no Poor-channel requirement for that WID.
DLXIV_AWGN = {0: -6, 1: -3, 2: 0, 3: 3, 4: 5, 5: 6, 6: 9, 7: 13, 8: 16, 9: 19, 10: 21, 11: 24, 12: 30, 13: 6}
DLXIV_POOR = {0: -1, 1: 3, 2: 5, 3: 7, 4: 10, 5: 11, 6: 14, 7: 19, 8: 23, 9: 27, 10: 31, 11: None, 12: None, 13: 11}

HI_SNR = 30  # dB; the D.6.4 SNR, used to isolate the other impairments


class Cond:
    """One channel condition. `args(row)` returns m110d_chan arguments, or None if not applicable."""

    def __init__(self, label, args, seeds=None):
        self.label = label
        self._args = args
        self.seeds = seeds  # None: use --seeds; 1 for deterministic conditions

    def args(self, row):
        a = self._args(row) if callable(self._args) else self._args
        return None if a is None else [str(x) for x in a]


def snr_rel(off, table=DLXIV_AWGN, extra=()):
    def f(row):
        t = table.get(row["wid"])
        return None if t is None else ["--snr", t + off, *extra]
    return f


def matrices():
    m = {}
    m["default"] = [
        Cond("clean", [], seeds=1),
        Cond("awgn D-LXIV+0", snr_rel(0)),
        Cond("awgn D-LXIV+6", snr_rel(6)),
        Cond("awgn D-LXIV+12", snr_rel(12)),
        Cond("foff +10 Hz @30", ["--snr", HI_SNR, "--foff", 10]),
        Cond("foff -30 Hz @30", ["--snr", HI_SNR, "--foff", -30]),
        Cond("ppm +100 @30", ["--snr", HI_SNR, "--ppm", 100]),
        Cond("poor @30", ["--snr", HI_SNR, "--preset", "poor"]),
    ]
    m["awgn"] = [Cond(f"awgn D-LXIV{off:+d}", snr_rel(off)) for off in (-9, -6, -3, 0, 3, 6, 9, 12, 15, 20)]
    m["foff"] = [Cond(f"foff {f:+g} Hz @30", ["--snr", HI_SNR, "--foff", f])
                 for f in (-75, -40, -35, -30, -20, -10, -5, -2, 2, 5, 10, 20, 30, 35, 40, 75)]
    m["drift"] = [Cond(f"fdrift {r:+g} Hz/s @30", ["--snr", HI_SNR, "--fdrift", r]) for r in (0.5, 1, 2, 3.5, -3.5)]
    m["drift"] += [
        Cond("sweep D.6.5 from 0 @30", ["--snr", HI_SNR, "--sweep", "75:3.5:0"]),
        Cond("sweep D.6.5 from 0 @24", ["--snr", 24, "--sweep", "75:3.5:0"]),
    ]
    m["ppm"] = [Cond(f"ppm {p:+g} @30", ["--snr", HI_SNR, "--ppm", p])
                for p in (-500, -300, -200, -100, -50, -30, -20, -10, 10, 20, 30, 50, 100, 200, 300, 500)]
    m["ppm"] += [Cond(f"ppm-drift {r:+g}/s @30", ["--snr", HI_SNR, "--ppm-drift", r]) for r in (10, 100)]
    m["fading"] = [
        Cond("good @30", ["--snr", HI_SNR, "--preset", "good"]),
        Cond("moderate @30", ["--snr", HI_SNR, "--preset", "moderate"]),
        Cond("poor @30", ["--snr", HI_SNR, "--preset", "poor"]),
        Cond("poor @D-LXIV poor", snr_rel(0, DLXIV_POOR, ("--preset", "poor"))),
        Cond("poor @D-LXIV poor+10", snr_rel(10, DLXIV_POOR, ("--preset", "poor"))),
        Cond("poor noiseless", ["--preset", "poor"]),
        Cond("D.6.4 poor +75 Hz @30", ["--snr", HI_SNR, "--preset", "poor", "--foff", 75]),
        Cond("D.6.4 poor -75 Hz @30", ["--snr", HI_SNR, "--preset", "poor", "--foff", -75]),
    ]
    m["static"] = [Cond(f"{p} @50", ["--snr", 50, "--preset", p]) for p in ("static-wid2", "static-wid10", "static-wid12")]
    m["band"] = []
    for lo, hi in ((300, 2700), (300, 3000), (300, 3300), (200, 3000)):
        m["band"] += [Cond(f"bandpass {lo}-{hi} Hz", ["--bandpass", f"{lo}:{hi}"], seeds=1),
                      Cond(f"bandpass {lo}-{hi} Hz @30", ["--snr", HI_SNR, "--bandpass", f"{lo}:{hi}"])]
    return m


def with_bandpass(c, bp):
    """Condition c with m110d_chan --bandpass bp appended."""
    def f(row):
        a = c.args(row)
        return None if a is None else [*a, "--bandpass", bp]
    lo_hi = "-".join(bp.split(":")[:2])
    return Cond(f"{c.label} bp {lo_hi}", f, c.seeds)


PRESET_WORDS = {
    "good": "ITU Good fading channel",
    "moderate": "ITU Moderate fading channel",
    "poor": "ITU Poor fading channel",
    "static-wid2": "static multipath channel for waveform 2 (Table D-LXV)",
    "static-wid10": "static multipath channel for waveform 10 (Table D-LXV)",
    "static-wid12": "static multipath channel for waveform 12 (Table D-LXV)",
}


def cond_words(c, row):
    """The condition in plain words. `row` is one README row it applies to."""
    parts = []
    m = re.search(r"D-LXIV(?: poor)?([+-]\d+)?$", c.label)
    if "D-LXIV" in c.label:
        off = int(m.group(1)) if m and m.group(1) else 0
        poor = "D-LXIV poor" in c.label
        ref = "required SNR for the Poor channel" if poor else "required SNR for that waveform"
        parts.append("noise at the standard's " + ref if off == 0 else
                     f"noise {abs(off)} dB {'above' if off > 0 else 'below'} the standard's {ref}")
    ca = c.args(row) or []
    i = 0
    while i < len(ca):
        f = ca[i]
        v = ca[i + 1] if i + 1 < len(ca) else ""
        if f == "--snr" and not parts:
            parts.append(f"noise at {float(v):g} dB SNR")
        elif f == "--preset":
            parts.append(PRESET_WORDS.get(v, v))
        elif f == "--foff":
            parts.append(f"frequency offset {float(v):+g} Hz")
        elif f == "--fdrift":
            parts.append(f"frequency drift {float(v):+g} Hz per second")
        elif f == "--ppm":
            parts.append(f"sample clock error {float(v):+g} ppm")
        elif f == "--ppm-drift":
            parts.append(f"sample clock drift {float(v):+g} ppm per second")
        elif f == "--sweep":
            parts.append(f"frequency sweep {v} (D.6.5 triangle)")
        elif f == "--bandpass":
            lo, hi = v.split(":")[:2]
            parts.append(f"radio audio filter {lo}-{hi} Hz")
        i += 2
    if "--snr" in ca and "D-LXIV" not in c.label and not any(p.startswith("noise") for p in parts):
        parts.insert(0, f"noise at {float(ca[ca.index('--snr') + 1]):g} dB SNR")
    return ", ".join(parts) if parts else "clean, no impairment"


def run_one(chan, rx, src, dst, cargs, seed, keep):
    p = subprocess.run([chan, "--seed", str(seed), *cargs, src, dst], capture_output=True, text=True, timeout=300)
    if p.returncode:
        return {"_err": f"m110d_chan rc={p.returncode}: {p.stderr.strip()}"}
    try:
        kv = score.run_one(rx, dst)
    finally:
        if not keep:
            try:
                os.remove(dst)
            except OSError:
                pass
    return kv


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rx", required=True)
    ap.add_argument("--chan", required=True)
    ap.add_argument("--set", required=True, help="test-set directory holding README.md and the WAVs")
    ap.add_argument("--matrix", default="default")
    ap.add_argument("--files", default="1,13,53,29,41", help="README row numbers (default: WID 0, 3, 13, 7, 10 SHORT)")
    ap.add_argument("--seeds", type=int, default=3)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--work", help="scratch directory for impaired WAVs (default: a new one under $TMPDIR)")
    ap.add_argument("--keep", action="store_true", help="keep the impaired WAVs")
    ap.add_argument("--csv", help="also write one row per run to this CSV file")
    ap.add_argument("--proofs", metavar="DIR", help="also write DIR/t6.json, one item per condition, for a report page")
    ap.add_argument("--decoder-label", help="decoder label for --proofs (default: m110d_rx @<short commit>)")
    ap.add_argument("--proofs-note", help="note text for --proofs")
    ap.add_argument("--bandpass", metavar="LO:HI[:N]", help="add m110d_chan --bandpass to every condition")
    a = ap.parse_args()

    rows = {r["num"]: r for r in score.parse_readme(os.path.join(a.set, "README.md"))}
    try:
        nums = [int(x) for x in a.files.split(",") if x.strip()]
    except ValueError:
        print(f"--files: bad list {a.files!r}", file=sys.stderr)
        return 2
    missing = [n for n in nums if n not in rows]
    if missing:
        print(f"README.md has no rows {missing}", file=sys.stderr)
        return 2
    all_m = matrices()
    names = list(all_m) if a.matrix == "all" else [s.strip() for s in a.matrix.split(",")]
    for n in names:
        if n not in all_m:
            print(f"unknown matrix {n!r}; choose from {', '.join(all_m)} or all", file=sys.stderr)
            return 2
    conds, seen = [], set()
    for n in names:
        for c in all_m[n]:
            if c.label not in seen:
                seen.add(c.label)
                conds.append(with_bandpass(c, a.bandpass) if a.bandpass else c)

    work = a.work or tempfile.mkdtemp(prefix="m110d-chan-")
    os.makedirs(work, exist_ok=True)
    jobs = []
    for ci, c in enumerate(conds):
        for n in nums:
            cargs = c.args(rows[n])
            if cargs is None:
                continue
            for s in range(1, (c.seeds or a.seeds) + 1):
                dst = os.path.join(work, f"r{n:02d}_c{ci:02d}_s{s}.wav")
                jobs.append((ci, n, s, cargs, dst))

    res = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        futs = {ex.submit(run_one, a.chan, a.rx, os.path.join(a.set, rows[n]["file"]), dst, cargs, s, a.keep):
                (ci, n, s, tuple(cargs)) for ci, n, s, cargs, dst in jobs}
        for f in concurrent.futures.as_completed(futs):
            res[futs[f]] = f.result()
    if not a.keep and not a.work:
        shutil.rmtree(work, ignore_errors=True)

    # tally
    cell = {}
    errors = []
    records = []
    for (ci, n, s, cargs), kv in sorted(res.items(), key=lambda t: t[0][:3]):
        r = rows[n]
        if "_err" in kv:
            errors.append(f"row {n} {conds[ci].label} seed {s}: {kv['_err']}")
            ok = id_ok = False
        else:
            ok = score.unescape(kv.get("text", "")).startswith(r["text"])
            id_ok = (kv.get("wid") == str(r["wid"]) and kv.get("interleaver") == r["il"]
                     and kv.get("wid_checksum") == "ok")
        c = cell.setdefault((ci, n), [0, 0, 0])
        c[0] += ok
        c[1] += 1
        c[2] += ok and not id_ok
        records.append({"row": n, "wid": r["wid"], "il": r["il"], "condition": conds[ci].label,
                        "args": " ".join(cargs), "seed": s, "text_pass": int(ok), "id_pass": int(id_ok),
                        "acquired": kv.get("acquired", ""), "acq_metric": kv.get("acq_metric", ""),
                        "freq_offset_hz": kv.get("freq_offset_hz", ""), "evm_db": kv.get("evm_db", ""),
                        "probe_quality": kv.get("probe_quality", ""), "reencode_agree": kv.get("reencode_agree", "")})

    heads = [f"#{n} W{rows[n]['wid']} {rows[n]['il'][:2].upper()}" for n in nums]
    lw = max(len("D-LXIV AWGN / Poor dB"), *(len(c.label) for c in conds))
    cw = max(9, *(len(h) for h in heads))
    print(f"{'condition':<{lw}}  " + "  ".join(f"{h:>{cw}}" for h in heads))
    tline = f"{'D-LXIV AWGN / Poor dB':<{lw}}  " + "  ".join(
        f"{str(DLXIV_AWGN[rows[n]['wid']]) + ' / ' + str(DLXIV_POOR[rows[n]['wid']] if DLXIV_POOR[rows[n]['wid']] is not None else '-'):>{cw}}"
        for n in nums)
    print(tline)
    print("-" * len(tline))
    for ci, c in enumerate(conds):
        cells = []
        for n in nums:
            v = cell.get((ci, n))
            if v is None:
                cells.append(f"{'n/a':>{cw}}")
                continue
            txt = f"{v[0]}/{v[1]}"
            args = c.args(rows[n])
            if "--snr" in args and "D-LXIV" in c.label:
                txt = f"{args[args.index('--snr') + 1]}dB {txt}"
            cells.append(f"{txt + ('*' if v[2] else ''):>{cw}}")
        print(f"{c.label:<{lw}}  " + "  ".join(cells))
    print("-" * len(tline))
    tot_ok = sum(v[0] for v in cell.values())
    tot = sum(v[1] for v in cell.values())
    print(f"runs: {tot}, text PASS: {tot_ok}")
    for e in errors:
        print("ERROR " + e, file=sys.stderr)
    if a.csv:
        with open(a.csv, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(records[0]) if records else ["row"])
            w.writeheader()
            w.writerows(records)
    if a.proofs:
        items = []
        for ci, c in enumerate(conds):
            cells = [(n, cell[(ci, n)]) for n in nums if (ci, n) in cell]
            if not cells:
                continue
            ok = sum(v[0] for _, v in cells)
            tot = sum(v[1] for _, v in cells)
            seeds = max(v[1] for _, v in cells)
            snrs = []
            for n, _ in cells:
                ca = c.args(rows[n])
                if "--snr" in ca:
                    snrs.append(float(ca[ca.index("--snr") + 1]))
            if not snrs:
                snr = "no added noise"
            elif min(snrs) == max(snrs):
                snr = f"SNR {min(snrs):g} dB in 3 kHz"
            else:
                snr = f"SNR {min(snrs):g} to {max(snrs):g} dB in 3 kHz, set per waveform"
            wids = ", ".join(str(rows[n]["wid"]) for n, _ in cells)
            seed_txt = "seed 1" if seeds == 1 else f"seeds 1 to {seeds}"
            items.append({
                "name": c.label,
                "mode": cond_words(c, rows[cells[0][0]]),
                "status": "pass" if ok == tot else "fail" if ok == 0 else "partial",
                "decoded": f"{ok} of {tot} runs decoded the message",
                "detail": f"{len(cells)} files (waveforms {wids}), {seed_txt}, {snr}",
            })
        doc = {"id": "T6", "order": 65, "title": "110D receiver on a simulated HF channel",
               "what": "The same recordings passed through a standard HF channel simulator "
                       "(noise, frequency offset, clock error, fading) before decoding.",
               "decoder": a.decoder_label or score.default_decoder_label(),
               "note": a.proofs_note or "Each line is one channel condition, summed over the files and seeds listed; "
                                        "a run passes when the decoded text starts with the sent message.",
               "items": items}
        prc = score.write_proofs(a.proofs, "t6.json", doc)
        if prc:
            return prc
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
