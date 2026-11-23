"""Test DTE for m110_lan: MIL-STD-188-110D Appendix A over TCP and over UDP (ctest appa_lan_dte).

Starts m110_lan (600S) with transmit audio to WAV files and receive audio on its stdin, then
plays the DTE. Every packet the modem sends is checked byte by byte against the appendix's
field layout; each check prints the paragraph, table or figure it tests. The DTE side is
written here from the appendix, not from m110_lan's code: its own CRC (the A.5.3 listing),
its own Reed-Solomon encoder from the printed g(x) (A.5.2.5) and its own packet builders.

Phases
  tcp       handshake, second DTE refused, keep-alive, NACK causes, arm/start/drain to a WAV,
            that WAV fed back as receive audio and the same bytes delivered, abort reception,
            bad CRC dropped, protocol errors answered with ERROR and close, handshake failures,
            30 s keep-alive timeout
  half      half-duplex receiver master and transmitter master (A.5.1.2.7)
  udp       control connection, status replies, ping, not-in-control, FEC-coded transfer with
            lost packets to a WAV, that WAV back as ENHANCED/ENCODED DATA, command request/ack
            and abort, packets from a stranger ignored, bad CRC and wrong version ignored

Usage: python3 -I -B test/lan/appa_dte.py --server BUILD/bin/m110_lan --work DIR [--phases tcp,half,udp]
"""
import argparse
import os
import queue
import random
import socket
import struct
import subprocess
import sys
import threading
import time

PASSED = 0
FAILED = 0


def check(ok, cite, what):
    global PASSED, FAILED
    print(f"{'PASS' if ok else 'FAIL'}  [{cite}] {what}", flush=True)
    if ok:
        PASSED += 1
    else:
        FAILED += 1
    return ok


# ---------------------------------------------------------------- A.5.3 CRC (the spec's listing)
def crc16(data):
    crc = 0
    for byte in data:
        j = 1
        while j < 0x100:
            bit = (crc & 1) ^ (1 if byte & j else 0)
            crc >>= 1
            if bit:
                crc ^= 0x9299
            j <<= 1
    return crc


# ---------------------------------------------------------------- A.5.2.5 RS, GF(16) mod x^4+x+1
EXP = [0] * 30
LOG = [0] * 16
_x = 1
for _i in range(15):
    EXP[_i] = EXP[_i + 15] = _x
    LOG[_x] = _i
    _x <<= 1
    if _x & 0x10:
        _x ^= 0x13


def gmul(a, b):
    return 0 if a == 0 or b == 0 else EXP[LOG[a] + LOG[b]]


def a_pow(e):
    return EXP[e % 15]


# g(x) = x^4 + a^13 x^3 + a^6 x^2 + a^3 x + a^10, as printed in A.5.2.5 (coefficients x^0..x^4)
G4 = [a_pow(10), a_pow(3), a_pow(6), a_pow(13), 1]


def generator(nk):
    if nk == 4:
        return G4
    # n-k = 3 has no printed generator; roots a^1..a^3 (docs/110d-appa-lan.md, ambiguities)
    p = [1]
    for r in range(1, nk + 1):
        q = [0] * (len(p) + 1)
        for i, c in enumerate(p):
            q[i + 1] ^= c
            q[i] ^= gmul(c, a_pow(r))
        p = q
    return p


def rs_encode(data, n):
    """Systematic: positions 0..n-k-1 parity (x^0..), then the k data symbols (Table A-XVIII)."""
    k = len(data)
    nk = n - k
    g = generator(nk)
    c = [0] * nk + list(data)
    rem = list(c)
    for i in range(n - 1, nk - 1, -1):
        f = rem[i]
        if f:
            for j in range(nk + 1):
                rem[i - nk + j] ^= gmul(f, g[j])
    return rem[:nk] + list(data)


EOM = bytes([0x4B, 0x65, 0xA5, 0xB2, 0x26, 0xD2, 0xD3, 0x69])  # A.5.2.5.2


def per_packet(bf, k):  # Figure A-22: L / N
    return min(-(-bf // (k * 8)), 1200)


def fec_sets(user, n, k, per):
    """user bytes -> EOM, zero pad, interleaver sets of n packets of per bytes (A.5.2.5)."""
    set_bytes = per * k
    s = bytes(user) + EOM
    if len(s) % set_bytes:
        s += bytes(set_bytes - len(s) % set_bytes)
    sets = []
    for off in range(0, len(s), set_bytes):
        blk = s[off:off + set_bytes]
        pk = [bytearray(per) for _ in range(n)]
        for row in range(per):
            hi = rs_encode([b >> 4 for b in blk[row * k:(row + 1) * k]], n)
            lo = rs_encode([b & 15 for b in blk[row * k:(row + 1) * k]], n)
            for j in range(n):
                pk[j][row] = hi[j] << 4 | lo[j]
        sets.append([bytes(p) for p in pk])
    return sets


def unfec_set(pk, n, k):
    """All n packets present: check every codeword's parity and return the user bytes."""
    per = len(pk[0])
    out = bytearray()
    ok = True
    for row in range(per):
        hi = [pk[j][row] >> 4 for j in range(n)]
        lo = [pk[j][row] & 15 for j in range(n)]
        ok &= rs_encode(hi[n - k:], n) == hi and rs_encode(lo[n - k:], n) == lo
        for j in range(k):
            out.append(hi[n - k + j] << 4 | lo[n - k + j])
    return bytes(out), ok


def strip_eom(s):
    s = s.rstrip(b"\x00")
    if not s.endswith(EOM):
        return None
    return s[:-8]


# ---------------------------------------------------------------- TCP packets (A.5.1.1)
PRE = b"\x49\x50\x55"


def tcp_packet(ptype, payload=b""):
    h = PRE + bytes([ptype]) + struct.pack(">H", len(payload))
    h += struct.pack(">H", crc16(h))
    if payload:
        h += payload + struct.pack(">H", crc16(payload))
    return h


def data_transfer(order, pid, data):
    return tcp_packet(0x00, bytes([0x00, order]) + pid + data)


class TcpDte:
    """A DTE on the TCP data socket. A reader thread blocked only on socket input (A.4.3)
    collects packets; a keep-alive thread sends the empty DATA packet after 2 s idle (A.5.1.2.2)."""

    def __init__(self, port, keepalive=True):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=10)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.q = queue.Queue()
        self.raw = []  # every packet as bytes on the wire
        self.closed = threading.Event()
        self.last_sent = time.time()
        self.keepalive = keepalive
        self.lock = threading.Lock()
        threading.Thread(target=self._reader, daemon=True).start()
        threading.Thread(target=self._ka, daemon=True).start()

    def _reader(self):
        buf = b""
        self.s.settimeout(None)
        while True:
            try:
                d = self.s.recv(65536)
            except OSError:
                d = b""
            if not d:
                self.closed.set()
                self.q.put(None)
                return
            buf += d
            while len(buf) >= 8:
                i = buf.find(PRE)
                if i < 0:
                    buf = buf[-2:]
                    break
                buf = buf[i:]
                if len(buf) < 8:
                    break
                n = struct.unpack(">H", buf[4:6])[0]
                total = 8 + (n + 2 if n else 0)
                if len(buf) < total:
                    break
                pkt, buf = buf[:total], buf[total:]
                self.raw.append(pkt)
                self.q.put(pkt)

    def _ka(self):
        while not self.closed.is_set():
            time.sleep(0.2)
            if self.keepalive and time.time() - self.last_sent >= 1.5:
                try:
                    self.send(tcp_packet(0x00))
                except OSError:
                    return

    def send(self, b):
        with self.lock:
            self.s.sendall(b)
            self.last_sent = time.time()

    def get(self, timeout=5.0, want=None):
        """Next packet, skipping keep-alives and, with want, packets that do not match."""
        end = time.time() + timeout
        while True:
            left = end - time.time()
            if left <= 0:
                return None
            try:
                p = self.q.get(timeout=left)
            except queue.Empty:
                return None
            if p is None:
                self.q.put(None)
                return None
            if p[3] == 0x00 and len(p) == 8:
                continue  # keep-alive
            if want is None or want(p):
                return p

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


def payload(p):
    n = struct.unpack(">H", p[4:6])[0]
    return p[8:8 + n]


def frame_ok(p):
    """A.5.1.1.1 / A.5.1.1.3 / A.5.3: preamble, header CRC over 6 bytes, payload CRC over payload."""
    n = struct.unpack(">H", p[4:6])[0]
    ok = p[:3] == PRE and struct.unpack(">H", p[6:8])[0] == crc16(p[:6]) and n <= 4086
    if n:
        ok &= len(p) == 8 + n + 2 and struct.unpack(">H", p[8 + n:])[0] == crc16(p[8:8 + n])
    else:
        ok &= len(p) == 8
    return ok


def is_cmd(c):
    return lambda p: p[3] == 0x00 and len(p) > 8 and p[8] == c


def tx_status(p):
    pl = payload(p)
    return pl[1], struct.unpack(">IIII", pl[2:18])


def wait_tx_state(d, state, timeout=10.0):
    end = time.time() + timeout
    seen = []
    while time.time() < end:
        p = d.get(end - time.time(), is_cmd(0x05))
        if p is None:
            break
        seen.append(payload(p)[1])
        if payload(p)[1] == state:
            return p, seen
    return None, seen


def handshake(d, cite_checks=False, version=12):
    """A.5.1.2.1: CONNECT both ways, CONNECT_ACK both ways, probe, then the four setup packets."""
    c = d.get(3.5)
    if cite_checks:
        check(c is not None and c == tcp_packet(0x01, bytes([12])) and frame_ok(c), "A.5.1.1.1, A.5.1.1.2, Table A-I",
              f"modem CONNECT is 49 50 55 01 0001 CRC 0C CRC: {c.hex() if c else None}")
    d.send(tcp_packet(0x01, bytes([version])))
    a = d.get(3.5, lambda p: p[3] == 0x02)
    if cite_checks:
        check(a == tcp_packet(0x02, bytes([12])), "A.5.1.2.1, Table A-I",
              f"modem answers CONNECT with CONNECT_ACK version 12: {a.hex() if a else None}")
    d.send(tcp_packet(0x02, bytes([12])))
    pr = d.get(6.5, is_cmd(0x0B))
    if cite_checks:
        check(pr == tcp_packet(0x00, b"\x0b"), "A.5.1.2.1, Table A-II",
              f"modem sends CONNECTION_PROBE (DATA, payload 0B): {pr.hex() if pr else None}")
    d.send(tcp_packet(0x00, b"\x0b"))
    pkts = [d.get(3.0) for _ in range(4)]
    return pkts


def feed(proc, wav_path, first):
    """Write a transmit WAV into the modem's receive input: the whole WAV the first time, the
    PCM after the 44-byte header afterwards (the input is one stream)."""
    b = open(wav_path, "rb").read()
    proc.stdin.write(b if first else b[44:])
    proc.stdin.flush()


def wav_ok(path):
    b = open(path, "rb").read()
    fmt = struct.unpack("<HHIIHH", b[20:36])
    return b[:4] == b"RIFF" and b[8:12] == b"WAVE" and fmt == (1, 1, 48000, 96000, 2, 16), len(b)


def wait_file(path, timeout=20.0):
    end = time.time() + timeout
    while time.time() < end:
        if os.path.exists(path):
            return True
        time.sleep(0.05)
    return False


def free_ports(n):
    socks, ports = [], []
    for _ in range(n):
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        ports.append(s.getsockname()[1])
        socks.append(s)
    for s in socks:
        s.close()
    return ports


class Server:
    def __init__(self, exe, work, tag, extra=()):
        self.tcp, self.rci, self.udp = free_ports(3)
        self.work = work
        self.tag = tag
        self.log = open(os.path.join(work, f"{tag}.log"), "wb")
        self.wav = os.path.join(work, f"{tag}_tx_%d.wav")
        for f in os.listdir(work):
            if f.startswith(f"{tag}_tx_"):
                os.remove(os.path.join(work, f))
        args = [exe, "--mode", "600S", "--tx-out", self.wav, "--rx-in", "-", "--port", str(self.tcp),
                "--rci-port", str(self.rci), "--udp-port", str(self.udp), "--bind", "127.0.0.1"] + list(extra)
        self.p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=self.log)
        self.first_feed = True
        # ready once it logs its "ready:" line (a probe connection would take the DTE slot)
        logpath = os.path.join(work, f"{tag}.log")
        end = time.time() + 10
        while time.time() < end:
            if b"m110_lan: ready:" in open(logpath, "rb").read():
                return
            if self.p.poll() is not None:
                break
            time.sleep(0.05)
        raise RuntimeError("server did not start")

    def wav_n(self, n):
        return self.wav.replace("%d", str(n))

    def feed(self, n):
        feed(self.p, self.wav_n(n), self.first_feed)
        self.first_feed = False

    def stop(self):
        self.p.kill()
        self.p.wait()
        self.log.close()


def expect_rejected(port, cite, what):
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.settimeout(3)
    try:
        d = s.recv(100)
    except socket.timeout:
        d = None
    s.close()
    check(d == b"", cite, f"{what}: connection closed by the modem with no bytes (got {d!r})")


def payload_bytes(n, seed):
    r = random.Random(seed)
    return bytes(r.randrange(256) for _ in range(n))


def send_message(d, data, start_first=False):
    """A.5.1.2.5: arm, wait for ready, pre-fill / start, LAST. Returns the packet IDs used."""
    d.send(tcp_packet(0x00, b"\x01"))  # TRANSMIT_ARM
    p, _ = wait_tx_state(d, 3)
    chunks = [data[i:i + 4072] for i in range(0, len(data), 4072)] or [b""]
    if start_first:
        d.send(tcp_packet(0x00, b"\x02"))
        wait_tx_state(d, 4)
    for i, c in enumerate(chunks):
        order = 2 if len(chunks) == 1 else 1 if i == 0 else 4 if i == len(chunks) - 1 else 3
        d.send(data_transfer(order, struct.pack(">4xQ", 1000 + i), c))
    if not start_first:
        d.send(tcp_packet(0x00, b"\x02"))


def collect_rx(d, timeout=30.0):
    """A.5.1.2.6: CARRIER_DETECTED, data FIRST.. LAST, NO_CARRIER. Returns (cd packets, data packets)."""
    cds, data = [], []
    end = time.time() + timeout
    while time.time() < end:
        p = d.get(end - time.time(), lambda p: is_cmd(0x08)(p) or is_cmd(0x00)(p))
        if p is None:
            break
        if payload(p)[0] == 0x08:
            cds.append(p)
            if payload(p)[1] == 0 and data:
                break
        else:
            data.append(p)
    return cds, data


# ======================================================================== TCP phase
def phase_tcp(a):
    srv = Server(a.server, a.work, "tcp")
    try:
        d = TcpDte(srv.tcp)
        init, txs, sts, cd = handshake(d, cite_checks=True)
        pl = payload(init) if init else b""
        rtt, mn, mx = struct.unpack(">III", pl[1:13]) if len(pl) >= 13 else (None,) * 3
        check(init is not None and frame_ok(init) and len(pl) == 18 and pl[0] == 0x0A, "A.5.1.1.10, Figure A-8",
              f"Initial Setup: command 0A, 17 argument bytes ({pl.hex()})")
        check(rtt is not None and rtt < 1000 and mn == 100 and mx == 5000, "A.5.1.1.10, A.5.1.2.1 item 1",
              f"round trip {rtt} ms measured over the probe, minimum latency {mn}, maximum {mx} (configured)")
        check(pl[13:18] == bytes([0, 3, 0, 0, 1]), "Tables A-VII, A-VIII, A-IX",
              "sync flag 0 ASYNCHRONOUS, 8 data bits (3), 1 stop bit (0), no parity (0), DATA ONLY MODE (1)")
        check(txs == tcp_packet(0x00, b"\x09" + struct.pack(">II", 600, 360)), "A.5.1.1.9, Figure A-7, A.5.1.2.1 item 2",
              f"Transmit Setup: 600 bit/s, blocking factor 360 bits ({payload(txs).hex() if txs else None})")
        st = tx_status(sts) if sts and is_cmd(0x05)(sts) else None
        check(st is not None and st[0] == 1 and st[1][0] == 1 << 20 and st[1][1:] == (0, 0, 0) and len(payload(sts)) == 18,
              "A.5.1.1.7, Figure A-5, Table A-V, A.5.1.2.1 item 3",
              f"Tx Status after connect: FLUSHED (1), FIFO space {st[1][0] if st else None}, the rest 0")
        check(cd == tcp_packet(0x00, b"\x08\x00" + bytes(8)), "A.5.1.1.8, Figure A-6, A.5.1.2.1 item 4",
              "Carrier Detect after connect: NO CARRIER (0), rate 0, blocking factor 0")

        expect_rejected(srv.tcp, "A.4.1", "a second DTE on the TCP data socket")
        expect_rejected(srv.rci, "A.4.1", "a second DTE on the UDP control connection")

        # keep-alive from the modem after 2 s of silence (A.5.1.2.2)
        n0 = len(d.raw)
        time.sleep(2.6)
        kas = [p for p in d.raw[n0:] if p == tcp_packet(0x00)]
        check(len(kas) >= 1, "A.5.1.2.2", f"modem sends the empty DATA keep-alive 49 50 55 00 0000 CRC after 2 s idle ({len(kas)} seen)")

        # NACK: data while the queues are not armed
        pid = bytes(range(1, 13))
        d.send(data_transfer(2, pid, b"x"))
        nk = d.get(3, is_cmd(0x04))
        check(nk == tcp_packet(0x00, b"\x04\x00" + pid), "A.5.1.1.6, Figure A-4, Table A-IV cause 0",
              f"data before TRANSMIT_ARM: Tx_Data_NAK cause 0 with the packet ID echoed ({payload(nk).hex() if nk else None})")
        d.send(tcp_packet(0x00, b"\x01"))
        p, seen = wait_tx_state(d, 3)
        check(p is not None, "A.5.1.2.5 steps 2-3, Table A-V", f"TRANSMIT_ARM -> Tx Status QUEUES_ARMED_AND_PORT_READY (3), states {seen}")
        d.send(tcp_packet(0x00, b"\x01"))
        p, seen = wait_tx_state(d, 3, 3)
        check(p is not None and seen == [3], "A.5.1.2.5 step 1", "TRANSMIT_ARM when not FLUSHED: Tx Status with the current state")
        pid2 = bytes(range(20, 32))
        d.send(data_transfer(3, pid2, b"y"))
        nk = d.get(3, is_cmd(0x04))
        check(nk == tcp_packet(0x00, b"\x04\x02" + pid2), "Table A-IV cause 2", "CONTINUATION with no FIRST: NACK MISSING FIRST PACKET")
        msg = payload_bytes(150, 1)
        d.send(data_transfer(1, struct.pack(">4xQ", 1), msg[:60]))
        p, _ = wait_tx_state(d, 3, 3)
        check(p is not None and tx_status(p)[1][1] == 60 and tx_status(p)[1][0] == (1 << 20) - 60, "A.5.1.1.7, Figure A-5",
              f"Tx Status after 60 bytes: FIFO fill 60, space {tx_status(p)[1][0] if p else None}")
        pid3 = bytes([7] * 12)
        d.send(data_transfer(1, pid3, b"z"))
        nk = d.get(3, is_cmd(0x04))
        check(nk == tcp_packet(0x00, b"\x04\x03" + pid3), "Table A-IV cause 3", "a second FIRST_ONLY: NACK MULTIPLE FIRST PACKET")
        d.send(tcp_packet(0x00, b"\x03"))
        p = d.get(3, is_cmd(0x05))
        check(p is not None and tx_status(p) == (3, ((1 << 20) - 60, 60, 0, 0)), "Table A-II 0x03, A.5.1.2.5 step 9",
              "Request Tx Status answered: READY, space, fill 60, critical ms 0, critical bytes 0")
        d.send(tcp_packet(0x00, b"\x02"))
        p, seen = wait_tx_state(d, 4, 3)
        check(p is not None, "A.5.1.2.5 steps 5-6", f"TRANSMIT_START -> STARTED (4), states {seen}")
        d.send(data_transfer(3, struct.pack(">4xQ", 2), msg[60:100]))
        d.send(data_transfer(4, struct.pack(">4xQ", 3), msg[100:]))
        p, seen = wait_tx_state(d, 5, 5)
        check(p is not None, "A.5.1.2.5 step 12, Table A-V", f"LAST packet in STARTED -> DRAINING_OK (5), states {seen}")
        p, seen = wait_tx_state(d, 1, 20)
        check(p is not None, "A.5.1.2.5 step 13", f"transmission finished -> FLUSHED (1), states {seen}")
        ok, size = wav_ok(srv.wav_n(1)) if os.path.exists(srv.wav_n(1)) else (False, 0)
        check(ok, "brief: transmit audio sink", f"transmit WAV written: 16-bit mono 48 kHz, {size} bytes")

        # the WAV back as receive audio
        srv.feed(1)
        cds, data = collect_rx(d)
        cd1 = cds[0] if cds else None
        check(cd1 == tcp_packet(0x00, b"\x08\x01" + struct.pack(">II", 600, 360)), "A.5.1.2.6 step 2, Figure A-6, Table A-VI",
              f"Carrier Detect CARRIER DETECTED, 600 bit/s, blocking factor 360 ({payload(cd1).hex() if cd1 else None})")
        orders = [payload(p)[1] for p in data]
        got = b"".join(payload(p)[14:] for p in data)
        sizes = [len(payload(p)) - 14 for p in data]
        check(len(orders) >= 2 and orders[0] == 1 and orders[-1] == 4 and all(o == 3 for o in orders[1:-1]),
              "A.5.1.1.5.1, Table A-III, A.5.1.2.6 steps 3-5", f"data packet orders {orders}: FIRST_ONLY, CONTINUATION..., LAST")
        check(all(frame_ok(p) and payload(p)[0] == 0 for p in data) and all(s <= 45 for s in sizes),
              "A.5.1.1.5, Figure A-3, A.5.1.1.8", f"data transfer payloads 00, order, 12-byte ID, data; chunks {sizes} <= 360 bits")
        ids = [payload(p)[2:14] for p in data]
        check(len(set(ids)) == len(ids), "A.5.1.1.5.2", "every modem data packet has a unique 12-byte packet ID")
        check(got == msg, "brief: loopback", f"received bytes equal the transmitted 150 bytes ({len(got)} delivered)")
        check(len(cds) >= 2 and cds[-1] == tcp_packet(0x00, b"\x08\x00" + bytes(8)), "A.5.1.2.6 step 7",
              "Carrier Detect NO CARRIER, rate 0, blocking factor 0 after the message")

        # ABORT_RECEPTION mid-message
        msg2 = payload_bytes(400, 2)
        send_message(d, msg2)
        p, seen = wait_tx_state(d, 1, 30)
        check(p is not None and os.path.exists(srv.wav_n(2)), "A.5.1.2.5", f"second message (400 bytes, pre-filled before START) transmitted, states {seen}")
        srv.feed(2)
        got_parts = []
        end = time.time() + 30
        while time.time() < end:
            p = d.get(end - time.time(), is_cmd(0x00))
            if p is None:
                break
            got_parts.append(p)
            if len(got_parts) == 2:
                break
        d.send(tcp_packet(0x00, b"\x06"))
        cds, rest = collect_rx(d, 10)
        orders = [payload(p)[1] for p in got_parts + rest]
        last_empty = rest and payload(rest[-1])[1] == 4 and len(payload(rest[-1])) == 14
        check(orders[:2] == [1, 3] and last_empty and len(orders) <= 4, "A.5.1.2.6 step 6, Table A-II 0x06",
              f"ABORT_RECEPTION after two packets: a LAST packet with no data follows (orders {orders})")
        check(cds and cds[-1] == tcp_packet(0x00, b"\x08\x00" + bytes(8)), "A.5.1.2.6 step 6", "then Carrier Detect NO CARRIER")

        # CRC failures are dropped silently
        bad = bytearray(tcp_packet(0x00, b"\x03"))
        bad[7] ^= 0xFF
        d.send(bytes(bad))
        bad2 = bytearray(tcp_packet(0x00, b"\x03"))
        bad2[8] ^= 0x01
        d.send(bytes(bad2))
        p = d.get(1.5, is_cmd(0x05))
        check(p is None, "A.5.1.2.3", "REQUEST_TX_STATUS with a bad header CRC and one with a bad payload CRC: no answer")
        d.send(tcp_packet(0x00, b"\x03"))
        p = d.get(2, is_cmd(0x05))
        check(p is not None, "A.5.1.2.3", "the connection still answers a good REQUEST_TX_STATUS")

        # protocol error: an unused payload command
        d.send(tcp_packet(0x00, b"\x07"))
        e = d.get(3, lambda p: p[3] == 0xFF)
        d.closed.wait(3)
        check(e is not None and frame_ok(e) and d.closed.is_set(), "A.5.1.1.4, A.5.1.2.3",
              f"payload command 0x07: ERROR packet (type FF) and the modem closes ({e.hex() if e else None})")
        d.close()
        time.sleep(0.3)

        # protocol error: an unused header type
        d = TcpDte(srv.tcp)
        handshake(d)
        d.get(1)
        d.send(tcp_packet(0x05, b"\x00"))
        e = d.get(3, lambda p: p[3] == 0xFF)
        d.closed.wait(3)
        check(e is not None and d.closed.is_set(), "A.5.1.1.1", "header type 0x05: ERROR (0xFF) and close")
        d.close()
        time.sleep(0.3)

        # a payload-bearing packet before the connection is up
        d = TcpDte(srv.tcp)
        d.get(2)
        d.send(tcp_packet(0x00, b"\x03"))
        e = d.get(3, lambda p: p[3] == 0xFF)
        d.closed.wait(3)
        check(e is not None and d.closed.is_set(), "A.5.1.2.3", "REQUEST_TX_STATUS before CONNECT: ERROR and close")
        d.close()
        time.sleep(0.3)

        # wrong version
        d = TcpDte(srv.tcp)
        d.get(2)
        d.send(tcp_packet(0x01, bytes([11])))
        closed = d.closed.wait(4)
        check(closed, "A.5.1.2.1", "CONNECT with version 11: the modem terminates the connection")
        d.close()
        time.sleep(0.3)

        # no CONNECT within 3 s
        d = TcpDte(srv.tcp, keepalive=False)
        t0 = time.time()
        closed = d.closed.wait(6)
        check(closed and 2.5 <= time.time() - t0 <= 4.5, "A.5.1.2.1",
              f"silent DTE: the modem closes after {time.time() - t0:.1f} s (3 s rule)")
        d.close()
        time.sleep(0.3)

        # CONNECT but no probe answer within 6 s
        d = TcpDte(srv.tcp, keepalive=False)
        d.get(3)
        d.send(tcp_packet(0x01, bytes([12])))
        d.get(3, lambda p: p[3] == 0x02)
        d.send(tcp_packet(0x02, bytes([12])))
        t0 = time.time()
        d.get(3, is_cmd(0x0B))
        closed = d.closed.wait(9)
        check(closed and 5.5 <= time.time() - t0 <= 7.5, "A.5.1.2.1",
              f"no CONNECTION_PROBE answer: the modem closes after {time.time() - t0:.1f} s (6 s rule)")
        d.close()
        time.sleep(0.3)

        # 30 s without any DATA packet from the DTE
        d = TcpDte(srv.tcp, keepalive=False)
        handshake(d)
        t0 = time.time()
        closed = d.closed.wait(40)
        check(closed and 29 <= time.time() - t0 <= 33, "A.5.1.2.2",
              f"DTE silent after connecting: the modem closes after {time.time() - t0:.1f} s (30 s rule)")
        d.close()
    finally:
        srv.stop()


# ======================================================================== half duplex
def phase_half(a):
    # receiver master: ARM during a reception -> NOT READY; START refused; READY at NO CARRIER
    srv = Server(a.server, a.work, "halfrx", ["--duplex", "half-rx"])
    try:
        d = TcpDte(srv.tcp)
        handshake(d)
        msg = payload_bytes(300, 3)
        send_message(d, msg)
        wait_tx_state(d, 1, 30)
        srv.feed(1)
        p = d.get(30, lambda p: is_cmd(0x08)(p) and payload(p)[1] == 1)
        d.send(tcp_packet(0x00, b"\x01"))
        p, seen = wait_tx_state(d, 2, 5)
        check(p is not None, "A.5.1.2.7.2, Table A-V", f"half-duplex receiver master: ARM while receiving -> QUEUES_ARMED_AND_PORT_NOT_READY (2), {seen}")
        d.send(tcp_packet(0x00, b"\x02"))
        p, seen = wait_tx_state(d, 2, 3)
        check(p is not None and seen == [2], "A.5.1.2.7.2", "TRANSMIT_START while not ready: another status PORT_NOT_READY")
        cds, data = collect_rx(d, 30)
        got = b"".join(payload(x)[14:] for x in data)
        p, seen = wait_tx_state(d, 3, 5)
        check(p is not None and got == msg, "A.5.1.2.7.2",
              f"reception completes ({len(got)} bytes, equal: {got == msg}); at NO CARRIER the status turns PORT_READY (3)")
        d.close()
    finally:
        srv.stop()
    # transmitter master: STARTED aborts the reception
    srv = Server(a.server, a.work, "halftx", ["--duplex", "half-tx"])
    try:
        d = TcpDte(srv.tcp)
        handshake(d)
        msg = payload_bytes(300, 4)
        send_message(d, msg)
        wait_tx_state(d, 1, 30)
        srv.feed(1)
        p = d.get(30, lambda p: is_cmd(0x00)(p) and payload(p)[1] == 1)
        d.send(tcp_packet(0x00, b"\x01"))
        p, seen = wait_tx_state(d, 3, 5)
        check(p is not None, "A.5.1.2.7.1", f"half-duplex transmitter master: ARM during a reception -> READY (3), {seen}")
        d.send(data_transfer(2, bytes(12), b"hello"))
        d.send(tcp_packet(0x00, b"\x02"))
        cds, rest = collect_rx(d, 10)
        last_empty = rest and payload(rest[-1])[1] == 4 and len(payload(rest[-1])) == 14
        check(bool(last_empty) and cds and payload(cds[-1])[1] == 0, "A.5.1.2.7.1",
              "STARTED aborts the reception: a LAST data packet, then Carrier Detect NO CARRIER")
        p, seen = wait_tx_state(d, 1, 20)
        check(p is not None and os.path.exists(srv.wav_n(2)), "A.5.1.2.7.1", "the transmission completes (FLUSHED, WAV 2 written)")
        d.close()
    finally:
        srv.stop()


# ======================================================================== UDP phase
def udp_packet(ptype, session, pl=b""):
    h = bytes([0x10 | ptype]) + struct.pack(">I", session)[1:]
    b = h + pl
    return b + struct.pack(">H", crc16(b))


def udp_ok(b):
    return len(b) >= 6 and struct.unpack(">H", b[-2:])[0] == crc16(b[:-2]) and b[0] >> 4 == 1


def status_fields(b):
    pl = b[4:-2]
    seq, tid, res, err, sv, drop, jit, fill, space = struct.unpack(">HHHBBHHII", pl[:20])
    rx_rate = int.from_bytes(pl[20:23], "big")
    rx_bf = int.from_bytes(pl[23:26], "big")
    tx_rate = int.from_bytes(pl[26:29], "big")
    tx_bf = int.from_bytes(pl[29:32], "big")
    return dict(seq=seq, tid=tid, res=res, err=err, sv=sv, rxs=(sv >> 5) & 3, cd=(sv >> 4) & 1, txs=(sv >> 1) & 7,
                ctl=sv & 1, drop=drop, jit=jit, fill=fill, space=space, rx_rate=rx_rate, rx_bf=rx_bf,
                tx_rate=tx_rate, tx_bf=tx_bf, len=len(pl))


class UdpDte:
    def __init__(self, srv, session):
        self.srv = srv
        self.session = session
        self.u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.u.bind(("127.0.0.1", 0))
        self.q = queue.Queue()
        self.all = []
        self.ping_requests = 0
        self.auto_reply = True
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        while True:
            try:
                b, frm = self.u.recvfrom(4096)
            except OSError:
                return
            self.all.append((time.time(), b))
            if udp_ok(b) and (b[0] & 15) == 1:
                self.ping_requests += 1
                if self.auto_reply:
                    self.send(udp_packet(2, self.session))  # A.5.2.6.2
                continue
            self.q.put(b)

    def send(self, b):
        self.u.sendto(b, ("127.0.0.1", self.srv.udp))

    def get(self, timeout, want=None):
        end = time.time() + timeout
        while True:
            left = end - time.time()
            if left <= 0:
                return None
            try:
                b = self.q.get(timeout=left)
            except queue.Empty:
                return None
            if want is None or want(b):
                return b

    def drain(self):
        while not self.q.empty():
            self.q.get()


def ptype(b):
    return b[0] & 15


def phase_udp(a):
    srv = Server(a.server, a.work, "udp")
    try:
        rci = socket.create_connection(("127.0.0.1", srv.rci), timeout=5)
        rf = rci.makefile("rwb", buffering=0)
        expect_rejected(srv.tcp, "A.4.1", "a TCP data socket DTE while a UDP DTE holds the control connection")
        sess = 0x5A17C3
        u = UdpDte(srv, sess)
        rf.write(f"UDP {u.u.getsockname()[1]}\n".encode())
        line = rf.readline().decode().strip()
        check(line == f"OK UDP {srv.udp}", "A.5.2.1", f"control connection gives the modem the DTE's UDP port ({line})")
        st = u.get(2, lambda b: ptype(b) == 4)
        f = status_fields(st) if st else {}
        check(st is not None and udp_ok(st) and len(st) == 4 + 32 + 2 and f["res"] == 0, "A.5.2.2, A.5.2.3, Table A-X, Figure A-13",
              f"STATUS REPLY: version 1, type 4, 32-byte payload, reserved 0, CRC over header and payload ({st.hex() if st else None})")
        check(f.get("tx_rate") == 600 and f.get("tx_bf") == 360 and f.get("txs") == 0 and f.get("ctl") == 0 and f.get("cd") == 0,
              "A.5.2.4.1, Figure A-15, Tables A-XIII, A-XIV",
              f"status: OTA transmit 600 bit/s, blocking factor 360, transmitter FLUSHED, not in control, carrier 0 ({f})")
        t = [time.time()]
        seqs = [f.get("seq")]
        for _ in range(4):
            b = u.get(1.5, lambda b: ptype(b) == 4)
            if b:
                t.append(time.time())
                seqs.append(status_fields(b)["seq"])
        gaps = [round(t[i + 1] - t[i], 2) for i in range(len(t) - 1)]
        check(len(gaps) == 4 and max(gaps) <= 0.65 and all((seqs[i + 1] - seqs[i]) % 65536 == 1 for i in range(4)),
              "A.5.2.4.1", f"status every 500 ms ({gaps}) with the sequence number incremented by 1 ({seqs})")

        # data before CONTROL
        n, k = 6, 2
        per = per_packet(360, k)
        hdr = struct.pack(">HHBBBBHHI", 1, 0, 1, 0x11, n, k, per, 0, 0)
        u.send(udp_packet(5, sess, hdr + bytes(per)))
        b = u.get(2, lambda b: ptype(b) == 4 and status_fields(b)["err"] & 0x10)
        check(b is not None, "Table A-XI bit 4", "ENCODED DATA before CONTROL: status error vector NOT IN CONTROL set")
        rf.write(b"CONTROL\n")
        check(rf.readline().strip() == b"OK CONTROL", "A.5.2.1", "control mode requested on the control connection")
        b = u.get(2, lambda b: ptype(b) == 4 and status_fields(b)["ctl"] == 1)
        check(b is not None, "Table A-XII bit 0", "status In Control bit set")

        # ping, status request
        u.drain()
        u.send(udp_packet(1, sess))
        b = u.get(2, lambda b: ptype(b) == 2)
        check(b == udp_packet(2, sess), "A.5.2.6.2, Table A-X", f"PING REQUEST -> PING REPLY, 0 bytes, our session id ({b.hex() if b else None})")
        u.drain()
        u.send(udp_packet(3, sess))
        b = u.get(0.3, lambda b: ptype(b) == 4)
        check(b is not None, "Table A-X type 3", "STATUS REQUEST -> STATUS REPLY at once")
        bad = bytearray(udp_packet(1, sess))
        bad[-1] ^= 1
        u.drain()
        u.send(bytes(bad))
        v2 = bytes([0x21]) + struct.pack(">I", sess)[1:]
        u.send(v2 + struct.pack(">H", crc16(v2)))
        b = u.get(1.5, lambda b: ptype(b) == 2)
        check(b is None, "A.5.2.6.3, A.5.2.3", "PING REQUEST with a bad CRC, and one with version 2: both ignored")
        t0 = time.time()
        while u.ping_requests == 0 and time.time() - t0 < 6:
            time.sleep(0.1)
        check(u.ping_requests >= 1, "A.5.2.6.2", "the modem sends PING REQUEST every 5 s (the DTE replies)")

        # a stranger's data is ignored
        stranger = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        stranger.sendto(udp_packet(5, sess, struct.pack(">HHBBBBHHI", 9, 0, 1, 0x11, n, k, per, 0, 0) + bytes(per)),
                        ("127.0.0.1", srv.udp))
        stranger.close()
        u.drain()
        b = u.get(1.5, lambda b: ptype(b) == 4 and status_fields(b)["txs"] != 0)
        check(b is None, "A.5.2.6.1", "ENCODED DATA from another address: ignored, transmitter stays FLUSHED")

        # transmit a message with lost packets
        msg = payload_bytes(200, 5)
        sets = fec_sets(msg, n, k, per)
        tid = 0x1234
        seq = 0
        dropped = 0
        u.drain()
        for si, s in enumerate(sets):
            for j, pk in enumerate(s):
                cv = (1 if si == 0 else 0) | (2 if si == len(sets) - 1 else 0)
                h = struct.pack(">HHBBBBHHI", tid, seq, cv, 0x11, n, k, len(pk), 0,
                                ((int(time.time()) % 3600) * 1000000 + int(time.time() * 1e6) % 1000000) & 0xFFFFFFFF)
                seq += 1
                if j in (1, 4) and si % 2 == 0:  # two of six lost in every other set (N-K = 4)
                    dropped += 1
                    continue
                u.send(udp_packet(5, sess, h + pk))
            time.sleep(0.02)
        states = []
        end = time.time() + 20
        last = None
        while time.time() < end:
            b = u.get(end - time.time(), lambda b: ptype(b) == 4)
            if b is None:
                break
            last = status_fields(b)
            if not states or states[-1] != last["txs"]:
                states.append(last["txs"])
            if states[-1] == 0 and len(states) > 1:
                break
        check(states[:1] in ([1], [2], [3]) and states[-1] == 0 and 3 in states, "A.5.2.6.5, Figure A-23, Table A-XIV",
              f"transmitter states {states}: QUEUEING (1) / STARTED (2) / DRAINING (3), then FLUSHED (0)")
        check(last is not None and last["tid"] == tid and last["drop"] == dropped, "A.5.2.4.1",
              f"status Current Transfer ID {last['tid'] if last else None:#x} = {tid:#x}, Packets Dropped {last['drop'] if last else None} = {dropped}")
        check(wait_file(srv.wav_n(1)) and wav_ok(srv.wav_n(1))[0], "A.5.2.5, brief",
              f"transfer with {dropped} lost packets recovered by the FEC and transmitted to WAV 1")

        # the WAV back: data arrives as ENHANCED / ENCODED DATA
        u.drain()
        srv.feed(1)
        pkts = []
        cd_seen = False
        end = time.time() + 30
        while time.time() < end:
            b = u.get(end - time.time())
            if b is None:
                break
            if ptype(b) == 4:
                sf = status_fields(b)
                if sf["cd"]:
                    cd_seen = cd_seen or (sf["rx_rate"] == 600 and sf["rx_bf"] == 360 and sf["rxs"] == 1)
                elif pkts and sf["rxs"] == 0:
                    break
            elif ptype(b) in (5, 6):
                pkts.append(b)
        check(cd_seen, "A.5.2.6.6 step 2, Figure A-13", "status with carrier detect 1, receiver STARTED, OTA receive 600 bit/s, blocking factor 360")
        ok_frames = all(udp_ok(b) for b in pkts)
        heads = []
        for b in pkts:
            pl = b[4:-2]
            if ptype(b) == 6:
                tid_, sq, cv, ft, nn, kk, ln = struct.unpack(">HHBBBBH", pl[:10])
                rate = int.from_bytes(pl[10:13], "big")
                bf = int.from_bytes(pl[13:16], "big")
                data = pl[20:]
            else:
                tid_, sq, cv, ft, nn, kk, ln, res = struct.unpack(">HHBBBBHH", pl[:12])
                rate = bf = None
                data = pl[16:]
            heads.append(dict(t=ptype(b), tid=tid_, seq=sq, cv=cv, ft=ft, n=nn, k=kk, len=ln, rate=rate, bf=bf, data=data,
                              res=None if ptype(b) == 6 else res))
        nsets = len(heads) // 6 if heads else 0
        first_set = heads[:6]
        check(len(heads) > 0 and len(heads) % 6 == 0 and all(h["t"] == 6 and h["cv"] & 1 and h["rate"] == 600 and h["bf"] == 360
                                                          for h in first_set),
              "A.5.2.4.3, A.5.2.6.6 step 3, Figure A-18",
              f"first interleaver set: 6 ENHANCED ENCODED DATA packets, first-set flag, data bit rate 600, blocking factor 360 ({len(heads)} packets)")
        check(all(h["t"] == 5 and not h["cv"] & 1 and h["res"] == 0 for h in heads[6:]), "A.5.2.4.2, Figure A-16",
              "later sets: ENCODED DATA, first-set flag clear, reserved 0")
        check(all(h["cv"] & 2 for h in heads[-6:]) and not any(h["cv"] & 2 for h in heads[:-6]) and all(h["cv"] & 0xF8 == 0 for h in heads),
              "Figure A-17, Table A-XV", "last-set flag on the last set only, reserved control bits 0")
        check(ok_frames and all(h["ft"] == 0x11 and h["n"] == 6 and h["k"] == 2 and h["len"] == len(h["data"]) == per for h in heads),
              "A.5.2.4.2, Table A-XVII, Figure A-22",
              f"FEC type 1, interleaver type 1, N 6, K 2, Data Length {per} = L/N = ceil(360/16), CRC good")
        check([h["seq"] for h in heads] == list(range(len(heads))) and len({h["tid"] for h in heads}) == 1, "A.5.2.4.2",
              "packet sequence numbers 0, 1, 2 ... in one transfer id")
        stream = b""
        parity_ok = True
        for s in range(nsets):
            user, ok = unfec_set([h["data"] for h in heads[s * 6:(s + 1) * 6]], 6, 2)
            parity_ok &= ok
            stream += user
        check(parity_ok, "A.5.2.5, Table A-XVIII", "every RS codeword's parity matches the DTE's own encoder (printed g(x))")
        got = strip_eom(stream)
        check(got == msg, "A.5.2.5.2, brief: loopback", f"EOM and padding removed: the 200 bytes come back ({len(got) if got else None} bytes)")

        # command request / ack and abort
        msg2 = payload_bytes(500, 6)
        sets = fec_sets(msg2, n, k, per)
        tid2 = 0x1235
        seq = 0
        for si, s in enumerate(sets):
            for pk in s:
                cv = (1 if si == 0 else 0) | (2 if si == len(sets) - 1 else 0)
                u.send(udp_packet(5, sess, struct.pack(">HHBBBBHHI", tid2, seq, cv, 0x11, n, k, len(pk), 0, 0) + pk))
                seq += 1
        check(wait_file(srv.wav_n(2)), "A.5.2.6.5", "second transfer (500 bytes) transmitted")
        time.sleep(0.5)
        u.drain()
        srv.feed(2)
        b = u.get(30, lambda b: ptype(b) in (5, 6))
        rx_tid = None
        if b:
            rx_tid = struct.unpack(">H", b[4:6])[0]
        u.send(udp_packet(7, sess, struct.pack(">HBB", 0, 9, 0)))  # unsupported command
        ack = u.get(1.0, lambda b: ptype(b) == 8)
        check(ack is None, "A.5.2.4.4", "Modem Command Request with command 9: ignored, no ack")
        time.sleep(0.7)
        u.send(udp_packet(7, sess, struct.pack(">HBB", 0, 1, 0)))  # ABORT RECEPTION
        pk_after = []
        ack = None
        end = time.time() + 5
        while time.time() < end:
            b = u.get(end - time.time())
            if b is None:
                break
            if ptype(b) == 8:
                ack = b
            elif ptype(b) in (5, 6):
                pk_after.append(b)
            if ack is not None and len([x for x in pk_after if x[8] & 2]) >= 6:
                break
        check(ack == udp_packet(8, sess, struct.pack(">HH", 0, 0)), "A.5.2.4.5, Figure A-20, A.5.2.6.7",
              f"ABORT RECEPTION acked: sequence 0, reserved 0 ({ack.hex() if ack else None})")
        lastset = [x for x in pk_after if x[8] & 2]
        check(len(lastset) == 6, "Table A-XVI", f"abort: the modem issues the last set (LAST flag) at once ({len(pk_after)} packets after the request)")
        u.send(udp_packet(7, sess, struct.pack(">HBB", 0, 1, 0)))
        ack2 = u.get(2, lambda b: ptype(b) == 8)
        check(ack2 == ack, "A.5.2.4.4", "the same request again (a retransmission): acked again with the same sequence number")
        b = u.get(3, lambda b: ptype(b) in (5, 6))
        check(b is None, "A.5.2.6.6", "no data after the aborted transfer")
        rf.close()
        rci.close()
        time.sleep(0.5)
        d = TcpDte(srv.tcp)
        pk = handshake(d)
        check(pk[0] is not None, "A.4.1", "after the control connection closes, a TCP DTE is accepted again")
        d.close()
    finally:
        srv.stop()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--phases", default="tcp,half,udp")
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    for ph in a.phases.split(","):
        print(f"--- {ph}", flush=True)
        {"tcp": phase_tcp, "half": phase_half, "udp": phase_udp}[ph](a)
    print(f"\n{PASSED}/{PASSED + FAILED} Appendix A DTE checks passed")
    return 0 if FAILED == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
