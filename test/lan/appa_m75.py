"""m110_lan at 75 bps (75S, 75L), and the TCP chunk framing of every mode (ctest appa_lan_75).

Appendix A gives the DTE no field to choose the waveform: rate and blocking factor are "user
configured" and go modem to DTE in Transmit Setup (A.5.1.2.1 items 1-2, A.5.1.1.9). So the
modem is configured with --mode 75S or --mode 75L, and the DTE reads the rate from Transmit
Setup.

Phases
  tx75     per mode: Transmit Setup reads 75 bps, blocking factor 45 (75S) or 360 (75L); a DTE
           sends a "DE AAM402 ..." message over the TCP data socket; the transmit WAV decodes
           exact, with its EOM, through ModemRX (m75_wav_rx) and through m110a_rx (I/Q)
  rx75     per mode: that WAV fed back into the modem's receive input in real time, 10 ms at a
           time, clean and at -6 dB (white noise in 300-3300 Hz, SNR over that band): no carrier
           on the noise before it; CARRIER DETECTED with rate 75 and the blocking factor, timed
           from the end of the preamble; the same bytes to the DTE, one packet per blocking
           factor under the chunk rule below; NO CARRIER; no receive audio dropped. Then, with
           the modem configured for 600S (75 bps finder off), a clean 75S burst still received
  framing  one packet per blocking factor of bf bits, carrying bytes floor(bf*i/8) to
           floor(bf*(i+1)/8) (docs/110d-appa-lan.md, "Blocking factor"). 150S (90 bits) and
           300S (180 bits) both ways exact with that pacing, delivery rate measured against the
           air rate; 600S, 1200S, 2400S, 4800S and 600L packet for packet as the earlier rule
           (ceil(bf/8) bytes per packet) gave them

Usage: python3 -I -B test/lan/appa_m75.py --server BUILD/bin/m110_lan --rx BUILD/bin/m110a_rx
           --wav-rx BUILD/bin/m75_wav_rx --work DIR [--phases tx75,rx75,framing]
Exit 77 (skipped) when numpy is not installed.
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import appa_dte as A  # noqa: E402  packets, CRC, handshake, server process
import appa_rx_stream as R  # noqa: E402  real-time feeder, collector, band noise (exits 77 without numpy)

np = R.np
FS = R.FS
LEAD_S = R.LEAD_S                 # transmit lead silence in each WAV
BF = {"75S": 45, "75L": 360, "150S": 90, "300S": 180, "600S": 360, "600L": 2880,
      "1200S": 720, "2400S": 1440, "4800S": 2880}
RATE = {m: int(m[:-1]) for m in BF}
PREAMBLE_S = {"75S": 0.6, "75L": 4.8}   # 3 or 24 frames of 0.2 s (MIL-STD-188-110 Table XI)
CD_LIMIT_S = 2.7                  # CARRIER DETECTED within this of the preamble's last sample


def msg_for(mode):
    return f"DE AAM402 {mode} OVER APPENDIX A THE QUICK BROWN FOX 0123456789".encode()


def chunks_new(n, bf):
    """The chunk rule: packet i carries bytes floor(bf*i/8) .. floor(bf*(i+1)/8)."""
    npk = max(1, (n * 8 + bf - 1) // bf)
    cuts = [min(n, bf * i // 8) for i in range(npk)] + [n]
    return [cuts[i + 1] - cuts[i] for i in range(npk)]


def chunks_old(n, bf):
    """The rule before: ceil(bf/8) bytes per packet."""
    c = (bf + 7) // 8
    return [min(c, n - o) for o in range(0, n, c)] or [0]


def transmit_setup(pkts):
    for p in pkts:
        if p is not None and A.payload(p)[:1] == b"\x09":
            return struct.unpack(">II", A.payload(p)[1:9])
    return None


def tx_wav(a, mode, tag):
    """Configure the modem for mode, connect a DTE, send the message. Returns (wav path, setup)."""
    srv = A.Server(a.server, a.work, tag, ["--mode", mode, "--rx-fast"])
    d = A.TcpDte(srv.tcp)
    try:
        setup = transmit_setup(A.handshake(d))
        A.send_message(d, msg_for(mode))
        p, seen = A.wait_tx_state(d, 1, 120)
        ok = p is not None and A.wait_file(srv.wav_n(1), 60)
        return (srv.wav_n(1) if ok else None), setup
    finally:
        d.close()
        srv.stop()


def to_iq(wav, out, rate=96000, offset=10000.0):
    """Audio to I/Q for m110a_rx: analytic signal, 48 -> 96 kHz, up by `offset` (as
    test/harris/modem.py does, without scipy)."""
    x = np.frombuffer(open(wav, "rb").read()[44:], dtype="<i2").astype(np.float64) / 32768.0
    n = len(x) + (len(x) & 1)
    X = np.fft.fft(x, n)
    h = np.zeros(n)
    h[0] = 1
    h[1:n // 2] = 2
    h[n // 2] = 1
    Z = X * h
    U = np.zeros(2 * n, dtype=complex)  # zero-pad the spectrum: 2x the rate
    U[:n // 2] = Z[:n // 2]
    U[-(n // 2):] = Z[n // 2:]
    z = np.fft.ifft(U) * 2
    z *= np.exp(2j * np.pi * offset * np.arange(len(z)) / rate)
    z.astype(np.complex64).tofile(out)
    return rate, offset


def rx_lines(text):
    msgs = []
    for line in text.splitlines():
        if line.startswith("msg="):
            msgs.append({k: v.strip('"') for k, v in re.findall(r'(\w+)=("[^"]*"|\S+)', line)})
        elif msgs and line.startswith("  hex="):
            msgs[-1]["hex"] = line.split("=", 1)[1].strip()
    return msgs


# ======================================================================== tx75
def phase_tx75(a, wavs):
    for mode in ("75S", "75L"):
        wav, setup = tx_wav(a, mode, f"m75tx_{mode}")
        A.check(setup == (75, BF[mode]), "A.5.1.2.1 item 2, A.5.1.1.9, Figure A-7",
                f"{mode}: Transmit Setup gives Tx rate 75 bps, Tx blocking factor {BF[mode]} bits ({setup})")
        A.check(wav is not None, "A.5.1.2.5", f"{mode}: the DTE's message is transmitted to a WAV")
        if wav is None:
            continue
        wavs[mode] = wav
        msg = msg_for(mode)
        p = subprocess.run([a.wav_rx, wav], capture_output=True, text=True, timeout=600)
        m = rx_lines(p.stdout)
        got = bytes.fromhex(m[0].get("hex", "")) if m else b""
        A.check(len(m) == 1 and m[0].get("mode") == mode and m[0].get("success") == "1" and m[0].get("eom") == "1"
                and got == msg, "brief: transmit audio decodes",
                f"{mode}: the transmit WAV through ModemRX (m75_wav_rx, AUTO): "
                f"{[(x.get('mode'), x.get('success'), x.get('eom'), x.get('bytes')) for x in m]}, exact={got == msg}")
        iq = os.path.join(a.work, f"m75tx_{mode}.cf32")
        rate, off = to_iq(wav, iq)
        p = subprocess.run([a.rx, "--format", "cf32", "--rate", str(rate), "--offset", str(int(off)), "--dial", "0",
                            "--expect", msg.decode(), iq], capture_output=True, text=True, timeout=600)
        m = rx_lines(p.stdout)
        A.check(p.returncode == 0, "brief: transmit audio decodes",
                f"{mode}: the transmit WAV as I/Q through m110a_rx (AUTO) decodes exact with EOM "
                f"(exit {p.returncode}; {[(x.get('mode'), x.get('success'), x.get('eom'), x.get('bytes')) for x in m]})")


# ======================================================================== rx75
def rx_case(a, mode, wav, snr, seed):
    label = f"{mode} {'clean' if snr is None else f'{snr:+g} dB'}"
    srv, d = R.start(a, f"m75rx_{mode}_{'clean' if snr is None else f'{snr:g}'}", ["--mode", mode])
    try:
        msg = msg_for(mode)
        w = np.frombuffer(open(wav, "rb").read()[44:], dtype="<i2").astype(np.float64) / 32768.0
        lead, tail = 4.0, 4.0
        x = np.concatenate([np.zeros(int(lead * FS)), w, np.zeros(int(tail * FS))])
        if snr is not None:
            x = x + R.band_noise(len(x), snr, seed)
        pre0 = int(lead * FS) + int(LEAD_S * FS)
        pre1 = pre0 + int(PREAMBLE_S[mode] * FS)
        col = R.Collector(d)
        fd = R.Feeder(srv.p, R.to_pcm(x), {"pre0": pre0, "pre1": pre1, "end": int(lead * FS) + len(w) - FS})
        fd.done.wait(len(x) / FS + 60)
        # the decode of the last block, then delivery (--rx-fast)
        end = time.time() + 30
        while time.time() < end and not any(r[4] for r in col.receptions()):
            time.sleep(0.2)
        time.sleep(0.5)
        col.stop = True
        rx = col.receptions()
        log = R.log_text(srv)
        early = [e for e in col.events() if e[1] == "cd" and e[2][0] == 1 and e[0] < fd.at["pre0"]]
        A.check(fd.dropped == 0, "brief: no receive audio dropped",
                f"{label}: {fd.dropped} of {fd.dropped + fd.written} samples dropped, {len(x) / FS:.1f} s fed in real time")
        A.check(not early, "A.5.1.2.6 step 1", f"{label}: no carrier on {lead:.0f} s of input before the burst")
        A.check(len(rx) == 1, "A.5.1.2.6", f"{label}: one reception, CARRIER DETECTED ... NO CARRIER ({len(rx)})")
        if not rx:
            print("\n".join(l for l in log.splitlines() if "rx:" in l))
            return None
        t_cd, rb, data, orders, t_nc, t_d = rx[0]
        lat = t_cd - fd.at["pre1"]
        A.check(rb == (75, BF[mode]), "A.5.1.2.6 step 2, Figure A-6",
                f"{label}: CARRIER DETECTED with Rx rate 75 and Rx blocking factor {BF[mode]} ({rb})")
        A.check(-0.05 <= lat <= CD_LIMIT_S, "A.5.1.2.6 step 2 (\"fully detected a preamble\")",
                f"{label}: CARRIER DETECTED {lat:.3f} s after the preamble's last sample went in "
                f"({t_cd - fd.at['pre0']:.3f} s after its first; limit {CD_LIMIT_S} s after the end)")
        A.check(t_cd < fd.at["end"], "Table A-VI",
                f"{label}: carrier up {fd.at['end'] - t_cd:.2f} s before the signal ends")
        sizes = [len(p[1]) for p in [e[2] for e in col.events() if e[1] == "data"]]
        A.check(data == msg, "A.5.1.2.6 steps 3-5", f"{label}: the {len(msg)} bytes delivered exact ({data[:24]!r})")
        A.check(sizes == chunks_new(len(msg), BF[mode]), "A.5.1.1.8, A.5.1.2.8.2",
                f"{label}: one packet per {BF[mode]}-bit chunk, whole bytes: sizes {sizes}")
        A.check(t_nc is not None and orders[:1] in ([1], [2]) and orders[-1:] in ([2], [4]), "A.5.1.2.6 steps 3-7",
                f"{label}: FIRST ... LAST, then NO CARRIER {(t_nc or 0) - fd.at['end']:+.2f} s from the signal's end")
        why = re.findall(r"rx: burst \d+ ends at .*", log)
        print(f"INFO  {label}: {why[-1] if why else 'no burst end logged'}")
        return lat
    finally:
        d.close()
        srv.stop()


def phase_rx75(a, wavs):
    lats = {}
    for mode in ("75S", "75L"):
        if mode not in wavs:
            A.check(False, "brief", f"{mode}: no transmit WAV (phase tx75 failed or did not run)")
            continue
        for snr, seed in ((None, 0), (-6.0, 31), (-6.0, 32)):
            lat = rx_case(a, mode, wavs[mode], snr, seed)
            if lat is not None:
                lats.setdefault(mode, []).append(round(lat, 3))
    print(f"INFO  carrier detect after the end of the preamble (s): {lats}")
    if "75S" in wavs:
        other_config(a, wavs["75S"])


def other_config(a, wav):
    """Configured for 600S, the 75 bps finder does not run (it costs CPU while idle); a clean
    75S burst is still received, through the preamble correlator, as before 75 bps transmit."""
    srv = A.Server(a.server, a.work, "m75rx_cfg600S", ["--rx-fast"])
    d = A.TcpDte(srv.tcp)
    try:
        A.handshake(d)
        A.feed(srv.p, wav, True)
        cds, data = A.collect_rx(d, 60)
        got = b"".join(A.payload(p)[14:] for p in data)
        rb = [struct.unpack(">BII", A.payload(c)[1:10]) for c in cds]
        log = R.log_text(srv)
        A.check(got == msg_for("75S") and (1, 75, 45) in rb, "A.5.1.2.6",
                f"configured 600S: a clean 75S burst delivered exact ({len(got)} bytes), carrier {rb}; "
                f"75 bps finder off: {'75 bps acquisition' not in log}")
    finally:
        d.close()
        srv.stop()


# ======================================================================== framing
def framing_case(a, mode, n):
    """Transmit n bytes in mode, feed the WAV back (paced delivery), check the packets."""
    srv = A.Server(a.server, a.work, f"m75fr_{mode}", ["--mode", mode])
    d = A.TcpDte(srv.tcp)
    try:
        A.handshake(d)
        msg = (f"DE AAM402 {mode} FRAMING " * (n // 20 + 2)).encode()[:n]
        A.send_message(d, msg)
        p, _ = A.wait_tx_state(d, 1, 120)
        if p is None or not A.wait_file(srv.wav_n(1), 60):
            A.check(False, "A.5.1.2.5", f"{mode}: transmission not written")
            return
        srv.feed(1)
        cds, data = [], []
        times = []
        end = time.time() + 120
        while time.time() < end:
            q = d.get(end - time.time(), lambda q: A.is_cmd(0x08)(q) or A.is_cmd(0x00)(q))
            if q is None:
                break
            if A.payload(q)[0] == 0x08:
                cds.append(A.payload(q))
                if A.payload(q)[1] == 0 and data:
                    break
            else:
                data.append(A.payload(q))
                times.append(time.time())
        got = b"".join(p[14:] for p in data)
        sizes = [len(p) - 14 for p in data]
        bf = BF[mode]
        want = chunks_new(n, bf)
        A.check(got == msg, "A.5.1.2.5, A.5.1.2.6", f"{mode}: {n} bytes both ways exact ({len(got)} received)")
        A.check(sizes == want, "A.5.1.1.8, A.5.1.2.8.2", f"{mode}: packet sizes {sizes} (chunk rule {want})")
        if bf % 8 == 0:
            old = chunks_old(n, bf)
            cuts = [sum(old[:i]) for i in range(len(old))]
            same = sizes == old and all(data[i][14:] == msg[c:c + old[i]] for i, c in enumerate(cuts))
            A.check(same, "brief: whole-byte modes unchanged",
                    f"{mode}: packet for packet as before ({len(old)} packets of {old[0]} bytes, last {old[-1]})")
        if len(times) > 2:
            # the delivery rate: bytes after the first packet over the time they took
            r = 8 * sum(sizes[1:-1]) / (times[-2] - times[0])
            print(f"INFO  {mode}: {len(sizes)} packets, delivery {r:.1f} bps over {len(sizes) - 2} chunk intervals "
                  f"(air {RATE[mode]} bps; the old rule would give {8 * ((bf + 7) // 8) * RATE[mode] / bf:.1f} bps)")
    finally:
        d.close()
        srv.stop()


def phase_framing(a):
    for mode, n in (("150S", 100), ("300S", 120), ("600S", 200), ("1200S", 300), ("2400S", 600),
                    ("4800S", 1200), ("600L", 800)):
        framing_case(a, mode, n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", required=True)
    ap.add_argument("--rx", required=True, help="m110a_rx")
    ap.add_argument("--wav-rx", required=True, help="m75_wav_rx")
    ap.add_argument("--work", required=True)
    ap.add_argument("--phases", default="tx75,rx75,framing")
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    wavs = {}
    for ph in a.phases.split(","):
        print(f"--- {ph}", flush=True)
        if ph == "rx75" and not wavs:
            phase_tx75(a, wavs)
        if ph == "tx75":
            phase_tx75(a, wavs)
        elif ph == "rx75":
            phase_rx75(a, wavs)
        elif ph == "framing":
            phase_framing(a)
    print(f"\n{A.PASSED}/{A.PASSED + A.FAILED} 75 bps and framing checks passed")
    return 0 if A.FAILED == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
