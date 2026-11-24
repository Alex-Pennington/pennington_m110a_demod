"""75 bps BER sweep: runs m75_ber (test/m75/ber.cpp) over seeds in parallel and totals it.

Each case is LABEL|M75_BER_ARGS. The seeds are split over --jobs processes. Per case it prints
messages, bits, bit errors, BER, its Clopper-Pearson 95% interval, exact messages (success, EOM,
bytes equal), sync (preamble found, mode named) and success.

The interval treats bits as independent. Errors after a Viterbi decoder come in bursts and
whole lost messages, so the interval is narrower than the truth when errors were seen; with no
errors, the upper bound (about 3 / bits) also assumes independent bits. The per-message counts
are printed beside it so the reader can judge.

Usage: python3 -I -B test/m75/ber_sweep.py --ber M75_BER [--jobs 4] [--seeds 1:24]
           --case 'LABEL|--mode 75L --snr -9' [--case ...]
           [--require 'LABEL:max_ber=1e-3'] [--require 'LABEL:min_sync=3'] [--require 'LABEL:min_exact=3']
--require makes the run fail unless the case meets it (the ctest gate). A false success (success=1
with bit errors) always fails the run.
"""
import argparse
import concurrent.futures as cf
import re
import shlex
import subprocess
import sys

from scipy.stats import beta

KV = re.compile(r'(\w+)=("[^"]*"|\S+)')


def cp(k, n, conf=0.95):
    a = 1 - conf
    lo = 0.0 if k == 0 else beta.ppf(a / 2, k, n - k + 1)
    hi = 1.0 if k == n else beta.ppf(1 - a / 2, k + 1, n - k)
    return lo, hi


def run(tool, args, s0, s1):
    p = subprocess.run([tool] + args + ["--seeds", f"{s0}:{s1}"], capture_output=True, text=True, timeout=36000)
    if p.returncode != 0:
        raise RuntimeError(f"m75_ber {' '.join(args)}: {p.stderr.strip()}")
    rows = []
    for line in p.stdout.splitlines():
        if line.startswith("seed="):
            rows.append({k: v.strip('"') for k, v in KV.findall(line)})
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ber", required=True, help="m75_ber")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--seeds", default="1:24")
    ap.add_argument("--case", action="append", required=True)
    ap.add_argument("--require", action="append", default=[])
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    s0, s1 = (int(v) for v in a.seeds.split(":"))
    cases = []
    for c in a.case:
        label, args = c.split("|", 1)
        cases.append((label, shlex.split(args)))
    # chunks of seeds per process
    nseed = s1 - s0 + 1
    per = max(1, -(-nseed // a.jobs)) if len(cases) == 1 else max(1, min(4, nseed))
    work = []
    for label, args in cases:
        for c0 in range(s0, s1 + 1, per):
            work.append((label, args, c0, min(s1, c0 + per - 1)))
    res = {label: [] for label, _ in cases}
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        futs = {ex.submit(run, a.ber, args, c0, c1): label for label, args, c0, c1 in work}
        for f in cf.as_completed(futs):
            res[futs[f]].extend(f.result())
    print(f"seeds {s0}:{s1}; BER interval: Clopper-Pearson 95%, bits taken as independent")
    print(f"{'case':34} {'msgs':>5} {'bits':>8} {'err':>6} {'BER':>9} {'95% lo':>9} {'95% hi':>9} "
          f"{'exact':>7} {'sync':>7} {'success':>7} {'false_ok':>8}")
    table, ok, fps = {}, True, 0
    for label, args in cases:
        rows = sorted(res[label], key=lambda r: int(r["seed"]))
        n = len(rows)
        bits = sum(int(r["bits"]) for r in rows)
        err = sum(int(r["err"]) for r in rows)
        ex_ = sum(int(r["exact"]) for r in rows)
        sy = sum(int(r["sync"]) for r in rows)
        su = sum(int(r["success"]) for r in rows)
        fp = sum(int(r["success"]) and int(r["err"]) > 0 for r in rows)
        fps += fp
        lo, hi = cp(err, bits)
        table[label] = dict(ber=err / max(1, bits), hi=hi, exact=ex_, sync=sy, n=n)
        print(f"{label:34} {n:>5} {bits:>8} {err:>6} {err / max(1, bits):9.2e} {lo:9.2e} {hi:9.2e} "
              f"{ex_:>3}/{n:<3} {sy:>3}/{n:<3} {su:>3}/{n:<3} {fp:>4}", flush=True)
        if a.verbose:
            for r in rows:
                if int(r["err"]) or not int(r["exact"]):
                    print(f"    seed {r['seed']}: err={r['err']} success={r['success']} eom={r['eom']} "
                          f"sync={r['sync']} mode={r['mode']} q={r['quality_db']} {r.get('err_msg', '')}")
    for req in a.require:
        label, cond = req.split(":", 1)
        key, val = cond.split("=")
        t = table[label]
        if key == "max_ber":
            good = t["ber"] <= float(val)
        elif key == "max_ber_hi":
            good = t["hi"] <= float(val)
        elif key == "min_sync":
            good = t["sync"] >= int(val)
        elif key == "min_exact":
            good = t["exact"] >= int(val)
        else:
            raise SystemExit(f"unknown requirement {key}")
        ok &= good
        print(f"require {label} {key} {val}: {'PASS' if good else 'FAIL'}")
    print(f"false successes: {fps}")
    return 0 if ok and fps == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
