# MIL-STD-188-110D Appendix D receiver (3 kHz), phase 1

`m110d_rx` decodes one 3 kHz Appendix D single-tone transmission from a WAV file [1]. It
reports the Waveform ID (WID), the interleaver, the constraint length, acquisition and EOM
status. It decodes the payload text for the BPSK waveforms, WID 1 to 5.

Phase 1 does not decode WID 0 (Walsh data), QPSK, 8PSK or QAM payloads, or K=9 coding. For
those waveforms it reports the WID and the interleaver only.

## Build and run

```sh
cmake -S . -B build-110d -DCMAKE_BUILD_TYPE=Release
cmake --build build-110d --target m110d_rx -j2

# one file
build-110d/bin/m110d_rx <file.wav>

# the 60-file interop set; expected values come from <set>/README.md
python3 -I test/110d/score.py --rx build-110d/bin/m110d_rx --set <test-set dir> --jobs 2

# check the generated mini-probe base sequences against Tables D-XXIII and D-XXV
build-110d/bin/m110d_rx --selftest
```

The WAV must be 16-bit PCM at a sample rate that is a multiple of 2400 Hz (the test set is
48 kHz mono). The output is one `key=value` per line:

| key | meaning |
|---|---|
| `acquired`, `acq_metric`, `acq_time_s` | preamble found; normalised fixed-subsection correlation (1.000 = exact); time of the acquired super-frame |
| `preamble`, `pn` | `nine-fixed` or `single-fixed` (D.5.2.1.3); preamble PN alignment that acquired, `restart` or `continuous` |
| `superframes`, `counts` | super-frames found and their down-counts (D.5.2.1.3.1) |
| `dcd` | 1 when every down-count passed parity and the WID checksum passed |
| `wid`, `interleaver`, `constraint_length`, `wid_checksum` | Waveform ID field (D.5.2.1.3.2, Tables D-XV to D-XVII) |
| `pn_chip_errors`, `fixed_symbol_chip_errors` | hard chip decisions in the acquired super-frame that disagree with the scrambling tables |
| `payload`, `payload_note` | `decoded` or `skipped`, and why |
| `blocks`, `eom`, `bytes`, `text` | interleaver blocks decoded, EOM seen, payload bytes up to the EOM (or every decoded byte without one); non-printing bytes are written `\xNN` |
| `reencode_agree`, `probe_quality` | share of soft coded bits the decoded path re-encodes with the same sign; mean normalised mini-probe correlation |
| `boundary_probe_after_frames` | data frames followed by the cyclically shifted (long-interleaver boundary) mini-probe |

## Measured pass table

This is the scorer's own output. It was run at commit `59604d4` against
`interop-110D-AppD-2026-09-08`, with exit status 0.

```
 #  exp WID exp IL       got WID got IL       ID    text  EOM exp/got  PN          note
---------------------------------------------------------------------------------------
 1        0 short              0 short        PASS  n/a   on/-         restart     
 2        0 medium             0 medium       PASS  n/a   on/-         restart     
 3        0 long               0 long         PASS  n/a   on/-         restart     
 4        1 ultra-short        1 ultra-short  PASS  PASS  on/1         restart     
 5        1 short              1 short        PASS  PASS  on/1         restart     
 6        1 medium             1 medium       PASS  PASS  on/1         restart     
 7        1 long               1 long         PASS  PASS  on/1         restart     
 8        2 ultra-short        2 ultra-short  PASS  PASS  on/1         restart     
 9        2 short              2 short        PASS  PASS  on/1         restart     
10        2 medium             2 medium       PASS  PASS  on/1         restart     
11        2 long               2 long         PASS  PASS  on/1         restart     
12        3 ultra-short        3 ultra-short  PASS  PASS  on/1         restart     
13        3 short              3 short        PASS  PASS  on/1         restart     
14        3 medium             3 medium       PASS  PASS  on/1         restart     
15        3 long               3 long         PASS  PASS  on/1         restart     
16        4 ultra-short        4 ultra-short  PASS  PASS  on/1         restart     
17        4 short              4 short        PASS  PASS  on/1         restart     
18        4 medium             4 medium       PASS  PASS  on/1         restart     
19        4 long               4 long         PASS  PASS  on/1         restart     
20        5 ultra-short        5 ultra-short  PASS  PASS  on/1         restart     
21        5 short              5 short        PASS  PASS  on/1         restart     
22        5 medium             5 medium       PASS  PASS  on/1         restart     
23        5 long               5 long         PASS  PASS  on/1         restart     
24        6 ultra-short        6 ultra-short  PASS  n/a   on/-         restart     
25        6 short              6 short        PASS  n/a   on/-         restart     
26        6 medium             6 medium       PASS  n/a   on/-         restart     
27        6 long               6 long         PASS  n/a   on/-         restart     
28        7 ultra-short        7 ultra-short  PASS  n/a   on/-         restart     
29        7 short              7 short        PASS  n/a   on/-         restart     
30        7 medium             7 medium       PASS  n/a   on/-         restart     
31        7 long               7 long         PASS  n/a   on/-         restart     
32        8 ultra-short        8 ultra-short  PASS  n/a   on/-         restart     
33        8 short              8 short        PASS  n/a   on/-         restart     
34        8 medium             8 medium       PASS  n/a   on/-         restart     
35        8 long               8 long         PASS  n/a   on/-         restart     
36        9 ultra-short        9 ultra-short  PASS  n/a   on/-         restart     
37        9 short              9 short        PASS  n/a   on/-         restart     
38        9 medium             9 medium       PASS  n/a   on/-         restart     
39        9 long               9 long         PASS  n/a   on/-         restart     
40       10 ultra-short       10 ultra-short  PASS  n/a   on/-         restart     
41       10 short             10 short        PASS  n/a   on/-         restart     
42       10 medium            10 medium       PASS  n/a   on/-         restart     
43       10 long              10 long         PASS  n/a   on/-         restart     
44       11 ultra-short       11 ultra-short  PASS  n/a   on/-         restart     
45       11 short             11 short        PASS  n/a   on/-         restart     
46       11 medium            11 medium       PASS  n/a   on/-         restart     
47       11 long              11 long         PASS  n/a   on/-         restart     
48       12 ultra-short       12 ultra-short  PASS  n/a   on/-         restart     
49       12 short             12 short        PASS  n/a   on/-         restart     
50       12 medium            12 medium       PASS  n/a   on/-         restart     
51       12 long              12 long         PASS  n/a   on/-         restart     
52       13 ultra-short       13 ultra-short  PASS  n/a   on/-         restart     
53       13 short             13 short        PASS  n/a   on/-         restart     
54       13 medium            13 medium       PASS  n/a   on/-         restart     
55       13 long              13 long         PASS  n/a   on/-         restart     
56        3 long               3 long         PASS  PASS  on/1         continuous  STD-AMB-001
57       13 long              13 long         PASS  n/a   on/-         continuous  STD-AMB-001
58        3 long               3 long         PASS  PASS  on/1         restart     single-fixed
59        3 long               3 long         PASS  PASS  on/1         restart     long-preamble
60        3 long               3 long         PASS  PASS  off/0        restart     no-EOM
---------------------------------------------------------------------------------------
WID+interleaver correct: 60/60
text PASS (WID 1-5):     24/24
```

"ID" passes when the WID and the interleaver both match the README and the WID checksum
passes. "text" is a prefix match against the README's expected text, which is the first
~100 characters of each message. File 60 has no EOM. It decodes as the message text followed
by zero bytes to the end of the 4096-bit long block (512 bytes), as its README row describes.

## What the test set settled (measured)

Each item below is a single decode run on the named file.

- **Preamble PN alignment, main matrix.** Files 1-55 and 58-60 acquire only with the
  scrambling index restarting at 0 for each Walsh channel symbol (D.5.2.1.1, D.5.2.1.4).
  Every chip of the acquired super-frame matches (`pn_chip_errors=fixed:0/288,count:0/128,wid:0/160`
  on file 15). Under the continuous reading the best metric is 0.323 (file 15).
- **Files 56 and 57 (STD-AMB-001, continuous).** Both acquire only under the continuous
  reading, with metric 0.890; the restart reading gives 0.302. Fixed symbols 0 to 7 match
  fixedPN at offsets 0, 32, ..., 224 with no chip errors. Fixed symbol 8, which needs PN
  indices 256 to 287, matches no fixedPN offset with di-bit 3: the best is 8 of 32 chips
  (`m110d_rx -v` on file 56). Count and WID decode with no chip errors, and the payload
  decodes. What the transmitter sent in fixed symbol 8 is NOT DETERMINABLE from this test set
  and this receiver.
- **K=7 tap order.** The code that decodes the set is Figure D-9 with the input entering the
  x^6 cell: T1 taps at delays {0,2,3,5,6} and T2 at {0,1,2,3,6}. This is the code this repo's
  110A path uses (`src/common/constants.h:79-80`). The time-reversed reading decodes file 13
  to garbage, with no EOM and re-encode agreement 0.8848 (`--code alt`).
- **BPSK scrambler steps.** Three generator shifts per BPSK data symbol decode the set. One
  shift per symbol does not (file 13 with `--bpsk-scr-shifts 1`: no EOM, agreement 0.8179).
  D.5.1.3 lists the iteration count for 8PSK and the QAM orders, but not for BPSK.
- **Scrambling tables.** Tables D-XVIII and D-XIX are images in the published PDF and were
  transcribed by hand. File 56 checks every chip of fixedPN[0..255], cntPN[0..127] and
  widPN[0..159], and they match. The 3 kHz waveform never uses cntPN[128..255] or
  widPN[160..255], so the test set cannot check those entries.
- **Long-interleaver boundary probe.** On file 7 (WID 1 long, 256 frames per block), the
  cyclically shifted mini-probe follows data frame 254, the second-to-last data block
  (D.5.2.2).
- **Single-fixed preamble.** File 58 acquires on the one-symbol fixed subsection with one
  super-frame, down-count 0. The nine-symbol shape gives metrics of 0.271 and 0.287 there.
- **Long preamble.** File 59 shows eight super-frames with down-counts 7 to 0.

## Standard sections implemented

| Section / table | Where |
|---|---|
| D.5.1, Table D-I: 2400 Bd, 1800 Hz sub-carrier, I = cos, Q = -sin | `to_baseband` in `src/m110d/appd_rx.h` |
| D.5.1.5: SRRC 35% matched filter | `srrc_taps` |
| D.5.1.2.1.1, Tables D-III, D-VI: BPSK transcoding, 8PSK points | `decode_payload`, `psk8` |
| D.5.1.3, Figure D-6: data scrambler, reset per data frame | `DataScrambler` in `src/m110d/appd_tables.h` |
| D.5.2, Tables D-XI, D-XII: frame U data + K probe | `waveform_params`, `decode_payload` |
| D.5.2.1, D.5.2.1.1, Tables D-XIII, D-XIV: Walsh preamble | `walsh_section`, `walsh_scores` |
| D.5.2.1.3: nine- and single-symbol fixed subsection | `kFixedNine`, `kFixedSingle`, `acquire` |
| D.5.2.1.3.1: down-count with parity | `count_from_bits` |
| D.5.2.1.3.2, Tables D-XV to D-XVII: WID field and checksum | `wid_from_bits` |
| D.5.2.1.4, Tables D-XVIII to D-XX: preamble scrambling | `kFixedPN`, `kCountPN`, `kWidPN` |
| D.5.2.2, Tables D-XXI, D-XXIII, D-XXV: mini-probes, boundary shift | `mini_probe`, `frank_base`, `tables_selftest` |
| D.5.3.2.1, D.5.3.2.3: K=7, full tail-biting | `ConvCode`, `viterbi_tb` |
| D.5.3.2.4, Tables D-XLIX, D-L: repetition then puncturing | `depuncture` |
| D.5.3.3, Tables D-XXXVII, D-LI: interleaver load, linear fetch | `interleaver_params`, `decode_payload` |
| D.5.4.3, D.5.4.5.1: EOM 4B65A5B2 | `find_eom` |

The TLC section (D.5.2.1.2) is not used. The receiver neither checks it nor counts its blocks.

## Limits of what was tested

- The test set is clean digital audio straight from the transmitter. Every file has a
  probe quality of 1.000 and a frequency offset estimate of 0.00 Hz (or -0.01 Hz on the
  continuous-PN files). No noise, multipath, frequency offset or sample-clock offset was
  tested.
- Channel estimation is a single complex gain per mini-probe, linearly interpolated across
  each data block. There is no equaliser and no symbol-timing tracking.
- Acquisition searches the whole file, offline, at every sample. It does not stream.

## Open questions

1. What the STD-AMB-001 transmitter sends in fixed symbol 8, where the continuous index runs
   past 255 (measured above; not chased).
2. The repo-docs citation rule asks for the cited source document to be filed in the repo's
   docs folder; this branch does not commit the standard's PDF.

## Credits

No code was taken from another project. The read-only reference
milwave-rs [2] was read during this work. It holds no Appendix D preamble, frame structure,
interleaver or tail-biting code, so nothing from it is used here.

## References

[1] Department of Defense, *Interoperability and Performance Standards for Data Modems*,
MIL-STD-188-110D, Appendix D. Distribution Statement A, approved for public release.
Available: https://everyspec.com/MIL-STD/MIL-STD-0100-0299/MIL-STD-188-110D_55856/

[2] HeroesLament, *milwave-rs*, MIT OR Apache-2.0. https://github.com/HeroesLament/milwave-rs
