"""Field structure inside the 600H 60-bit block.

Uses only whole vocoder blocks: from bit 54 (after the header) up to the first copy of the
end-of-voice trailer, in the six 600_x streams (melp600 separately: no trailer).

Prints per column:
  p1      share of ones
  ac1     correlation of the column with itself one block later (slow fields: pitch, gain)
  adj     |phi| with the next column inside the block (bits of one field depend on each other)
and the matrix of strongest within-block column dependences.

Also tests the byte-reversed reading of the stream (vocoder bytes sent MSB first).

python3 -I -B test/harris/harris600/structure.py PAYLOAD_DIR
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import H600, load_all
import melpe_frames as mf
from edges import TRAILER


def voice_blocks(bits, start=54, size=60):
    t = "".join(map(str, bits))
    end = t.find(TRAILER)
    if end < 0:
        end = len(t)
    n = (end - start) // size
    return np.asarray(bits[start:start + n * size]).reshape(n, size)


def phi(a, b):
    a = a.astype(float); b = b.astype(float)
    if a.std() == 0 or b.std() == 0:
        return 0.0
    return float(np.corrcoef(a, b)[0, 1])


def main(pd):
    st = load_all(pd, H600)
    six = [s for s in H600 if s != "melp600"]
    per = {s: voice_blocks(st[s]) for s in six}
    for s in six:
        print("%-20s %3d whole blocks (%.1f s)" % (s, len(per[s]), len(per[s]) * 0.1))
    R = np.concatenate([per[s] for s in six])
    # lag-1 autocorrelation within each message, pooled
    a1 = np.zeros(60)
    for c in range(60):
        x = np.concatenate([per[s][:-1, c] for s in six]).astype(float)
        y = np.concatenate([per[s][1:, c] for s in six]).astype(float)
        a1[c] = np.corrcoef(x, y)[0, 1]
    n = len(R)
    print("blocks %d; |r| above %.2f is ~3 sigma" % (n, 3 / np.sqrt(n)))
    print("col   p1    ac1   adj(c,c+1)")
    for c in range(60):
        adj = phi(R[:, c], R[:, c + 1]) if c < 59 else float("nan")
        print("%3d  %.2f  %+.2f  %+.2f %s%s" % (c, R[:, c].mean(), a1[c], adj,
              "A" if abs(a1[c]) > 3 / np.sqrt(n) else " ", "D" if abs(adj) > 3 / np.sqrt(n) else ""))
    C = np.corrcoef(R.T.astype(float))
    np.fill_diagonal(C, 0)
    pairs = sorted(((abs(C[i, j]), i, j) for i in range(60) for j in range(i + 1, 60)), reverse=True)
    print("strongest within-block dependences |r| (i, j):")
    print("  " + ", ".join("%d-%d %.2f" % (i, j, v) for v, i, j in pairs[:30]))

    # byte-reversed reading
    def rev(b):
        n8 = len(b) // 8 * 8
        return b[:n8].reshape(-1, 8)[:, ::-1].reshape(-1)
    for name, f in (("as sent", lambda b: b), ("bytes reversed", rev)):
        z = {P: sum(mf.period_z(f(st[s])[54:], P) for s in six) / np.sqrt(6) for P in (20, 54, 60, 120)}
        print("%-15s pooled z: " % name + "  ".join("P%d %.1f" % kv for kv in z.items()))


if __name__ == "__main__":
    main(sys.argv[1])
