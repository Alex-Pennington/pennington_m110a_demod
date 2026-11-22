"""MELPe frame handling and a speech measure for the Harris MELP test set.

Pure Python and numpy. Nothing here runs a coder; score.py drives the external MELPe
program and uses this module for the bit handling and the measure.

Frame sizes (STANAG 4591 Ed. 1 s5.5 and Annexes D and M; RFC 8130 s3.1):
  2400 bit/s: 54 bits per 22.5 ms frame, sync bit = bit 54, alternating 0/1
  1200 bit/s: 81 bits per 67.5 ms frame, sync bit = bit 1, alternating 0/1
   600 bit/s: 54 bits per 90 ms frame, no sync bit

Bit numbering: bit 1 (B_01) is transmitted first (STANAG 4591 s5.5.3). A frame here is a
uint8 array of 0/1 values, index 0 = B_01.

Coder byte packing (measured against the reference coder, -b 54 density):
  2400, 1200: B_k sits in byte (k-1)//8, bit (k-1)%8 counted from the LSB (RFC 8130
              Figures 2 and 3). 7 and 11 bytes; unused high bits are 0.
  600:        B_k sits in byte (k-1)//8, bit 7-(k-1)%8 counted from the LSB. 7 bytes;
              the two low bits of byte 6 are 0. This differs from RFC 8130 Figure 4,
              which places B_01 in the LSB.
"""
import numpy as np

FS = 8000

RATES = {
    #      bits  samples  bytes  sync bit index (0-based) or None   byte packing
    2400: (54, 180, 7, 53, "lsb"),
    1200: (81, 540, 11, 0, "lsb"),
    600: (54, 720, 7, None, "msb"),
}


def frame_bits(rate):
    return RATES[rate][0]


def frame_samples(rate):
    return RATES[rate][1]


def frame_bytes(rate):
    return RATES[rate][2]


def sync_index(rate):
    return RATES[rate][3]


# ---- bit streams -------------------------------------------------------------------

def bytes_to_bits(data, order="msb"):
    """Byte string -> 0/1 array. order 'msb': bit 7 of each byte first; 'lsb': bit 0 first."""
    a = np.frombuffer(bytes(data), dtype=np.uint8)
    return np.unpackbits(a, bitorder="big" if order == "msb" else "little")


def bits_to_bytes(bits, order="msb"):
    b = np.asarray(bits, dtype=np.uint8)
    return np.packbits(b, bitorder="big" if order == "msb" else "little").tobytes()


def cut_frames(bits, rate, offset=0, reverse=False, invert=False):
    """Cut a bit stream into frames of the rate's size, starting at offset.

    reverse: the transmitted order within a frame is B_N first (bits are reversed).
    invert:  every bit is complemented.
    Returns an (n_frames, n_bits) uint8 array; a partial last frame is dropped.
    """
    n = frame_bits(rate)
    b = np.asarray(bits, dtype=np.uint8)[offset:]
    k = len(b) // n
    f = b[: k * n].reshape(k, n).copy()
    if reverse:
        f = f[:, ::-1].copy()
    if invert:
        f ^= 1
    return f


def pack_frames(frames, rate, packing=None):
    """Frames (n, n_bits) of B_01.. order -> the reference coder's -b 54 byte stream.

    packing None: the rate's packing as measured from the coder ("lsb" puts B_01 in the
    LSB of byte 0, as RFC 8130; "msb" in the MSB). Pass the other one to test it."""
    n_bits, _, n_bytes, _, default = RATES[rate]
    packing = packing or default
    f = np.asarray(frames, dtype=np.uint8)
    if f.ndim != 2 or f.shape[1] != n_bits:
        raise ValueError(f"frames must be (n, {n_bits}) for {rate} bit/s")
    pad = np.zeros((f.shape[0], n_bytes * 8 - n_bits), dtype=np.uint8)
    full = np.concatenate([f, pad], axis=1)
    return np.packbits(full, axis=1, bitorder="little" if packing == "lsb" else "big").tobytes()


def unpack_frames(data, rate, packing=None):
    """The reference coder's -b 54 byte stream -> frames (n, n_bits) of B_01.. order."""
    n_bits, _, n_bytes, _, default = RATES[rate]
    packing = packing or default
    a = np.frombuffer(bytes(data), dtype=np.uint8)
    a = a[: len(a) // n_bytes * n_bytes].reshape(-1, n_bytes)
    full = np.unpackbits(a, axis=1, bitorder="little" if packing == "lsb" else "big")
    return full[:, :n_bits].copy()


def sync_score(frames, rate):
    """Share of adjacent frame pairs whose sync bit alternates; None at 600 bit/s."""
    i = sync_index(rate)
    if i is None or len(frames) < 2:
        return None
    s = np.asarray(frames)[:, i]
    return float((np.diff(s.astype(np.int8)) != 0).mean())


def find_sync(bits, rate, min_frames=8, index=None):
    """Best offset 0..n_bits-1 by alternation of the bit at `index` within each frame
    (default: the rate's sync bit). Alternation is unchanged by inverting every bit.
    Returns (offset, score) or (None, None)."""
    i = sync_index(rate) if index is None else index
    if i is None:
        return None, None
    best = (None, -1.0)
    for off in range(frame_bits(rate)):
        f = cut_frames(bits, rate, off)
        if len(f) < min_frames:
            continue
        s = float((np.diff(f[:, i].astype(np.int8)) != 0).mean())
        if s > best[1]:
            best = (off, s)
    return best if best[0] is not None else (None, None)


def period_z(bits, period):
    """How strongly a bit stream repeats with the given period, whatever the phase.

    Cut into rows of `period` bits, a framed stream's column means stray from the stream's
    own share of ones at biased positions (codebook MSBs, voicing, sync). Returns
    z = (sum_c n(m_c - p)^2 / (p(1-p)) - (period-1)) / sqrt(2(period-1)), about N(0, 1)
    for a stream with no structure at that period; NaN below 4 rows."""
    b = np.asarray(bits, dtype=np.float64)
    n = len(b) // period
    if n < 4 or period < 2:
        return float("nan")
    x = b[: n * period]
    p = x.mean()
    if p <= 0 or p >= 1:
        return float("nan")
    m = x.reshape(n, period).mean(axis=0)
    s = (n * (m - p) ** 2 / (p * (1 - p))).sum()
    return float((s - (period - 1)) / np.sqrt(2 * (period - 1)))


PERIOD_MIN, PERIOD_MAX = 4, 130


def period_check(streams, rate, skip=0):
    """Pooled frame-period evidence over streams of one mode.

    Returns (best period, its pooled z, pooled z at the rate's frame size, matches). It does
    not match when the rate's frame size shows no structure (z < 3) while another period,
    not a multiple of it, shows strong structure (z >= 6)."""
    n = frame_bits(rate)

    def pooled(P):
        v = [period_z(np.asarray(s)[skip:], P) for s in streams]
        v = [t for t in v if t == t]
        return sum(v) / np.sqrt(len(v)) if v else float("nan")

    zs = {P: pooled(P) for P in range(PERIOD_MIN, PERIOD_MAX + 1)}
    zs = {P: v for P, v in zs.items() if v == v}
    if not zs:
        return None, float("nan"), float("nan"), True
    best = max(zs, key=zs.get)
    zn = zs.get(n, float("nan"))
    mismatch = (zn < 3) and zs[best] >= 6 and best % n != 0
    return best, zs[best], zn, not mismatch


def random_frames(rate, n, seed=1):
    rng = np.random.default_rng(seed)
    return rng.integers(0, 2, size=(n, frame_bits(rate)), dtype=np.uint8)


# ---- speech measure ----------------------------------------------------------------

HOP = 180          # 22.5 ms analysis hop
WIN = 320          # 40 ms window: two periods at 80 Hz
LAG_MIN = 26       # 8000/26 = 308 Hz
LAG_MAX = 100      # 8000/100 = 80 Hz
VOICED_R = 0.5     # normalised autocorrelation peak for a voiced frame
TRACK_LN = 0.1     # |ln(f0/f0_prev)| below this counts as a continuing pitch track (~10 %)
ACTIVE_DB = -40.0  # frame energy within this of the loudest frames counts as active


def speech_measure(x):
    """Speech measure on 8 kHz audio (int16 or float array).

    Per 22.5 ms frame: normalised autocorrelation over lags for 80-308 Hz; voiced when the
    peak exceeds VOICED_R and the frame is active. Returns a dict:
      frames      analysis frames
      voiced      share of frames voiced with pitch in 80-308 Hz
      track       share of frames voiced whose pitch is within ~10 % of the previous
                  voiced frame's (a continuing pitch contour: speech has one, a pitch
                  drawn at random per frame does not)
      f0_median   median pitch of voiced frames, Hz (0 when none)
    """
    x = np.asarray(x, dtype=np.float64)
    n = (len(x) - WIN) // HOP + 1
    if n < 2:
        return {"frames": max(n, 0), "voiced": 0.0, "track": 0.0, "f0_median": 0.0}
    idx = np.arange(WIN)[None, :] + HOP * np.arange(n)[:, None]
    fr = x[idx]
    fr = fr - fr.mean(axis=1, keepdims=True)
    energy = (fr ** 2).mean(axis=1)
    ref = np.percentile(energy, 95)
    active = energy > max(ref * 10 ** (ACTIVE_DB / 10), 1e-3)
    seg = WIN - LAG_MAX
    a = fr[:, :seg]
    ea = (a ** 2).sum(axis=1)
    rmax = np.zeros(n)
    lag = np.zeros(n, dtype=int)
    for L in range(LAG_MIN, LAG_MAX + 1):
        b = fr[:, L:L + seg]
        r = (a * b).sum(axis=1) / np.sqrt(ea * (b ** 2).sum(axis=1) + 1e-12)
        better = r > rmax
        rmax[better] = r[better]
        lag[better] = L
    voiced = active & (rmax > VOICED_R)
    f0 = np.where(voiced, FS / np.maximum(lag, 1), 0.0)
    track = np.zeros(n, dtype=bool)
    track[1:] = voiced[1:] & voiced[:-1] & (
        np.abs(np.log(np.maximum(f0[1:], 1) / np.maximum(f0[:-1], 1))) < TRACK_LN)
    return {
        "frames": int(n),
        "voiced": float(voiced.mean()),
        "track": float(track.mean()),
        "f0_median": float(np.median(f0[voiced])) if voiced.any() else 0.0,
    }


# ---- WAV / raw I/O -----------------------------------------------------------------

def read_raw(path):
    return np.fromfile(path, dtype="<i2")


def write_wav(path, x, fs=FS):
    import wave
    x = np.asarray(x, dtype="<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(fs)
        w.writeframes(x.tobytes())
