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
  block 1 window check: far off (over 15 noise powers) 0/5720 probes, 35/5720 data, expected for right bytes 0.00, p 3.1e-137 (not real under 1e-06); verdict NOT REAL: data symbols far off where the probes are not (bit errors)
  tail block 2 (data end, EOM, flush, zero padding): probe agreement 0.983, data agreement 0.987, random-bytes control 0.927 (chance 0.500): the bytes fit the signal
    block 2 window 1 frames 289-296: ...
  block 2 window check: far off (over 15 noise powers) 0/5720 probes, 0/5720 data, expected for right bytes 0.00, p 1 (not real under 1e-06); verdict real
```
The verdict is `real`, `NOT REAL: data below the probes over the block (bit errors)` (the old
comparison) or `NOT REAL: data symbols far off where the probes are not (bit errors)`.
gale-rx works out its own "content ok" from the block line's two numbers, so it does not see
this verdict until it reads the `window check` line.

> **Superseded 2026-11-24:** "(the old comparison)" above. The verdict's `data below the probes`
> no longer repeats the block line's comparison. It reads the window frames and allows for
> sampling spread; see "Tail and fit, corrected" below. The block line is unchanged.

**The tail.** The block lines cover `len // block_bytes` whole blocks. A message with EOM ends
with the data's last bytes, then EOM 4B65A5B2, 144 flush bits and zeros to the end of the
interleaver block (`api/modem_tx.cpp`, `M110ACodec::encode_bits_with_probes`). m110a_rx cuts the
payload at the EOM, so that last block was never compared, and a message under one block was
not checked at all. When m110a_rx reports `eom=1`, the check now also compares blocks up to
ceil((8 × bytes + 32 + 144) / data bits per block), counted in bits because 150S and 300S
blocks hold 11.25 and 22.5 bytes. Each is printed as `tail block N (data end, EOM, flush,
zero padding): ...`, a form gale-rx's block pattern does not match, followed by its window rows
and window check. A message under one block keeps its `not checked (no whole block)` line. An
indented `  tail check (eom=1, ...)` line and the tail lines follow it. The tail check line
carries no label, so gale-rx sees one head line per message. The random-bytes control is high
in a tail (0.93 above) because the padding is the same for any bytes. Limits:

- It assumes the sender pads with zeros after the flush, as ModemTX does. That is unverified
  for other senders, Harris among them. A sender that pads otherwise makes a right tail look
  wrong.
- It does nothing for `eom=0`. A message cut before its EOM has no known end, and its last
  partial block stays unchecked.
- It needs the signal to last to the end of the last block, and says `tail not checked`
  otherwise.

> **Superseded 2026-11-24:** "counted in bits" above held for the last block, not for the first
> tail block. That one came from the block lines' whole-byte count, so a 150S or 300S message of
> 968 B or more could print no tail and no `tail not checked`. See "Tail and fit,
> corrected" below.

`--bytes FILE` checks the given bytes in place of the receiver's. `--frames-json FILE` writes
the per-frame counts used for calibration. `ctest rx_reencode_window` covers the window check.

Calibration, block by block: the old verdict against the new one, with the truth. Truth is
the synthetic source or the keyed payload. Exact means every byte of the block is right;
wrong means at least one is not. Tail blocks had no old verdict, because the old check never
compared them.

| Set | Block | Truth | Old | New | Blocks |
|---|---|---|---|---|---|
| ModemTX, AWGN, 10 modes, 2 to 30 dB | whole | exact | fit | real | 140 |
| ModemTX, Watterson moderate and poor, 150L, 300L, 600L, 1200L (12 runs) | whole | exact | fit | real | 197 |
| ModemTX, Watterson | whole | wrong (1 to 3 bytes) | fit | NOT REAL | 3 |
| ModemTX, Watterson | whole | wrong | do not fit | NOT REAL | 12 |
| Review's fades, 300L to 2400L, 10 and 15 dB, −10 to −20 dB | whole | exact | fit | real | 10 |
| Review's fades | whole | wrong (78 bytes) | fit | NOT REAL | 1 |
| Review's fades | whole | wrong | do not fit | NOT REAL | 22 |
| Review's fades | tail | exact | — | real | 20 |
| Review's fades | tail | wrong (21 to 156 bytes) | — | NOT REAL | 6 |
| Review's gale-rx cases F and K (300L fades) | whole | exact | fit | real | 4 |
| Review's F | whole | wrong (12 bytes) | fit | NOT REAL | 1 |
| Review's F and K | tail | exact / wrong (31 bytes) | — | real / NOT REAL | 4 / 1 |
| 1200L, 720 B, −15 dB 1 s fade, 10/15/20 dB, m110a_rx at 1af44e2 | whole | exact / wrong (1 to 8 bytes) | fit | real / NOT REAL | 8 / 10 |
| The same, this branch's base receiver | whole | exact / wrong (1 to 9 bytes) | fit | real / NOT REAL | 7 / 11 |
| The same, tail (EOM and padding only) | tail | exact | — | real | 9 |
| Random-length messages with EOM, 10 modes, 12 and 20 dB | tail | exact | — | real | 58 |
| RSP2 150L on air | whole | unknown: no sent payload; receiver output only | fit | real | 4 |
| Net 300L bursts 2, 3, 4 | whole | unknown: no sent payload; receiver output only | fit | real | 34 |
| Net 300L burst 1, blocks 2 and 9 | whole | wrong | do not fit | NOT REAL | 2 |
| Net 300L burst 1, other blocks | whole | not known | fit | real | 9 |
| RSP2 and bursts 1 to 4 | tail | not known | — | real | 5 |
| 6a captures, keyed 2400S/1200S/600S under one block | tail | exact | — | real | 8 |
| 6a capture 5 (no key for its session) | tail | not known | — | real | 3 |
| 6a capture 11, 2400S, success=0 | whole | wrong (143 bytes) | do not fit | NOT REAL | 1 |

> **Corrected 2026-11-24:** the RSP2 150L and net 300L bursts 2 to 4 rows read "exact (as
> received)". That took the receiver's own output as the truth it was checked against, which is
> circular: no sent payload is held for them. They are unknown, and the exact blocks in this
> table are 467, not 505.

No wrong block is called real, and no exact block is called anything else. Among exact
blocks, the most far-off data symbols seen was 1. Every wrong block the old comparison passed
had 3 or more, p 9e−10 or less. At `TAU` = 15 and with no far-off probes, a block needs 3
far-off data symbols to be NOT REAL.

### Tail and fit, corrected (2026-11-24)

A review of the check above found two false rejects. Both are fixed in `test/rx/reencode_check.py`,
and `ctest rx_reencode_window` carries both as cases (`tails`, `whole2400`).

**A silent tail.** The tail began where `len // block_bytes` said the whole blocks end, with
`block_bytes` floored: 22 B for 300S's 22.5 and 11 B for 150S's 11.25. From 968 B on, that count
can reach the message's last block (for every length from 1408 B in 150S and 1892 B in 300S),
and then the tail printed nothing at all, neither a tail block nor `tail not checked`. 300S,
1277 B, eom=1: the block lines run to block 58, and the message has 58
(ceil((8 × 1277 + 176) / 180)). The tail now starts after the blocks the block
lines printed, and counts the message's blocks in bits. When the block lines already reach the
last block, one line says so:

```
  block 58 window check: far off (over 15 noise powers) 0/680 probes, 0/680 data, expected for right bytes 0.00, p 1 (not real under 1e-06); verdict real
  tail check (eom=1, 58 blocks): the block lines cover the data end (block 57), EOM, flush and zero padding (block 58)
```

The second line is new; before, the output ended with the first. When the block lines run past
the last block (150S from 1463 B, 300S from 1958 B), the line ends `; block lines N-M lie past
the message end`. The block lines themselves are unchanged.

**A false NOT REAL in a tail of EOM and padding.** The tail's probe and data agreement took every
frame of the block. A block's last two frames carry D1/D2 probes, not the scrambler, so they read
wrong against the reference. Each frame's data is phased from its own probes and the next
frame's, so the data of a block's second-last frame is phased from D1/D2 probes only. In the
message's last block the last frame's data is too, because no frame follows it. In a middle
block the data loses one frame to this and the probes lose two, which lifts data above probes.
The last block has no such lift, and its D1/D2 frames swing the probe figure from 0.925 to 0.967
at 14 dB with right bytes. 2400S, 540 B (exactly 3 blocks), clean AWGN, right bytes:

| SNR, noise seed | Before: tail block 4 probe / data, verdict | After: probe / data, verdict |
|---|---|---|
| 8 dB, "DE AAM402 repro " and random bytes, seed 1 | 0.852 / 0.808, NOT REAL: data below the probes | 0.908 / 0.862, real |
| 14 dB, the `whole2400` case's payload, seed 10 | 0.965 / 0.929, NOT REAL: data below the probes | 0.998 / 0.996, real |
| 14 dB, the same, seed 15 | 0.965 / 0.927, NOT REAL: data below the probes | 0.998 / 0.993, real |

The tail line's figures and its fit, and every window check verdict, whole blocks included, now
read the frames the window check reads: the block's last `TAIL` frames are left out. The block
lines are unchanged.

That left a second cause at low SNR. A 2400S block's window frames hold 448 probes and 896 data
symbols. 2400S, 540 B, 8 dB, 20 noise seeds, every decode exact: over 140 block readings (60 from
the block lines' analysis and 80 from the tail's, which reads blocks 1 to 3 again), the
probe-minus-data gap over the window frames measured +0.011 on average with an sd of 0.023.
Against `FIT` = 0.03 alone, 35 of the 140 failed. The all-frames comparison before this change
failed 20, and 9 of the 20 messages had a whole block NOT REAL. So data below the probes counts as not fit only when the gap
passes `FIT` or z × se, whichever is larger. Here se = sqrt(p (1 − p) (1/probes + 1/data)) and
z = 4.75, the normal tail at `ALPHA` = 1e−6 (`fits()`). For 300L and the other long blocks, se is
small and `FIT` decides as before. For a 2400S block at 8 dB the allowance is about 0.08. The
largest right-bytes gap in those 140 readings was 0.059.

> **Superseded 2026-11-24:** the "real" verdicts at 8 dB in the table above and in "What it costs"
> below. A block that passes but where one wrong byte would not show is now `NOT VERIFIABLE`, not
> real; see "NOT VERIFIABLE" below. The 14 dB tails stay real.

**What it costs.** One block in this sweep that the old comparison called NOT REAL is now real.
It was one flipped bit in a 2400S message under one block at 8 dB: the old check read 0.823 /
0.786 over all frames, the new one reads 0.879 / 0.843 over the window frames, with one far-off
data symbol (p 0.00027). That gap lies inside the spread right bytes show at that SNR, so the
old NOT REAL there came from the same D1/D2 frames that rejected right bytes.

Sweep (`reencode_window.py --cases sweep`, 4 random-length eom=1 messages per mode and SNR,
half the 150S and 300S messages 900 to 1400 B, noise seeds 100 to 103). The before column is
the check at 8a9b719 (`--check`), the after column this one, on the same audio:

| Mode | SNR | Exact blocks NOT REAL, before / after | Sent bytes NOT REAL, before / after (blocks) | Silent tails, before / after | One flipped bit NOT REAL, before / after |
|---|---|---|---|---|---|
| 150S | 8 | 0 / 0 | 0 / 0 (230) | 1 / 0 | 4/4 / 4/4 |
| 150L | 8 | 0 / 0 | 0 / 0 (8) | 0 / 0 | 4/4 / 4/4 |
| 300S | 8 | 0 / 0 | 0 / 0 (117) | 1 / 0 | 4/4 / 4/4 |
| 300L | 8 | 0 / 0 | 0 / 0 (6) | 0 / 0 | 4/4 / 4/4 |
| 600S | 8 | 0 / 0 | 0 / 0 (10) | 0 / 0 | 4/4 / 4/4 |
| 600L | 8 | 0 / 0 | 0 / 0 (4) | 0 / 0 | 4/4 / 4/4 |
| 1200S | 8 | 0 / 0 | 0 / 0 (11) | 0 / 0 | 4/4 / 4/4 |
| 1200L | 8 | 0 / 0 | 0 / 0 (9) | 0 / 0 | 4/4 / 4/4 |
| 2400S | 8 | 1 / 0 | 1 / 0 (8) | 0 / 0 | 3/4 / 2/4 |
| 2400L | 8 | 0 / 0 | 0 / 0 (5) | 0 / 0 | 3/4 / 3/4 |
| 150S | 20 | 0 / 0 | 0 / 0 (205) | 0 / 0 | 4/4 / 4/4 |
| 150L | 20 | 0 / 0 | 0 / 0 (8) | 0 / 0 | 4/4 / 4/4 |
| 300S | 20 | 0 / 0 | 0 / 0 (116) | 1 / 0 | 4/4 / 4/4 |
| 300L | 20 | 0 / 0 | 0 / 0 (6) | 0 / 0 | 4/4 / 4/4 |
| 600S | 20 | 0 / 0 | 0 / 0 (10) | 0 / 0 | 4/4 / 4/4 |
| 600L | 20 | 0 / 0 | 0 / 0 (6) | 0 / 0 | 4/4 / 4/4 |
| 1200S | 20 | 0 / 0 | 0 / 0 (9) | 0 / 0 | 4/4 / 4/4 |
| 1200L | 20 | 0 / 0 | 0 / 0 (5) | 0 / 0 | 4/4 / 4/4 |
| 2400S | 20 | 0 / 0 | 0 / 0 (10) | 0 / 0 | 4/4 / 4/4 |
| 2400L | 20 | 0 / 0 | 0 / 0 (7) | 0 / 0 | 4/4 / 4/4 |

Every message decoded exact and reported eom=1, so the sweep has no wrong receiver blocks. The
blocks the sweep did not catch with one flipped bit sit at 8 dB in 2400S and 2400L. Inferred,
not measured: there an 8PSK neighbour lies about 5 noise powers from the sent point (squared
distance 0.586 against a noise power near 0.13), under `TAU`, so a wrong symbol is a near miss
and only the gap can show it. The 150S flips are counted by the flipped bit's own
block. ModemTX sends each byte LSB first (`api/modem_tx.cpp`), and a 150S byte can straddle two
blocks; the first sweep run labelled them by the byte's first bit, and both straddling flips
were rerun alone, NOT REAL in their block under both checks. The old lines (head, block and
`not checked` lines, the 609cc4b form) came out byte-identical between the check at 609cc4b
and this one, in 20 runs (one message per mode at 8 and 20 dB, 33 old lines).

### NOT VERIFIABLE (2026-11-24)

Held-out messages showed what the fit alone could not settle. At 2400S 6 and 8 dB, two wrong
bytes moved the window gap by 0.001 to 0.008, inside the spread right bytes show, with no
far-off symbols. 8a9b719 called those blocks NOT REAL, but it called the same blocks NOT REAL with
the exact bytes too. The fit's allowance calls both real. Neither says what the signal shows. So
a third verdict, on the same window check line:

```
  block 1 window check: far off (over 15 noise powers) 0/5720 probes, 0/5720 data, expected for right bytes 0.00, p 1 (not real under 1e-06); verdict real
  block 1 window check: far off (over 15 noise powers) 0/448 probes, 0/896 data, expected for right bytes 0.00, p 1 (not real under 1e-06); verdict NOT REAL: data below the probes over the block (bit errors)
  block 1 window check: far off (over 15 noise powers) 0/448 probes, 0/896 data, expected for right bytes 0.00, p 1 (not real under 1e-06); verdict NOT VERIFIABLE: a one-byte error would not show at this block's SNR (10 data symbols moved, far-off power 0.01 with 2 more far off needed, gap shift 0.009 against 0.100)
```

NOT REAL is unchanged. A block that passes is `real` only where one wrong byte would have shown,
and that power is measured on the block (`one_byte_power()`):

- One wrong data bit changes 10 coded bits. 110A's K=7 code has generators 133 and 171 (octal),
  of weight 5 each. Each coded bit is one data symbol, or 2 at 300 bps and 4 at 150 bps, so one
  wrong byte moves at least d = 10 × repetition data symbols of the re-encoded reference, each to
  the nearest other point.
- **Far-off power.** A moved symbol sits on the sent point, `sep` noise powers from the
  re-encoded one, where `sep` is the frame's measured distance to the nearest other point. It is
  far off when |s + n|² > `TAU` with unit noise: a non-central χ² with 2 degrees of freedom.
  The test fires when the far-off count reaches what makes p < `ALPHA` for right bytes. The
  power is the chance that d moved symbols bring that many, averaged over the window frames.
- **Gap power.** The d symbols lower data agreement by d (p − c (1 − p)) / data symbols, with c
  the share of wrong decisions that land on the re-encoded point: 1 for BPSK, ½ for QPSK and
  8PSK. That is compared against the fit's allowance, max(`FIT`, 4.75 × se).
- `real` needs power 0.99 or more by either (`POWER`); otherwise `NOT VERIFIABLE`. The gap never
  carries it in practice: one byte moves 0.003 (300L) to 0.011 (2400S) of the data, under
  `FIT`. The far-off test does, where the nearest other point lies well beyond `TAU`.

The 0.07 cap on the allowance (one commit, the same day) is gone. It kept 6a capture 11's
garbage decode NOT REAL with margins of 0.011 and 0.014, chosen on the cases it was judged on.
That block is now `NOT VERIFIABLE`, never real.

**What it means on air.** The net 300L bursts b2 to b4 sit at `sep` of about 6.5 noise powers
(BPSK, about 2 dB per symbol). There one wrong byte moves 20 data symbols, each far off about 5 %
of the time, so they are `NOT VERIFIABLE`: the check cannot tell one wrong byte there. RSP2 150L
(`sep` 21 to 25) stays real.

Measured on b2 by the independent review: wrong bytes injected into b2's blocks, the share the
check catches (NOT REAL), by wrong bytes per block. The review found the power model right or
slightly optimistic.

| Wrong bytes per block | 1 | 2 | 4 | 8 |
|---|---|---|---|---|
| Caught (b2) | 12 % | 18 to 55 % | 79 to 97 % | 97 to 100 % |

So on b2 one wrong byte goes unseen 7 times in 8, which is what `NOT VERIFIABLE` says, and several
wrong bytes per block are mostly caught.

Measured (verdict counts real / NOT VERIFIABLE / NOT REAL; 8a9b719 has no third verdict):

Held-out validation (`build/scratch/validate.py`, new seeds, 20 messages per point; each gives
the sent bytes and a wrong copy: the receiver's when it decoded wrong, else 1 to 3 bytes changed;
wrong blocks are those whose bytes differ):

| Mode | SNR | Wrong copies natural, changed | Exact blocks, 8a9b719 real/NOT REAL | Exact blocks, now | Wrong blocks, 8a9b719 real/NOT REAL | Wrong blocks, now |
|---|---|---|---|---|---|---|
| 2400S | 6 | 14, 6 | 38/5 | 0/43/0 | 19/13 | 0/25/7 |
| 2400S | 8 | 0, 20 | 42/6 | 0/48/0 | 4/28 | 0/4/28 |
| 2400S | 10 | 0, 20 | 40/0 | 0/40/0 | 0/23 | 0/0/23 |
| 2400L | 6 | 19, 1 | 23/1 | 0/24/0 | 18/3 | 0/20/1 |
| 2400L | 8 | 1, 19 | 27/0 | 0/27/0 | 3/19 | 0/3/19 |
| 2400L | 10 | 0, 20 | 23/0 | 0/23/0 | 3/19 | 0/3/19 |

8a9b719 called 47 wrong blocks real here (19 of 32 at 2400S 6 dB, 18 of 21 at 2400L 6 dB) and 12
exact blocks NOT REAL; this check calls none of either.

The review's truth table (its per-frame dumps; each version's own verdict code on the same
counts): exact blocks 410 real, 57 NOT VERIFIABLE, 0 NOT REAL (8a9b719: 467 real); wrong blocks 0
real, 1 NOT VERIFIABLE (6a capture 11), 80 NOT REAL (8a9b719: 81 NOT REAL); blocks of unknown truth
5 real, 50 NOT VERIFIABLE (8a9b719: 55 real). RSP2 and net bursts 2 to 4 count as unknown: no sent
payload is held for them, only the receiver's output. The 34 wrong blocks the block lines call fit (case F, the 1200L fades
on both receivers, Watterson, the 600L review fades) stay NOT REAL.

Real captures, run through this check (truth as the review's):

| Capture | Block | Truth | 8a9b719 | Now | Blocks |
|---|---|---|---|---|---|
| RSP2 150L | whole | unknown: no sent payload; receiver output only | real | real | 4 |
| RSP2 150L | tail | not known | real | real | 1 |
| net 300L b2, b3, b4 | whole | unknown: no sent payload; receiver output only | real | NOT VERIFIABLE | 34 |
| net 300L b1 | whole | wrong (blocks 2, 9) | NOT REAL | NOT REAL | 2 |
| net 300L b1 | whole | not known | real | NOT VERIFIABLE | 9 |
| net 300L b1 to b4 | tail | not known | real | NOT VERIFIABLE | 4 |
| 6a, 2400S/1200S/600S under one block | tail | exact | real | NOT VERIFIABLE | 8 |
| 6a | tail | not known | real | NOT VERIFIABLE | 3 |
| 6a capture 11, success=0 | whole | wrong (143 bytes) | NOT REAL | NOT VERIFIABLE | 1 |

8 dB sweep on new seeds (`reencode_window.py --cases sweep --sweep-snr 8 --sweep-tag sweep-nv`, 4
random-length eom=1 messages per mode, half the 150S and 300S ones 900 to 1400 B; every decode
exact and eom=1):

| Mode | Exact blocks, receiver's bytes | Sent bytes | One flipped bit (its block) |
|---|---|---|---|
| 150S | 214/0/0 | 214/0/0 | 0/0/4 |
| 150L | 8/0/0 | 8/0/0 | 0/0/4 |
| 300S | 118/0/0 | 118/0/0 | 0/0/4 |
| 300L | 6/0/0 | 6/0/0 | 0/0/4 |
| 600S | 10/0/0 | 10/0/0 | 0/0/4 |
| 600L | 5/0/0 | 5/0/0 | 0/0/4 |
| 1200S | 11/0/0 | 11/0/0 | 0/0/4 |
| 1200L | 2/4/0 | 2/4/0 | 0/0/4 |
| 2400S | 0/13/0 | 0/13/0 | 0/1/3 |
| 2400L | 0/7/0 | 0/7/0 | 0/1/3 |

`POWER` = 0.99 is a choice, not a measurement: it lets a block be called real when one wrong
bit would escape the far-off test at most 1 time in 100. No wrong block came out real in any of
the sets above (the review's 81, the held-out 152, the sweep's 40 flipped bits, the real
captures' 3).

### Multipath: probes phased from other frames (2026-11-25)

A review of master 548cc27 ran gale-rx over a scratch store of Watterson recordings (three
messages each, with gaps) and found right bytes NOT REAL in two messages, both by "data below the
probes":

```
  block 1: probe agreement 0.845, data agreement 0.808, random-bytes control 0.247 (chance 0.250): the bytes DO NOT FIT the signal (bit errors)
  block 1 window check: far off (over 15 noise powers) 0/5720 probes, 0/5720 data, expected for right bytes 0.00, p 1 (not real under 1e-06); verdict NOT REAL: data below the probes over the block (bit errors)
```

**The cause.** Each frame's symbols were decided against a gain measured on its own frame's probes
and the next frame's. The scrambler repeats every 160 symbols, so each frame's 16 or 20 probes are a
fixed pattern. On a two-path channel, each received probe carries an echo of the probes a few
symbols before it, and over a frame that echo correlates with the pattern. A gain measured on those
probes takes part of the echo in, and so decides the same probes better than it decides the data,
whose echo is random. Probe agreement read 0.02 to 0.05 above the data of right bytes, past `FIT`.
Phasing the data from the probes that bracket them (the frame before and their own) changed nothing
(0.809 against 0.808); leaving the probes' own frame out did.

**The fix.** A frame's probes are decided against the gain of the frames either side of them, never
their own. Its data are decided against the probes around them: the frame before and its own. A
block's last two frames carry D1/D2 probes, not the scrambler, so they are no frame's reference;
where one side has no reference frame, the other side alone is used. The far-off test's gain and
noise power are unchanged. (Measuring the probe residuals against the other frames' gain as well
was tried and backed out. No reproduced case needs it, and it cost a NOT REAL: one flipped bit at
1200S 6 dB, 2 far-off data symbols at 15 noise powers, dropped to 1.)

The two messages regenerate bit for bit with this build: ModemTX, `hfchansim --preset poor`, the
review's seeds. They were cut to the message and resampled 48 to 8 kHz as gale-rx does:

| Message | Block | Before: probe / data, verdict | After: probe / data, verdict |
|---|---|---|---|
| 1200L 800 B, poor, 6 dB, seed 3 | 1 | 0.845 / 0.808, NOT REAL | 0.822 / 0.809, NOT VERIFIABLE |
| the same | tail 2 | 0.847 / 0.805, NOT REAL | 0.816 / 0.795, NOT VERIFIABLE |
| 150S 100 B, poor, 18 dB, seed 2 | 5 | 0.938 / 0.903, NOT REAL | 0.889 / 0.900, real |
| the same recording, 2400L (decoded wrong) | 1, 2 | 0.688 / 0.620, 0.423 / 0.229, NOT REAL | 0.675 / 0.645, 0.385 / 0.224, NOT REAL |

The same review read a keyed 6a dummy-load message NOT REAL with its bytes exact (6a capture 5,
1200S, 38 B, under one block, so the tail check alone covers it). Its run did not survive and does
not reproduce here. The check calls that message NOT VERIFIABLE on both the 548cc27 and this
receiver, at 8 kHz and after 8 to 48 to 8 kHz, with six different cuts, before and after this change:
probe 0.934, data 0.918 against an allowance of 0.064, and no symbol decided wrong further than 6
noise powers out. The capture's own record ends the message at 61.4 s; the review's run ended it at
60.85 s, so its receiver or its cut differed from every one tried here.

`ctest rx_reencode_window` carries the two messages (`net`: the sent bytes never NOT REAL, and one
flipped bit never real) and a 1200S 38 B stand-in for the 6a message at 4 and 6 dB, read at 8 kHz
and after 8 to 48 to 8 kHz (`sixa`).

Measured, before and after on the same audio. The counts are real / NOT VERIFIABLE / NOT REAL;
wrong blocks are blocks whose bytes differ from the sent ones.

| Set | Exact blocks, before | Exact blocks, after | Wrong blocks, before | Wrong blocks, after |
|---|---|---|---|---|
| AWGN, every mode, 30 dB down to its knee (64 messages) | 216/12/0 | 216/12/0 | — | — |
| eom=1, random lengths, every mode, 12 and 20 dB (40) | 93/7/0 | 93/7/0 | — | — |
| Review's fades, 300L to 2400L (48) | 25/5/0 | 25/5/0 | 0/0/29 | 0/0/29 |
| 1200L, −15 dB 1 s fade (18) | 25/0/0 | 25/0/0 | 0/0/11 | 0/0/11 |
| Watterson moderate and poor, 7 modes (28) | 29/34/0 | 29/34/0 | 0/2/14 | 0/3/13 |
| Fades inside block 1, case F and K files (9) | 19/0/0 | 19/0/0 | 0/0/3 | 0/0/3 |
| Held-out 2400S and 2400L, 6, 8 and 10 dB (120) | 0/205/0 | 0/205/0 | 0/55/97 | 0/56/96 |
| Real captures: RSP2, net 300L b1 to b4, 6a 0, 5, 8 and 11 | 66 verdicts | the same 66 | b1 blocks 2 and 9 NOT REAL | the same |

On the Watterson set the largest gap of an exact block (probe minus data) falls from 0.051 to 0.022;
on AWGN from 0.021 to 0.006.

**What it costs.** Two wrong blocks that the gap alone called NOT REAL are now NOT VERIFIABLE. Neither
is real:

| Block | Wrong bytes | Before: probe / data, verdict | After: probe / data, verdict |
|---|---|---|---|
| 1200L, Watterson poor, seed 2, block 1 (`success=0`) | 2 of 720 | 0.860 / 0.824, NOT REAL | 0.842 / 0.835, NOT VERIFIABLE |
| Held-out 2400S, 6 dB, message 7, block 2 | most | 0.767 / 0.671, NOT REAL | 0.752 / 0.698, NOT VERIFIABLE |

**Why no gap rule separates them.** A gap rule calls a block NOT REAL when its gap passes a
threshold. For the first block, every threshold that catches it also catches the N14 right bytes
above, on either statistic:

| Statistic | N14 1200L right bytes, gap | This 1200L block, 2 wrong bytes, gap |
|---|---|---|
| Before (probes phased from their own frame) | 0.037 (block 1), 0.042 (tail) | 0.036 |
| After (probes phased from the frames either side) | 0.013 (block 1), 0.021 (tail) | 0.007 |

Before the fix the gap was mostly the multipath bias of the probes, which right and wrong bytes
share. With the bias removed, two wrong bytes in 720 move the data by less than right bytes vary
on this channel. The far-off test does not settle it either: p 1.5e−6 against `ALPHA` 1e−6, both
before and after.

The second block is not the multipath case. It is AWGN at 6 dB on a 2400S block of 448 probes and
896 data symbols. There, right bytes leave a gap of up to 0.112 with odds of 1e−6 (the allowance
over the window frames), and the block's gap is now 0.054. On the block line the fix moved data
from 0.671 to 0.698 and probes from 0.767 to 0.752: the before figure took 0.042 of its gap from
the phase references alone, and with that gone the gap is inside what right bytes leave at this
SNR. The block's one-byte power is 0.02, so even one wrong byte would not show there.

Both blocks are accepted as NOT VERIFIABLE (coordinator ruling, 2026-11-25). Neither is real, so no
wrong block passes, and gale-rx does not count NOT VERIFIABLE as a pass.

Output lines of a block whose verdict does not change (the 150S message above, block 1, right
bytes): the figures move and the form is the same.

```
before:
  block 1: probe agreement 0.912, data agreement 0.928, random-bytes control 0.531 (chance 0.500): the bytes fit the signal
  block 1 window check: far off (over 15 noise powers) 0/680 probes, 0/680 data, expected for right bytes 0.00, p 1 (not real under 1e-06); verdict real
after:
  block 1: probe agreement 0.874, data agreement 0.935, random-bytes control 0.532 (chance 0.500): the bytes fit the signal
  block 1 window check: far off (over 15 noise powers) 0/680 probes, 0/680 data, expected for right bytes 0.00, p 1 (not real under 1e-06); verdict real
```

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
