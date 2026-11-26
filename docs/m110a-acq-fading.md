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

## 600L at the knee: a wrong success the block rule cannot stop

An independent review of this change against 548cc27 found one new wrong-byte success: AWGN 600L at
0 dB, seed 23 of its held-out set, 2 bytes wrong with `success=1`, q 3.1 dB, worst block 3.1 dB
against the 3.00 dB threshold. On the same input 548cc27 decoded exact at the same q 3.1 and worst
block 3.1. The tracking DFE changed the bytes, not the quality figure.

**The rig.** The review's harness (`ab.py`, munin's) did not survive, so every figure below comes
from a rig rebuilt from repo tools: a 200-byte payload drawn from numpy `default_rng([seed, mode
index])`; `modem_tx_wav --lead 0 --tail 0`; for CCIR, `hfchansim --preset moderate|poor --snr inf
--seed seed` (the fading only); `test/rx/synth_iq.py --snr S --seed seed --lead 0.5 --tail 0` (AWGN
in 3 kHz, 0.5 s of noise first); `m110a_rx` in AUTO on the I/Q. Its noise draws are not the
review's: at the same nominal SNR it reads about 0.3 dB easier (600L at 0 dB, q 3.2-3.5 dB against
the review's 3.1), and seed 23 decodes exact on both builds. The q and block figures come from
calibration builds that print them to 3 decimals; their verdicts and bytes are the release
builds'.

**The rule, re-applied.** The block-threshold cap (the worst block of any exact AWGN success that
clears `quality_db`, on the documented AWGN set, seeds 1-4) is, per build:

| Mode | 548cc27 cap (dB) | this branch cap (dB) | Threshold (dB) |
|---|---|---|---|
| 600L | 3.231 | 3.165 | 3.00 |
| 600S | 3.129 | 3.040 | 2.75 |
| 1200L | 5.205 | 5.105 | 4.5 |
| 1200S | 4.943 | 4.884 | 4.5 |
| 2400L | 8.423 | 8.423 | 8.25 |
| 2400S | 7.961 | 7.961 | 8.0 |

The tracking DFE lowers the 600L cap by 0.07 dB, and 3.00 dB stays under it. On CCIR at 5 and 10 dB
(the review's rows, seeds 1-8 and 21-25) no 600L success scores a worst block under 3.6 dB, so the
rule's second step, the fewest CCIR wrong-byte transmissions, has nothing to move 600L for. **The
rule leaves 600L at 3.00 dB.** (2400S's 8.0 dB is above its cap on this rig, 7.961; the 8PSK
modes keep the earlier DFE and are outside this change.)

**The knee.** 600L at 0 dB, held-out seeds 101-310 (210 inputs, the same for both builds):

| Build | Exact successes | Wrong-byte successes (95% upper bound, Wilson) | Rejected exact |
|---|---|---|---|
| 548cc27 | 200 | 2 (3.4 %) | 0 |
| this branch | 199 | 7 (6.7 %) | 2 |

Every wrong success is 1-3 bytes wrong, one Viterbi error event. Paired by input, 6 are wrong on
this branch only, 1 on 548cc27 only and 1 on both: a sign test gives p = 0.125 for "this branch is
worse at the knee", suggestive and not established. This branch's wrong decodes score worst blocks
of 3.140 to 3.224 dB, among the exact ones; raising the threshold over this branch's decodes:

| 600L block threshold (dB) | Exact | Wrong | Rejected exact |
|---|---|---|---|
| 3.00 | 199 | 7 | 2 |
| 3.10 | 176 | 7 | 25 |
| 3.15 | 128 | 6 | 73 |
| 3.20 | 42 | 1 | 159 |
| 3.25 | 10 | 0 | 191 |

No block threshold separates them: 3.25 dB removes the 7 at the cost of 189 of 199 exact decodes,
and lies above the cap. The block quality is a channel-SNR figure; a decode one error event wrong
and one exact read the same channel. Seed 23 is one instance of this residual, not a fault of the
recalibration.

**Every mode the tracking DFE runs, at its lowest AWGN point** (this branch; held-out seeds
101-400 for 600S/600L, 101-250 for the rest; per transmission; upper bound 95 %, Wilson, or
Clopper-Pearson at 0):

| Mode, SNR | Inputs | Exact | Wrong-byte successes (upper bound) | Bytes wrong | Rejected exact |
|---|---|---|---|---|---|
| 600L, 0 dB | 300 | 284 | 9, 3.0 % (5.6 %) | 1-3 | 5 |
| 600S, 0 dB | 300 | 280 | 15, 5.0 % (8.1 %) | 1-2 | 5 |
| 1200L, 3 dB | 150 | 140 | 10, 6.7 % (11.8 %) | 1-3 | 0 |
| 1200S, 3 dB | 150 | 103 | 6, 4.0 % (8.5 %) | 1-2 | 41 |
| 300L, -2 dB | 150 | 96 | 3, 2.0 % (5.7 %) | all 360 | 3 |
| 300S, -2 dB | 150 | 115 | 0 (2.4 %) | - | 0 |
| 150L, -2 dB | 150 | 97 | 1, 0.7 % (3.7 %) | all 270 | 5 |
| 150S, -2 dB | 150 | 120 | 0 (2.4 %) | - | 0 |

At 600 and 1200 bps every wrong success is the residual above: 1-3 bytes, one error event, worst
block just over the threshold. The 300L and 150L ones are another kind: whole blocks without an
EOM, every byte wrong, q 1.51-1.56 dB (threshold 1.5) and worst blocks 1.91-1.94 dB (300L,
threshold 1.75) and 1.57 dB (150L, threshold 0.5). On the same four inputs 548cc27 takes no
preamble ("Mode detection failed", single-path correlation 0.62-0.68 under its 0.70 gate); this
branch's multipath search takes them, and the 300L threshold, lowered to 1.75 dB to keep the
exact -2 dB decodes (worst blocks 1.97-2.01 dB), admits them. Not addressed here.

`test/rx/repro_rake_escape.sh BIN_DIR OUT_DIR` rebuilds these four inputs (300L seeds 144, 208,
211 and 150L seed 208, AWGN -2 dB, the rig above), writes each as I/Q and as a 48 kHz mono FLAC
(m110a_rx's own audio of that I/Q, the form gale-rx records) with a closed sidecar, and prints the
verdict and the bytes against the payload sent: on the I/Q, and through a model of gale-rx's audio
call (8 kHz, then 96 kHz cf32 at +10 kHz). On this branch's build all four are `success=1` on the
I/Q with every byte wrong. Through the audio model only 300L seed 211 still is (q 1.6 dB); the
others read q 1.2-1.4 dB, under 1.5, and are refused. gale-rx's own verdict on the FLACs is gale-rx's
to give.

**A second measure, tried and dropped: the Viterbi path reliability.** The 75 bps gate's term
(docs/m110a-75bps.md): over the message's information bits, to the end of the EOM, the sum of
exp(-|L|/s), |L| the max-log reliability of each bit (`m75::bit_reliability`, the same K=7 code)
over the Viterbi input. It was computed in a calibration build for 150-1200 bps and swept on
calibration seeds 1001-1100 at the knee (600L/600S at 0 dB, 1200S/1200L at 3 dB), scale s = 16
(s = 8 gives the same picture), per transmission exact / wrong successes:

| Highest sum allowed | 600L | 600S | 1200S | 1200L |
|---|---|---|---|---|
| no term | 94 / 4 | 96 / 4 | 77 / 1 | 69 / 0 |
| 3.0 | 92 / 2 | 94 / 3 | 77 / 1 | 69 / 0 |
| 1.0 | 85 / 2 | 91 / 2 | 75 / 1 | 68 / 0 |
| 0.3 | 83 / 1 | 84 / 2 | 66 / 1 | 60 / 0 |
| 0.1 | 75 / 0 | 69 / 0 | 48 / 1 | 55 / 0 |

Clearing every wrong success costs 19 of 94 exact 600L decodes and 27 of 96 600S ones, and the
1200S one stays at any setting: it sums 0.035, below the median exact decode (0.045). About half of
the knee's wrong successes are decoded with a wide margin. On 600L seed 1017 (2 bytes wrong) the
six bits with the lowest |L| are exactly the six wrong bits, but the true path lies 70 behind the
decoded one, while exact decodes reach down to 2. The decoder followed a run of strongly wrong
soft bits. Inference, not measured: the tracking DFE feeding its own wrong decisions back makes
such runs (error propagation). No term over the decoder's own metrics can see an error the soft
bits themselves assert. The measure was not kept.

**What catches these: a byte check against the signal.** `test/rx/reencode_check.py`, which gale-rx
runs on 110A decodes (greybeard-ale `tools/gale_rx.py`), re-encodes the bytes and checks them against the received symbols. On
seed 1017's audio (the m110a_rx `--dump-audio` output) it gives block 1 NOT REAL (49 of 5720 data
symbols far off, p 1.3e-198); on an exact 600L 0 dB decode, the control (the 54-byte MS-DMT
reference transmission through `synth_iq.py`, seed 23), NOT VERIFIABLE (0 far off). That is one pair, not a measured rate.

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
