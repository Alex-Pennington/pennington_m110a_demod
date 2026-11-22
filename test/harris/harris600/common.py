"""Shared helpers for the Harris 600H format analysis (test/harris/harris600/).

Reads the payload bit files written by the modem stage (<slug>.bits, ASCII 0/1 in stream
order). Pure Python and numpy. Nothing here runs a coder.
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))  # test/harris, for melpe_frames

HEADER = "011111011010100110011010100100000100111110000011110010"
H600 = ["600_1", "600_2", "600_3_multiples_m1", "600_3_multiples_m2",
        "600_3_multiples_m3", "600_3_multiples_m4", "melp600"]
H1200 = ["1200_1", "1200_2", "1200_3"]
V2400 = ["melp2400"]
ALL = H600 + H1200 + V2400


def load_bits(payload_dir, slug):
    with open(os.path.join(payload_dir, slug + ".bits")) as f:
        s = f.read().strip()
    return np.frombuffer(s.encode(), dtype=np.uint8) - ord("0")


def load_all(payload_dir, slugs=ALL):
    return {s: load_bits(payload_dir, s) for s in slugs}


def blocks(bits, start, size):
    """Cut bits[start:] into rows of `size`; partial last row dropped."""
    b = np.asarray(bits)[start:]
    n = len(b) // size
    return b[: n * size].reshape(n, size)


def field_value(rows, cols):
    """Integer value of the bits at `cols` (first column = MSB) in each row."""
    v = np.zeros(len(rows), dtype=np.int64)
    for c in cols:
        v = (v << 1) | rows[:, c].astype(np.int64)
    return v


def column_z(rows):
    """Per-column z of the share of ones against the pooled share."""
    r = np.asarray(rows, dtype=np.float64)
    p = r.mean()
    m = r.mean(axis=0)
    return (m - p) / np.sqrt(p * (1 - p) / len(r)), m
