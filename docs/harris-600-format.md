# Harris 600 bps MELP (600H): what the bitstreams show

The Harris RT-1694D sends 600 bps voice under D1, D2 = 5, 7, which this repo's receiver
calls `600H` (`docs/harris-melpe.md`, Modem). The payload is not STANAG 4591 600 bps MELPe.
The engineers and source code that made it are gone, so this document recovers what it
can from the published papers and the decoded bits alone.

Every statement carries one label:

- **[paper]**: read from a cited paper or standard.
- **[measured]**: measured on the payloads by a script in `test/harris/harris600/`
  (commands at the end).
- **[inferred]**: a reading of measurements or papers that no measurement here settles.
- **NOT DETERMINABLE**: cannot be settled from what is at hand. The text says what would
  settle it.

## 1. What the papers say

### Harris, MILCOM 2001 [1]

- [paper] The coder has a standard MELP 2400 front end (MIL-STD-3005 analysis, with the
  noise pre-processor). A block buffer collects four frames of parameters, and each
  parameter has its own block vector quantiser.
- [paper] Frames are **25 ms**, and a block is four frames, **100 ms**. "This yields a total
  of **sixty bits per block** of duration 100 ms, or 600 bits per second."
- [paper] Superframe: the 100 ms block is the only unit. The paper describes no header,
  sync bit or superframe above it.
- [paper] Bits per 100 ms block (Table 1) and how each parameter is quantised:

| Parameter | Bits | Quantiser [1] |
|---|---|---|
| Aperiodic flag (jitter) | 0 | Not sent: runs are short and rarely recur in a block. |
| Band-pass voicing | 4 | A 4-bit codebook of the most probable voicing transitions over the four frames (16 patterns of 4 × 5 band decisions). |
| Energy (gain) | 11 | A 2048-entry VQ of both MELP gains (G1, G2) over four frames: an 8-dimensional vector, K-means trained. |
| Fourier magnitudes | 0 | Not sent: a flat vector for unvoiced frames, one fixed vector for voiced frames. |
| Pitch | 7 | Final MELP pitch, median-filtered (order 3), then four frames vector-quantised with a 128-entry codebook, searched on voiced frames. |
| Spectrum (LSF) | 10 + 10 + 9 + 9 | Four frames of LSFs, four-stage MSVQ, M-best search with perceptual weighting. Trained by K-means then generalised Lloyd, on a sliding window. |
| **Total** | **60** | |

- [paper] The paper gives no bit transmission order, no codebook contents, and no index
  ordering.
- [paper] The coder runs on a TI TMS320VC5416 in the RF-5800H/PRC-150. The 2400 bps MELP
  source code is TI 54x assembly combined with Harris's MELP 600 vocoder.

### Bharat Electronics, NCC 2009 [2] ("Q8")

- [paper] A different, later coder, built from [1] as a guide. It uses **27.5 ms** frames
  and four-frame **110 ms** blocks of **66 bits**: band-pass voicing 6, energy 12, pitch 10,
  spectrum 10 + 10 + 9 + 9. Aperiodic flag and Fourier magnitudes are 0. Indices are MSVQ
  with M-best search, LBG-trained on TIMIT plus Indian speakers.
- [paper] It does not describe a 60-bit frame or Harris's layout. It is not the Harris
  format, and it is cited here only because the brief named it.

### The shared MELP model [3], [4], [5]

- [paper] MELPe 2400: a 22.5 ms frame of 54 bits. Pitch and overall voicing take 7 bits
  (99-level log quantiser from 20 to 160 samples, Hamming weight 1 or 2 codes reserved).
  Band-pass voicing takes 4 bits. G2 is 5 bits, uniform from 10 to 77 dB. G1 is 3 bits,
  adaptive. LSF is a 4-stage MSVQ of 7 + 6 + 6 + 6 bits. Fourier magnitudes take 8 bits,
  the aperiodic flag 1 and sync 1. In unvoiced frames, Hamming parity over LSF stage 1 and
  the gains replaces 13 of the bits ([3] s5.3, s5.4, Annex A.5.2.19; [4] Table 1).
- [paper] MELPe 1200: 81 bits per 67.5 ms (three frames), sync bit first ([3] Annex D;
  [4] Table 2).
- [paper] STANAG 4591 600 bps is Thales's coder, not Harris's: 54 bits per 90 ms (four
  22.5 ms frames), no sync bit, 5 voicing-mode bits first ([3] Annex M, published
  separately; [4] Tables 3 and 4).
- [paper] The MELPe noise pre-processor tech report [6] covers voice activity and noise
  estimation only. It says nothing about the bitstream.

## 2. Stream structure

### Header, blocks, trailer

[measured] Every stream has the same shape: the common 54-bit header, a whole number of
vocoder units, then a 108-bit end-of-voice trailer, then zeros up to the byte the EOM search
stopped at. In the nine 600_x and 1200_x streams, and in melp600, the trailer begins exactly
on a unit boundary counted from bit 54.

[measured] In melp2400 the final run of ones is 109 bits long (bits 15173-15281). Taking
the trailer to start at 15174 assumes that it falls on the 54-bit grid from bit 54, as in
the other ten streams. So the melp2400 row below is [inferred], and the 109th one is the
last bit of the frame before it.

| Stream | Unit | Units from bit 54 | Duration | Trailer at bit | Trailer |
|---|---|---|---|---|---|
| 600_1 | 60 | 56 | 5.6 s | 3414 = 54 + 56·60 | W, W' |
| 600_2 | 60 | 53 | 5.3 s | 3234 = 54 + 53·60 | W, W' |
| 600_3 m1 to m4 | 60 | 39, 27, 16, 8 | 3.9, 2.7, 1.6, 0.8 s | 54 + N·60 | W, W' |
| melp600 | 60 | 81 | 8.1 s | 4914 = 54 + 81·60 | 108 ones |
| 1200_1 | 81 | 112 | 7.6 s | 9126 = 54 + 112·81 | W, W' |
| 1200_2 | 81 | 71 | 4.8 s | 5805 = 54 + 71·81 | W, W' |
| 1200_3 | 81 | 73 | 4.9 s | 5967 = 54 + 73·81 | W, W' |
| melp2400 | 54 | 280 [inferred] | 6.3 s | 15174 = 54 + 280·54 [inferred] | ones (109-bit run) |

- Header (stream order):
  `011111011010100110011010100100000100111110000011110010`
- First trailer word W (54 bits):
  `101001010010100010100000000100010101000101110011111101`
- Second trailer word W' (54 bits), W with its last bit cleared:
  `101001010010100010100000000100010101000101110011111100`

[measured] In all nine W-streams the second word differs from W in the last bit only.
W followed by W occurs in none of them.

[measured] The streams fall into two groups:

- 600_x and 1200_x end in W, W', and stream bit 54 is 1.
- melp600 ends in 108 ones and melp2400 in a 109-bit run of ones, and stream bit 54 is 0.

[inferred] So the set was recorded in two sessions or radio settings. The modem stage's
audio passband and carrier offset split the files the same way (`docs/harris-melpe.md`).

[inferred] The header and each trailer word are 54 bits whatever the voice rate, which is the
2400 bps frame size. They look like start- and end-of-voice framing words written by the
radio, outside the vocoder. NOT DETERMINABLE: whether they are STANAG 4591 Annex O
framing or Harris's own. Annex O is not in the edition at hand.

[measured] In 600_x, column 0 of the first block is 1 in all six messages (p = 0.53
overall; chance about 1 in 50). In 1200_x the same bit is the first frame's sync bit. NOT
DETERMINABLE whether bit 54 is a flag or chance.

### The 60-bit period

[measured] period_z from bit 54 (`period.py`):

| Stream(s) | P = 20 | 30 | 54 | **60** | 81 | 120 |
|---|---|---|---|---|---|---|
| 600_1 | 5.7 | 5.3 | -0.5 | **8.3** | 2.1 | 5.7 |
| 600_2 | 4.0 | 5.7 | -0.7 | **8.4** | -0.1 | 4.9 |
| melp600 | 17.3 | 4.8 | 1.0 | **13.9** | 1.1 | 10.3 |
| pooled 600H (7) | 11.8 | 9.1 | 0.9 | **16.0** | 0.5 | 10.1 |
| pooled 1200H (3) | -0.1 | 2.4 | 22.0 | 1.2 | **41.7** | 1.3 |
| random 3000-bit streams, max over P 4..130 | median 2.8, max 4.3 | | | | | |

- [measured] The grid is anchored to the header. Each 600_x stream's 60-column profile,
  cut from bit 54, best matches the others at shift 0, all six. The trailer positions above
  say the same for all seven 600H streams.
- [measured] The bits go out in vocoder order. Reading each byte reversed drops the pooled
  six-stream z at 60 from 11.6 to 3.8.
- [measured] No column alternates from block to block: the lowest lag-1 autocorrelation is
  -0.11. So 600H has no sync bit, which agrees with [1].
- [measured] The 20-bit "sub-period" is not a separate unit. Every period-20 bias is also
  a period-60 bias. In the 600_x streams the three 20-bit thirds of the block profile do not
  correlate (r = -0.13, 0.23, -0.02). In melp600 they do (0.39, 0.24, 0.60), which is why
  its z at 20 is high. Section 3 measures the structure that drives it.

## 3. Block layout hypothesis

Block columns are numbered 0 to 59 in stream order, from the block start. The evidence uses
the 199 whole blocks of the six 600_x messages, and melp600 (81 blocks) checks it.

### The hypothesis (L1)

| Columns | Bits | Field [inferred] | Evidence [measured] |
|---|---|---|---|
| 0-18 | 19 | Spectrum, part A | Same bias profile as cols 41-59 (below). Weakly tied to the excitation fields (mean \|r\| 0.105 with 19-22, 0.107 with 30-40). |
| 19-22 | 4 | Band-pass voicing | Internally dependent (mean \|r\| 0.204, null 0.057). Holds from block to block more than chance: 36 against 24 over all blocks, 21 against 15.7 over non-pause blocks only. Leans to 0000 in pauses (18 of 34). Tied to cols 30-40 (0.160) and to cols 23-29 (MI 0.27 bit). |
| 23-29 | 7 | Pitch | The slowest, most repeating field: same value as the previous block 48 times against 13 by chance. 28 of those 48 are the pause value; over non-pause blocks only, 20 against 7.4. Mean \|r\| 0.209. Only 39 of 128 values seen (entropy 4.5 bits). One fixed value in pause blocks (partly circular: pause blocks are defined by that value). |
| 30-40 | 11 | Energy (both gains, four frames) | Internally dependent (0.142). Leans toward zeros in pauses (cols 31-40: z -0.6 to -3.0, nine of ten below -1.2). Holds 5 against 1.7 (non-pause only: 3 against 1.2, too few to carry weight). Most common pause value is all zeros (5 of 34). |
| 41-59 | 19 | Spectrum, part B | Bias profile matches cols 0-18 at r = 0.91 (melp600: 0.86), best at an offset of exactly 41. |

[inferred] The widths 4, 7 and 11 are exactly [1]'s band-pass voicing, pitch and energy,
and 19 + 19 = 38 is its spectrum. Naming each field by its width is the main inference.

[measured] Limits of the evidence for the three middle fields:

- **Voicing and gain rest mostly on pauses.** Their hold excess and their zero lean come
  largely from pause blocks. The gain counts are too small to carry weight.
- **The pause definition is partly circular.** Pause blocks are defined by the value of
  cols 22-29, so "one fixed pitch value in pauses" holds by construction. Only the
  behaviour of the other columns in those blocks is independent evidence.
- **The pitch start at 23 is not pinned.** Over non-pause blocks, 7-bit windows starting at
  20, 21, 22 and 23 hold 14, 14, 15 and 20 times, against 4.3, 4.0, 5.5 and 7.4 by chance.
  That is nearly the same excess for each start.
- **The voicing window is not confirmed either.** The best 4-bit hold window starts at 23,
  inside the pitch candidate, not at 19 (`windows.py 4`). The 19-22 boundary rests on
  internal dependence and the 41-column offset of the outer chunks, not on the repeat test.

[inferred] Inside the two 19-bit chunks the stage boundaries are not resolved:

- In chunk B, the dependence measure peaks at a 10 | 9 split (cols 41-50 | 51-59; ratio
  1.36).
- In chunk A, it peaks at 15, not at 10. This measure is weak on trained-VQ indices.
- [1] says the stages are 10, 10, 9 and 9 bits. The data are consistent with each chunk
  holding one 10-bit and one 9-bit stage.
- Two readings fit. Either chunk A is stages 1 and 3 and chunk B is stages 2 and 4, or
  each chunk codes a frame pair, as STANAG 600 does ([4] Table 3).
- NOT DETERMINABLE which.

[measured] Cols 10-18 and 51-59 sit at the null for dependence on everything else
(0.051-0.070). That is what fine-detail later MSVQ stages look like. Cols 0-9 and 41-50 are
mildly tied to the excitation fields, as a first spectral stage would be (voiced and
unvoiced spectra differ).

[inferred] Bit significance inside each field is not known. Column 26 behaves as the top
bit of a slowly moving number: the smoothness ratio is 0.66-0.70, against a shuffled
baseline of 0.85-0.90, read MSB-first from 26 or LSB-first ending at 26. That suggests the
pitch codebook's order follows pitch at least roughly. NOT DETERMINABLE: MSB-first or
LSB-first inside fields.

### The layout read straight off [1] Table 1 (L2) does not fit

[measured] L2 is band-pass voicing 0-3, energy 4-14, pitch 15-21, spectrum 22-59. Under it:

- the 4-bit voicing field holds less often than chance (10 against 14);
- the 7-bit pitch field holds 1 against 3.6;
- the steady 7-bit field lands inside the first spectrum stage.

`fields.py L2` prints the comparison.

### Pauses

[measured] The value 01100010 in cols 22-29 runs at the starts of messages: the first 10
blocks of 600_2, 4 of m3, 3 of m4. It also runs inside 600_1 (blocks 3-8 and 44-49). It
holds in 34 of 199 blocks. melp600 uses the same value in 7 of its 81 blocks.

[inferred] That is the code for "four unvoiced frames" or "pause". Under L1 it is col 22
(the voicing LSB) = 0 plus pitch = 1100010.

[measured] Not every pause gets it. m1 never shows it in 3.9 s.

### Shared values across messages

[measured] Values of cols 22-29 recur as runs across messages. Examples:

- `00111010 00111001` appears in five messages, melp600 included;
- `11101101 01110110 01001011 01001110` appears in m1 and in m2;
- `00011101 01101001 01010110` appears in 600_1 and 600_2, and its last two values in m3
  and melp600.

At the best lags, message pairs match 3 to 7 blocks against 0.5 to 1.7 by chance
(`align.py`).

[measured] Blocks matched that way agree on the other 52 columns barely more than random
pairs do (best cols 20-21, z 2.0-2.5). melp600 shares the same frequent values.

[inferred] So the recurring values belong to the pitch codebook and a narrow-pitched
talker. They do not show that the same phrases were spoken.

## 4. Are 1200H and 2400V standard STANAG 4591 after the header?

### 1200H: standard 1200 bps MELPe frames, from bit 54 to the trailer

[measured] Evidence:

- **Sync:** the sync bit alternates in 99-100 % of frames (`docs/harris-melpe.md`).
- **Layout:** the per-column profile matches the reference coder's own 1200 bps output on
  other speech (`colprofile.py`):

| Test | Share-of-ones profile | Lag-1 profile |
|---|---|---|
| Harris 1200H against the coder | +0.69 | +0.61 |
| Random frames against the coder, 200 draws of 256 frames | mean -0.01, sd 0.11, max +0.29 | mean -0.01, sd 0.11, max +0.25 |
| The coder against itself, on different speech | +0.41 | +0.30 |

  Harris 1200H sits about 6 sd above the random spread on both profiles.

- **Time structure:** each decode, in original frame order against 12 decodes of the same
  frames shuffled (sync re-set) (`timestruct.py`):

| Decode | track | Energy-track lag-1 correlation | Pause share |
|---|---|---|---|
| 1200_1 | 0.328 (shuffled 0.235, z +4.9) | 0.929 (0.668, z +5.7) | 0.346 (0.263, z +7.5) |
| 1200_2 | 0.392 (0.273, z +4.6) | 0.923 (0.664, z +9.6) | 0.264 (0.198, z +4.8) |
| 1200_3 | 0.174 (0.110, z +4.0) | 0.921 (0.690, z +8.3) | 0.394 (0.311, z +5.3) |
| Coder on speech, 1200 (2 files) | z +2.8, +5.0 | z +3.9, +8.4 | z +5.1, +8.2 |
| Random frames, 1200 (one draw of 110 frames) | z -0.6 | z +1.0 | z -0.0 |

  1200_3 passes on order (z +4.0 to +8.3) even though its track is low. So its low score
  in `docs/harris-melpe.md` is the measure, not the frames.
- **The talker:** the decoded 1200H audio has its f0 median at 99, 105 and 110 Hz
  (1200_1, 1200_2, 1200_3). The 10th to 90th percentiles are 93-121 Hz for 1200_1,
  98-121 Hz for 1200_2 and 96-145 Hz for 1200_3. So 93-121 Hz covers 1200_1 and 1200_2,
  and 93-145 Hz covers all three. The decodes show phrase-length bursts separated by pauses
  (`timeline.py`).
  [inferred] One male talker.
- The syllabic-band share of the energy modulation spectrum (2-8 Hz) did not separate
  speech from shuffles on these short files: 1200_1 reads 0.17 against 0.57 shuffled. It
  is reported, not used.

### 2400V (melp2400): not in the STANAG 4591 2400 bps bit layout

[measured] The frames are real: 54-bit period z 109, and one column alternates in 100 % of
frames. But the contents fail the standard tests (`check2400.py`):

| Reading | Pitch codes with exactly 2 bits set (STANAG: never sent) | Unvoiced-frame Hamming parity consistent |
|---|---|---|
| Reference coder on calibration speech | 0.0 % | 13/13, 7/7, 15/15, 11/11 |
| melp2400, frames from bit 6 (alternating bit = B_54) | 26.1 % | 1/14, 0/8, 0/7, 3/9 |
| melp2400, from bit 5, each frame reversed | 15.2 % | 0/2, 1/4, 1/3, 0/3 |
| melp2400, from bit 6, inverted | 8.8 % | 1/1, 1/1, 0/1, 0/0 |
| melp2400, from bit 54, 6-bit words last first | 13.6 % | 0/3, 0/4, 1/4, 0/4 |
| Random frames | expected 21/128 = 16.4 % (one draw of 300: 18.7 %) | 0/3, 0/5, 0/4, 0/3 (one draw) |

- The per-column profile matches the coder's own 2400 output at only +0.19 (share of ones)
  and +0.28 (lag-1). That is inside the random spread: 200 draws of 283 random frames give
  mean -0.01, sd 0.14, max +0.39 (share of ones) and mean +0.00, sd 0.14, max +0.37
  (lag-1). The coder against itself on different speech gives +0.72 and +0.75.
- Decoded as standard, the original order beats the shuffles only weakly: z +2.1 on track
  and on energy lag-1. Calibration speech reaches z +7 to +14.
- It is not keystream-scrambled. Outside the alternating column it is strongly structured:
  share-of-ones chi-square z 112, 20 of 53 columns with a significant lag-1, col 4 at 0.75.

[inferred] melp2400 is a 54-bit, 22.5 ms frame with one alternating bit. Three things are
not excluded:

- Harris's own 2400 bps MELP layout;
- an arbitrary bit permutation of standard MELPe 2400, beyond the serialisations tested;
- LPC-10e (FED-STD-1015), which has the same 54-bit, 22.5 ms geometry with a sync bit. No
  LPC-10e encoder is at hand to test it.

[inferred] On the grid from bit 54, where the trailer is taken to fall on a frame boundary
as in the other ten streams, the alternating bit is frame column 5, not 53. NOT DETERMINABLE
here: the layout. The same known audio at 2400V would settle it (section 6).

### What the header does to alignment

[measured] All vocoder data starts at bit 54, right after the header, in every mode: 600H
blocks, 1200H frames, and the trailer grid of 2400V.

A reader that searches for the 2400 sync bit as B_54 lands at bit 6, 48 bits before the
frame start the trailer implies. A reader that assumes frames from bit 0 is off by 54
(one 2400 frame, two-thirds of a 1200 frame, 54 of 60 bits of a 600H block). That is why
the 600H period showed nothing at 54 and why the payload is not a whole number of frames.

## 5. Cross-check: the same talker or phrases across modes?

- [measured] Durations do not pair up:
  - 600H messages run 5.6, 5.3, 3.9, 2.7, 1.6, 0.8 and 8.1 s;
  - 1200H run 7.6, 4.8 and 4.9 s;
  - 2400V runs 6.3 s.
  No 600H message is within 0.3 s of a 1200H or 2400V one.
- [measured] Pauses: the 1200H decodes start with 0.4-0.5 s of silence and show regular
  phrase bursts. Among the 600H messages, only 600_2 (1.0 s), m3 and m4 start with the pause
  code. The code marks only some pauses, so a block-by-block pause match against 1200H is
  not possible.
- [measured] Pitch: the 1200H talker is narrow (f0 roughly 93-121 Hz in 1200_1 and
  1200_2). The 600H pitch candidate uses only 39 of 128 values, which fits a narrow talker
  but does not prove the same one.
- [measured] Across the two sessions, melp600 and the 600_x streams share the same frequent
  pitch-candidate values. melp2400's standard-layout decode gives f0 87-241 Hz, which is a
  decode of the wrong layout and not a measure of its talker.
- **Result:** NOT DETERMINABLE that the same utterances appear at 600 and at 1200/2400.
  [inferred] Both 600 groups and the 1200 group carry speech from a male talker, and
  600_x and 1200_x come from one session. A recording of the same known audio at every rate
  would answer it (section 6).

## 6. Recordings that would help most

Each one should go through the radio, recorded as these were, with the source audio kept.
"Known audio" means a file we hold, played into the radio's audio input.

1. **The same 30-60 min of known speech at 600H, plus the source file.** This is the one
   that matters most. It is a known-plaintext pair: align blocks to the source, run MELP
   analysis on the source in 25 ms frames, and estimate each codebook entry as the mean of
   the parameters that map to its index. Pitch (128 × 4) and voicing (16 × 4 × 5) need
   minutes. Gain (2048 × 8) and the four LSF stages (3072 entries × 40 values, a linear
   least-squares problem) need tens of minutes. It also confirms every field position.
2. **Silence (input muted), 30 s at 600H.** Gives every field's idle code and the
   noise-floor gain index, and confirms which columns are voicing, pitch and gain (they hold
   still).
3. **A slow pitch sweep at constant level** (a pulse train from 80 to 250 Hz over 20 s).
   Only the pitch field should move. Gives the pitch bits, their significance, and the
   codebook order.
4. **One steady sound at level steps** (a sustained vowel, or the same pulse train, in
   6 dB steps). Only the gain field should move. Gives the gain bits and the order of its
   levels.
5. **Alternating vowel and hiss** (about 300 ms each). The voicing field should toggle.
   Confirms the 4-bit voicing field and its patterns.
6. **The same known audio at 1200H and 2400V.** 1200H is decodable and gives the reference
   timeline and pitch for alignment. 2400V settles its non-standard layout the same way.
7. **One recording per setting the radio offers for 600 voice**, with the settings noted.
   It would tell whether melp600 and the 600_x files differ only by trailer (all-ones
   against W) and bit 54.

## 7. What a decoder needs

- **The codebooks.** This blocks everything. The layout alone gives indices, not
  parameters. Harris's four codebooks are trained and not published: pitch 128, voicing 16,
  gain 2048, LSF 1024 / 1024 / 512 / 512 [1]. They are lost with the firmware source.
  [inferred] Recording 1 above can recover them by estimation. Nothing in this set can,
  because no 600H message has its source audio.
- **The frame rate.** Harris frames are 25 ms [1]. STANAG MELPe frames are 22.5 ms. Each
  100 ms block decodes to four 25 ms parameter sets, which a 22.5 ms synthesiser must
  resample: 40 frames per second in, 44.4 out.
- **Synthesis.** There are two routes, and choosing between them is Alex's licence call:
  - **Transcode to standard 2400 frames and run the private coder externally with `-m S`.**
    The coder has no parameter-input mode, so standard 2400 frames would have to be built
    from recovered parameters: pitch onto the 99-level table, gains onto G2/G1, voicing,
    and LSFs onto the STANAG MSVQ (7, 6, 6, 6) and Fourier magnitudes onto its 8-bit
    codebook. The pitch, gain and voicing quantisers are in the STANAG text [3]. The LSF
    and Fourier codebooks are in the coder's source, which must not enter the repo. So the
    transcoder would have to run beside the private coder, outside the repo, or learn those
    tables by probing the coder as a black box. It adds a second quantisation loss.
  - **A MELP synthesiser in the repo, written from the published description** (MIL-STD-3005
    / STANAG 4591 [3]: mixed excitation, pulse dispersion, adaptive spectral enhancement).
    It takes the recovered parameters directly, needs no STANAG codebook (600H sends no
    Fourier magnitudes [1]; a flat or fixed vector stands in), and skips the
    re-quantisation.

  [inferred] Either way the in-repo pieces are the same: the block parser (section 3), the
  Harris codebooks once recovered, and the 25 to 22.5 ms resampler.

## Reproducing

All scripts are under `test/harris/harris600/`. They read the modem stage's `<slug>.bits`,
are pure Python and numpy, and call the reference coder only as an external program.

```sh
P=<payload dir>; M=<MELPe reference coder>; W=<work dir>; S="<8 kHz s16le speech files>"
python3 -I -B test/harris/harris600/period.py    $P          # section 2: periods, phase
python3 -I -B test/harris/harris600/edges.py     $P          # header, trailer positions
python3 -I -B test/harris/harris600/structure.py $P          # per-column p1, ac1, dependence
python3 -I -B test/harris/harris600/windows.py   $P 4 6 7    # hold rate, multi-information
python3 -I -B test/harris/harris600/values.py    $P 22 8     # cols 22-29 value sequences
python3 -I -B test/harris/harris600/contrast.py  $P          # pause against speech, per column
python3 -I -B test/harris/harris600/pausewin.py  $P 3 4 5 7  # windows constant in pauses
python3 -I -B test/harris/harris600/smooth.py    $P 4 7 9 11 # ordered-codebook smoothness
python3 -I -B test/harris/harris600/fields.py    $P          # layouts L1, L2
python3 -I -B test/harris/harris600/splits.py    $P          # chunk boundaries, group |r|
python3 -I -B test/harris/harris600/align.py     $P          # cross-message alignment
python3 -I -B test/harris/harris600/melp600.py   $P          # melp600 phase and layout
python3 -I -B test/harris/harris600/check2400.py $P $M $W $S # 2400V FEC and erasure test
python3 -I -B test/harris/harris600/colprofile.py $P $M $W $S # 1200H/2400V against the coder
python3 -I -B test/harris/harris600/timestruct.py $P $M $W $S # order against shuffled decodes
python3 -I -B test/harris/harris600/timeline.py  $P $W       # pause timelines, f0 (after timestruct)
```

The calibration speech was the Codec2 samples hts1a, hts2a and ve9qrp_10s [7]
(`check2400.py`, `colprofile.py`: all three; `timestruct.py`: hts1a and ve9qrp_10s).

## References

[1] M. W. Chamberlain, "A 600 bps MELP vocoder for use on HF channels," in *Proc. IEEE
    MILCOM 2001*, pp. 447-453. Harris Corporation, RF Communications Division. Not filed in
    this repo.

[2] T. Ranjith, P. E. Subha and T. R. Ramamohan, "Real time implementation of 600 bps MELP
    vocoder," in *Proc. National Conference on Communications (NCC 2009)*, IIT Guwahati,
    pp. 326-329. Not filed in this repo.

[3] NATO Standardization Agency, "The 600 bit/s, 1200 bit/s and 2400 bit/s NATO
    interoperable narrow band voice coder," STANAG 4591, Ed. 1: s5.3, s5.4, s5.5, Annexes
    A, D, M. Not filed in this repo.

[4] V. Demjanenko and D. Satterlee, "RTP Payload Format for the Mixed Excitation Linear
    Prediction Enhanced (MELPe) Codec," IETF RFC 8130, Mar. 2017, s3.1, Tables 1-4.

[5] *Analog-to-Digital Conversion of Voice by 2,400 Bit/Second Mixed Excitation Linear
    Prediction (MELP)*, MIL-STD-3005, Dec. 1999. Cited through [1] and [3]; not at hand.

[6] MELPe noise pre-processor technical report ("MelpeNppTechReport"), from the
    DVdevelopment group archive. Not filed in this repo.

[7] D. Rowe, codec2, GitHub repository drowe67/codec2, LGPL-2.1,
    https://github.com/drowe67/codec2 (speech samples under `raw/`).
