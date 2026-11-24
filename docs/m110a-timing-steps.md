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

With no step and no drift, the grid stays where the preamble put it. The drift tracking
(*Clock drift*, below) moves it a sample at a time and logs nothing.

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

### Fades inside a block: the window check

The per-block comparison assumes the channel is even over the block. A fade inside a block
lowers probe and data agreement together. Inside the fade, the wrong codeword the Viterbi
decoder chose agrees with the received symbols as well as the right one would, so averaged
over 4.8 s a few wrong bytes move data agreement by 0.001 to 0.01. An independent review found
300L at 10 dB with a −15 dB, 3 s fade: 12 wrong bytes in block 1, probe 0.911 and data 0.898,
"fit". 1200L with a −15 dB, 1 s fade gave 1 to 9 wrong bytes in a fitting block, on m110a_rx
from 1af44e2 and from this branch's base alike.

Counting hard disagreements per window does not close it. The interleaver spreads a wrong
codeword's symbols over the block, so some of them land where the signal is clean, but one
wrong byte leaves about 7 of them in 5760 data symbols. Hard counts cannot tell 7 from noise.
A floor on window error share fails as well: real 300L blocks reach 0.36 in one window and
decode exact.

What separates them is the distance. Where the signal is clean, a wrong symbol is no near
miss. The received symbol sits on the point that was sent, a whole constellation step from
the re-encoded one, and that is tens of noise powers. So the check measures each symbol's
distance from where the sent symbol should land, in units of the noise. The channel's gain
and phase come from the 2 × k probes around the symbol: a frame's data lies between the
previous frame's probes and its own. The noise power is the median of those probes'
residuals divided by ln 2, so one crash does not move it. A symbol is **far off** when it is
decided as another point than the one sent and lies more than `TAU` = 15 noise powers out.
Right bytes put data symbols far off only as noise or a crash puts probes there. The expected
count is `RHO` = 2 times the probes' rate in the block, plus the Gaussian tail e^−15 per
symbol. The block is NOT REAL when its far-off data symbols are less likely than `ALPHA` = 1e−6
under that (Poisson). The block's last 2 frames are left out, because their probes differ
from the scrambler's: ModemTX gives 8 to 16 probe errors there on a clean signal.

The new lines come after each block line. The block line itself is unchanged, and gale-rx
parses it:

```
  block 1: probe agreement 0.911, data agreement 0.898, random-bytes control 0.498 (chance 0.500): the bytes fit the signal
    block 1 window 1 frames 1-8: probes 1.000 (160/160 right), data 0.994 (159/160 agree), far off 0 probes, 1 data
    ...
  block 1 window check: far off (over 15 noise powers) 0/5720 probes, 34/5720 data, expected for right bytes 0.00, p 1e-85 (not real under 1e-06); verdict NOT REAL: data symbols far off where the probes are not (bit errors)
```

The verdict is `real`, `NOT REAL: data below the probes over the block (bit errors)` (the old
comparison) or `NOT REAL: data symbols far off where the probes are not (bit errors)`.
gale-rx works out its own "content ok" from the block line's two numbers, so it does not see
this verdict until it reads the `window check` line.

`--bytes FILE` checks the given bytes in place of the receiver's. `--frames-json FILE` writes
the per-frame counts used for calibration. `ctest rx_reencode_window` covers the window check.

Calibration, block by block, old verdict against new, with the truth:

CALIBRATION_TABLE

## Left open

- **`signal_frames` on the hand-cut slice:** the base receiver reports 1 block (180 B).
  Replicating its rule (last frame at coherence 0.5 or more, with the 8-frame mean at 0.25 or
  more) on the same audio gives 623 frames, which is 2 blocks of 288. The weak tail (frames
  576–623, coherence about 0.05–0.29) sits on the rule's margin. This branch, on the uncut
  slice, reports 2 blocks (360 B). The rule is unchanged here.
- **Steps under 8 symbols:** the drift tracking below moves the grid one sample (0.05 symbol)
  per 8 frames at most, so a sudden 2.4-symbol shift (1 ms) is still left to the DFE, and on
  this burst it still ends the decode.

## Clock drift: `grid_steps` also follows the sender's clock

The sender's and the recorder's sample clocks differ, so the data's symbols drift against the
grid the preamble sets. The 300L preambles in four bursts of one net, recorded on an
FT-897D with 0 overruns, all show the same steady drift. The probes were fitted over 30-frame
windows, timing ±50 symbols:

| Burst | Data with probes | Drift | Residual about the line | Carrier |
|---|---|---|---|---|
| 1 | 4.8 s (1 block) | +38.1 ppm | 0.037 symbol rms | +2.5 Hz |
| 2 | 52.8 s | +35.4 ppm | 0.020 symbol rms | |
| 3 | 18.2 s | +35.7 ppm | 0.023 symbol rms | +2.4 Hz |
| 4 | 21.8 s | +35.7 ppm | 0.009 symbol rms | |

+36 ppm is 0.086 symbol per second, so the probes move 1.5 symbols in 18 s. Probe coherence
holds at 0.61–0.65 on the moving grid to the end of each burst, and falls on the fixed grid from
0.64 to 0.43 by 4.5 s and to 0.04 by 16 s. The base receiver lost the probes and reported
`Signal covers 0 of N interleaver blocks` on all four.

**The 300L chain itself matches the standard.** With the drift resampled out of the audio,
the base receiver decodes bursts 2, 3 and 4, and the re-encode check passes every block:
data agreement 0.965–0.984 against probe agreement 0.957–0.972. MIL-STD-188-110D §5.3.2.3.3 b
(repeat in pairs), §5.3.2.3.4 and §5.3.2.3.5 (load row +9, fetch row +1 and column −17,
40 × 144 at 300 bps long) agree with `M110ACodec` and `mode_config.h`.

**The drift breaks every long mode, not only 300L.** ModemTX → resampled by +36 ppm →
`m110a_rx`, AUTO, at 12 dB: 150L, 600L, 1200L and 300L fail on the base receiver, and 2400S
(one 0.6 s block) still decodes.

So every 8 frames, from the 9th data frame on, `grid_steps` fits a parabola to the last 32
frames' probe coherence on the grid and a quarter symbol either side. When the peak is a sample
or more away, the grid moves one sample towards it. It moves only where the last 8 frames reach
coherence 0.25, the same gate the steps use, so a fade or noise leaves the grid where it is. A
grid that moves a sample at most per 8 frames follows drift up to about 150 ppm.

## On air, the drift

The four FT-897D bursts above, the RSP2 burst of the same evening (3-frame preamble with
300L's D1/D2, +32.9 ppm, +11.6 Hz), and the 12:37Z burst. Re-encode check per block, data /
probe agreement:

| Burst | Base receiver | This receiver |
|---|---|---|
| FT-897D 1 (1 block) | no bytes (`0 of 1 blocks`) | no bytes: the end detector stops the message 2.8 s early in a fade (8 dB in 9 s). With that fade levelled by hand: 180 B, 0.968 / 0.971, fits |
| FT-897D 2 (10 blocks) | no bytes (`0 of 12 blocks`) | 1912 B, 10 blocks, each 0.965–0.977 / 0.960–0.972, all fit |
| FT-897D 3 (3 blocks) | no bytes (`0 of 4 blocks`) | 540 B, 0.963–0.965 / 0.957–0.962, all fit |
| FT-897D 4 (3 blocks) | no bytes (`0 of 4 blocks`) | 540 B, 0.972–0.984 / 0.966–0.968, all fit |
| RSP2, shifted −11.6 Hz | 180 B, block 1 0.973 / 0.975, fits | 360 B, 0.973 / 0.975 and 0.971 / 0.973, both fit |
| 12:37Z | 360 B, 0.773 / 0.908, 0.714 / 0.898 | 360 B, 0.761 / 0.908, 0.698 / 0.898, do not fit |

On bursts 2–4 the receiver's bytes are the same (sha256) as those from the base receiver on
the audio with the drift resampled out.

**The re-encode check now removes the carrier offset.** It phased each frame from its own
probes and the next frame's and removed no frequency offset. At the RSP2 burst's +11.6 Hz the
data, 20 symbols ahead of the probes, turned about 35° against them. So the check scored the
receiver's bytes 0.712 / 0.953 and 0.716 / 0.948, "do not fit", and the same bytes (sha256)
fitted on the audio shifted by −11.6 Hz. It now measures the offset from the probes' phase step
from frame to frame (to ±30 Hz) and removes it first. Measured with the new check:

| Input | Old check | New check |
|---|---|---|
| RSP2 burst, as recorded | 0.712 / 0.953, 0.716 / 0.948, do not fit | 0.973 / 0.973, 0.973 / 0.973, fit |
| Our TX, 300L, +15 Hz and −15 Hz, 15 dB (exact decodes) | data 0.507–0.519, do not fit | data 1.000 / probe 0.997, fit |
| Our TX, 1200S, ±15 Hz | data 0.225–0.294, do not fit | 1.000 / 0.978, fit |
| Our TX, 2400S, ±15 Hz | probe 0.04–0.20 | 0.933–0.967 / 0.933–0.950, fit |
| FT-897D 3 (+2.4 Hz) | 0.964–0.965 / 0.957–0.964, fit | 0.968–0.971 / 0.958–0.965, fit |
| 12:37Z (−1.5 Hz) | 0.761 / 0.908, 0.698 / 0.898 | 0.764 / 0.910, 0.698 / 0.897, do not fit |

The random-bytes control stays at chance: 0.236–0.260 for QPSK (chance 0.250), 0.503–0.507 for
BPSK (chance 0.500).

**`ctest tx_loopback_drift`:** our TX resampled by +36 and −36 ppm, 10 dB, 400 bytes, 150L,
300L, 600L and 1200L. This receiver decodes 8/8 exact with EOM. The base receiver decodes 0/8:
six give no bytes, and 300L at −36 ppm reports `success=1` with 156 of 400 bytes wrong.

**Watterson matrix, base receiver against this one:** our TX through `hfchansim --preset
moderate` and `poor`, 5 and 10 dB, 150S to 2400L (10 modes), 5 seeds, 200 bytes, `m110a_rx`
AUTO, payload exact. Base 77/200, this receiver 78/200. One cell differs: 300S moderate 5 dB
seed 5, exact only on this receiver. `success=1` with wrong bytes: base 30, this receiver 29.
This harness differs from the one in *Proof* above (payload and length), so its totals are not
comparable with 117/200.

**The 12:37Z burst is not explained.** Its drift
is +25 ppm, and removing it changes nothing. Its timing also moves ±0.5 symbol every 1–2 s,
with probe coherence dipping at each move. Taking its 15 ms step as 51 ms of lost audio
(160 − 36.8 symbols) does not help. Our TX through `hfchansim` (Watterson 1 Hz, 0.4 ms, 6 dB,
+25 ppm) decodes exactly with this receiver.
