#!/usr/bin/env python3
"""Score m110d_rx against the MIL-STD-188-110D Appendix D interop test set.

Usage:
  python3 -I test/110d/score.py --rx build-110d/bin/m110d_rx --set <test-set dir> [--jobs N]

The expected WID, interleaver, EOM setting and text are read from README.md in the test-set
directory (its "Files" table); nothing from the test set is copied into this repo. The text
column there is the first ~100 characters of each message, so the text check is a prefix
match: decoded text must start with it. For the no-EOM file the decoded text runs on into
the zero padding of the last interleaver block; the prefix match passes that by design.

Text is scored for the waveforms the receiver decodes: WID 1-5 (BPSK) and WID 6-13 (QPSK,
8PSK, 16/32/64/256QAM). For WID 0 the receiver reports WID and interleaver only, and the text
column reads "n/a".

Exit status: 0 when every file reports the expected WID and interleaver and every WID 1-13
file passes the text check, 1 otherwise.
"""
import argparse
import concurrent.futures
import os
import re
import subprocess
import sys

IL_NAMES = {"ultra-short", "short", "medium", "long"}
TEXT_WIDS = range(1, 14)  # waveforms whose payload text is scored


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


def run_one(rx, path):
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
    a = ap.parse_args()

    rows = parse_readme(os.path.join(a.set, "README.md"))
    if len(rows) != 60:
        print(f"README.md: expected 60 rows, parsed {len(rows)}", file=sys.stderr)
        return 2
    for r in rows:
        if r["il"] not in IL_NAMES:
            print(f"README.md row {r['num']}: unknown interleaver {r['il']!r}", file=sys.stderr)
            return 2

    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        futs = {r["num"]: ex.submit(run_one, a.rx, os.path.join(a.set, r["file"])) for r in rows}
        res = {n: f.result() for n, f in futs.items()}

    hdr = f"{'#':>2}  {'exp WID':>7} {'exp IL':<11}  {'got WID':>7} {'got IL':<11}  {'ID':<4}  {'text':<4}  {'EOM exp/got':<11}  {'PN':<10}  note"
    print(hdr)
    print("-" * len(hdr))
    n_id = n_text = n_text_scope = n_text_eom = 0
    n_text_by, n_scope_by = [0, 0], [0, 0]  # [WID 1-5, WID 6-13]
    for r in rows:
        k = res[r["num"]]
        gw = k.get("wid", "-")
        gi = k.get("interleaver", "-")
        id_ok = gw == str(r["wid"]) and gi == r["il"] and k.get("wid_checksum") == "ok"
        n_id += id_ok
        if r["wid"] in TEXT_WIDS:
            n_text_scope += 1
            n_scope_by[r["wid"] >= 6] += 1
            got = unescape(k.get("text", ""))
            t_ok = got.startswith(r["text"])
            n_text += t_ok
            n_text_by[r["wid"] >= 6] += t_ok
            tcol = "PASS" if t_ok else "FAIL"
            eom_got = k.get("eom", "-")
            n_text_eom += t_ok and eom_got == {"on": "1", "off": "0"}.get(r["eom"])
        else:
            tcol = "n/a"
            eom_got = "-"
        note = r["variant"] or ""
        if k.get("payload_note") and r["wid"] in TEXT_WIDS:
            note = (note + " " + k["payload_note"]).strip()
        if k.get("_rc"):
            note = (note + f" rc={k['_rc']}").strip()
        print(f"{r['num']:>2}  {r['wid']:>7} {r['il']:<11}  {gw:>7} {gi:<11}  {'PASS' if id_ok else 'FAIL':<4}  "
              f"{tcol:<4}  {r['eom'] + '/' + eom_got:<11}  {k.get('pn', '-'):<10}  {note}")
    print("-" * len(hdr))
    print(f"WID+interleaver correct: {n_id}/60")
    print(f"text PASS (WID 1-5):     {n_text_by[0]}/{n_scope_by[0]}")
    print(f"text PASS (WID 6-13):    {n_text_by[1]}/{n_scope_by[1]}")
    print(f"text PASS, EOM as expected (WID 1-13): {n_text_eom}/{n_text_scope}")
    return 0 if n_id == 60 and n_text == n_text_scope else 1


if __name__ == "__main__":
    sys.exit(main())
