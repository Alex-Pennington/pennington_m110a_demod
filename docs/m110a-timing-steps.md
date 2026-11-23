# 110A data timing steps, and checking bytes without a CRC

## The problem

A received 110A burst can move in time all at once partway through its data. This happens
when audio at the sender or in the recording chain skips or repeats a few milliseconds, as a
sound-card underrun does. Every later symbol then arrives earlier or later by the same amount.

`BrainDecoder` samples the data on a fixed grid set by the preamble: every 20 samples at
48 kHz from the data start. The DFE reaches ±5 symbols (11 feed-forward taps). A step
larger than that loses every probe from the step to the end of the message.

## The on-air case

The case is one 300L burst received over HF skywave as 8 kHz USB audio. AUTO acquired it as
300L (SNR 5.4 dB), with the countdown unreadable. Its probes were lost 1.2 s into the data.
The audio and the payload stay out of the repository.

Measured on the 48 kHz audio `m110a_rx` decodes (`--dump-audio`): the probes searched over
0.5 s windows, timing ±50 symbols in 0.1-symbol steps, frequency ±60 Hz.

| Data time | Best probe coherence | Timing | Frequency | Coherence on the preamble's grid |
|---|---|---|---|---|
| 0.0 s | 0.54 | 0 | −1.50 Hz | 0.54 |
| 0.5 s | 0.49 | −0.2 sym | −1.50 Hz | 0.47 |
| 1.0 s | 0.13 | 0 | −2.00 Hz | 0.13 |
| 1.5–10.0 s | 0.31–0.54 | +36.5 to +37.4 sym | −1.25 to −1.75 Hz | 0.02–0.05 |
| 10.5 s on | 0.02 | — | — | 0.02 |

Noise gives about 0.02.

- **The burst carries 300L probes to its end, 10.45 s of data (627 frames).** That is
  partway through the third 4.8 s interleaver block, so the station stopped mid-block.
- **The probes move once, by 36.8 symbols (15.3 ms), between data frames 73 and 74.** Frame
  73 is coherent on the old grid (0.57) and frame 74 on the new one (0.52), with no frames in
  between.
- **The channel itself holds steady.** The frequency stays at −1.5 Hz ±0.25 Hz. The timing
  holds ±0.5 symbol over 9 s, under about 20 ppm. Probe coherence after the step is as high
  as before it.
- **The audio shows no gap at the step.** Its RMS stays level in 2 ms blocks, with no zeros.
  No block repeats exactly: the near-step correlation is 0.74, the same as a control window.
  15 ms is beyond any HF delay spread, and the switch takes no time. Whether the
  discontinuity happened at the sender or in the recording chain is not established.

## The fix: `BrainDecoder::grid_steps`

At data frame f, the grid moves when all of these hold, with W = 8 frames:

- On the current grid, the W frames before f reach probe coherence 0.25.
- On the current grid, the W frames after f fall under 0.1.
- A grid 8 to 50 symbols away reaches 0.25 over the next 2W frames, and over their second W
  alone.

The search uses quarter-symbol steps, then refines to 1 sample. The step takes effect at the
first frame from f whose probes the new grid reads better than the old.

Noise gives about 1/(W·K) = 0.006–0.008 per window, where K is 20 or 16 probes. The largest
noise value over the 400 grids searched is about 0.04, against a threshold of 0.25. Steps
under 8 symbols are left to the DFE, so an echo inside its span never moves the grid. A fade
or the end of the signal leaves no other grid at 0.25.

`-v` logs each step, for example:

```
[RX] grid step at data frame 75 (data +1.25 s): 37.2 symbols (15.5 ms), coherence 0.42 on the new grid, 0.096 on the old
```

With no step, the symbol stream is the same as before, sample for sample.

## Proof

**The step re-anchor is proven on synthetic signals only.** It stays that way until a
cut-position sweep on air gives stable bytes; see the on-air section below.

| Check | Base (4aa1c6f) | This branch |
|---|---|---|
| `test_grid_step` (new): 300L and 2400S, 15 ms of silence or replay inserted or 15 ms deleted mid-message, AUTO and forced, payload exact | 0/12 | 12/12 |
| ctest, all 38 | (37 without the new test) | 38/38 |
| `test_decode_quality` (reference PCM ladder) | 51/51 | 51/51, output identical |
| `test_late_start`, `test_signal_end`, `test_table_xi_modes`, `test_mode_detect`, `test_preamble_countdown` | pass | pass, output identical |
| ±75 Hz (`hfchansim_freq_loopback`) | pass | pass |
| Watterson matrix: `hfchansim` moderate and poor, 5 and 10 dB, 10 modes, 5 seeds, payload exact | 117/200 | 117/200, no cell different |
| Harris set, 8 recordings, 11 messages (`m110a_rx`, AUTO) | — | byte-identical to base |
| `m110d_rx`, `m110d_chan` | — | binaries identical (md5) |
| `m110a_rx`, 30 min of white noise, AUTO and 14 forced modes | — | 0 preambles, 0 success |
| API `decode()`, 60 × 30 s of noise in each of 15 modes | — | 0 success; 0 data extractions, so noise alone never reaches `grid_steps` |
| API `decode()`, each standard mode's message then 3 min of noise, 10 seeds (30 min of noise per mode), AUTO and forced; grid steps counted | — | 0 grid steps in 330 data extractions over 11 modes; 220/220 success and exact |
| `m110a_rx -v` over Watterson moderate and poor, 5 and 10 dB, 10 modes, 3 seeds; grid steps counted | — | 0 grid steps in 1515 data extractions over 120 cells |

## On air: before and after

| Receiver | Input | Mode | success | Bytes | Grid step | Re-encode check (data / probe agreement) |
|---|---|---|---|---|---|---|
| base | slice | 300L | 0 | 0 | — | — |
| base | slice with 15.4 ms cut by hand at 8.362 s | 300L | 1 | 180 | — | block 1: 0.720 / 0.910 |
| this branch | slice | 300L | 1 | 360 | +37.2 sym at data frame 75 | block 1: 0.773 / 0.908, block 2: 0.714 / 0.898 |

**Receiver `success=1` on this slice is unverified, and the re-encode check says the bytes
are wrong.** In both blocks the data agreement falls 0.13 to 0.19 below the probe agreement.
Calibrated on synthetic signals (below), that is roughly 3 to 5 % bit errors. A right
payload sits within 0.03 of the probes.

The cut-position sweep says the same:

- 15.4 ms cut every 10 ms from 8.30 to 8.45 s, base receiver: `success=1` and 180 bytes at all
  16 positions, with 16 different payloads. This branch gives the same 16, since with the
  step cut out there is nothing to re-anchor.
- 1 ms cut every 0.1 s from 7.5 to 9.5 s, this branch: `success=1` at 2 positions of 21, each
  with different bytes. A shift of 2.4 symbols (1 ms) is under the 8-symbol minimum and is
  left to the DFE, which does not hold it on this signal.

So on this burst the step re-anchor recovers the probes, and the decode it enables carries
bit errors. Why it carries them is not established here. The recording's raw symbol accuracy
(probe agreement 0.90) is one that the synthetic 300L decodes exactly at (0.93 in AWGN).

## The re-encode check: `test/rx/reencode_check.py`

110A carries no CRC, and `success` comes from probe quality, not from the bytes.

```sh
python3 -I test/rx/reencode_check.py CAPTURE.wav --rx build/bin/m110a_rx --tx build/bin/modem_tx_wav
python3 -I test/rx/reencode_check.py CAPTURE.wav --rx build/bin/m110a_rx --sweep T0 T1 STEP MS
```

The check decodes, re-encodes the bytes with ModemTX, and compares symbols.

- **Probe agreement:** the share of probe symbols decided right, which is the channel's raw
  symbol accuracy.
- **Data agreement:** the share of data symbols equal to the re-encoded ones.
- **Grid:** the receive grid comes from the probes alone and is tracked through steps. The
  data start is chosen among the probe grid's aliases (every lcm(160, frame) symbols).

Wrong bytes still agree above chance, because the Viterbi decoder returns the codeword nearest
the received symbols. So the verdict compares data with probe: the bytes fit when data is
within 0.03 of probe or above.

Calibration on this modem's own 300L:

- 21 exact decodes (AWGN, Watterson moderate and poor, 15 ms steps) had a gap of 0.023 at
  most.
- The right payload with bits flipped, at probe agreement 0.93, gave a gap of 0.019 at 0.3 %
  bit errors, 0.048 at 1 %, 0.120 at 3 % and 0.301 at 10 %.
- On 300L, 1200S and 2400S with a 15 ms step, right bytes gave data at or above probe, and
  random bytes gave chance.

The sweep deletes MS ms at each position and counts the different payloads. A cut that truly
repairs a step gives the same bytes at nearby positions.

The payload is never printed, only its length and a sha256 prefix.

## Left open

- **`signal_frames` on the hand-cut slice:** the base receiver reports 1 block (180 B).
  Replicating its rule (last frame at coherence 0.5 or more, with the 8-frame mean at 0.25 or
  more) on the same audio gives 623 frames, which is 2 blocks of 288. The weak tail (frames
  576–623, coherence about 0.05–0.29) sits on the rule's margin. This branch, on the uncut
  slice, reports 2 blocks (360 B). The rule is unchanged here.
- **No fine timing tracking in the data:** a step under 8 symbols is left to the DFE, and on
  this burst a 2.4-symbol shift ends the decode.
