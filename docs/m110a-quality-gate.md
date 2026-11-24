# 110A success gate: every interleaver block, one message per preamble

`success=1` from `ModemRX` (and from `m110a_rx`) means the decode is trusted. 110A carries
no CRC, so the verdict comes from the probes. Until this change it read only the first
1440 data symbols (`quality_db`), and on a fading channel a later interleaver block could
be lost while the first scored well. Separately, `m110a_rx` reported every preamble chain
as a message of its own, so one long transmission on a multipath channel came out as
14-16 messages. This page says what changed, how the new thresholds were set and what
they were measured to do.

## What success reads now

`success` needs both:

1. `quality_db >= quality_threshold_db`, as before (probe SNR over the first 1440 data
   symbols; thresholds in `api/modem_rx.cpp`, `quality_threshold_db`).
2. `block_quality_min_db >= block_quality_threshold_db`: the worst interleaver block of
   the message clears the mode's block threshold.

`block_quality_db` has one entry per interleaver block the message occupies: up to the
block holding the EOM, or without an EOM the whole blocks the signal covers. A block's
quality is the **flat-channel SNR with the same expected coded-bit error rate**:

- each frame's probe SNR is read as `quality_db` reads it (with the DFE, its own
  a-priori probe error on that frame; otherwise a gain fit to that frame's probes);
- it becomes the error rate of that frame's coded bits for the mode: 8PSK
  `2/3 Q(sqrt(2 s) sin(pi/8))`, QPSK `Q(sqrt(s))`, BPSK with r-fold repetition
  `Q(sqrt(2 r s))`;
- the block's mean of that is what the deinterleaver hands the Viterbi decoder;
- the SNR giving that mean on a flat channel is the block's flat equivalent;
- the block's quality is that, or, when higher, the flat equivalent of its best three
  quarters of frames less 3.25 dB (`BLOCK_TRIM_FRACTION`, `BLOCK_TRIM_MARGIN_DB`).

On a flat channel this is about `quality_db`, so the block thresholds sit near the old
ones. A fade counts by the errors it causes, not by its share of the power.

The second term is for dropouts. With the signal gone and the noise still there, the DFE
output goes to about zero: each such frame reads about 0 dB, but its symbols reach the
decoder as erasures, which cost far less than errors. test_signal_end's 300L case, the
signal off for 1 s inside a 4.8 s block at about 20 dB SNR, decodes exact, yet that block's
mean alone is 1.8 dB, below the net bursts' blocks with real bit errors (2.0 and 2.3 dB).
In a reproduction of that case (the same gap and noise, with an EOM) the block's mean
scores 1.8 dB and its best three quarters 12.8 dB. A block degraded throughout (the net burst
blocks: every frame at 1-4 dB) has no clean three quarters to fall back on. On the
calibration sets below the second term changes no verdict: the counts are identical with
and without it.

A ratio of summed probe error over the block was tried first. It ranks a block by its
worst frames' power, and on 600S it could not separate wrong from exact decodes.

Like `quality_db`, the block gate is a confidence threshold, not a proof:

- a decode a few bytes wrong at the knee can clear it;
- a decode whose data start is off by whole probe periods (160 symbols; a long
  interleaver block is a whole number of them) has probes that still line up, so it
  scores as well as a right one. On CCIR these came only from late preamble chains (see
  below), which the chain merge keeps from being reported.

## One message per preamble (m110a_rx)

The preamble scan chains correlation peaks 480 symbols apart, within 2 symbols. On
Watterson moderate (1 ms) and poor (2 ms) the peak moves between the two paths, 2.4 or
4.8 symbols, and fades drop frames, so one 24-frame preamble split into 14-16 chains. Each
chain's window held the whole transmission, so each was decoded and printed. On 2400L
poor, where the decode fails, that showed as 16 messages of 1440 bytes (one interleaver
block each) without EOM.

The message framing (`Receiver::process_messages`) now merges them:

- the chains that start inside the front chain's preamble and on its frame grid (within
  ±10 symbols of a whole number of frames) are candidates;
- each is decoded as before. "Inside the preamble" means at most P − 1 frames after the
  front chain, where P is the preamble length of the mode that both decodes read, or 24
  when either is unread, so a short message that follows within 24 frames stays separate;
- one message is printed: the decode with success, then EOM, then the best worst block,
  then the best `quality_db`. `preamble_chains` says how many chains it merged, and
  `preamble_hits` counts the frames over all of them.

The end detector is unchanged: a message still ends at a power step that stays down 4 s.

## Calibration

Data, all with the base build at origin/master 59818c2, which reports every chain; the
per-frame probe SNR came from a calibration build of this change with a temporary
per-frame dump (bytes and verdicts identical to the base on all 180 CCIR cases):

- **AWGN**: the 156-case set (2400S/L at 7, 9, 11 dB and no noise; 1200S/L at 3, 5,
  7; 600S/L at 0, 2, 4; 4800S at 14, 16, 18; 300S/L, 150S/L at −6, −4, −2, 0 dB in
  3 kHz; seeds 1-4; 200-byte payloads).
- **CCIR**: Watterson moderate and poor (hfchansim presets), 5 and 10 dB, seeds 1-5,
  modes 2400S, 1200S, 600S, 300S, 2400L, 1200L, 600L, 300L, 150L: 180 cases.

Rule, per mode: the threshold may not exceed the worst block of any exact AWGN decode
that clears `quality_db`, so no flat-channel success is lost. Under that, it takes the
value with the fewest CCIR wrong-byte successes, then the fewest rejected exact decodes,
counted per transmission after the chain merge.

| Mode | Threshold (dB) | AWGN: lowest worst block of an exact success | CCIR per chain, exact / wrong / rejected-exact, without → with the block gate |
|---|---|---|---|
| 2400S | 8.0 | 8.20 | 0/3/0 → 0/1/0 |
| 2400L | 8.25 | 8.37 | 1/12/0 → 1/2/0 |
| 1200S | 4.75 | 4.92 | 7/13/0 → 7/4/0 |
| 1200L | 4.5 | 5.15 | 122/36/13 → 122/35/13 |
| 600S | 2.75 | 2.91 | 23/9/0 → 22/7/1 |
| 600L | 3.0 | 3.15 | 220/4/9 → 220/4/9 |
| 300S | 0.5 | 2.69 | 33/3/0 → 33/2/0 |
| 300L | 2.25 | 2.93 | 225/9/4 → 224/7/5 |
| 150S, 150L | 0.5 | 2.21, 2.57 | 150L: 230/7/3 → 230/7/3 |
| 4800S | 12.5 | 17.42 | not in the CCIR set |

Notes:

- **300L** is set from the net 300L bursts instead of CCIR, inside the CCIR-safe range:
  on burst 1, `reencode_check.py` finds blocks 2 and 9 do not fit the signal, and they
  score 2.3 and 2.0 dB; the lowest block that fits, on any burst, scores 2.5 dB. At
  2.25 dB the message fails on block 9; block 2 alone would pass.
- **1200L**: most of its per-chain wrong successes are late-chain decodes with good
  probes (the data-start ambiguity above), up to 8.8 dB. No block threshold separates
  them; the chain merge removes them.
- **300S/150** sit far below their AWGN floor: on CCIR, exact decodes score down to
  0.5 dB. 0.5 dB still removes the two 300S chain decodes that were wrong with every block
  low.
- **4800S** is uncoded and keeps its `quality_db` threshold; no block threshold makes its
  success mean bit-exact (see `quality_threshold_db`).
- The sample is small at the knee: 1-13 wrong successes per mode. The thresholds are
  set on these sets and checked on held-out seeds 6-8 (below).

## Measured

Base: origin/master 59818c2. This change: a fresh Release build of this branch. Same
inputs for both (the harness seeds the payload, the noise and hfchansim; a re-run of the
base on 4 cases gave identical output). Per message counts every message `m110a_rx`
prints; per transmission counts each case (one transmission) once.

**CCIR** (the calibration set: moderate and poor, 5 and 10 dB, seeds 1-5, 200 bytes):

| Mode | Cases | Messages | Exact successes | Wrong-byte successes | Rejected-but-exact | Transmissions exact with EOM | Transmissions with a wrong-byte success |
|---|---|---|---|---|---|---|---|
| 2400S | 20 | 42 → 20 | 0 → 0 | 3 → 1 | 0 → 0 | 0 → 0 | 3 → 1 |
| 1200S | 20 | 42 → 20 | 7 → 6 | 11 → 2 | 0 → 0 | 7 → 6 | 7 → 2 |
| 600S | 20 | 42 → 20 | 23 → 14 | 9 → 4 | 0 → 0 | 14 → 14 | 5 → 4 |
| 300S | 20 | 42 → 20 | 33 → 18 | 3 → 0 | 0 → 0 | 18 → 18 | 3 → 0 |
| 2400L | 20 | 256 → 22 | 1 → 1 | 11 → 2 | 0 → 0 | 1 → 1 | 5 → 2 |
| 1200L | 20 | 256 → 20 | 121 → 15 | 30 → 3 | 14 → 1 | 18 → 15 | 13 → 3 |
| 600L | 20 | 256 → 20 | 220 → 20 | 2 → 0 | 9 → 0 | 20 → 20 | 2 → 0 |
| 300L | 20 | 256 → 24 | 225 → 20 | 9 → 0 | 4 → 0 | 20 → 20 | 8 → 0 |
| 150L | 20 | 256 → 20 | 230 → 20 | 7 → 0 | 3 → 0 | 20 → 20 | 7 → 0 |
| Total | 180 | 1448 → 186 | 860 → 114 | 85 → 12 | 30 → 1 | 118 → 114 | 53 → 12 |

Per message, exact successes fall with the message count: the base printed the same
transmission up to 16 times. Per transmission, 4 transmissions that some chain decoded
exact are now reported from a chain 1-3 bytes wrong that clears the gate (1200S once,
1200L three times). The 12 wrong-byte successes left are 1-16 bytes wrong at the knee,
except one 2400S decode, 34 bytes wrong without EOM.

**AWGN** (156 cases): exact successes 99 → 99, wrong-byte successes 9 → 9 (8 are 4800S,
uncoded; 1 is a 2400L knee decode), rejected-but-exact 0 → 0. Messages 162 → 156: six
low-SNR 300L/150L transmissions had printed a second chain.

**Held-out CCIR** (seeds 6-8, the same channels and modes, not used to set anything):

| Mode | Cases | Messages | Exact successes | Wrong-byte successes | Rejected-but-exact | Transmissions exact with EOM | Transmissions with a wrong-byte success |
|---|---|---|---|---|---|---|---|
| 2400S | 12 | 24 → 12 | 0 → 0 | 0 → 0 | 0 → 0 | 0 → 0 | 0 → 0 |
| 1200S | 12 | 24 → 12 | 5 → 4 | 9 → 1 | 0 → 1 | 5 → 4 | 5 → 1 |
| 600S | 12 | 24 → 12 | 15 → 11 | 8 → 1 | 0 → 0 | 11 → 11 | 7 → 1 |
| 300S | 12 | 24 → 12 | 22 → 12 | 2 → 0 | 0 → 0 | 12 → 12 | 2 → 0 |
| 2400L | 12 | 136 → 12 | 0 → 0 | 4 → 0 | 0 → 0 | 0 → 0 | 2 → 0 |
| 1200L | 12 | 136 → 12 | 46 → 8 | 15 → 3 | 23 → 0 | 9 → 8 | 7 → 3 |
| 600L | 12 | 136 → 12 | 96 → 12 | 3 → 0 | 20 → 0 | 12 → 12 | 3 → 0 |
| 300L | 12 | 136 → 14 | 107 → 12 | 10 → 0 | 10 → 0 | 12 → 12 | 6 → 0 |
| 150L | 12 | 138 → 12 | 110 → 12 | 10 → 0 | 9 → 0 | 12 → 12 | 6 → 0 |
| Total | 108 | 778 → 110 | 401 → 71 | 61 → 5 | 62 → 1 | 73 → 71 | 38 → 5 |

Of the two transmissions lost, one 1200S exact decode is rejected by the block gate (worst
block 4.7 dB, threshold 4.75) and one 1200L transmission is reported from a chain 2 bytes
wrong. The 5 wrong-byte successes left are 1-7 bytes wrong, each with EOM.

**Net 300L bursts 1-4**: the same bytes (sha256) and one message each, before and after.
`reencode_check.py` finds blocks 2 and 9 of burst 1 do not fit the signal and every
checked block of bursts 2-4 fits. Burst 1 is now `success=0` (block 9, 2.0 dB); bursts
2-4 stay `success=1`.

**6a captures** (12, by index): the same bytes and the same verdicts before and after
(10 messages, on captures 5, 8 and 11). `reencode_check.py` can check one of them (the
others are single short messages with no whole block): capture 11's fourth message does
not fit the signal, and it is `success=0` before and after.

**110D**: `m110d_rx` and `m110d_chan` are byte-identical to the base build. `m110_lan`
links the 110A library and differs; `appa_lan_dte` passes.

**ctest**: 42 of 42 pass, including `test_block_gate` (new), `test_signal_end` and
`rx_end_fade` (with its new `paths` case).
