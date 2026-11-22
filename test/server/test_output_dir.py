#!/usr/bin/env python3
"""m110a_server creates its --output-dir, and TX -> RXAUDIOINJECT works through it.

Usage: test_output_dir.py <m110a_server> <work dir>

1. Start the server with --output-dir <work>/out/a/b (no trailing slash, none
   of it exists). For every mode the server and ModemTX both take (150S to
   2400L), send a payload on the data port, SENDBUFFER, inject the PCM file the
   server reports, and require exactly the payload back on the data port, with
   no byte after it.
2. Start the server with an output dir under a regular file and require it to
   exit non-zero with "Cannot create output directory".
"""
import os
import shutil
import socket
import subprocess
import sys
import time

PAYLOAD = b"OUTPUT DIR TEST 0123456789"
MODES = ("150S", "150L", "300S", "300L", "600S", "600L", "1200S", "1200L", "2400S", "2400L")


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def connect(port, deadline):
    while True:
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=2)
        except OSError:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.05)


class Lines:
    def __init__(self, sock):
        self.sock, self.buf = sock, b""

    def until(self, prefix, timeout=30):
        """Return the first line starting with prefix; fail on an ERROR line."""
        end = time.monotonic() + timeout
        while True:
            while b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                text = line.decode(errors="replace").strip()
                if text.startswith("ERROR"):
                    sys.exit(f"FAIL: server said {text!r}")
                if text.startswith(prefix):
                    return text
            if time.monotonic() > end:
                sys.exit(f"FAIL: no {prefix!r} line within {timeout}s")
            self.sock.settimeout(max(0.1, end - time.monotonic()))
            chunk = self.sock.recv(4096)
            if not chunk:
                sys.exit(f"FAIL: control connection closed waiting for {prefix!r}")
            self.buf += chunk


def one_mode(ctrl, data, lines, out, mode):
    ctrl.sendall(f"CMD:DATA RATE:{mode}\n".encode())
    lines.until("OK:DATA RATE")
    data.sendall(PAYLOAD)
    time.sleep(0.3)  # the server polls every 10 ms; let it buffer the bytes
    ctrl.sendall(b"CMD:SENDBUFFER\n")
    sent = lines.until("OK:SENDBUFFER")
    if " FILE:" not in sent:
        sys.exit(f"FAIL {mode}: SENDBUFFER reported no file: {sent!r}")
    pcm = sent.split(" FILE:", 1)[1]
    if os.path.dirname(os.path.abspath(pcm)) != os.path.abspath(out):
        sys.exit(f"FAIL {mode}: PCM {pcm!r} not inside {out!r}")
    if not os.path.isfile(pcm) or os.path.getsize(pcm) == 0:
        sys.exit(f"FAIL {mode}: PCM {pcm!r} missing or empty")

    ctrl.sendall(f"CMD:RXAUDIOINJECT:{pcm}\n".encode())
    lines.until("OK:RXAUDIOINJECT:STARTED")
    lines.until("OK:RXAUDIOINJECT:COMPLETE", timeout=120)

    # read until 1 s passes with nothing more, so a byte after the payload shows
    got = b""
    data.settimeout(1)
    try:
        while True:
            chunk = data.recv(4096)
            if not chunk:
                break
            got += chunk
    except socket.timeout:
        pass
    if got != PAYLOAD:
        sys.exit(f"FAIL {mode}: decoded {got!r}, sent {PAYLOAD!r}")
    os.remove(pcm)
    print(f"PASS {mode}: TX -> {os.path.basename(pcm)} -> inject decoded {len(got)} bytes")


def roundtrip(server, work):
    out = os.path.join(work, "out", "a", "b")
    cport, dport = free_port(), free_port()
    proc = subprocess.Popen([server, "--quiet", "--control-port", str(cport),
                             "--data-port", str(dport), "--output-dir", out])
    try:
        deadline = time.monotonic() + 10
        ctrl = connect(cport, deadline)
        data = connect(dport, deadline)
        lines = Lines(ctrl)
        lines.until("MODEM READY")
        if not os.path.isdir(out):
            sys.exit(f"FAIL: {out} not created at startup")

        for mode in MODES:
            one_mode(ctrl, data, lines, out, mode)
        ctrl.close()
        data.close()
        print(f"PASS: created {out}; TX -> inject decoded the payload exactly in {', '.join(MODES)}")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


def uncreatable(server, work):
    blocker = os.path.join(work, "not_a_dir")
    with open(blocker, "w") as f:
        f.write("x")
    r = subprocess.run([server, "--quiet", "--control-port", str(free_port()),
                        "--data-port", str(free_port()),
                        "--output-dir", os.path.join(blocker, "sub")],
                       capture_output=True, text=True, timeout=10)
    if r.returncode == 0 or "Cannot create output directory" not in r.stderr:
        sys.exit(f"FAIL: uncreatable dir: rc={r.returncode} stderr={r.stderr!r}")
    print("PASS: uncreatable output dir refused at startup")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    server, work = sys.argv[1], sys.argv[2]
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    roundtrip(server, work)
    uncreatable(server, work)
    shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
