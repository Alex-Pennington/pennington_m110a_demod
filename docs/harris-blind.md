# Harris 600H and 2400V: blind decoding with the data at hand

No further recordings can be made, so this section takes the existing payloads as far as
they go. It follows `docs/harris-600-format.md` and `docs/harris-melpe.md`, and uses the same
labels:

- **[source]**: read from a cited patent, paper or source file.
- **[measured]**: measured on the payloads by a script in `test/harris/blind/`.
- **[inferred]**: a reading that no measurement here settles.
- **NOT DETERMINABLE**: cannot be settled from what is at hand.

Columns in a 600H block are numbered 0 to 59 in stream order from the block start (stream
bit 54 + 60k). Frames of melp2400 are cut from stream bit 60 + 54k, the grid on which the
alternating bit is frame bit 54.

## State

- **A (melp2400 as LPC-10e): done. Verdict: no.**
  - Standard layout, sync reading: 0.405 valid pitch codes (LPC-10e 1.000, chance 0.484),
    and 0 of 3 unvoiced frames carry the parity.
  - Any bit permutation: the GF(2) test finds 0 relations, against 14-20 for LPC-10e.
  - Decoded: no time structure, with track z +1.6 and energy z +0.9 against +5 to +32.
  - The LPC-10e decode path is not added to the repo.
- **B (sources): done.**
  - US 6,917,914 B2 gives the full 60-bit bit order:
    https://patents.google.com/patent/US6917914
  - US 8,589,151 B2 gives the voicing map table and the 22.5 ↔ 25 ms interpolation:
    https://patents.google.com/patent/US8589151
  - Their family is WO 2004/070541 A2 and EP 1 597 721 B1:
    https://patents.google.com/patent/WO2004070541A2
  - The RF-5800H spec sheet: https://dtsheet.com/doc/640479/harris-rf-5800h-mp
  - No source publishes the codebooks.
- **C (blind 600H decode): done as far as the data allow.**
  - The layout is confirmed against the patent: shared LSF codebooks (z about +7) and a
    stage-1 boundary at 10 bits.
  - Pitch index order: none recoverable (|z| ≤ 1.6 for every map).
  - Gain and pitch popcounts are slow proxies (z +6.0 and +8.0).
  - bpv 0 is the all-unvoiced block.
  - Synthesis would give timing and loudness only, not speech, so none was built.

## A. Is melp2400 (2400V) LPC-10e? No

### The decoder used

[source] The decoder is the US government LPC-10e (FED-STD-1015, version 53) 1996 C code,
translated from Fortran with f2c by Andy Fingerhut (1996). The copy used is the `lpc10/`
directory of SoX [A1], fetched from the chirlu/sox mirror on GitHub. The files carry no
licence line. The README credits the code and its f2c translation. SoX's own wrapper,
`src/lpc10.c`, is LGPL-2.1. The public-domain status of the US government code is not
written in these files. NOT DETERMINABLE from the files at hand.

[measured] The code builds as-is with `gcc -O2`. A small driver (`lpc10tool`: `enc` / `dec`,
ASCII bits, s16le audio) was built outside the repo. Neither the driver nor the library is
in this branch, because the verdict is no (below).

[source] Frame structure, from `chanwr.c`, `encode.c` and `decode.c`:

- 54 bits per 22.5 ms frame, and bit 54 is the sync bit, which alternates.
- Bits 1 to 53 carry pitch/voicing (7 bits), RMS (5), RC1 to RC4 (5 each), RC5 to RC8
  (4 each), RC9 (3) and RC10 (2). They are interleaved by the fixed table IBLIST, LSB first
  within each parameter.
- The pitch/voicing code takes only 62 of 128 values: 0 for unvoiced, 127 for a
  transition, and 60 voiced codes (table ENTAU). DETAU marks the other 66 as error codes.
- In unvoiced and transition frames, RC5 to RC10 carry 20 Hamming(8,4) parity bits over
  bits 1 to 4 of RC1, RC2, RC3, RMS and RC4 (table ENCTAB). That parity is linear over
  GF(2).

### Standard bit layout, every offset and reading

`lpc10.py` reads the stream at every frame offset 0 to 53, in four readings: as-is, frame
reversed, payload bytes MSB first, and inverted. The calibration is the LPC-10e encoder's
own output on the Codec2 samples [A2].

| Stream, reading | Sync alternates | Valid pitch codes | Unvoiced/transition frames | Parity consistent |
|---|---|---|---|---|
| LPC-10e on hts1a / hts2a / ve9qrp_10s | 1.000 | 1.000 / 1.000 / 1.000 | 79 / 69 / 266 | 79 / 69 / 266 |
| the same, inverted | 1.000 | 0.94-0.99 | same frames | 0 |
| random bits, best of 216 readings | 0.57 | max 0.553, median 0.479 | 3-4 | 0 |
| **melp2400, sync reading (offset 6 from bit 54)** | **1.000** | **0.405** | **3 of 279** | **0 of 3** |
| melp2400, best of all 216 readings | - | max 0.642, median 0.487 | - | max 0 |

[measured] On the sync reading:

- Only 0.405 of the pitch codes are ones LPC-10e can send, below the chance rate of
  62/128 = 0.484. 166 of 279 frames carry an error code, and 102 distinct codes occur.
- Only 3 frames are coded unvoiced, against 37-60 % in speech coded by the encoder.
- None of the three carries the parity.

### Any bit permutation: GF(2) parity test

`gf2.py` runs a test that does not depend on bit order. In LPC-10e unvoiced frames, the
7 pitch columns are all zero and 20 columns are XORs of others.

The test works in three steps:

1. Pick the 7 columns that are jointly zero in the most frames.
2. Over those frames, count the independent affine GF(2) relations among the non-constant
   columns.
3. Count the relations of weight 4 or less that hold in at least 90 % of those frames,
   which tolerates a mixed subset.

| Stream | Frames all-zero on the 7 columns | Exact relations | Relations holding in ≥ 90 % |
|---|---|---|---|
| LPC-10e hts1a, columns permuted | 77 of 133 | 20 | 94 |
| LPC-10e hts2a, permuted | 49 of 133 | 14 | 212 (other frames: 0) |
| LPC-10e ve9qrp_10s, permuted | 247 of 444 | 20 | 149 |
| **melp2400** | **115 of 279 (0.412), frame cols 0, 1, 2, 10, 11, 12, 52** | **0 (other frames: 0)** | **0** |

[measured] Under every permutation tried, the LPC-10e calibration finds its pitch columns
and 14 to 20 exact relations. melp2400 has a 7-column all-zero code in 41 % of frames, but
no linear relation at all among its other 46 columns in those frames.

[measured] One limit: on MELPe 2400 output with its columns permuted, step 1 picks the wrong
columns (ve9qrp_10s: 0 relations). In the standard layout, its 114 unvoiced frames show 13
relations. So this test excludes permuted LPC-10e, but not permuted MELPe 2400.

### Decoded as LPC-10e: time structure

| Decode (LPC-10e decoder) | track | Energy lag-1 | Pause share |
|---|---|---|---|
| Calibration speech, 3 files | z +12.0 to +32.3 | z +5.4 to +26.8 | z +3.3 to +18.6 |
| **melp2400, sync reading** | 0.191 (shuffled 0.164, **z +1.6**) | 0.778 (0.748, **z +0.9**) | 0.266 (0.181, z +3.2) |
| Random frames, sync bit set | z -0.8 | z +0.5 | z +0.5 |

z is the original frame order against 12 decodes of the same frames shuffled.

**Verdict A: no.** [measured] The pitch-code validity is 0.405 against 1.000, the parity
check passes 0 of 3 frames against 100 %, the permutation-free parity test finds 0 relations
against 14-20, and the decode has no time structure (z +1.6 / +0.9 against +5 to +32).

[measured] melp2400 shares LPC-10e's sync position, but that is all.

[source] US 8,589,151 [B2] lists Harris coders "LPC10e 2400" and "LPC10e 600" in its test
tables. The RF-5800H data sheet lists "LPC-10-52E" at 600 and 2400 bps [B5].

[inferred] LPC-10e v52 has the same unvoiced parity scheme, so the GF(2) test covers it
too.

[inferred] What melp2400 is stays open. Two readings remain:

- Harris's own 2400 layout;
- a permuted MELPe 2400, which the GF(2) test cannot exclude.

The 7-bit zero code in 41 % of frames fits an unvoiced pitch code. Its frames carry no
Hamming parity, unlike STANAG MELPe and LPC-10e. NOT DETERMINABLE without a known-audio
recording.

## B. Literature and patents on the Harris 600 bps MELP

| Source | Number, date | What it gives |
|---|---|---|
| [B1] M. W. Chamberlain (Harris), "Voice over bandwidth constrained lines with mixed excitation linear prediction transcoding" | US 6,917,914 B2. Filed 2003-01-31, granted 2005-07-12. Published as US 2004/0153317 A1, 2004-08-05. Expired. https://patents.google.com/patent/US6917914 | **The 60-bit block layout and bit order.** Index formation (decoder Block 203) and packing (Block 110), quoted below. Decoder lookups (BPV_CB, PITCH_CB, GAIN_CB, LSF_CB1 + LSF_CB2 + MEAN). UV flag from BPV. Fixed Fourier magnitude vectors FS_MAG_V and FS_MAG_UV. Jitter = 0. Spectrum per frame pair: stage 1 10 bits (1024), stage 2 9 bits (512). Gain: 2048 entries, L1 distance, K-means. Pitch: median order 3, 128 entries, k-means, MSE over voiced frames. LSF: K-means then generalised Lloyd, TIMIT, sliding window, M-best perceptual search. No codebook values. No header or sync. |
| [B2] M. W. Chamberlain (Harris), "Vocoder and associated method that transcodes between MELP vocoders with different speech frame rates" | US 8,589,151 B2. Filed 2006-06-21 (appl. 11/425,437), granted 2013-11-19. Published as US 2007/0299659 A1. Israeli counterpart IL 196093 A. https://patents.google.com/patent/US8589151 | Restates Table 1: voicing 4, energy 11, pitch 7, spectrum 10 + 10 + 9 + 9. The 25 ms ↔ 22.5 ms interpolation weights (fixed tables, a nine-frame cycle). Table 3, "MELP 600 BPV MAP" (u = unvoiced band, v = voiced): (u,u,u,u,u) 0.15, (v,u,u,u,u) 0.15, (v,v,v,u,u) 0.11, (v,v,v,v,v) 0.41, remaining 0.18. DRT/DAM for MELPe 600 and LPC10e 600. The PRC-150C runs the transcoding. No codebook values, pitch range or header. |
| [B3] WO 2004/070541 A2 and EP 1 597 721 B1, "600 bps mixed excitation linear prediction transcoding" | PCT and EP family of [B1]. https://patents.google.com/patent/WO2004070541A2 | Same disclosure as [B1]. Not read separately. |
| [B4] M. W. Chamberlain, "A 600 bps MELP vocoder for use on HF channels," IEEE MILCOM 2001 | Already in `docs/harris-600-format.md` [1] | Bit budget, quantiser types, frame length 25 ms. No bit order. |
| [B5] Harris RF-5800H-MP specification sheet | Third-party mirror, https://dtsheet.com/doc/640479/harris-rf-5800h-mp | HF vocoders: "LPC-10-52E (600/2400), MELP (600/2400)". |

[source] The bit order, from [B1] decoder Block 203, verbatim except ellipses. `bitbuf[k]`
is block column k:

```
spect1_stage1 = 1*bitbuf[0] + 2*bitbuf[1] + ... + 512*bitbuf[9]
spect1_stage2 = 1*bitbuf[10] + 2*bitbuf[11] + ... + 256*bitbuf[18]
bpv_index     = 1*bitbuf[19] + 2*bitbuf[20] + 4*bitbuf[21] + 8*bitbuf[22]
pitch_index   = 1*bitbuf[23] + 2*bitbuf[24] + ... + 64*bitbuf[29]
gain_index    = 1*bitbuf[30] + 2*bitbuf[31] + ... + 1024*bitbuf[40]
spect2_stage1 = 1*bitbuf[41] + 2*bitbuf[42] + ... + 512*bitbuf[50]
spect2_stage2 = 1*bitbuf[51] + 2*bitbuf[52] + ... + 256*bitbuf[59]
gainG1_frame[0..3] = GAIN_CB[gain_index][0,2,4,6];  gainG2_frame[0..3] = GAIN_CB[gain_index][1,3,5,7]
lsf_frame = LSF_CB1[stage1] + LSF_CB2[stage2] + MEAN
if bpv_frame[0] == 0 then uvflag_frame[0] = 1
```

[source] [B1] says the five MELP 2400 voicing bands of a frame are reduced to two bits, and
that a 4-bit codebook covers the most probable four-frame transitions. With [B2] Table 3,
the four per-frame classes appear to be uuuuu, vuuuu, vvvuu and vvvvv ([inferred]).

[measured] This is exactly layout L1 of `docs/harris-600-format.md` s3, which was found
blind: voicing 19-22, pitch 23-29, gain 30-40, spectrum 0-18 and 41-59. The patent settles
four points that were NOT DETERMINABLE there:

1. Chunk A codes frames 0-1 and chunk B frames 2-3. The 38 spectrum bits are not split as
   stages 1 and 3 against stages 2 and 4.
2. Each chunk is 10 + 9 bits, as measured in chunk B.
3. Both chunks index the same two codebooks. The r = 0.91 match between the two chunks'
   bias profiles is that shared codebook.
4. Every field is LSB first, so col 23 is the pitch index LSB.

[source] None of the sources found gives the codebook values. None says the 600 codebooks
derive from the STANAG 2400 ones. [B1] and [B2] say they were trained on TIMIT. Searches
for Harris MELP 600 pitch or gain codebook contents found nothing beyond these patents.

## C. Blind partial decode of 600H

`fields600.py` applies the [B1] layout. It covers 199 blocks of the six 600_x messages and,
reported apart, 82 blocks of melp600. Pause blocks have cols 22-29 = 01100010; in [B1]
terms that is pitch_index 35 with bpv bit 3 = 0.

### Layout checks against the patent

- [measured] **Shared codebooks.** The same index occurs in both halves more often than
  chance:

  | Stream | spect1[n+1] = spect2[n] | spect1 = spect2 in the same block |
  |---|---|---|
  | 600_x | 0.028 (shuffled 0.005 ± 0.003) | 0.018 (0.006 ± 0.004) |
  | melp600 | - | 0.055 (0.009 ± 0.008) |

- [measured] **The stage boundary.** Equality excess of the first sub-window, by where the
  chunk is split:

  | Split after | 8 bits | 9 | **10** | 11 | 12 |
  |---|---|---|---|---|---|
  | 600_x, z | +4.5 | +6.0 | **+6.8** | +6.4 | +3.9 |
  | melp600, z | +4.1 | +4.7 | **+5.8** | +5.4 | +3.1 |

  Both peak at 10, which is the patent's stage 1 (cols 0-9 and 41-50).

### Pitch (cols 23-29)

- [measured] **No index order maps to pitch.** The test compares consecutive non-pause
  blocks against all pairs, with equal pairs dropped, for four maps:

  | Map | 600_x ratio (shuffled), z |
  |---|---|
  | LSB-first binary (patent order) | 0.939 (1.000), z -1.2 |
  | LSB-first Gray | z +1.1 |
  | MSB-first binary | z -1.6 |
  | MSB-first Gray | z -0.2 |

  melp600 gives z +0.1 to +1.6. A log-uniform map is monotone in the index, so it is
  covered by these tests, and it fails too.
- [measured] If the codebook were sorted and log-uniform over 20-160 samples, it would put
  the 600_x talker's median at 136, 138, 117 or 90 Hz depending on the map. The 1200H
  talker reads 93-145 Hz. These numbers rest on an ordering the data do not show, so they
  are not a pitch measurement.
- [inferred] This fits [B1] and [B2]: a k-means-trained 128-entry codebook of 4-frame
  vectors, with entries in no pitch order.
- [measured] **Something slow survives.** The popcount (number of ones) of the pitch index
  has lag-1 correlation 0.706 over non-pause blocks (shuffled 0.027, z +8.0). In melp600
  it is 0.136 (z +1.5). Holds alone cannot explain the 600_x value: 20 of about 160 pairs
  hold.
- [inferred] This is what an LBG splitting tree leaves behind (each bit a ± split). If so,
  popcount is a coarse proxy for pitch with unknown sign and scale. NOT DETERMINABLE.

### Gain (cols 30-40)

- [measured] **Weak ordering.** LSB-first binary gives a ratio of 0.794 (shuffled 1.009,
  z -3.5) in 600_x, but z -1.5 in melp600. Hamming distance between consecutive indices is
  5.07 against 5.43 (z -2.6).
- [measured] **Popcount looks like a level proxy.**
  - Lag-1 correlation 0.535 over non-pause blocks (shuffled 0.065, z +6.0). In melp600:
    0.185, z +1.8.
  - Mean popcount 3.91 of 11 in pause blocks against 5.77 in others.
  - With bpv_index = 0, every gain bit is 1 less often: 0.25-0.43, against 0.50-0.61 with
    bpv ≠ 0.
  - The commonest pause gain index is 0 (5 of 34 blocks).
  - Control: the spectrum fields' popcounts give z -0.3 to +2.9.
- [inferred] So index 0 and low-popcount gain entries are quiet: a coarse loudness
  envelope, with no dB scale.

### Voicing (cols 19-22)

- [measured] In 600_x, bpv_index 0 takes 18 of 34 pause blocks and 38 of 165 others. Mean
  popcount is 0.59 in pauses against 1.62 elsewhere. Its weak ordering gives z -2.8.
- [inferred] Index 0 is the all-unvoiced block. That matches [B1]'s first-entry rule
  (uvflag from bpv_frame = 0) and [B2]'s 0.15 (u,u,u,u,u). The other 15 entries'
  patterns are NOT DETERMINABLE.
- [measured] melp600 behaves differently. Index 0 takes 3 of 82 blocks, and 5, 10 and 9
  lead. Its pause blocks use 4, 0, 2 and 6.
- [inferred] So the two sessions differ in more than the trailer. Why is NOT DETERMINABLE.

### How much can be recovered, and what synthesis would give

| Parameter | Recovered | Status |
|---|---|---|
| Field positions, widths, bit order | all, per [B1] | [source], checked [measured] |
| Block timing (100 ms) and frame timing (4 × 25 ms) | yes | [source] |
| Pause / all-unvoiced blocks | yes: pitch 35 pause code, bpv 0 | [measured] + [inferred] |
| Voicing of the 15 other bpv entries | no | NOT DETERMINABLE |
| Pitch values | no. Only a popcount proxy with no sign or scale | [measured] |
| Gain values | no dB values. A coarse level proxy (popcount) | [measured] + [inferred] |
| LSF spectrum (LSF_CB1 1024 × 20, LSF_CB2 512 × 20, MEAN) | no | NOT DETERMINABLE: TIMIT-trained, not published, not shown to derive from STANAG |
| Fourier magnitudes, jitter | fixed by design (FS_MAG_V / FS_MAG_UV, jitter 0), but the vectors' values are unpublished | [source] |

[inferred] A crude synthesis would carry three things: a pulse-or-noise source switched by
bpv 0 against not-0, a level envelope from the gain popcount, and a neutral spectrum. That
gives speech/pause timing and a rough loudness envelope. It carries no words, since no
spectral information is recoverable. It would not even carry intonation, because pitch
values are not recoverable. No synthesis was built or scored: no speech measure can be
expected to pass on it, and its output must not be called speech.

## What stays impossible with the current data

- **The four Harris 600 codebooks**: pitch 128 × 4, gain 2048 × 8, BPV 16 × 4, LSF
  1024 + 512 entries × 20 plus MEAN, and FS_MAG_V and FS_MAG_UV. They are trained, not
  published [B1, B2], and no 600H message has its source audio. Recovering them needs
  known plaintext (recording 1 of `docs/harris-600-format.md` s6), which cannot now be made.
- **What melp2400 is.** It is not LPC-10e in any bit order. Whether it is a permuted MELPe
  2400 or Harris's own layout needs known audio at 2400V.
- **Same talker or utterance across modes**: NOT DETERMINABLE, as before.

## Reproducing

```sh
P=<payload dir>; L=<lpc10tool built from SoX lpc10/>; M=<MELPe reference coder>; W=<work dir>
S="<8 kHz s16le speech: Codec2 hts1a, hts2a, ve9qrp_10s>"
python3 -I -B test/harris/blind/lpc10.py     $P $L $W $S     # A: standard layout, all readings, audio
python3 -I -B test/harris/blind/gf2.py       $P $L $M $W $S  # A: permutation-free parity test
python3 -I -B test/harris/blind/fields600.py $P              # C: 600H under the [B1] layout
```

Build `lpc10tool` from SoX's `lpc10/*.c`, except `bitio.c`, plus a driver that calls
`lpc10_encode` and `lpc10_decode` with samples scaled to ±1 (as SoX's `src/lpc10.c` does).
The driver used here is not in the repo.

## References

[A1] SoX, `lpc10/` directory (LPC-10e FED-STD-1015 v53, f2c translation by A. Fingerhut,
     1996), GitHub mirror chirlu/sox, https://github.com/chirlu/sox/tree/master/lpc10
     (files `chanwr.c`, `encode.c`, `decode.c`).

[A2] D. Rowe, codec2, https://github.com/drowe67/codec2, LGPL-2.1 (speech samples under
     `raw/`).

[B1] M. W. Chamberlain, Harris Corp., US 6,917,914 B2, 2005-07-12 (filed 2003-01-31).

[B2] M. W. Chamberlain, Harris Corp., US 8,589,151 B2, 2013-11-19 (filed 2006-06-21).

[B3] WO 2004/070541 A2; EP 1 597 721 B1 (family of [B1]).

[B4] M. W. Chamberlain, "A 600 bps MELP vocoder for use on HF channels," IEEE MILCOM 2001.

[B5] Harris RF-5800H-MP specification sheet (third-party copy), dtsheet.com.
