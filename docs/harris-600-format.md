# Harris 600 bps MELP (600H): format

The Harris RT-1694D sends 600 bps voice under D1, D2 = 5, 7, which this repo's receiver
calls `600H` (`docs/harris-melpe.md`, Modem). The payload is not STANAG 4591 600 bps MELPe.
The engineers and source code that made it are gone. This document states what the
published sources say, Harris's MILCOM 2001 paper [1] and two Harris patents [8], [9], and
what the decoded bits show.

Every statement carries one label:

- **[paper]**: read from a cited paper, patent or standard.
- **[measured]**: measured on the payloads by a script in `test/harris/harris600/` or
  `test/harris/blind/` (commands at the end).
- **[inferred]**: a reading of measurements or papers that no measurement here settles.
- **NOT DETERMINABLE**: cannot be settled from what is at hand. The text says what would
  settle it.

## 1. What the sources say

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
  ordering. US 6,917,914 B2 gives the bit order [8]. Section 3 sets it out and tests it.
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
last bit of the frame before it. [measured] What supports the reading: the alternating
column 5 alternates through frame 279, the last frame; column 53 is 1 in 71 % of frames; and
header, 280 units, 108 ones and 6 zeros rebuild the stream bit for bit (`claims2400.py`,
`docs/harris-tx.md`).

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
  its z at 20 is high.

## 3. Block layout

Block columns are numbered 0 to 59 in stream order, from the block start at stream bit
54 + 60k. The measurements use the 199 whole blocks of the six 600_x messages ("600_x") and
the 81 of melp600, reported apart.

### The layout

[paper] US 6,917,914 B2 prints the 60-bit block [8, col. 7, ll. 19-45; col. 11, Block 110;
cols. 11-13, Block 203]. Column k is the patent's `bitbuf[k]` (claim P2 below tests this).
Each index is read LSB first: the field's first column has weight 1.

| Columns | Bits | Field [8] | Codebook [8] |
|---|---|---|---|
| 0-9 | 10 | Spectrum, frames 0-1, stage 1 | LSF_CB1, 1024 × 20 |
| 10-18 | 9 | Spectrum, frames 0-1, stage 2 | LSF_CB2, 512 × 20 |
| 19-22 | 4 | Band-pass voicing, four frames | BPV_CB, 16 × 4 |
| 23-29 | 7 | Pitch, four frames | PITCH_CB, 128 × 4 |
| 30-40 | 11 | Gain G1 and G2, four frames | GAIN_CB, 2048 × 8 |
| 41-50 | 10 | Spectrum, frames 2-3, stage 1 | LSF_CB1 |
| 51-59 | 9 | Spectrum, frames 2-3, stage 2 | LSF_CB2 |

[paper] What the decoder does with the fields [8, col. 13, Blocks 204-210]:

- **Spectrum.** Each frame pair's 20-value vector is LSF_CB1 + LSF_CB2 + MEAN. Values 0-9
  are the first frame's ten LSFs, values 10-19 the second's. Both frame pairs use the same
  two codebooks.
- **Gain.** G1 of frames 0-3 comes from entries 0, 2, 4 and 6 of the gain vector, and G2
  from entries 1, 3, 5 and 7.
- **Voicing.** A frame is unvoiced when its BPV_CB value is 0.
- **Fourier magnitudes.** They are not sent. The decoder uses one fixed vector for unvoiced
  frames (FS_MAG_UV) and one for voiced frames (FS_MAG_V).
- **Jitter.** It is set to 0.

[paper] How the encoder fills the fields [8]:

- **Voicing.** Each frame's five band decisions are mapped to a 2-bit class (BPV_STAGE1).
  The four classes are then matched to a 16-entry codebook of the most probable four-frame
  sequences (BPV_STAGE2) [8, col. 5, ll. 9-22; col. 11, Block 107].
- **Pitch.** Median-filtered (order 3), then vector-quantised over four frames with a
  128-entry codebook. The search minimises "mean squared error of voiced frames of pitch"
  [8, col. 6, ll. 13-21; col. 11, Block 108].
- **Gain.** A 2048-entry codebook searched by L1 distance, trained by K-means on speech
  scaled to several levels [8, col. 5, ll. 24-41; col. 11, Block 109].
- **Spectrum.** Two frames are quantised at a time with a two-stage VQ of 10 and 9 bits,
  using M-best search with perceptual weighting. It is trained by K-means and then
  generalised Lloyd [8, col. 6, ll. 55-67].

[paper] None of the codebook values is printed in [8] or [9]. Nor are BPV_STAGE1, MEAN,
FS_MAG_V or FS_MAG_UV.

[paper] US 8,589,151 B2 restates the bit budget [9]. It adds three things:

- Table 3, "MELP 600 BPV MAP" [9, col. 19]. It gives the probability of five-band voicing
  patterns per frame: (u,u,u,u,u) 0.15, (v,u,u,u,u) 0.15, (v,v,v,u,u) 0.11, (v,v,v,v,v)
  0.41, remaining 0.18 (u = unvoiced band, v = voiced).
- The 25 ms to 22.5 ms interpolation weights for transcoding [9, cols. 11-14].
- A description of the spectrum quantiser as "a four-stage vector quantization process",
  stages of 10, 10, 9 and 9 bits over four frames [9, col. 20, ll. 33-40]. This disagrees
  with [8]'s two frame-pair VQs. Claim P5 tests it.

[inferred] The four patterns Table 3 names cover 0.82 of frames. That fits [8]'s 2-bit frame
classes being uuuuu, vuuuu, vvvuu and vvvvv. Neither patent says so.

### Each claim against the recovered bits

The commands are under *Reproducing*. `claims600.py` prints the tests T1 to T9 that the table
names. Unless the table says otherwise, z compares the data with 200 shuffles of block
order inside each message.

| # | Claim [source] | Verdict | Evidence [measured] |
|---|---|---|---|
| P1 | 60 bits per 100 ms block, with no sync bit or header inside the block [1], [8] | **Supported** | Period 60, z 16.0 pooled (`period.py`). At the 600 bps air rate, 60 bits take 100 ms. No column alternates from block to block: the lowest lag-1 correlation is -0.11 in 600_x and -0.23 in melp600 (T9). |
| P2 | `bitbuf[0]` goes out first, so the layout reads in stream order [8, Block 110] | **Supported** | The 7-bit field at cols 23-29 holds from block to block 48 times against 13.2 shuffled (z +10.6). In the mirrored block, pitch would sit at cols 30-36, which give z +4.8. melp600: +3.9 against +0.9 (T1). Reading the payload bytes reversed drops the period-60 z from 11.6 to 3.8 (`period.py`). |
| P3 | Cols 19-40 hold voicing 4, then pitch 7, then gain 11 [8] | **Supported** | Of the six orders of the widths 4, 7 and 11, (4, 7, 11) gives the largest within-field dependence over cross-field dependence: 600_x +0.054 (next +0.035), melp600 +0.081 (next +0.069) (T2). The voicing field's own hold is weak: z +3.4 in 600_x, +0.1 in melp600 (T1). |
| P4 | Cols 0-18 code frames 0-1 and cols 41-59 code frames 2-3, through the same two codebooks and the same bit order [8, Blocks 110, 206] | **Supported** | The share-of-ones profiles of cols 0-18 and 41-59 correlate +0.91 in the same orientation and -0.14 reversed (melp600: +0.86 / -0.18). Equal stage-1 values against the null of a random other block: cols 41-50 of block n against cols 0-9 of block n+1 (adjacent frame pairs) 0.036 against 0.005, z +6.9. Within one block, 0.025, z +4.5. Cols 0-9 of block n against block n+1 (100 ms apart): 0.000, z -1.1. 52 stage-1 values occur in both chunks, against 16 for a control window (T3). |
| P5 | Spectrum is one four-stage VQ of 10, 10, 9, 9 bits over four frames [9, col. 20] | **Contradicted** | Separate stage codebooks would share no index values between cols 0-9 and cols 41-50 beyond chance. Under P4 they do, most often between adjacent frame pairs. [8]'s printed decoder (Block 206) agrees with the data. |
| P6 | Each spectrum chunk is stage 1 of 10 bits, then stage 2 of 9 bits [8, col. 6] | **Supported, weakly** | Equality excess of the first s bits of the two chunks, s = 8 to 12: 600_x z +3.3, +4.6, +4.6, +3.9, +2.3; melp600 +2.9, +3.2, +3.0, +3.0, +1.7 (T5). `fields600.py`, with a different statistic, peaks at 10 in both groups (+6.8 and +5.5). A boundary at 9 is not excluded. The stage-2 fields do not repeat (z +0.9), as residual stages should not. |
| P7 | Every index is LSB first [8, Block 203] | **NOT DETERMINABLE directly; consistent** | The bit order inside a field relabels the codebook. Without the codebook it cannot be read off the bits. Indirect evidence: index distance between consecutive blocks is below chance only in the LSB-first reading. Gain: z -3.9 LSB-first against -0.8 MSB-first. Stage 1, cols 0-9: -2.9 against +0.4 in 600_x, and -2.4 against +0.7 in melp600. Pitch: -2.2 against -1.7 (T6). That means similar entries sit at nearby LSB-first indices; [8] does not say the codebooks are ordered. |
| P8 | Voicing is a 16-entry codebook of four-frame class sequences, with a frame unvoiced when its class is 0 [8, Blocks 107, 204, 209] | **Positions supported; entries NOT DETERMINABLE** | All 16 indices occur. In 600_x index 0 is the commonest (56 of 199 blocks) and has the lowest gain popcount (3.79 of 11; other indices 4.0-9.3). It takes 18 of the 37 blocks with the pause pitch code (T7). In melp600, index 0 takes only 3 of 81 blocks. Which index codes which pattern cannot be read off the bits. [inferred] Index 0 is the quietest pattern and likely the all-unvoiced one. |
| P9 | The pitch search scores voiced frames only [8, col. 6, l. 20] | **Contradicted in combination with P8's reading of index 0** | If bpv index 0 were all four frames unvoiced, every pitch entry would score zero in those blocks, and one fixed pitch index would follow. The 56 bpv-0 blocks carry 18 different pitch indices, 35 in 18 of them (T8). So either index 0 is not all-unvoiced, or the encoder scores unvoiced frames too. NOT DETERMINABLE which. |
| P10 | Pitch is a 128-entry trained vector codebook over four frames [8, col. 6] | **Supported** | 39 of 128 indices occur. It is the slowest field (P2). No index order maps to pitch: \|z\| ≤ 1.8 for LSB-first, MSB-first, binary and Gray (`fields600.py`). Pitch values: NOT DETERMINABLE. |
| P11 | Gain is a 2048-entry codebook of G1/G2 over four frames [8, Blocks 109, 208] | **Position supported; values NOT DETERMINABLE** | Popcount lag-1 correlation over non-pause blocks 0.535 (shuffled 0.065, z +6.0). Mean popcount 3.91 in pause blocks against 5.77 elsewhere, and index 0 is the commonest pause value (`fields600.py`). The G1/G2 interleave cannot be tested without the codebook. |
| P12 | Fourier magnitudes and jitter are not sent [1], [8] | **Consistent** | The fields of P1-P4 account for all 60 bits. There is nothing further to test. |
| P13 | Four 25 ms frames per block [1], [8] | **NOT DETERMINABLE** | The block is 100 ms by the air rate. How it splits into frames happens inside the decoder and leaves no trace in the bits. |
| P14 | 25 ms ↔ 22.5 ms interpolation [9, cols. 11-14] | **NOT DETERMINABLE** | A transcoder or receiver process. The recordings carry only the 600H bits. |
| P15 | Stream framing | **Not addressed** | Neither patent describes the 54-bit header or the 108-bit trailer (s2). [inferred] They sit outside the vocoder. |

### What the fields show

- **[measured] The pause code.** Pitch index 35 (cols 23-29 = 1100010 in stream order)
  marks 37 of the 199 600_x blocks. It runs at the starts of messages: the first 10 blocks
  of 600_2, 4 of m3 and 3 of m4. It also runs inside 600_1, at blocks 3-8 and 44-49.
  melp600 uses it in 7 of its 81 blocks. m1 never shows it in 3.9 s, so not every pause gets
  it. [inferred] It is the pitch codebook entry the encoder picks for silence.
- **[measured] Shared values across messages.** Values of cols 22-29 recur as runs across
  messages. At the best lags, message pairs match 3 to 7 blocks against 0.5 to 1.7 by
  chance (`align.py`). Blocks matched that way agree on the other 52 columns barely more
  than random pairs do. [inferred] So the recurring values are pitch codebook entries of a
  narrow-pitched talker. They do not show that the same phrases were spoken.
- **[measured] Pitch popcount.** The number of ones in the pitch index has a lag-1
  correlation of 0.706 over non-pause blocks (shuffled 0.027, z +8.0). In melp600 it is
  0.136 (z +1.5) (`fields600.py`). [inferred] An LBG splitting tree leaves this behind, so
  popcount is a coarse proxy for pitch with unknown sign and scale.
- **[measured] The two sessions differ in the voicing field.** In 600_x, bpv index 0 leads.
  In melp600, indices 5, 10 and 9 lead and index 0 is rare (T7). The trailer also differs
  (s2). Why: NOT DETERMINABLE.

Corrections to the first write-up of this review, on `feat/harris-blind`
(`docs/harris-blind.md`), which this section replaces:

- [8] does not say that bpv index 0 is all-unvoiced. Its rule is per frame: a frame is
  unvoiced when its BPV_CB value is 0 (Block 209). P9 puts the index-0 reading in doubt.
- The 10-bit stage-1 boundary is weakly supported, not confirmed (P6).
- LSB-first is the patent's statement, not a finding (P7).
- `fields600.py` counts 82 melp600 blocks. The 82nd holds the start of the trailer. The
  melp600 figures above use 81.

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

#### Structured hypotheses (`claims2400.py`)

The 280 frames of 54 bits from stream bit 54 to the trailer, one message. z is against the
null the row names. Calibrations: 600H (199 blocks, fields contiguous by US 6,917,914) and
1200H (256 frames, standard MELPe 1200 order).

| | Hypothesis | Measured | Verdict |
|---|---|---|---|
| V1 | 54-bit frame (2400 bps, 22.5 ms) | strongest period of 4-400: 54, z 110.8; autocorrelation peaks at 54, 108, 162, 216 (z +27.6 to +14.9) | supported |
| V1 | other frame lengths: 27, 48, 60 (25 ms), 72, 81 | z 61.9, 10.7, 6.4, 29.5, 34.2, each within 1.7 sd of independent bits with melp2400's own 54-column profile; largest excess over all 397 periods +4.0, against a shuffled-bits maximum of 4.4 | contradicted |
| V2 | frames start at stream bit 54 + 54k | header end and trailer start on that grid; column 5 alternates 279/279, frames 0 and 279 included | supported |
| V3 | sync bit in the standard position (last bit, B_54) | column 53 alternates 105/279; the grid that puts it last overlaps the header by 48 bits | contradicted |
| V4 | superframe of 2, 3, 4 or 6 frames (outside column 5) | z -0.4, -1.0, -0.1, -0.6 against frame-order shuffles | contradicted |
| V5 | dependent bits sit next to each other | adjacent \|phi\| 0.162 against 0.083 for random column orders, z +8.6 (600H +3.6, 1200H +4.7) | supported |
| V5 | every field is contiguous | 1200H's standard order also reads z +4.7, so the test does not separate the two | not determinable |
| V6 | contiguous fields of MELPe 2400's widths (7, 7, 8, 6, 6, 6, 5, 4, 3, 1, 1) | dependence breaks give 42 segments, 35 single columns; the same method splits 600H's known fields into 43 single columns of 50 | not determinable |
| V7 | standard MELPe 2400, frame rotated or reversed (108 readings) | the two readings with the sync at B_54 send two-bit pitch codes in 26.4 % and 18.6 % of frames (coder 0.0 %, random 16.4 %) | contradicted |
| V8 | columns 4, 6, 7, 8, 16, 17, 18 all zero (117 of 280 frames) are a slowly varying state, as an unvoiced pitch code is | mean run 4.18 frames against 1.71 ± 0.10 shuffled, z +23.8; 16 of 46 other columns shift at \|z\| >= 3 | supported |

[measured] Structure sits in columns 0-28: the strongly coupled groups 0-4, 6-7, 12-13, 14-15
and 17-20, and 19 of the 20 columns that persist from frame to frame (ac1 >= 0.18; the 20th is
33). Columns 29-53 show neither, except the pairs 29-30 and 48-49.

[inferred] A 54-bit, 22.5 ms MELP-like frame with its sync at column 5 and a voicing-like
7-bit code, in a layout that is not the standard's. That fits Harris's own 2400 layout and
does not exclude an arbitrary permutation of MELPe 2400 (V7 tests rotations and reversals
only). Nothing here shows that the frames carry speech, and none is claimed. One message
cannot place the field boundaries. Known audio at 2400V would (section 6, item 6).

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

- **The codebooks.** This blocks everything. The layout (s3) gives indices, not
  parameters. The tables are trained and printed in no source [1], [8], [9]: PITCH_CB
  128 × 4, BPV_CB 16 × 4, GAIN_CB 2048 × 8, LSF_CB1 1024 × 20, LSF_CB2 512 × 20, MEAN 20,
  FS_MAG_V and FS_MAG_UV. They are lost with the firmware source. [8] and [9] say only that
  they were trained, on TIMIT for the spectrum. The STANAG 4591 codebooks, which Alex
  allowed from NATO's distribution (`docs/harris-melpe.md`, *Alex's rulings*), are a
  different coder's tables. No source says Harris's 600 bps tables derive from them, so
  they do not stand in.
  [inferred] Recording 1 above can recover them by estimation. Nothing in this set can,
  because no 600H message has its source audio. So no intelligible 600H audio can be made
  from this set. A synthesis from the fields alone would carry pause timing and a loudness
  proxy (s3, P8 and P11), with no spectrum and no pitch values. It would not be speech, and
  none is built.
- **The frame rate.** Harris frames are 25 ms [1], [8]. STANAG MELPe frames are 22.5 ms.
  Each 100 ms block decodes to four 25 ms parameter sets, which a 22.5 ms synthesiser must
  resample: 40 frames per second in, 44.4 out. [9] prints Harris's own weights for this: a
  linear blend of the previous and current 25 ms parameters in steps of 0.1, with nine
  25 ms frames to ten 22.5 ms frames [9, cols. 11-14].
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
    Fourier magnitudes [1], [8]; a flat or fixed vector stands in), and skips the
    re-quantisation.

  [inferred] Either way the in-repo pieces are the same: the block parser (section 3), the
  Harris codebooks once recovered, and the 25 to 22.5 ms resampler.

## Reproducing

The scripts are under `test/harris/harris600/` and `test/harris/blind/`. They read the modem stage's `<slug>.bits`,
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
python3 -I -B test/harris/blind/claims600.py     $P          # section 3: claims P1-P11 (T1-T9)
python3 -I -B test/harris/blind/fields600.py     $P          # section 3: popcounts, orderings, stage split
python3 -I -B test/harris/blind/claims2400.py    $P          # section 4: 2400V hypotheses V1-V8
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

[8] M. W. Chamberlain, "Voice over bandwidth constrained lines with mixed excitation linear
    prediction transcoding," U.S. Patent 6 917 914 B2, Harris Corporation. Filed in this
    repo at `docs/standards/US6917914B2.pdf`.

[9] M. W. Chamberlain, "Vocoder and associated method that transcodes between mixed
    excitation linear prediction (MELP) vocoders with different speech frame rates," U.S.
    Patent 8 589 151 B2, Harris Corporation. Filed in this repo at
    `docs/standards/US8589151B2.pdf`.
