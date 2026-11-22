# Harris MELP test transmissions

Eight recordings of MIL-STD-188-110A serial-tone transmissions from a Harris RT-1694D
(AN/PRC-150), carrying MELP / MELPe voice. The files are not in the repo. They sit in the
gitignored folder `excluded_from_git/harris-melpe-wavs/`, with a `HANDOFF.md` that gives their
origin. They are 48 kHz mono 16-bit WAVs: `600 #1`, `600 #2`, `600 #3 multiples`, `1200 #1`,
`1200 #2`, `1200 #3`, `melp600` and `melp2400`.

## Modem

### How the files are run

`m110a_rx` reads I/Q, not audio. Each WAV is turned into the input greybeard-ale's
`tools/m110a_check.py` gives it: the analytic signal (Hilbert transform) of the audio, resampled
to 96 kS/s and shifted so the suppressed carrier sits at +10 kHz, written as cf32. The only
difference is that the source is the native 48 kHz audio (×2), not audio first reduced to
8 kHz. Then:

```sh
m110a_rx --format cf32 --rate 96000 --offset 10000 --dial 0 -v FILE.cf32
```

Under `-v`, the AUTO pass prints one `[RX PREAMBLE]` line per preamble: the common-preamble
correlation, D1 and D2 with their best and runner-up Walsh correlations, the three count
digits with theirs, and the mode they map to. Every `msg=` line carries `d1=`, `d2=` and
`countdown=`.

### What the preambles carry

Measured on every preamble in the eight files. Correlations are normalised so a clean symbol
reads 1.0. "Runner-up" is the strongest of the other seven D values (D1, D2) or the other three
count values (C1 to C3).

| File | Preambles | D1, D2 | D1 / D2 corr (runner-up) | C1, C2, C3 | Countdown | Carrier offset |
|---|---|---|---|---|---|---|
| 600 #1 | 1 | 5, 7 | 0.997 / 0.996 (≤ 0.029) | 4, 4, 6 | 2 | +2.6 Hz |
| 600 #2 | 1 | 5, 7 | 0.996 / 0.996 (≤ 0.028) | 4, 4, 6 | 2 | +2.5 Hz |
| 600 #3 multiples | 4 | 5, 7 on all four | 0.996-0.997 (≤ 0.031) | 4, 4, 6 | 2 | +2.5 to +2.7 Hz |
| 1200 #1 | 1 | 5, 6 | 0.997 / 0.996 (≤ 0.023) | 4, 4, 6 | 2 | +2.6 Hz |
| 1200 #2 | 1 | 5, 6 | 0.997 / 0.996 (≤ 0.032) | 4, 4, 6 | 2 | +2.5 Hz |
| 1200 #3 | 1 | 5, 6 | 0.997 / 0.996 (≤ 0.033) | 4, 4, 6 | 2 | +2.6 Hz |
| melp600 | 1 | 5, 7 | 0.965 / 0.953 (≤ 0.090) | 4, 4, 6 | 2 | -3.7 Hz |
| melp2400 | 1 | 7, 7 | 0.967 / 0.961 (≤ 0.079) | 4, 4, 6 | 2 | -3.7 Hz |

Every count digit reads at 0.96 or more, against 0.061 or less for its runner-up. Count
4, 4, 6 is the six-bit word 00 00 10, which is 2: the first of three preamble frames, so every
transmission has the zero or short preamble [1, 5.3.2.3.7.2.1].

What the D1, D2 values mean [1, Table XI], [2, Table XI], [3, Table XI]:

- **7, 7** (melp2400) is *2400 (Digital voice)*. Table XI defines it for the short setting only.
- **5, 7** (600 files, melp600) and **5, 6** (1200 files) are not modes in Table XI. The note
  to Table XI says: "The D1, D2 combinations 5,6 and 5,7 are reserved for application-specific
  use" [1], [2], [3]. None of 110C, 110C Change 1 or 110D defines what they carry. They are
  Harris's own codes, and the receiver names them that way: `600H` and `1200H`, never as Table
  XI modes.

### Which waveform each code carries

`test/harris/harris_score` decodes one message under every candidate: 600 bps BPSK and
1200 bps QPSK (20 unknown / 20 known symbols per frame) and 2400 bps 8PSK (32 / 16), each with
the interleaver bypassed, short (0.6 s) and long (4.8 s). For each it re-encodes the decoded
bits and counts how many disagree with the received hard bits: the hard-decision Viterbi path
metric of each interleaver block, as a fraction of its coded bits. A wrong candidate decodes
noise and disagrees on about a quarter to a third of its bits. It also looks for the EOM
4B65A5B2 [1, 5.3.2.3.1] in the decoded bits.

Median per-block disagreement, phase tracked on the probes, no equalizer:

| Message | Winner | Its median | Best other candidate | EOM found by |
|---|---|---|---|---|
| 600 #1 (5, 7) | 600 short | 0.0000 | 2400 bypass, 0.180 | 600 short only |
| 1200 #1 (5, 6) | 1200 short | 0.0000 | 2400 bypass, 0.244 | 1200 short only |
| melp2400 (7, 7) | 2400 short | 0.0257 | 600 bypass, 0.283 | 2400 short only |

All six 600 messages and melp600 give the 600 #1 picture, and all three 1200 files the
1200 #1 picture: 0.0000 on every block up to the EOM for the winner, and no EOM for any other
candidate. A long-interleave candidate covers one block at most, since every message is shorter
than one 4.8 s block. The count of 2 already says short or zero.

The known symbols say the same, independently of any decode. In each 1440-symbol (0.6 s)
block, the last two known segments carry the Walsh patterns of D1 and then D2 instead of zeros,
as 110C requires before each new interleaver block [1, 5.3.2.3.7.1.2]:

- 20 / 20 messages (5, 7 and 5, 6): frames 34 and 35 of every 36, at symbols 1380 and 1420.
- melp2400 (32 / 16): frames 28 and 29 of every 30, at symbols 1376 and 1424.

At each boundary the scorer printed (up to six per message), the pattern correlates at 0.81 to 1.00. All but one read 0.92 or more; the exception, 0.81, is the last boundary of 600 #3's third message, where the signal ends. That fixes the frame structure (20 / 20, or 32 / 16
for 7,7) and the 1440-symbol block. 110C gives the zero (bypassed) setting the same pattern
[1, 5.3.2.3.7.1.2], so the block pattern cannot tell short from bypassed. The decode can: the
bypassed candidates fail on every file.

So:

| D1, D2 | Waveform | Receiver mode |
|---|---|---|
| 7, 7 | 2400 bps 8PSK, 32 / 16, short interleave (40 × 72): 2400S's waveform | `2400V` (Table XI 2400 digital voice) |
| 5, 7 | 600 bps BPSK, 20 / 20, short interleave (40 × 18): 600S's waveform | `600H` (Harris application-specific) |
| 5, 6 | 1200 bps QPSK, 20 / 20, short interleave (40 × 36): 1200S's waveform | `1200H` (Harris application-specific) |

AUTO also reads 7, 5 as `75S` and 5, 5 as `75L` (Table XI). It names the mode, then returns
"M75 modes not yet supported": there is no 75 bps data path.

`test/test_table_xi_modes` (ctest) sends each code from this modem's TX and requires AUTO to
name it. For 600H and 1200H it requires the exact payload as well. For 75S and 75L it sends the
preamble only.

### Payload

`test/harris/score.py` produces the payloads and the evidence for them (its modem stage, `test/harris/modem.py`), then decodes them to speech (the MELPe stage below):

```sh
python3 -I -B test/harris/score.py --rx build/bin/m110a_rx --score build/bin/harris_score \
    --set <test-set dir> --payload <payload dir> --melpe <MELPe coder> --search
```

It runs `m110a_rx` (AUTO, DFE) over each file, then decodes each message it reports twice more
with `harris_score`: once with probe phase tracking only, and once with a fractionally spaced
equalizer and PLL (`--eq 6 0.02 0.005`). `<slug>.bin` is the equalized decode, cut at the EOM.
110A has no CRC, so a payload counts as verified when the two `harris_score` decodes are
byte-identical and both found the EOM. That is two receive chains that share only the
deinterleaver and the Viterbi decoder.

| Message | Mode | Bytes | EOM | Disagreement, EQ | Disagreement, tracked | Probe SER | m110a_rx (DFE) |
|---|---|---|---|---|---|---|---|
| 600_1 | 600H | 442 | yes | 0.00 % | 0.00 % | 0.93 % | same bytes, 9.6 dB |
| 600_2 | 600H | 418 | yes | 0.00 % | 0.00 % | 1.26 % | same bytes, 9.7 dB |
| 600_3_multiples_m1 | 600H | 314 | yes | 0.00 % | 0.00 % | 3.01 % | same bytes, 9.7 dB |
| 600_3_multiples_m2 | 600H | 224 | yes | 0.00 % | 0.00 % | 3.04 % | same bytes, 9.6 dB |
| 600_3_multiples_m3 | 600H | 142 | yes | 0.00 % | 0.00 % | 6.50 % | same bytes, 9.7 dB |
| 600_3_multiples_m4 | 600H | 82 | yes | 0.00 % | 0.00 % | 3.97 % | same bytes, 9.9 dB |
| 1200_1 | 1200H | 1156 | yes | 0.00 % | 0.00 % | 0.85 % | same bytes, 9.4 dB |
| 1200_2 | 1200H | 740 | yes | 0.00 % | 0.00 % | 0.29 % | same bytes, 9.7 dB |
| 1200_3 | 1200H | 760 | yes | 0.00 % | 0.00 % | 1.16 % | same bytes, 9.6 dB |
| melp600 | 600H | 628 | yes | 0.00 % | 0.00 % | 10.70 % | same bytes, 8.3 dB |
| melp2400 | 2400V | 1911 | yes | 0.04 % | 2.26 % | 9.64 % | 43 bytes differ, 6.8 dB, success=0 |

The columns:

- *Disagreement* is the mean coded-bit disagreement over the interleaver blocks before the EOM.
- *Probe SER* is the share of zero-valued known symbols whose 8PSK position is wrong, with
  phase tracked and no equalizer. It is an 8-ary decision, so it runs higher than the BPSK and
  QPSK data decisions.
- *m110a_rx* is its own decode, with its `quality_db`.

`600 #3 multiples` holds four messages. `m110a_rx` finds each preamble, at 0.763, 7.557, 13.216
and 17.495 s.

melp2400 is the weak file. Its audio passband is 703 to 2918 Hz, which cuts into the 2400-baud
spectrum. Without an equalizer, 2.3 % of its coded bits disagree. The Viterbi decoder corrects
them: the equalized decode (0.04 % disagreement) is byte-identical to the tracked-only one, and
to the equalized decode at four other equalizer settings, from 9 to 21 taps. `m110a_rx`'s own
DFE decode of it differs from all of these in 43 bytes, and scores 6.8 dB against its 7.75 dB
2400 threshold, so it reports `success=0`. The other equalizers in `m110a_rx` (`--eq NONE`,
`DFE_RLS`, `MLSE_*`, `TURBO`) differ from it in 21 to 218 bytes. The same receiver returns 53 of 54
bytes on noiseless own-TX 2400S, with the last byte wrong, so the defect is in its 2400 receive
chain and not in the 2400V mode. The melp2400 payload is the `harris_score` decode.

**Bit order.** Bit *k* of the stream, in the order bits leave the K=7 Viterbi decoder (after
descrambling and deinterleaving), is bit *k* mod 8 of byte ⌊*k* / 8⌋, LSB first. That is the
packing `m110a_rx` prints as `hex=`, and the bit order its EOM search uses. `score.py` writes the
same bits as ASCII `0` / `1` to `<slug>.bits`, and a `BIT-ORDER.txt` beside them. No stream is a
whole number of 54-bit or 81-bit frames. Where the MELPe frames start inside it is outside the
modem.

### Open questions

- **Table XII and the count digits.** Table XII maps the two-bit count values 00, 10, 01, 11 to
  symbols 4, 5, 6, 7 [1, Table XII], [3, Table XII]. The rule in the text ("adding a 1 before the
  two-bit value") and its worked example (23 = 010111 → 5, 5, 7) give value + 4: 01 → 5 and
  10 → 6. Table XII's symbol column (5 = 101, 6 = 110) is the two-bit value with its bits
  reversed. The receiver and the TX use value + 4. On these files the count reads 4, 4, 6 on every
  preamble. That is countdown 2 under value + 4, which the first of a short preamble's three frames must
  carry, and `m110a_rx` finds three frames (`preamble_hits=3`). Under Table XII as printed it
  would be 1. NOT
  DETERMINABLE from the standard alone which one Table XII intends. The readings fit value + 4.
- **The 2400 receive chain.** 2400S and 2400V lose the last byte of a noiseless own-TX message,
  and decode melp2400 with 43 wrong bytes. The 2400 quality threshold (7.75 dB) was set on 2400L.
  This needs its own receive-chain work.
- **TX known symbols.** This modem's TX sends every known segment as zeros. 110C puts D1 and D2
  in the two before each new interleaver block [1, 5.3.2.3.7.1.2]. The Harris radio does.
  The receiver also treats every known segment as zeros, so its DFE trains on the wrong
  reference in two of every 36 (or 30) segments of a Harris signal. The 600H and 1200H files
  decode regardless.
- **TX end of message.** This modem's TX ends a message with four frames of zero data. It does
  not send the EOM 4B65A5B2 [1, 5.3.2.3.1] or the interleaver flush [1, 5.3.2.3.2]. So
  `eom=1` on own-TX loopback is not tested.
- **75 bps.** AUTO names 75S and 75L. There is no 75 bps data path, in TX or in RX.
- `src/m110a/preamble_codec.h` has its own D1, D2 table, with 75 bps marked "estimated" as 7,7
  short and 5,7 long. Those collide with 2400 digital voice and the Harris 600 code. Nothing in
  the build includes the file.

## MELPe

This section covers the part after the modem: payload bits to MELPe frames to audio, and
how `test/harris/score.py` decides whether the audio is speech.

### Coders available

| Tree | What it is | MELPe rates | Builds on Linux |
|---|---|---|---|
| `src/melpe_dist` | `melpe_api.h` plus two Win64 static archives (`libmelpe.a`, `libmelpe_api.a`). The object names (`melp_ana`, `lib600_*`, `bitorder600`) are those of the STANAG 4591 reference coder. No source, no licence line. | 2400, 1200, 600 (per the header) | No: Windows COFF objects |
| `src/vocoder` | C++ wrapper over `melpe_api.h`. A stub: it returns no data. | none | not built |
| `src/codec2_vocoder` | Command line over Codec2 (LGPL) [6]. Not MELPe. `extern/codec2` is an empty submodule. | none | not built |

So nothing in this repo decodes MELPe on Linux. The scorer runs the STANAG 4591 reference
coder's command line as an external program (`--melpe`); that coder is not part of this
repo.

### Frames

From STANAG 4591 [4] section 5.5 and Annexes D and M, and RFC 8130 [5] section 3.1:

| Rate | Bits per frame | Frame | Sync bit |
|---|---|---|---|
| 2400 bps | 54 | 22.5 ms | bit 54, alternates 0/1 from frame to frame |
| 1200 bps | 81 | 67.5 ms | bit 1, alternates 0/1 from frame to frame |
| 600 bps | 54 | 90 ms | none |

Bit 1 is transmitted first [4, s5.5.3]. At each rate one frame carries exactly the modem's
bits for one frame time (54 bits in 22.5 ms is 2400 bps, 81 in 67.5 ms is 1200 bps, 54 in
90 ms is 600 bps), so frames run back to back in the payload with no room for framing
bits. At 2400 and 1200 bps the sync bit gives the frame alignment; at 600 bps nothing in
the frame does.

MIL-STD-188-110D Table XI [3] lists one voice code only, "2400 (Digital voice)", D1,D2 7,7,
and section 5.3.1.2 points voice digitisation at STANAG 4198 or 4591. The Modem section
above measures the rest: 5,7 carries the 600 bps waveform (`600H`) and 5,6 the 1200 bps
waveform (`1200H`). That the 600H payload is 600 bps MELPe and the 1200H payload 1200 bps
MELPe is read off the rates, not stated by any document at hand. Where the frames start
in each payload, and the order of their bits, are found by measurement (below).

### Byte packing of the reference coder

Measured by encoding 10 s of speech with the reference coder at each rate and reading the
output (`-b 54`, its default):

| Rate | Bytes per frame | Where bit k lands | Evidence |
|---|---|---|---|
| 2400 | 7 | byte (k-1)/8, bit (k-1)%8 from the LSB, as RFC 8130 Figure 2 | the only alternating bit is byte 6 bit 5 (bit 54); byte 6 bits 6-7 are always 0 |
| 1200 | 11 | the same, as RFC 8130 Figure 3 | the only alternating bit is byte 0 bit 0 (bit 1); byte 10 bits 1-7 are always 0 |
| 600 | 7 | byte (k-1)/8, bit 7-(k-1)%8 from the LSB (inference) | byte 6 bits 0-1 are always 0, the opposite end to RFC 8130 Figure 4 |

The 600 bps row is an inference from where the unused bits sit; frames packed that way
decode to speech (the calibration below), which supports it. The coder's `-b 56` output
is a file packing (bytes MSB first, the last byte's unused high bits dropped), not the
transmission order. Its `-b 06` output is nine 6-bit words per 2400 bps frame, taken from
bit 1 upward with bit 1 as the LSB, which matches RFC 8130's numbering. `melpe_frames.py`
holds the packing (`pack_frames`, `unpack_frames`).

### Speech measures

A decode counts as speech only when it passes two measures. Each one alone can be fooled.

**Voiced pitch track** (`melpe_frames.speech_measure`, `track`). This works on the decoded
audio, in 22.5 ms hops with a 40 ms window. A frame is voiced when its normalised
autocorrelation peaks above 0.5 at a lag for 80-308 Hz, and the frame is within 40 dB of
the loudest frames. `track` is the share of frames that are voiced and within about 10 %
of the previous frame's pitch.

- *What it shows:* the decode is steady voiced audio from frame to frame. Frames cut at
  the wrong offset, bits in the wrong order and random bits all score low (table below).
  So it is what finds the bit order and the frame offset.
- *What it does not show:* that the audio is speech. An independent review falsified that
  reading. A coded 110 Hz buzz with vibrato scores 0.98-0.99, and one coded frame repeated
  0.997. Shuffling the frames of a real 1200 bps decode left it passing (0.321 and 0.312
  for 1200_2, against 0.386 in order).

**Time structure.** This asks whether the loudness of the decode carries on from frame to
frame in the frames' own order, and whether it changes at all.

- *Boundary test* (`melpe_frames.boundary_ratio`, 1200 and 600 bps). A MELPe frame at these
  rates holds three or four 22.5 ms sub-frames. B is the mean loudness step (dB) across
  frame boundaries divided by the mean step between sub-frames of one frame. It uses the
  coder's synthesis delay, measured once per rate on shuffled speech. In speech decoded in
  order, a boundary is like any other step and B is about 1. Out-of-order frames and
  random bits jump at the boundaries, so B is well above 1. Pass: B <= 1.33.
- *Shuffled-order test* (`score.order_test`, 2400 bps). A 2400 bps frame holds only two
  gains, and the decoder smooths across its boundaries, so B does not separate random bits
  there. Instead: the lag-1 autocorrelation of the energy per frame (`energy_contour`, r1)
  of the decode, against the same frames decoded in 32 shuffled orders. Pass: r1 beats the
  shuffles' mean by 0.20 or more.
- *Energy depth*, every rate: the standard deviation of the energy per frame. Pass:
  5 dB or more. This is what fails a buzz and a repeated frame at any rate.
- *What it shows:* the decode's loudness has the frame-to-frame order of a real signal
  that rises and falls. A buzz, a repeated frame, shuffled frames and random bits fail it.
- *What it does not show:* the pitch, the spectrum, or that anyone could understand the
  words. Inverted 2400 bps bits pass it, because inverting every gain index keeps their
  continuity. They fail the pitch track. Only listening shows that the words are
  intelligible.

Calibration of the pitch track alone. `score.py --calibrate` on 16 s of male speech (the
Codec2 test samples hts1a, hts2a and ve9qrp_10s [6]), coded and decoded through the
reference coder:

Calibration: `score.py --calibrate` on 16 s of male speech (the Codec2 test samples hts1a,
hts2a and ve9qrp_10s [6]), coded and decoded through the reference coder:

| Case | 2400 bps | 1200 bps | 600 bps |
|---|---|---|---|
| speech, before coding | 0.317 | 0.317 | 0.317 |
| speech, coded and decoded | 0.304 | 0.346 | 0.368 |
| speech frames cut 17 bits late | 0.062 | 0.051 | 0.146 |
| speech frames bit-reversed | 0.089 | 0.042 | 0.132 |
| speech frames inverted | 0.025 | 0.036 | 0.129 |
| random bits, three seeds | 0.049-0.059 | 0.062-0.080 | 0.089-0.097 |
| random bits, sync bit set | 0.056 | 0.074 | - |
| all-zero bits | 0.003 | 0.067 | 0.020 |

The share of voiced frames alone does not separate speech from random bits (about 0.45
against 0.3), so the scorer uses `track`.

Calibration of the time structure, on the same speech coded at each rate, whole (16 s)
and in 5 s parts (about the length of a Harris message):

| Case | 2400 bps: order margin, depth | 1200 bps: B, depth | 600 bps: B, depth |
|---|---|---|---|
| coded speech, whole and 5 s parts | +0.28 to +0.38, 12-17 dB | 0.82-1.30, 10-17 dB | 1.01-1.17, 9-17 dB |
| random bits, 10 seeds (5 s) | -0.09 to +0.08 | 1.37-2.17 | 1.47-2.50 |
| shuffled coded speech | -0.03 to +0.13 | 2.37-3.07 | 2.41-3.20 |
| shuffled Harris frames | -0.09 to +0.02 (melp2400) | 2.01-2.97 (1200 files) | - |
| coded 110 Hz buzz with vibrato | -0.33, 2.3 dB | 1.56, 1.7 dB | 1.68, 0.4 dB |
| one coded frame repeated | 0.00, 3.7 dB | 2.01, 0.2 dB | 1.86, 0.2 dB |
| speech cut 17 bits late, reversed | +0.04 to +0.12 | 1.49-1.65 | 2.03-2.13 |
| speech bits inverted | +0.31 (fails the track) | 1.35 | 2.22 |

The limits sit in the gaps: B 1.33 (speech at most 1.30, everything else at least 1.35),
order margin 0.20 (speech at least 0.28, everything else at most 0.13 apart from the
inverted bits), depth 5 dB (speech at least 8.8 dB, buzz and repeated frame at most
3.7 dB). The 1200 bps gap is narrow: one 5 s part of speech reads 1.30, and inverted bits
1.35. A shuffled-order test was tried at 1200 and 600 bps and dropped. At 55-74 frames,
random bits beat their own shuffles by up to +0.21 and shuffled speech by up to +0.29,
while one 5 s part of coded speech managed only +0.14. A syllable-rate (2-8 Hz)
modulation share of the energy contour was tried first and dropped too: shuffled coded
speech scored as high as speech in order, because shuffling whole 67.5 or 90 ms frames
leaves energy contour power at 2-8 Hz.

`score.py --calibrate` runs all of these cases (54 at the three rates) as a self-check
through the reference coder, and exits non-zero if any is classified wrongly. All 54 come
out as expected. `ctest -L fast` checks the same logic on audio stand-ins that need no
coder.

A search over many decoding choices raises the best wrong reading. On coded speech
written as a payload with a lead of 23 bits (600 bps) or 11 bits (2400 bps), searching
rate, byte order, offset, reversal and inversion together (1300-1500 decodes) found the
right choice each time, at 0.353 and 0.286. The best wrong choice scored 0.266 and 0.239.
Every one of the high wrong choices was a 600 bps decode: reversed, inverted, or of a
stream at another rate. A 600 bps decode of mis-cut bits sounds speech-like inside each
90 ms block, because the coder's codebooks are trained on speech. Searching byte order and
offset only, at the right rate (`--search`), the right choice won again and the best wrong
choice scored 0.185 (600 bps) and 0.104 (2400 bps); random bytes at 1200 bps scored at
most 0.098.

So the scorer:

- takes the rate from the receiver mode in the modem stage's sidecar (600H 600 bps,
  1200H 1200 bps, 2400V 2400 bps), or from the file's slug without one;
- checks the frame period first (below) and does not decode a mode whose bits do not
  repeat at the rate's STANAG 4591 frame size;
- with `--search` tries both payload byte orders and every frame offset, and at 600 bps
  both coder packings (measured MSB-first, and RFC 8130's LSB-first); the sync bit fixes
  the offset at 2400 and 1200 bps when it alternates in at least 90 % of frames.
  `--search-all` adds every rate, frame reversal and inversion;
- calls a message speech when `track` >= 0.25 and beats every other choice it tried by
  0.05, *and* the time-structure test passes (below). A message that passes the first and
  not the second has frames in the right place, but no evidence here that they carry
  speech.

### Frame period

`melpe_frames.period_z` measures how strongly a stream repeats with a period P, whatever
the frame phase. Cut into rows of P bits, a framed stream's column means stray from the
stream's own share of ones at its biased positions: codebook index MSBs, voicing bits, the
sync bit. The statistic z is about N(0, 1) when there is no structure at P. It is measured
against the stream's own mean, so a stream with more zeros than ones shows no period from
that alone. `period_check` pools z over every message of a mode, for P from 4 to 130.
A mode fails the check when z at the rate's frame size is below 3 while another period,
not a multiple of it, reaches 6 or more. Random bits peak at about 3.5 over that range.

### Running it

```
python3 -I -B test/harris/score.py --melpe <reference coder> --payload DIR \
    [--rx build/bin/m110a_rx --score build/bin/harris_score] [--set <test-set dir>] \
    [--search] [--audio DIR] [--proofs DIR --decoder TEXT]
python3 -I -B test/harris/score.py --melpe <reference coder> --calibrate <8 kHz s16le speech> ...
```

With `--rx` and `--score` the modem stage runs first and writes the payloads into
`--payload`. Without them the MELPe stage reads payloads and sidecars written before.

`--proofs DIR` writes `DIR/t7.json` and the audio it names under `DIR/t7-audio/`: the
decoded speech, and the recording resampled to 8 kHz.

A recording with several preambles ("600 #3 multiples" has four) has one payload file per
message, `<slug>_m<N>.bin`. Each message is searched and scored on its own and gets its
own table row. The recording keeps one proof item: `decoded` and `detail` give each
message's result in order, and the decoded speech is the messages joined with 0.5 s of
silence between them. The item is "pass" when every message passes, "partial" when every
message is at least partial, and "fail" otherwise. Text fields carry no file paths,
host names or frequencies. `ctest -L fast` runs `test/harris/test_harris.py` (packing, sync
search, the frame-period check and the speech measure on synthetic signals, and the proof
output), which needs no MELPe coder.

### Results on the Harris set

`score.py --rx ... --score ... --search` over the eight recordings. The modem stage's
payloads are byte-identical to those of the modem scorer on feat/harris-mode, all 11.

| Message | Mode | Bytes | MELPe frames | Frame start | Sync bit alternates | track (next best) | Time test, depth | Result |
|---|---|---|---|---|---|---|---|---|
| 600_1 | 600H | 442 | not STANAG 4591 | - | - | - | - | not decoded |
| 600_2 | 600H | 418 | not STANAG 4591 | - | - | - | - | not decoded |
| 600_3_multiples_m1 to m4 | 600H | 314, 224, 142, 82 | not STANAG 4591 | - | - | - | - | not decoded |
| 1200_1 | 1200H | 1156 | 113 × 81 bits | bit 54 | 100 % | 0.325 (0.115) | B 1.09, 18.4 dB | speech: both pass |
| 1200_2 | 1200H | 740 | 72 × 81 bits | bit 54 | 99 % | 0.386 (0.130) | B 1.01, 18.6 dB | speech: both pass |
| 1200_3 | 1200H | 760 | 74 × 81 bits | bit 54 | 99 % | 0.172 (0.131) | B 1.37, 19.4 dB | frames found; both fail |
| melp600 | 600H | 628 | not STANAG 4591 | - | - | - | - | not decoded |
| melp2400 | 2400V | 1911 | 283 × 54 bits | bit 6 + 54k | 100 % | 0.121 (0.107) | order +0.152, 15.6 dB | frames found; both fail |

Limits: track 0.25 and 0.05 over the next choice; B 1.33 (1200, 600 bps) or order margin
0.20 (2400 bps); depth 5 dB. 1200_3 misses B by 0.04 and sits just above the lowest
non-speech reading of the calibration (1.35). melp2400 beats its shuffles by +0.152,
which is more than random bits or shuffled speech did (at most +0.13) and less than coded
speech did (at least +0.28).

Bit order: at 2400 and 1200 bps the frames read with stream bit k as frame bit k+1 (the
payload's LSB-first bytes, bit 1 first [4, s5.5.3]), packed for the coder as RFC 8130 [5].
The sync bit then alternates in 99-100 % of frames; random bits reach at most 0.59.

**A common 54-bit header.** All 11 streams, in all three modes, begin with the same 54
bits: `011111011010100110011010100100000100111110000011110010` (stream order). They differ
from bit 54 on. The 1200 bps frames start right after it, at bit 54, in all three 1200
files. The melp2400 frames fill the stream exactly: 15288 bits = 6 + 283 × 54, so its
frame grid passes bit 60. That the header is the STANAG 4591 Annex O frame
synchronization sequence [4, Annex O] is an inference: Annex O is not in the edition at
hand.

**600H is not STANAG 4591 600 bps MELPe.** Frame period, pooled over each mode's messages:

| Mode | Strongest period | z | z at the STANAG frame size |
|---|---|---|---|
| 1200H | 81 bits | 42.4 | 42.4 (81) |
| 2400V | 54 bits | 109.2 | 109.2 (54) |
| 600H | 60 bits | 14.9 | 0.4 (54) |

The 600H payloads repeat every 60 bits, with a 20-bit sub-period, and show no structure at
the 54 bits of a STANAG 4591 600 bps frame. 60 bits is 100 ms at 600 bps: the block of
Harris's own 600 bps MELP vocoder, four 25 ms frames coded together in 60 bits [7]. That
the RT-1694D's 600 bps voice is that vocoder is an inference from the period. An
independent adversarial review could not falsify that the 60-bit period comes from the
vocoder and not from the modem: over 20 seeds, random streams of the same lengths reach
z 1.9-3.4 at their strongest period, and the 1200H streams, through the same modem chain,
give z 1.4 at 60. [7] does not
give its bit layout or codebooks, and the reference coder here cannot decode it. Before
this check, a search of every frame offset, both byte orders and both coder packings
found no 600H choice that stood out: in six of the seven 600H messages the best and
second-best readings were within 0.04 of each other, at scattered offsets, byte orders and
packings. The seventh, the 12-frame fourth message of 600 #3, read 0.362 against 0.298:
too short to mean anything.

**melp2400 and 1200_3.** Their frames are certain (the sync bit alternates in 99-100 % of
283 and 74 frames), but both speech measures fail: the pitch track reads below 0.25, and
the time-structure test falls short (B 1.37 for 1200_3, order margin +0.152 for
melp2400). So there is no evidence here that these two decodes are speech, though the
frames are in the right place. The melp2400 parameters are
speech-like: with RFC 8130 Table 1 positions, adjacent frames carry pitch codes within 2
of each other in 32 % of frames (coded calibration speech 45 %, random bits 7 %; the same
stream cut 27 bits late 10 %). Pauses do not explain the low reading: melp2400 is active
(within 20 dB of its loudest frames) in 66 % of frames, and its track over active frames
is 0.18, against about 0.55 for 1200_1 and 1200_2. NOT DETERMINABLE here whether the
cause is the speech itself or the recording; listening to the decoded audio would answer
it.

## References

[1] *Interoperability and Performance Standards for Data Modems*, MIL-STD-188-110C,
    Department of Defense, 23 Sep. 2011, §5.3.2.3.1, §5.3.2.3.2, §5.3.2.3.7.1.2,
    §5.3.2.3.7.2.1, Tables XI and XII. Distribution A. Not
    filed in this repo.

[2] *Interoperability and Performance Standards for Data Modems*, MIL-STD-188-110C w/Change 1,
    Department of Defense, 3 Jan. 2012, Table XI. Distribution A. Not filed in this repo.

[3] *Interoperability and Performance Standards for Data Modems*, MIL-STD-188-110D,
    Department of Defense, 2017, Tables XI and XII. Filed in this repo at
    `docs/standards/MIL-STD-188-110D.pdf`.

[4] NATO Standardization Agency, "The 600 bit/s, 1200 bit/s and 2400 bit/s NATO
    interoperable narrow band voice coder," STANAG 4591, Ed. 1. Not filed in this repo.

[5] V. Demjanenko and D. Satterlee, "RTP Payload Format for the Mixed Excitation Linear
    Prediction Enhanced (MELPe) Codec," IETF RFC 8130, Mar. 2017.

[6] D. Rowe, codec2, GitHub repository drowe67/codec2, LGPL-2.1,
    https://github.com/drowe67/codec2 (speech samples under `raw/`).

[7] M. W. Chamberlain, "A 600 bps MELP vocoder for use on HF channels," in Proc. IEEE
    MILCOM 2001. Harris Corporation, RF Communications Division. Not filed in this repo.
