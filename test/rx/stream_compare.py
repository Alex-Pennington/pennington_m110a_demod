"""Streaming receiver against whole-burst decode() over generated and recorded 110A bursts.

Each case is one audio file, decoded both ways by stream_vs_whole (ModemRX::decode() and
start / push_samples / finish, 10 ms a push): the bytes, the verdict, the blocks the stream
reported, and each decode's CPU time. docs/m110a-streaming-rx.md has the results.

Sets
  awgn   the quality gate's AWGN set (docs/m110a-quality-gate.md): 2400S/L at 7, 9, 11 dB and
         no noise; 1200S/L at 3, 5, 7; 600S/L at 0, 2, 4; 4800S at 14, 16, 18; 300S/L and
         150S/L at -6, -4, -2, 0; and 75S/75L at -8, -4, 0 and no noise. SNR in 3 kHz,
         white Gaussian noise; 200-byte payloads; seeds 1-4.
  ccir   hfchansim Watterson moderate and poor at 5 and 10 dB, seeds 1-5, modes 2400S, 1200S,
         600S, 300S, 2400L, 1200L, 600L, 300L, 150L; 200-byte payloads.
  files  the audio files given with --files (48 kHz: .wav, .f32, raw s16le).

Usage: python3 -I -B test/rx/stream_compare.py --bin BUILD/bin --work DIR --sets awgn,ccir
           [--files F1,F2] [--jobs N] [--seeds N]
Prints one row per case and a summary; exit 0 when every case gives the same bytes and
verdict both ways. Needs numpy.
"""
import argparse
import concurrent.futures as cf
import os
import subprocess
import sys

try:
    import numpy as np
except ImportError:
    print("SKIP: numpy is not installed")
    sys.exit(77)

FS = 48000
TX_RMS = 7000.0 / 32768.0


def text(n, seed):
    s = (f"DE AAM402 STREAM {seed} " + "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 0123456789 ") * (n // 40 + 2)
    return s[:n].encode()


def read_wav(path):
    b = open(path, "rb").read()
    return np.frombuffer(b[44:], dtype="<i2").astype(np.float64) / 32768.0


def write_s16(path, x, wav=True):
    pcm = np.clip(np.round(x * 32768.0), -32768, 32767).astype("<i2").tobytes()
    with open(path, "wb") as f:
        if wav:
            n = len(pcm)
            f.write(b"RIFF" + (36 + n).to_bytes(4, "little") + b"WAVEfmt " + (16).to_bytes(4, "little"))
            f.write((1).to_bytes(2, "little") + (1).to_bytes(2, "little") + FS.to_bytes(4, "little"))
            f.write((FS * 2).to_bytes(4, "little") + (2).to_bytes(2, "little") + (16).to_bytes(2, "little"))
            f.write(b"data" + n.to_bytes(4, "little"))
        f.write(pcm)


def make_tx(a, mode, payload, out):
    pb = out + ".bin"
    open(pb, "wb").write(payload)
    subprocess.run([os.path.join(a.bin, "modem_tx_wav"), "--mode", mode, "--lead", "0.5", "--tail", "1.0", pb, out],
                   check=True, capture_output=True)
    return pb


def awgn_case(a, mode, snr, seed):
    tag = f"awgn_{mode}_{'clean' if snr is None else f'{snr:g}'}_{seed}"
    wav = os.path.join(a.work, tag + ".wav")
    pb = wav + ".tx.wav.bin"
    if not os.path.exists(wav):
        tx = wav + ".tx.wav"
        pb = make_tx(a, mode, text(200, seed), tx)
        x = read_wav(tx)
        os.remove(tx)
        if snr is not None:
            # SNR in 3 kHz: the noise power over the 24 kHz band is 8 times that in 3 kHz
            rng = np.random.default_rng(1000 * seed + int(10 * (snr + 20)) + 7 * len(mode))
            sigma = np.sqrt(TX_RMS ** 2 / 10 ** (snr / 10) * 8.0)
            x = x + rng.standard_normal(len(x)) * sigma
        write_s16(wav, x)
    return tag, wav, pb


def ccir_case(a, mode, preset, snr, seed):
    tag = f"ccir_{mode}_{preset}_{snr:g}_{seed}"
    wav = os.path.join(a.work, tag + ".wav")
    pb = wav + ".tx.wav.bin"
    if not os.path.exists(wav):
        tx = wav + ".tx.wav"
        pb = make_tx(a, mode, text(200, seed), tx)
        x = read_wav(tx)
        os.remove(tx)
        src, dst = wav + ".in.pcm", wav + ".out.pcm"
        write_s16(src, x, wav=False)
        subprocess.run([os.path.join(a.bin, "hfchansim"), src, dst, "--preset", preset, "--snr", str(snr),
                        "--seed", str(seed)], check=True, capture_output=True, cwd=a.work)
        y = np.frombuffer(open(dst, "rb").read(), dtype="<i2").astype(np.float64) / 32768.0
        os.remove(src)
        os.remove(dst)
        write_s16(wav, y)
    return tag, wav, pb


def run(a, case):
    tag, path, pb = case
    cmd = [os.path.join(a.bin, "stream_vs_whole")]
    if pb and os.path.exists(pb):
        cmd += ["--expect", pb]
    p = subprocess.run(cmd + [path], capture_output=True, text=True)
    line = next((l for l in p.stdout.splitlines() if l.startswith("file=")), "")
    kv = dict(f.split("=", 1) for f in line.split() if "=" in f)
    return tag, kv, p.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--sets", default="awgn")
    ap.add_argument("--files", default="")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--seeds", type=int, default=0, help="seeds per case (default: 4 AWGN, 5 CCIR)")
    a = ap.parse_args()
    a.bin, a.work = os.path.abspath(a.bin), os.path.abspath(a.work)
    os.makedirs(a.work, exist_ok=True)
    sets = a.sets.split(",")
    gens = []
    if "awgn" in sets:
        grid = [("2400S", [7, 9, 11, None]), ("2400L", [7, 9, 11, None]), ("1200S", [3, 5, 7]), ("1200L", [3, 5, 7]),
                ("600S", [0, 2, 4]), ("600L", [0, 2, 4]), ("4800S", [14, 16, 18]),
                ("300S", [-6, -4, -2, 0]), ("300L", [-6, -4, -2, 0]), ("150S", [-6, -4, -2, 0]),
                ("150L", [-6, -4, -2, 0]), ("75S", [-8, -4, 0, None]), ("75L", [-8, -4, 0, None])]
        for mode, snrs in grid:
            for snr in snrs:
                for seed in range(1, (a.seeds or 4) + 1):
                    gens.append(("awgn", mode, snr, seed))
    if "ccir" in sets:
        for mode in ["2400S", "1200S", "600S", "300S", "2400L", "1200L", "600L", "300L", "150L"]:
            for preset in ["moderate", "poor"]:
                for snr in [5, 10]:
                    for seed in range(1, (a.seeds or 5) + 1):
                        gens.append(("ccir", mode, preset, snr, seed))

    def gen(g):
        return awgn_case(a, *g[1:]) if g[0] == "awgn" else ccir_case(a, *g[1:])

    with cf.ThreadPoolExecutor(a.jobs) as ex:
        cases = list(ex.map(gen, gens))
    cases += [(os.path.basename(f), f, None) for f in a.files.split(",") if f]

    rows = []
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        for tag, kv, out in ex.map(lambda c: run(a, c), cases):
            rows.append((tag, kv))
            print(f"{tag:32s} same={kv.get('same', '?')} whole_ok={kv.get('whole_ok', '?')} "
                  f"stream_ok={kv.get('stream_ok', '?')} bytes={kv.get('whole_bytes', '?')}/{kv.get('stream_bytes', '?')} "
                  f"exact={kv.get('whole_exact', '?')}/{kv.get('stream_exact', '?')}/{kv.get('blocks_exact', '?')} "
                  f"blocks={kv.get('blocks', '?')} blocks_same={kv.get('blocks_same', '?')} "
                  f"blocks_pass={kv.get('blocks_pass', '?')} "
                  f"cpu={kv.get('cpu_whole', '?')}/{kv.get('cpu_stream', '?')} in={kv.get('input_s', '?')}", flush=True)

    def n(pred):
        return sum(1 for _, kv in rows if pred(kv))
    same = n(lambda kv: kv.get("same") == "1")
    print(f"\n{len(rows)} cases: same bytes and verdict {same}, different {len(rows) - same}")
    print(f"  whole exact {n(lambda kv: kv.get('whole_exact') == '1')}, stream exact {n(lambda kv: kv.get('stream_exact') == '1')}, "
          f"blocks concatenated exact {n(lambda kv: kv.get('blocks_exact') == '1')}")
    print(f"  success: whole {n(lambda kv: kv.get('whole_ok') == '1')}, stream {n(lambda kv: kv.get('stream_ok') == '1')}")
    print(f"  blocks equal to the stream's result bytes {n(lambda kv: kv.get('blocks_same') == '1')}")
    print(f"  every block passing the gate when the whole decode succeeds: "
          f"{n(lambda kv: kv.get('whole_ok') == '1' and kv.get('blocks_pass') == '1')} of "
          f"{n(lambda kv: kv.get('whole_ok') == '1')}; a block passing gate while the whole decode fails: "
          f"{n(lambda kv: kv.get('whole_ok') == '0' and kv.get('blocks_pass') == '1' and int(kv.get('blocks', 0)) > 0)}")
    cw = sum(float(kv.get("cpu_whole", 0)) for _, kv in rows)
    cs = sum(float(kv.get("cpu_stream", 0)) for _, kv in rows)
    ins = sum(float(kv.get("input_s", 0)) for _, kv in rows)
    print(f"  CPU: whole {cw:.1f} s, stream {cs:.1f} s, over {ins:.1f} s of input")
    for tag, kv in rows:
        if kv.get("same") != "1":
            print(f"  DIFFERENT {tag}: whole ok={kv.get('whole_ok')} bytes={kv.get('whole_bytes')} exact={kv.get('whole_exact')} "
                  f"q={kv.get('whole_q')} bqmin={kv.get('whole_bqmin')}; stream ok={kv.get('stream_ok')} "
                  f"bytes={kv.get('stream_bytes')} exact={kv.get('stream_exact')} q={kv.get('stream_q')} "
                  f"bqmin={kv.get('stream_bqmin')}")
    return 0 if same == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
