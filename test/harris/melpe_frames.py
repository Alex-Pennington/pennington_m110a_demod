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


# Measured on all 11 Harris streams (docs/harris-melpe.md, Results): every stream opens
# with this 54-bit header (stream order, LSB-first bytes) and ends with a 108-bit trailer
# on a vocoder-unit boundary. Neither is vocoder data.
HARRIS_HEADER = "011111011010100110011010100100000100111110000011110010"
HARRIS_TRAILER_BITS = 108


def strip_header_trailer(stream_bits):
    """Stream bits (LSB-first order) -> (vocoder bits between header and trailer, True)
    when the stream opens with HARRIS_HEADER; else (the bits unchanged, False)."""
    b = np.asarray(stream_bits, dtype=np.uint8)
    h = np.frombuffer(HARRIS_HEADER.encode(), dtype=np.uint8) - ord("0")
    n = len(h)
    if len(b) > n + HARRIS_TRAILER_BITS and np.array_equal(b[:n], h):
        return b[n:len(b) - HARRIS_TRAILER_BITS].copy(), True
    return b, False


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


MOD_HOP = 80            # 10 ms energy frames
MOD_BAND = (2.0, 8.0)   # syllable-rate modulation, Hz
MOD_SPAN = (0.5, 25.0)  # modulation range the share is taken over, Hz


def modulation(x):
    """Share of the log-energy contour's modulation power at syllable rate.

    The audio's energy in 10 ms frames, in dB (floored 50 dB under the loudest), has its
    mean removed, is Hann-windowed and transformed. Returns the power at 2-8 Hz as a share
    of the power at 0.5-25 Hz. Speech, whose loudness rises and falls with its syllables,
    puts much of it at 2-8 Hz; a steady buzz has little modulation at all, and frames in
    random order spread it toward the frame rate. NaN for under 1 s of audio."""
    x = np.asarray(x, dtype=np.float64)
    n = len(x) // MOD_HOP
    if n * MOD_HOP < FS:
        return float("nan")
    e = (x[: n * MOD_HOP].reshape(n, MOD_HOP) ** 2).mean(axis=1)
    db = 10 * np.log10(np.maximum(e, 1e-3))
    db = np.maximum(db, db.max() - 50)
    v = (db - db.mean()) * np.hanning(n)
    p = np.abs(np.fft.rfft(v)) ** 2
    f = np.fft.rfftfreq(n, MOD_HOP / FS)
    span = (f >= MOD_SPAN[0]) & (f <= MOD_SPAN[1])
    band = (f >= MOD_BAND[0]) & (f <= MOD_BAND[1])
    tot = p[span].sum()
    return float(p[band].sum() / tot) if tot > 0 else 0.0


def energy_contour(x, rate):
    """Decoded audio's energy per MELPe frame of the rate, in dB, floored 50 dB under the
    loudest frame. Returns (r1, depth): the lag-1 autocorrelation of the contour, and its
    standard deviation in dB.

    r1 shows that each frame's loudness carries on from the frame before. depth shows that
    loudness changes at all. NaN for under 8 frames; r1 is 0 when depth is 0."""
    x = np.asarray(x, dtype=np.float64)
    n_s = frame_samples(rate)
    n = len(x) // n_s
    if n < 8:
        return float("nan"), float("nan")
    e = (x[: n * n_s].reshape(n, n_s) ** 2).mean(axis=1)
    db = 10 * np.log10(np.maximum(e, 1e-3))
    db = np.maximum(db, db.max() - 50)
    v = db - db.mean()
    depth = float(v.std())
    den = float((v * v).sum())
    r1 = float((v[1:] * v[:-1]).sum() / den) if den > 0 else 0.0
    return r1, depth


# Sub-frames per MELPe frame (2400: two gains per frame; 1200: three 22.5 ms frames;
# 600: four), and the reference coder's synthesis delay in samples: where a frame boundary
# falls in the decoded audio. The delay was measured as the phase at which boundary steps
# stand out most in shuffled coded speech.
SUBFRAMES = {2400: 2, 1200: 3, 600: 4}
DECODER_PHASE = {2400: 140, 1200: 105, 600: 100}


def boundary_ratio(x, rate, phase=None):
    """Loudness steps across MELPe frame boundaries against steps inside frames.

    The decoded audio's energy in sub-frame blocks, in dB (floored 50 dB under the
    loudest), from the decoder's phase. B = mean |dB step| across frame boundaries / mean
    |dB step| between sub-frames of one frame. In speech decoded in its own order the
    loudness at a boundary carries on as it does inside a frame (B about 1); when frames
    are out of order or their bits are random, it jumps at the boundaries (B well above
    1). NaN under 8 frames."""
    phase = DECODER_PHASE[rate] if phase is None else phase
    k = SUBFRAMES[rate]
    sub = frame_samples(rate) // k
    x = np.asarray(x, dtype=np.float64)[phase:]
    n = len(x) // sub
    if n < 8 * k:
        return float("nan")
    e = (x[: n * sub].reshape(n, sub) ** 2).mean(axis=1)
    db = 10 * np.log10(np.maximum(e, 1e-3))
    db = np.maximum(db, db.max() - 50)
    st = np.abs(np.diff(db))
    at = np.arange(1, n) % k == 0
    w = st[~at].mean()
    return float(st[at].mean() / w) if w > 0 else float("inf")


def shuffled_frames(frames, rate, seed):
    """The frames in a random order, with the sync bit (2400, 1200) set to alternate again
    so the shuffle changes only the order of the frames' content."""
    rng = np.random.default_rng(seed)
    f = np.asarray(frames)[rng.permutation(len(frames))].copy()
    i = sync_index(rate)
    if i is not None and len(f):
        f[:, i] = (np.arange(len(f)) + frames[0, i]) % 2
    return f


# ---- WAV / raw I/O -----------------------------------------------------------------

def read_raw(path):
    return np.fromfile(path, dtype="<i2")


def read_wav_to_end(path):
    """16-bit PCM WAV -> (rate, int16 samples of channel 0), reading the data chunk to the
    end of the file rather than to the length its header declares (some test files declare
    less than they hold)."""
    with open(path, "rb") as f:
        b = f.read()
    if b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a RIFF WAVE file")
    i, rate, nch, bits = 12, None, 1, 16
    while i + 8 <= len(b):
        cid, size = b[i:i + 4], int.from_bytes(b[i + 4:i + 8], "little")
        if cid == b"fmt ":
            nch = int.from_bytes(b[i + 10:i + 12], "little")
            rate = int.from_bytes(b[i + 12:i + 16], "little")
            bits = int.from_bytes(b[i + 22:i + 24], "little")
        elif cid == b"data":
            if bits != 16 or rate is None:
                raise ValueError(f"{path}: expected 16-bit PCM")
            d = b[i + 8:]
            d = d[: len(d) // (2 * nch) * 2 * nch]
            return rate, np.frombuffer(d, dtype="<i2").reshape(-1, nch)[:, 0].copy()
        i += 8 + size + (size & 1)
    raise ValueError(f"{path}: no data chunk")


def write_wav(path, x, fs=FS):
    import wave
    x = np.asarray(x, dtype="<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(fs)
        w.writeframes(x.tobytes())
