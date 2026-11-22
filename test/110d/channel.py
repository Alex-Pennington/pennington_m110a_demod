#!/usr/bin/env python3
"""Channel matrix for m110d_rx: impair test-set files with m110d_chan, decode, score.

Usage:
  python3 -I test/110d/channel.py --rx build-110d/bin/m110d_rx --chan build-110d/bin/m110d_chan \
      --set <test-set dir> [--matrix default] [--files 1,13,53,29,41] [--seeds N] [--jobs 2]

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

SNR is in 3 kHz (m110d_chan --noise-bw default) against the burst's average power (E.5.5).
Impaired WAVs go to a scratch directory under $TMPDIR (or --work) and are deleted after each
decode unless --keep is given.
"""
import argparse
import concurrent.futures
import csv
import importlib.util
import os
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
    m["awgn"] = [Cond(f"awgn D-LXIV{off:+d}", snr_rel(off)) for off in (-3, 0, 3, 6, 9, 12, 15, 20)]
    m["foff"] = [Cond(f"foff {f:+g} Hz @30", ["--snr", HI_SNR, "--foff", f])
                 for f in (-75, -40, -30, -20, -10, -5, -2, 2, 5, 10, 20, 30, 40, 75)]
    m["drift"] = [Cond(f"fdrift {r:+g} Hz/s @30", ["--snr", HI_SNR, "--fdrift", r]) for r in (0.5, 1, 2, 3.5, -3.5)]
    m["drift"] += [
        Cond("sweep D.6.5 from 0 @30", ["--snr", HI_SNR, "--sweep", "75:3.5:0"]),
        Cond("sweep D.6.5 from 0 @24", ["--snr", 24, "--sweep", "75:3.5:0"]),
    ]
    m["ppm"] = [Cond(f"ppm {p:+g} @30", ["--snr", HI_SNR, "--ppm", p])
                for p in (-500, -300, -100, -30, -10, 10, 30, 100, 300, 500)]
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
    return m


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
                conds.append(c)

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
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
