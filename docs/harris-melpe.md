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

`test/harris/score.py` produces the payloads and the evidence for them:

```sh
python3 -I test/harris/score.py --rx build/bin/m110a_rx --score build/bin/harris_score \
    --set <test-set dir> --out <payload dir> --work <scratch dir>
```

It runs `m110a_rx` (AUTO, DFE) over each file, then decodes each message it reports twice more
with `harris_score`: once with probe phase tracking only, and once with a fractionally spaced
equalizer and PLL (`--eq 6 0.02 0.005`). `<slug>.bin` is the equalized decode, cut at the EOM.
110A has no CRC, so a payload counts as verified when the two `harris_score` decodes are
byte-identical and both found the EOM. That is two receive chains that share only the
deinterleaver and the Viterbi decoder.

| Message | Mode | Bytes | EOM | Disagreement, EQ | Disagreement, tracked | Probe SER | m110a_rx (DFE) |
|---|---|---|---|---|---|---|---|
| 600_1 | 600H | 442 | yes | 0.00 % | 0.00 % | 0.93 % | same bytes, 16.4 dB |
| 600_2 | 600H | 418 | yes | 0.00 % | 0.00 % | 1.26 % | same bytes, 15.8 dB |
| 600_3_multiples_m1 | 600H | 314 | yes | 0.00 % | 0.00 % | 3.01 % | same bytes, 16.2 dB |
| 600_3_multiples_m2 | 600H | 224 | yes | 0.00 % | 0.00 % | 3.04 % | same bytes, 15.8 dB |
| 600_3_multiples_m3 | 600H | 142 | yes | 0.00 % | 0.00 % | 6.50 % | same bytes, 16.3 dB |
| 600_3_multiples_m4 | 600H | 82 | yes | 0.00 % | 0.00 % | 3.97 % | same bytes, 16.9 dB |
| 1200_1 | 1200H | 1156 | yes | 0.00 % | 0.00 % | 0.85 % | same bytes, 15.7 dB |
| 1200_2 | 1200H | 740 | yes | 0.00 % | 0.00 % | 0.29 % | same bytes, 16.1 dB |
| 1200_3 | 1200H | 760 | yes | 0.00 % | 0.00 % | 1.16 % | same bytes, 15.7 dB |
| melp600 | 600H | 628 | yes | 0.00 % | 0.00 % | 10.70 % | same bytes, 11.9 dB |
| melp2400 | 2400V | 1911 | yes | 0.04 % | 2.26 % | 9.64 % | same bytes, 10.8 dB |

The columns:

- *Disagreement* is the mean coded-bit disagreement over the interleaver blocks before the EOM.
- *Probe SER* is the share of zero-valued known symbols whose 8PSK position is wrong, with
  phase tracked and no equalizer. It is an 8-ary decision, so it runs higher than the BPSK and
  QPSK data decisions.
- *m110a_rx* is its own decode, with its `quality_db`. All 11 succeed (`success=1`, `eom=1`).

`600 #3 multiples` holds four messages. `m110a_rx` finds each preamble, at 0.763, 7.557, 13.216
and 17.495 s.

melp2400 is the weak file. Its audio passband is 703 to 2918 Hz, which cuts into the 2400-baud
spectrum. Without an equalizer, 2.3 % of its coded bits disagree. The Viterbi decoder corrects
them: the equalized decode (0.04 % disagreement) is byte-identical to the tracked-only one, and
to the equalized decode at four other equalizer settings, from 9 to 21 taps. `m110a_rx` decodes
it to the same bytes. See *The 2400 receive chain* below.

### The 2400 receive chain

Two receiver defects made `m110a_rx` lose 2400 bps payloads. Both are fixed.

- **Carrier from the preamble.** The fine AFC search scores correlation magnitude over 72-symbol
  segments, which hardly changes within ±1 Hz. On a clean own-TX 2400S signal at 0 Hz it settled
  at −0.50 Hz, and the data phase drifted about 100° over one 0.6 s block. All 1440 data
  symbols were then off by at least one 8PSK position before the phase tracker. The tracker
  recovered all but the last byte. `BrainDecoder::refine_carrier` now fits a line through the
  phases of the 32-symbol blocks of every common segment from the frame found to the last. It
  derotates the preamble and data symbols along that line. On the same signal: 0.00 Hz, and
  0 of 1440 symbols wrong.
- **Block-boundary probes.** A 110C transmitter sends D1 and D2 in the two known segments before
  each new interleaver block [1, 5.3.2.3.7.1.2]. The Harris radio does. The receiver took every
  known segment as zeros, so in two of every 30 (36 at 150 to 1200 bps) segments the phase
  tracker and the DFE trained on the wrong reference. On melp2400 the tracker's frequency
  estimate went from −3.7 to −6.7 Hz and 45 bytes came out wrong. Those two segments per block
  are now left out of probe training and of `quality_db`, so a transmitter that sends them and
  one that sends zeros (this modem's TX) are read the same way. With both changes, the quality of every Harris file
  rises, by 3.6 dB (melp600) to 7.0 dB.

Measured on the MS-DMT reference recordings (150S to 2400L) and this modem's own TX (150S to
2400L, and 2400V). Each was decoded with the mode given and with AUTO, using DFE, clean and with
white noise at noise/signal 0.5, 1.0 and 1.5 (0 to 24 kHz, three seeds): 420 decodes. Neither
fix changes a threshold.

| Mode | Exact, before | Exact, after | Exact with `success=1`, before → after | `success=1` with wrong bytes, before → after |
|---|---|---|---|---|
| 150S to 600L (each) | 20 / 40 | 20 / 40 | 20 → 20 | 0 → 0 |
| 1200S | 18 / 40 | 20 / 40 | 16 → 18 | 0 → 0 |
| 1200L | 18 / 40 | 20 / 40 | 16 → 20 | 2 → 0 |
| 2400S | 0 / 40 | 12 / 40 | 0 → 10 | 2 → 0 |
| 2400L | 10 / 40 | 14 / 40 | 8 → 10 | 0 → 0 |
| 2400V | 0 / 20 | 2 / 20 | 0 → 2 | 2 → 0 |

No decode returned fewer correct bytes after the change than before. MS-DMT 2400S clean went
from 51 / 54 bytes at 6.3 dB to 54 / 54 at 11.9 dB with `success=1`. Own-TX 2400S and 2400V
clean went from 53 / 54 (the last byte wrong) with `success=1` to 54 / 54. Own-TX with added
noise finds no preamble in this measurement, before and after. Its leading symbols plus the 0.1 s
noise lead-in put the preamble outside `BrainDecoder`'s 500-symbol search window; `m110a_rx`
cuts 40 ms before each preamble, so it does not meet this. The 150 to 600 bps failures are those
own-TX noise cases.

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
- **TX known symbols.** This modem's TX sends every known segment as zeros. 110C puts D1 and D2
  in the two before each new interleaver block [1, 5.3.2.3.7.1.2]. The Harris radio does. The
  receiver skips those two segments, so it reads either kind.
- **Quality thresholds.** `quality_db` no longer counts the block-boundary probes, so it reads
  higher than before wherever a transmitter sends D1 and D2 there. The thresholds were set on
  the old figure (`api/modem_rx.cpp`, `quality_threshold_db`). They were not re-derived. On the
  420 decodes above, no wrong payload passes them.
- **TX end of message.** This modem's TX ends a message with four frames of zero data. It does
  not send the EOM 4B65A5B2 [1, 5.3.2.3.1] or the interleaver flush [1, 5.3.2.3.2]. So
  `eom=1` on own-TX loopback is not tested.
- **75 bps.** AUTO names 75S and 75L. There is no 75 bps data path, in TX or in RX.
- `src/m110a/preamble_codec.h` has its own D1, D2 table, with 75 bps marked "estimated" as 7,7
  short and 5,7 long. Those collide with 2400 digital voice and the Harris 600 code. Nothing in
  the build includes the file.

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
