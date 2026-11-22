# Harris 600H and 2400V: blind decoding with the data at hand

This section takes the existing payloads as far as they go. It follows `docs/harris-600-format.md` and `docs/harris-melpe.md`, and uses the same
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
  - Both patents are filed at `docs/standards/US6917914B2.pdf` and
    `docs/standards/US8589151B2.pdf`.
- **C (600H under the patent layout): moved.** `docs/harris-600-format.md` s3 states the
  layout and gives a verdict for each claim the patents make, tested against the bits.

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
| [B1] M. W. Chamberlain (Harris), "Voice over bandwidth constrained lines with mixed excitation linear prediction transcoding" | US 6,917,914 B2. Filed 2003-01-31, granted 2005-07-12. Published as US 2004/0153317 A1, 2004-08-05. Expired. https://patents.google.com/patent/US6917914 | **The 60-bit block layout and bit order.** Index formation (decoder Block 203) and packing (Block 110), set out in `docs/harris-600-format.md` s3. Decoder lookups (BPV_CB, PITCH_CB, GAIN_CB, LSF_CB1 + LSF_CB2 + MEAN). UV flag from BPV. Fixed Fourier magnitude vectors FS_MAG_V and FS_MAG_UV. Jitter = 0. Spectrum per frame pair: stage 1 10 bits (1024), stage 2 9 bits (512). Gain: 2048 entries, L1 distance, K-means. Pitch: median order 3, 128 entries, k-means, MSE over voiced frames. LSF: K-means then generalised Lloyd, TIMIT, sliding window, M-best perceptual search. No codebook values. No header or sync. |
| [B2] M. W. Chamberlain (Harris), "Vocoder and associated method that transcodes between MELP vocoders with different speech frame rates" | US 8,589,151 B2. Filed 2006-06-21 (appl. 11/425,437), granted 2013-11-19. Published as US 2007/0299659 A1. Israeli counterpart IL 196093 A. https://patents.google.com/patent/US8589151 | Restates Table 1: voicing 4, energy 11, pitch 7, spectrum 10 + 10 + 9 + 9. The 25 ms ↔ 22.5 ms interpolation weights (fixed tables, a nine-frame cycle). Table 3, "MELP 600 BPV MAP" (u = unvoiced band, v = voiced): (u,u,u,u,u) 0.15, (v,u,u,u,u) 0.15, (v,v,v,u,u) 0.11, (v,v,v,v,v) 0.41, remaining 0.18. DRT/DAM for MELPe 600 and LPC10e 600. The PRC-150C runs the transcoding. No codebook values, pitch range or header. |
| [B3] WO 2004/070541 A2 and EP 1 597 721 B1, "600 bps mixed excitation linear prediction transcoding" | PCT and EP family of [B1]. https://patents.google.com/patent/WO2004070541A2 | Same disclosure as [B1]. Not read separately. |
| [B4] M. W. Chamberlain, "A 600 bps MELP vocoder for use on HF channels," IEEE MILCOM 2001 | Already in `docs/harris-600-format.md` [1] | Bit budget, quantiser types, frame length 25 ms. No bit order. |
| [B5] Harris RF-5800H-MP specification sheet | Third-party mirror, https://dtsheet.com/doc/640479/harris-rf-5800h-mp | HF vocoders: "LPC-10-52E (600/2400), MELP (600/2400)". |

The layout these sources give, and how each of their claims fares against the 600H bits,
is in `docs/harris-600-format.md` s3. It is not repeated here.

## What stays impossible with the current data

- **The four Harris 600 codebooks**: pitch 128 × 4, gain 2048 × 8, BPV 16 × 4, LSF
  1024 + 512 entries × 20 plus MEAN, and FS_MAG_V and FS_MAG_UV. They are trained, not
  published [B1, B2], and no 600H message has its source audio. Recovering them needs
  known plaintext (recording 1 of `docs/harris-600-format.md` s6) or the radio play-back
  route (`docs/harris-melpe.md`, *Current state*).
- **What melp2400 is.** It is not LPC-10e in any bit order. Whether it is a permuted MELPe
  2400 or Harris's own layout needs known audio at 2400V.
- **Same talker or utterance across modes**: NOT DETERMINABLE, as before.

## Reproducing

```sh
P=<payload dir>; L=<lpc10tool built from SoX lpc10/>; M=<MELPe reference coder>; W=<work dir>
S="<8 kHz s16le speech: Codec2 hts1a, hts2a, ve9qrp_10s>"
python3 -I -B test/harris/blind/lpc10.py     $P $L $W $S     # A: standard layout, all readings, audio
python3 -I -B test/harris/blind/gf2.py       $P $L $M $W $S  # A: permutation-free parity test
python3 -I -B test/harris/blind/fields600.py $P              # 600H fields (docs/harris-600-format.md s3)
python3 -I -B test/harris/blind/claims600.py $P              # patent claims (docs/harris-600-format.md s3)
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

[B1] M. W. Chamberlain, Harris Corp., US 6,917,914 B2. Filed in this repo at
     `docs/standards/US6917914B2.pdf`.

[B2] M. W. Chamberlain, Harris Corp., US 8,589,151 B2. Filed in this repo at
     `docs/standards/US8589151B2.pdf`.

[B3] WO 2004/070541 A2; EP 1 597 721 B1 (family of [B1]).

[B4] M. W. Chamberlain, "A 600 bps MELP vocoder for use on HF channels," IEEE MILCOM 2001.

[B5] Harris RF-5800H-MP specification sheet (third-party copy), dtsheet.com.
