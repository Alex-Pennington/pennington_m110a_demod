"""m110_lan's receiver against a live audio stream (ctest appa_rx_stream).

Starts m110_lan with receive audio on its stdin, makes transmit audio with m110_lan's own
transmitter (through the TCP data socket, as appa_dte.py does), then plays it back into the
modem's stdin in real time, 10 ms at a time, the way a sound card or an audio relay would. A
block the modem's input does not take at once (the pipe is full) is dropped and counted, so a
receiver that stops reading while it decodes shows up as dropped audio.

Phases
  stream   a 32 s 600S burst, then a short burst while the first is still being decoded:
           no audio dropped; CARRIER DETECTED within a stated time of the preamble's start and
           before the burst's signal ends (A.5.1.2.6 step 2, Table A-VI); both messages exact;
           NO CARRIER after each; the data delivered block by block as the burst is received
           (the first packet before its signal ends), each packet timed against the end of
           its interleaver block on the air
  noise    600S bursts in band-limited noise (300-3300 Hz; SNR over that band, the way
           greybeard-ale's audio relay sets it): no carrier on noise alone, the burst found by
           its preamble at -2 dB, found and decoded exact at 2 dB
  lowq     a 2400S burst at 4 dB SNR, which the decoder scores below its quality gate: no data
           without --rx-low-quality, the bytes delivered and logged as below the gate with it

Usage: python3 -I -B test/lan/appa_rx_stream.py --server BUILD/bin/m110_lan --work DIR [--phases stream,noise,lowq]
Exit 77 (skipped) when numpy is not installed.
"""
import argparse
import os
import re
import struct
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import appa_dte as A  # noqa: E402  the DTE side: packets, CRC, handshake, server process

try:
    import numpy as np
except ImportError:
    print("SKIP: numpy is not installed")
    sys.exit(77)

FS = 48000
TX_RMS = 7000.0 / 32768.0      # m110_lan's transmit level
CARRIER_LIMIT_S = 1.0          # CARRIER DETECTED within this of the preamble's first sample
LEAD_S = 0.5                   # m110_lan's transmit lead silence (--tx-lead default)


def text(n, tag):
    s = (f"DE AAM402 {tag} " + "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 0123456789 ") * (n // 40 + 2)
    return s[:n].encode()


def make_wav(srv, d, data, n):
    """Transmit data through the modem; returns the transmit audio as float samples."""
    A.send_message(d, data)
    p, seen = A.wait_tx_state(d, 1, 60)
    if p is None or not A.wait_file(srv.wav_n(n), 30):
        raise RuntimeError(f"transmission {n} not written (states {seen})")
    b = open(srv.wav_n(n), "rb").read()
    return np.frombuffer(b[44:], dtype="<i2").astype(np.float64) / 32768.0


def band_noise(n, snr_db, seed):
    """White Gaussian noise band-limited to 300-3300 Hz, scaled so the transmit level over the
    noise power is snr_db."""
    rng = np.random.default_rng(seed)
    w = rng.standard_normal(n)
    f = np.fft.rfft(w)
    hz = np.fft.rfftfreq(n, 1.0 / FS)
    f[(hz < 300) | (hz > 3300)] = 0
    x = np.fft.irfft(f, n)
    x *= np.sqrt(TX_RMS ** 2 / 10 ** (snr_db / 10) / np.mean(x ** 2))
    return x


def to_pcm(x):
    return np.clip(np.round(x * 32768.0), -32768, 32767).astype("<i2").tobytes()


class Collector:
    """Takes every Carrier Detect and Data Transfer packet the modem sends, with arrival time."""

    def __init__(self, d):
        self.d = d
        self.ev = []  # (time, kind, fields)
        self.lock = threading.Lock()
        self.stop = False
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        while not self.stop:
            p = self.d.get(0.2, lambda p: A.is_cmd(0x08)(p) or A.is_cmd(0x00)(p))
            if p is None:
                continue
            t = time.time()
            pl = A.payload(p)
            with self.lock:
                if pl[0] == 0x08:
                    st, rate, bf = struct.unpack(">BII", pl[1:10])
                    self.ev.append((t, "cd", (st, rate, bf)))
                else:
                    self.ev.append((t, "data", (pl[1], bytes(pl[14:]))))

    def events(self):
        with self.lock:
            return list(self.ev)

    def receptions(self):
        """[(t_cd, (rate, bf), data bytes, orders, t_no_carrier or None, t_first_data or None)]"""
        out, cur = [], None
        for t, k, f in self.events():
            if k == "cd" and f[0] == 1:
                cur = [t, f[1:], b"", [], None, None]
                out.append(cur)
            elif k == "cd" and f[0] == 0 and cur is not None:
                cur[4] = t
                cur = None
            elif k == "data" and cur is not None:
                if cur[5] is None:
                    cur[5] = t
                cur[2] += f[1]
                cur[3].append(f[0])
        return out


class Feeder:
    """Writes PCM into the modem's stdin in real time, 10 ms per write, never blocking: a block
    the pipe does not take is dropped and counted. marks: {name: sample index}; the time each
    marked sample was written is recorded."""

    def __init__(self, proc, pcm, marks):
        self.fd = proc.stdin.fileno()
        os.set_blocking(self.fd, False)
        self.pcm = pcm
        self.marks = dict(marks)
        self.at = {}
        self.dropped = 0
        self.written = 0
        self.done = threading.Event()
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        blk = 480 * 2
        t0 = time.time()
        k = 0
        for off in range(0, len(self.pcm), blk):
            delay = t0 + k * 0.01 - time.time()
            if delay > 0:
                time.sleep(delay)
            b = self.pcm[off:off + blk]
            try:
                n = os.write(self.fd, b)
            except BlockingIOError:
                n = 0
            self.written += n // 2
            self.dropped += (len(b) - n) // 2
            for name, s in self.marks.items():
                if name not in self.at and off // 2 <= s < (off + blk) // 2:
                    self.at[name] = time.time() + (s - off // 2) / FS
            k += 1
        self.done.set()


def log_text(srv):
    return open(os.path.join(srv.work, f"{srv.tag}.log"), "rb").read().decode(errors="replace")


def start(a, tag, extra=()):
    srv = A.Server(a.server, a.work, tag, ["--rx-fast"] + list(extra))
    d = A.TcpDte(srv.tcp)
    A.handshake(d)
    return srv, d


# ======================================================================== stream
def phase_stream(a):
    srv, d = start(a, "rxstream")
    try:
        long_msg = text(2252, "LONG")
        short_msg = text(120, "SHORT")
        w1 = make_wav(srv, d, long_msg, 1)
        w2 = make_wav(srv, d, short_msg, 2)
        sil = lambda s: np.zeros(int(s * FS))
        parts = [sil(1.0), w1, sil(3.0), w2, sil(20.0)]
        x = np.concatenate(parts)
        s1 = len(parts[0])
        s2 = s1 + len(w1) + len(parts[2])
        marks = {"pre1": s1 + int(LEAD_S * FS), "end1": s1 + len(w1) - FS,  # the 1 s tail is silence
                 "data1": s1 + int((LEAD_S + 0.6) * FS),  # after the 3-frame 600S preamble
                 "pre2": s2 + int(LEAD_S * FS), "end2": s2 + len(w2) - FS}
        col = Collector(d)
        fd = Feeder(srv.p, to_pcm(x), marks)
        fd.done.wait(len(x) / FS + 30)
        time.sleep(1.0)
        col.stop = True
        rx = col.receptions()
        log = log_text(srv)
        decodes = [float(m) for m in re.findall(r"decoded in ([0-9.]+) s", log)]

        A.check(fd.dropped == 0, "brief: gap 3",
                f"no receive audio dropped: {fd.dropped} of {fd.dropped + fd.written} samples dropped, "
                f"{len(x) / FS:.1f} s fed in real time; decodes took {decodes} s")
        A.check(len(rx) == 2, "A.5.1.2.6", f"two receptions (CARRIER DETECTED ... NO CARRIER): {len(rx)}")
        if rx:
            t_cd, rb, data, orders, t_nc, t_d = rx[0]
            lat = t_cd - fd.at["pre1"]
            A.check(rb == (600, 360), "A.5.1.2.6 step 2, Figure A-6",
                    f"CARRIER DETECTED with the detected rate and blocking factor: {rb}")
            A.check(0 <= lat <= CARRIER_LIMIT_S, "A.5.1.2.6 step 2, Table A-VI",
                    f"CARRIER DETECTED {lat:.3f} s after the preamble's first sample went in (limit {CARRIER_LIMIT_S} s)")
            A.check(t_cd < fd.at["end1"], "Table A-VI",
                    f"carrier up while the 32 s burst is still on the air ({fd.at['end1'] - t_cd:.1f} s before its end)")
            A.check(data == long_msg, "A.5.1.2.6 steps 3-5",
                    f"the 2252-byte burst delivered exact ({len(data)} bytes, starts {data[:9]!r})")
            A.check(t_nc is not None and orders[:1] == [1] and orders[-1:] == [4], "A.5.1.2.6 steps 3-7",
                    f"FIRST_ONLY ... LAST, then NO CARRIER ({len(orders)} packets); first data "
                    f"{(t_d or 0) - fd.at['end1']:.2f} s after the signal ended")
            # Each data packet against the end of its interleaver block on the air (0.6 s
            # blocks from the end of the preamble): a block is decoded once the next block is
            # in (the Viterbi traceback reaches into it, and it is deinterleaved whole)
            pk = [t for t, k, f in col.events() if k == "data" and t_cd <= t <= (t_nc or 1e18)]
            if pk and "data1" in fd.at:
                delay = [t - (fd.at["data1"] + 0.6 * (i + 1)) for i, t in enumerate(pk)]
                ds = sorted(delay)
                print(f"INFO  600S 32 s burst: first data {pk[0] - fd.at['end1']:+.2f} s, last data "
                      f"{pk[-1] - fd.at['end1']:+.2f} s, NO CARRIER {(t_nc or 0) - fd.at['end1']:+.2f} s against the "
                      f"end of the signal; packet after its block's end: min {ds[0]:.2f} s, median "
                      f"{ds[len(ds) // 2]:.2f} s, max {ds[-1]:.2f} s ({len(pk)} packets)")
                A.check(pk[0] < fd.at["end1"], "A.5.1.2.6 steps 3-5",
                        f"data delivered while the burst is on the air: the first packet "
                        f"{fd.at['end1'] - pk[0]:.1f} s before the signal ends")
        if len(rx) > 1:
            t_cd, rb, data, orders, t_nc, t_d = rx[1]
            A.check(data == short_msg and t_nc is not None, "brief: gap 3",
                    f"the burst sent while the first was being decoded, delivered exact ({len(data)} bytes), "
                    f"then NO CARRIER")
        A.check(re.search(r"burst 2 .* decoded in", log) is not None, "brief: gap 3",
                "the second burst was found and decoded from audio read during the first decode")
    finally:
        d.close()
        srv.stop()


# ======================================================================== noise
def noise_case(a, snr, seed, tag, must_decode):
    srv, d = start(a, tag)
    try:
        msg = text(100, f"SNR {snr}")
        w = make_wav(srv, d, msg, 1)
        lead, tail = 4.0, 6.0
        sig = np.concatenate([np.zeros(int(lead * FS)), w, np.zeros(int(tail * FS))])
        x = sig + band_noise(len(sig), snr, seed)
        pre = int(lead * FS) + int(LEAD_S * FS)
        col = Collector(d)
        fd = Feeder(srv.p, to_pcm(x), {"pre": pre, "end": int(lead * FS) + len(w) - FS})
        fd.done.wait(len(x) / FS + 30)
        time.sleep(1.0)
        col.stop = True
        rx = col.receptions()
        early = [e for e in col.events() if e[1] == "cd" and e[2][0] == 1 and e[0] < fd.at["pre"]]
        A.check(not early, "brief: gap 4", f"{snr:+} dB: no carrier on {lead:.0f} s of noise alone before the burst")
        A.check(len(rx) == 1, "brief: gap 4", f"{snr:+} dB: one burst found by its preamble ({len(rx)} receptions)")
        if rx:
            t_cd, rb, data, orders, t_nc, t_d = rx[0]
            lat = t_cd - fd.at["pre"]
            A.check(rb == (600, 360) and 0 <= lat <= CARRIER_LIMIT_S, "A.5.1.2.6 step 2",
                    f"{snr:+} dB: CARRIER DETECTED {rb} {lat:.3f} s after the preamble started")
            A.check(t_nc is not None, "A.5.1.2.6 step 7", f"{snr:+} dB: NO CARRIER after it")
            log = log_text(srv)
            verdict = re.findall(r"rx: burst.*(?:decoded in|not decoded).*", log)
            if must_decode:
                A.check(data == msg, "brief: gap 4", f"{snr:+} dB: decoded exact ({len(data)} bytes)")
            else:
                print(f"INFO  {snr:+} dB: {len(data)} bytes delivered; decoder: {verdict[-1] if verdict else 'none'}")
    finally:
        d.close()
        srv.stop()


def phase_noise(a):
    noise_case(a, -2.0, 11, "rxnoise_m2", False)
    noise_case(a, 2.0, 12, "rxnoise_p2", True)


# ======================================================================== lowq
def lowq_case(a, tag, extra, x_fn):
    srv, d = start(a, tag, ["--mode", "2400S"] + list(extra))
    try:
        msg = text(600, "LOWQ")
        w = make_wav(srv, d, msg, 1)
        x = x_fn(w)
        col = Collector(d)
        fd = Feeder(srv.p, to_pcm(x), {})
        fd.done.wait(len(x) / FS + 30)
        time.sleep(2.0)
        col.stop = True
        return msg, col.receptions(), log_text(srv)
    finally:
        d.close()
        srv.stop()


def phase_lowq(a):
    cache = {}

    def noisy(w):
        if "x" not in cache:
            sig = np.concatenate([np.zeros(2 * FS), w, np.zeros(3 * FS)])
            cache["x"] = sig + band_noise(len(sig), 4.0, 21)
        return cache["x"]

    msg, rx, log = lowq_case(a, "rxlowq_off", [], noisy)
    gate = re.search(r"not decoded \((Decode quality [^)]*)\)", log)
    A.check(len(rx) == 1 and rx[0][2] == b"" and rx[0][4] is not None, "brief: gap 5",
            f"2400S at 4 dB, default: CARRIER DETECTED then NO CARRIER, no data "
            f"({gate.group(1) if gate else 'decoder verdict not found'})")
    msg, rx, log = lowq_case(a, "rxlowq_on", ["--rx-low-quality"], noisy)
    line = re.search(r"BELOW THE QUALITY GATE.*", log)
    data = rx[0][2] if rx else b""
    errs = sum(1 for i in range(min(len(data), len(msg))) if data[i] != msg[i]) + abs(len(data) - len(msg))
    A.check(len(rx) == 1 and len(data) > 0 and rx[0][4] is not None and line is not None, "brief: gap 5",
            f"--rx-low-quality: {len(data)} bytes delivered, {errs} differ from the {len(msg)} sent; "
            f"log: {line.group(0) if line else None}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--phases", default="stream,noise,lowq")
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    for ph in a.phases.split(","):
        print(f"--- {ph}", flush=True)
        {"stream": phase_stream, "noise": phase_noise, "lowq": phase_lowq}[ph](a)
    print(f"\n{A.PASSED}/{A.PASSED + A.FAILED} receive stream checks passed")
    return 0 if A.FAILED == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
