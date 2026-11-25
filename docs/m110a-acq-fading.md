# 110A under frequency-selective fading: acquisition, the tracking DFE, MLSE quality, --ppm

On the MARS net of 2026-11-25, four 110A bursts were recorded by two receivers (munin's
FT-897D and an RSP2), with notches sweeping across the band. Before this change only one of
them got past the preamble gate, and that one decoded block 1, garbled block 2, and lost block 3.
Its MLSE_ADAPTIVE decode reported success at 82.5 dB over two blocks that reencode_check
calls NOT REAL. This page says what was wrong and what the receiver does now. Bursts are
named by UTC time.

## Acquisition through a channel

`BrainDecoder::find_preamble` correlates the 288-symbol common segment at one sample per
symbol and takes the preamble when that single-path correlation exceeds 0.70. Under multipath,
the common segment arrives as several delayed copies. The single-path correlation then sees one
of them, and the delay spread costs it the share of the energy the other paths carry. On the
night's bursts it read 0.43 to 0.72. A 7-tap channel fitted to the same symbols explained 66 % to
97 % of their energy.

When the single-path search misses its gate, `find_preamble_multipath` scores every start t by

    m(t) = sum over lags k = -3..3 of
           sum over the four 72-symbol pieces b of |sum_i x[t + (72 b + i + k) sps] conj(c[72 b + i])|^2
           / (72 sum_i |x[t + (i + k) sps]|^2)

This is the share of the received energy explained by a channel of 7 symbol-spaced taps (±1.25 ms)
that is constant over each 72-symbol piece. The pieces add without their phase, so a channel
that changes over the 120 ms segment still counts in full. Each lag is normalised by the energy
that lag read. An earlier form divided every lag by the lag-0 energy, and a start over digital
silence beside a signal then read up to 10^35. A clean preamble through any such channel scores
about 1. Noise scores about 0.1, 4 × 7 / 288. The preamble is taken at the highest m(t), when
m reaches 0.50:

| Input | Highest m |
|---|---|
| band-limited noise, 30 min, 8998 detection windows | 0.214 |
| 110D interop set, 60 files | 0.237 |
| 110B App C, the two T8 recordings and the net's App C burst | 0.218 |
| 3G ALE, T9 set | 0.211 |
| 2G ALE vectors, 42 files | 0.446 |
| the net's 110A bursts, every chain, both receivers | 0.571 to 0.938 |

An ALE FSK tone 75 n Hz from the carrier meets the common segment's spectral lines (its period
is 32 symbols). It correlates at every lag alike, so the 7 lags add it up. That is the highest
non-110A score.

The start chosen is the centre of the 7-lag window over the channel's energy. The DFE's
feed-forward taps (±5 symbols) cover the paths either side of it. The channel estimate
(`mp_taps`, from the found frame's last two common blocks) then reads the preamble through a
rake (maximal-ratio combining of the 7 lags):

- **D1/D2 and the countdown**: each Walsh pattern's correlation through the rake. It is
  phase-free, as before.
- **The carrier line** (`refine_carrier_multipath`): over a 24-frame preamble the channel
  changes, so each frame gets its own estimate. The residual frequency is the mean of the
  frames' in-frame phase slopes, weighted by each frame's combined energy.
- **The end of the signal** (`frame_coherence_multipath`): the single-symbol probe coherence
  reads only the sampled path's share. On the 01:18:50Z burst, whose strongest path held a
  quarter of the energy, no frame reached 0.5, so no frame counted as signal and nothing was
  decoded. With a multipath preamble, each frame's probes are combined over the 7 lags with a
  channel estimated from the probes of the 4 frames before it (never the frame itself, so noise
  still reads about 1/K). It is causal, so the streaming receiver counts the same frames.

A preamble the single-path search finds is read exactly as before.

**A forced mode** (`--mode`, `RxConfig::mode`) takes the best multipath start even under 0.50
(`gate_bypassed`). Such a preamble stands only when its D1/D2 read the forced mode itself; the
existing check otherwise refuses success (`preamble_contradicts`). The data probes and the
quality gate still judge the decode. Before, a forced mode under the gate gave "No data symbols
extracted".

## The tracking DFE

After acquiring the 01:04:15Z burst, the receiver lost block 2. The cause was not timing. Over
the burst the strongest path moves between -1 and +4 symbols as paths fade in and out, and every
offline equaliser tried decided 87 % of block 2's data symbols right. Those were T- and
T/2-spaced, RLS and LMS, scored against the re-encoded truth. Our DFE decided 81 % of block 1
and 66 % of block 2, for four reasons:

1. **Data decisions were free 8-PSK.** 150 to 600 bps send one of two antipodal points around each
   symbol's scrambler value, and 1200 bps one of four. At 1-2 dB a free 8-PSK decision is wrong far
   more often, and both the feedback line and the LMS update take it. `DFE::process_constrained`
   now decides among the mode's own points (`FrameDfe` `data_step`: 4, 2 or 1).
2. **The LMS step was too slow for HF fading.** At the nominal symbol level of 0.55 RMS, the steps
   (0.005, 0.002) are an effective unit-power step of about 0.0015. They are now 4 times larger
   (`FrameDfe::TRACKING`). The factor was compared at 3, 4 and 6 on the CCIR and AWGN sets, over
   the modes this DFE runs. CCIR seeds 1-5 decoded 86, 88 and 89 transmissions exact, none with
   wrong bytes. AWGN gave up 0, 1 and 6 exact decodes, because the step's noise lowers a flat
   channel's quality figure.
3. **A probe-only phase tracker ran in front of the DFE** and fought it in fades. With the DFE it no
   longer runs; the DFE tracks the carrier itself.
4. **The DFE passed the received probes through.** The codec's 180-degree check reads the probes,
   so with the tracker gone it read the channel's own phase and flipped every bit. The DFE now
   passes on its own a-priori outputs on the probes, in the reference phase as the data is.

With all four (at 6 times the step), the DFE decides 87 % of the data symbols right in every block
of the 01:04:15Z burst, and it decodes exactly: 408 bytes and the EOM.

**Only 150 to 1200 bps.** The 8PSK modes (2400 and 4800 bps) keep the earlier DFE and the phase
tracker in front of it. With tracking, 2400S and 2400L on CCIR seeds 1-5 decoded 11 more transmissions
exact. But 6 transmissions with wrong bytes were reported as successes: five with 1 byte wrong and one with 163. The earlier DFE had 1. Their worst blocks were 8.3 to
11.0 dB, among exact ones at 9.0 to 11.5 dB, so no threshold parts them. `frame_dfe_equalize`
and `FrameDfe` keep the earlier behaviour when `data_step` is 0, their default. `ModemRX` and the
streaming receiver pass 4 (BPSK) or 2 (QPSK), or 0 for 8PSK.

## MLSE_ADAPTIVE quality

`AdaptiveMLSE` admits only the known symbol at each probe position, so its output probes equal
the reference. Scored on them, every decode read about 82 dB, noise included, and every block
read 40 dB. It now reports the DFE's kind of figure: per frame, the probes against the channel
estimate held before they update it (the a-priori error), and `judge()` and the block gate read
it. On the 01:04:15Z burst its decode is now `success=0` at -2.7 dB (blocks -3.2 and -8.2 dB).
In AWGN, 600S at 0 dB had been a success with wrong bytes; it is now rejected (q 0.6 and 0.9 dB).
Exact decodes still pass, for example 2400S at 11 dB (q 11.8) and 1200L at 5 dB (q 5.9).

## --ppm

`m110a_rx --ppm P` used to move only the dial, by dial × P, so with `--dial 0` it did nothing. A
receiver clock P ppm fast also takes P ppm more samples per second, so the symbol timing drifts.
`SdrFrontEnd` now resamples the 48 kHz audio by 1 + P × 1e-6 (cubic interpolation). That is done
on the audio, not by giving `IQSource` the true rate, because its decimation plan drops a stage
when the ratio falls just under an integer. On a 2400L message (12 dB, 1500 bytes) from a receiver
200 ppm fast, only the branch given `--ppm 200` decoded it, exact at q 13.2 dB. At -200 ppm, on
600L, the same: exact at q 12.9 dB. The sender's symbol clock is not the receiver's to know; the
data grid's drift tracking follows it (docs/m110a-timing-steps.md).

## The gate, recalibrated

By the rule of docs/m110a-quality-gate.md: a block threshold may not exceed the worst block of any
exact AWGN decode that clears `quality_db`. Under that cap it takes the value with the fewest CCIR
wrong-byte transmissions, then the fewest rejected exact ones. Two thresholds moved:

- **300L, 2.25 → 1.75 dB.** The multipath search now acquires 300L at -2 dB in AWGN, which it
  missed before, and those decodes are exact with worst blocks of 1.97 to 2.01 dB. On CCIR (seeds
  1-8) every value from 1.5 to 2.25 dB gives the same transmissions. The cap moved, not the data.
- **1200S, 4.75 → 4.5 dB.** It rejects one exact CCIR transmission fewer and admits no wrong one.
  1200H shares the code path but is not in the sets, so it keeps 4.75.

The quality_db thresholds were set from a reference set this branch does not reproduce, and are
unchanged.

## Measured

The base is origin/master 018f0c6. Both builds decode the same impaired files, through munin's
A/B harness (`ab.py`: `m110a_rx` on I/Q, per transmission). On this harness the base gives
exactly the master figures in docs/m110a-quality-gate.md: 111/3, 68/1 and 99/9. Exact means a
reported message with success, EOM and the payload. Wrong means a reported success whose bytes are
not the payload (a correct prefix without EOM is not counted wrong).

| Set | Base: exact / wrong | This branch: exact / wrong |
|---|---|---|
| CCIR moderate and poor, 5 and 10 dB, seeds 1-5 (180) | 111 / 3 | 140 / 1 |
| held-out seeds 6-8 (108) | 68 / 1 | 84 / 0 |
| AWGN, the 156-case set | 99 / 9 | 106 / 9 |
| fade suite (long modes, -10/-15/-20 dB fades, 48) | 15 / 0 | 25 / 0 |
| 4800S at 18, 19, 20 dB, seeds 1-8 (24) | 24 / 0 | 24 / 0, identical per message |

On CCIR, 1200S goes from 2 to 19 exact transmissions (seeds 1-5) and from 1 to 12 (seeds 6-8).
600S goes from 13 to 20, and 1200L from 17 to 20. The one wrong success left is the 2400S case
the base gets wrong, with the same bytes. None of munin's eight DFE cases is a wrong success. On
AWGN the branch decodes 300S, 150S and 150L at -2 dB, which the base never acquires (+3, +3, +2).
It gives up one exact 1200S decode at 3 dB, which reads 4.99 dB against the 5.0 dB quality
threshold.

**Acquisition rate against SNR** (hfchansim's own SNR; whole-buffer ModemRX decodes; 2400S, 600S,
2400L, 600L, 300L, 150L, seeds 1-3; 18 per point; "acquired" means a preamble taken):

| Channel | -15 dB | -10 dB | -5 dB | 0 dB | 5 dB |
|---|---|---|---|---|---|
| moderate, base | 0 | 6 | 14 | 18 | 18 |
| moderate, branch | 4 | 8 | 18 | 18 | 18 |
| poor, base | 0 | 2 | 18 | 18 | 18 |
| poor, branch | 5 | 18 | 18 | 18 | 18 |

**False detections.** BrainDecoder's detection pass slid over each file in 0.6 s windows, 0.2 s
apart (`m110a_rx` reported no message on any of these files, base or branch):

| Input | Windows | Preambles taken | Highest multipath metric |
|---|---|---|---|
| band-limited noise, 30 min | 8998 | 0 | 0.214 |
| 110D interop set, 60 files | 2225 | 0 | 0.237 |
| 110B App C, the two T8 recordings and the net's App C burst | 1786 | 0 | 0.218 |
| 2G ALE vectors, 42 files | 1595 | 0 at the 0.50 gate | 0.446 |
| 3G ALE, T9 set | 269 | 0 | 0.211 |

**Captures.** The 6a captures (12) and the verified RSP2 150L of 2026-11-23 19:59:58Z give the
same bytes and the same reencode_check verdicts as the base. So do the 2026-11-23 net 300L bursts
2-4. Burst 1 is `success=0` in both; its block 2 goes from NOT REAL to NOT VERIFIABLE, and block 9
stays NOT REAL. The 2026-11-23 12:37Z 300L burst, which never decoded, was a base success with
both blocks NOT REAL; it is now `success=0`. 110D's `m110d_rx` and `m110d_chan` are byte-identical
to the base build's.

**The 2026-11-25 net.** All four 110A bursts are acquired on both receivers, with the mode their
D1/D2 name. 01:18:50Z (600L) is a success: 122 bytes and the EOM, exact against the reference
decode. 01:04:15Z (300L) decodes 408 bytes and the EOM. Its blocks 1 and 2 are exact against the
reference, and block 3 has 2 bytes wrong; it is `success=0`, worst block 1.7 dB. 01:06:26Z and
01:07:17Z (300L) decode to 367 bytes and the EOM on both receivers. The two receivers' bytes agree
on all 367 for each burst. The RSP2's 01:07:17Z decode is a success (worst block 2.1 dB); the
other three are not. reencode_check (master a97e019) finds every block of 01:07:17Z, both
receivers, fits the signal, NOT VERIFIABLE. On the RSP2's 01:06:26Z its tail block 3 does not fit
(probe 0.752, data 0.688).
