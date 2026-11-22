"""Byte-exact loopback for the Harris-mode transmitter (ctest harris_tx_loopback).

payload -> m110a_tx -> 48 kHz WAV -> I/Q (modem.py's route) -> m110a_rx (AUTO) ->
the payload bytes again, with success=1 and eom=1, the mode named from D1/D2, and
the number of symbols a 110C message of that length takes: the 3-frame preamble plus
whole 1440-symbol interleaver blocks holding payload, EOM (32 bits) and flush (144).

Cases per mode (600H, 1200H), from a fixed seed:
  - framed: random whole vocoder units (60 or 81 bits) wrapped by --frame as the
    RT-1694D frames them (54-bit header, units, 108-bit trailer, zeros to 16 bits)
  - bytes: random bytes through --bytes, one length that ends on an interleaver block
    and one that does not

Usage: python3 -I -B test/harris/tx_loopback.py --rx RX --tx TX --work DIR
"""
import argparse
import math
import os
import random
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modem  # noqa: E402

INFO_PER_BLOCK = {"600H": 360, "1200H": 720}
UNIT = {"600H": 60, "1200H": 81}
D = {"600H": ("5", "7"), "1200H": ("5", "6")}
# docs/harris-600-format.md, measured in the nine 600_x and 1200_x streams
HEADER = "011111011010100110011010100100000100111110000011110010"
W1 = "101001010010100010100000000100010101000101110011111101"
W2 = "101001010010100010100000000100010101000101110011111100"


def run_case(a, mode, name, args, payload_bits, units=None):
    wav = os.path.join(a.work, f"{mode}_{name}.wav")
    sym = os.path.join(a.work, f"{mode}_{name}.sym")
    sent = os.path.join(a.work, f"{mode}_{name}.sent")
    p = subprocess.run([a.tx, "--mode", mode] + args + ["--lead", "0.5", "--tail", "0.5",
                        "--symbols", sym, "--payload-out", sent, wav],
                       capture_output=True, text=True)
    if p.returncode != 0:
        print(f"{mode} {name}: m110a_tx failed: {p.stderr.strip()}")
        return False
    bits = open(sent).read().strip()
    if payload_bits is not None and bits != payload_bits:
        print(f"{mode} {name}: m110a_tx sent other bits than given")
        return False
    if units is not None:
        framed = HEADER + units + W1 + W2
        framed += "0" * (-len(framed) % 16)
        if bits != framed:
            print(f"{mode} {name}: --frame did not give header + units + W, W' + zeros to 16 bits")
            return False
    want = bytes(sum(int(bits[i + k]) << k for k in range(8)) for i in range(0, len(bits), 8))
    nsym = os.path.getsize(sym)
    want_sym = 1440 * (1 + math.ceil((len(bits) + 32 + 144) / INFO_PER_BLOCK[mode]))
    iq = os.path.join(a.work, f"{mode}_{name}.cf32")
    modem.to_iq(wav, iq)
    msgs = modem.run_rx(a.rx, iq)
    os.remove(iq)
    m = msgs[0] if len(msgs) == 1 else {}
    got = bytes.fromhex(m.get("hex", ""))
    ok = (len(msgs) == 1 and m.get("success") == "1" and m.get("eom") == "1" and m.get("mode") == mode
          and (m.get("d1"), m.get("d2")) == D[mode] and got == want and len(bits) % 8 == 0
          and nsym == want_sym)
    print(f"{mode} {name}: {len(bits)} bits, {nsym} symbols (want {want_sym}); m110a_rx: "
          f"messages={len(msgs)} mode={m.get('mode')} d1,d2={m.get('d1')},{m.get('d2')} "
          f"success={m.get('success')} eom={m.get('eom')} bytes {len(got)}/{len(want)} "
          f"{'identical' if got == want else 'DIFFERENT'} -> {'PASS' if ok else 'FAIL'}")
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rx", required=True)
    ap.add_argument("--tx", required=True)
    ap.add_argument("--work", required=True)
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    rng = random.Random(110)
    ok = True
    for mode in ("600H", "1200H"):
        units = "".join(rng.choice("01") for _ in range(UNIT[mode] * 23))
        f = os.path.join(a.work, f"{mode}_units.bits")
        with open(f, "w") as fh:
            fh.write(units + "\n")
        ok &= run_case(a, mode, "framed", ["--bits", f, "--frame"], None, units)
        # 4 blocks exactly: payload + EOM + flush = 4 * info bits per block
        for name, nbytes in (("bytes_block_end", (4 * INFO_PER_BLOCK[mode] - 176) // 8),
                             ("bytes_mid_block", 301)):
            data = bytes(rng.randrange(256) for _ in range(nbytes))
            f = os.path.join(a.work, f"{mode}_{name}.bin")
            with open(f, "wb") as fh:
                fh.write(data)
            bits = "".join(str((c >> k) & 1) for c in data for k in range(8))
            ok &= run_case(a, mode, name, ["--bytes", f], bits)
    print("harris_tx_loopback: all cases exact" if ok else "harris_tx_loopback: FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
