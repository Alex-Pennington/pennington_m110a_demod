# MIL-STD-188-110D Appendix D receiver (3 kHz)

## Handoff

This section describes the receiver on `feat/110d-stream`, cut from master `db8329a`: the
equaliser receiver, plus streaming input (`m110d_rx --stream`) and a band-limiting condition
in the channel simulator (`m110d_chan --bandpass`). The sections below it hold the detail and
the full measured tables.

### State of the receiver

- **Clean interop set** (60 files from another transmitter, every 3 kHz waveform, WID 0-13,
  four interleavers, five variants). It passes 60/60 on WID and interleaver and 60/60 on text
  with EOM as expected, at 48 kHz and at 8 kHz from a file and through stdin (measured on
  `feat/110d-stream`). As one continuous `--stream` feed with gaps it gives the 60
  messages in order, none doubled. That holds at 48 and 8 kHz, at real-time pace and as fast
  as the receiver reads (measured; see "Streaming input").
- **Implemented:**
  - preamble acquisition over +-112.5 Hz;
  - WID, interleaver and constraint-length detection;
  - payload decode for Walsh, BPSK, QPSK, 8PSK and 16/32/64/256QAM, with K=7 coding;
  - frequency tracking and symbol-timing tracking;
  - a probe-aided block MMSE-DFE equaliser for WID 1-13;
  - any input rate from 6840 Hz up, and a WAV on stdin;
  - streaming: `--stream` decodes a continuous feed on stdin and prints each transmission
    when it completes, with memory bounded by `--max-message` (default 120 s).
- **Not implemented:** K=9 coding.
- **STD-AMB-001:** files 56 and 57 acquire only with the PN running continuously across the
  section. The main matrix acquires only with PN restarting per Walsh symbol. The receiver
  finds whichever is in use.

### Channel matrix as it stands (measured)

Default files: WID 0, 3, 13, 7 and 10, all SHORT. Seeds: 3, and 10 for fading. Each cell is
one decode of a burst, not a BER figure.

| Matrix | Before tracking (`c10b5da`) | Tracking (`79de273`) | Equaliser, master `1f02930` | Equaliser, `fix/110d-drift` | master `db8329a` | `feat/110d-stream` |
|---|---|---|---|---|---|---|
| default (110) | 87 | 95 | 104 | 104 | 104 | 104 |
| awgn (150) | not split out (the baseline ran awgn in one combined table) | 111 | 116 | 116 | 116 | 116 |
| foff (240) | 135 | 240 | 240 | 240 | 240 | 240 |
| drift and D.6.5 sweep (105) | 60 | 105 | 100 | 105 | 105 | 105 |
| ppm and ppm drift (270) | 144 | 270 | 270 | 270 | 270 | 270 |
| static (45) | 6 | 6 | 36 | 36 | 36 | 36 |
| fading, 10 seeds (400) | 70 | 93 | 322 | 322 | 322 | 322 |

- The last two columns are full runs from fresh builds of master `db8329a` and of this
  branch, compared cell for cell from their CSVs: every cell is equal (280 cells at 3 seeds,
  40 at 10). On the 60 clean files the branch's file-mode output is byte-identical to
  master's.

- The `1f02930` and `fix/110d-drift` columns are full runs of every matrix from fresh
  builds, side by side: `--matrix default,awgn,foff,drift,ppm,static --seeds 3` and
  `--matrix fading --seeds 10`. Five cells differ between them, all in drift, each 2/3 on master and 3/3 on the fix
  (files 1/13/53/29/41): fdrift +3.5 Hz/s @30 on WID 10; fdrift -3.5 Hz/s @30 on WID 13, 7
  and 10; sweep D.6.5 @30 on WID 10. Every other cell is equal.
- **Support placement.** Inside the periodic probe a delay d and d +- L fit alike, so the
  support search scores each unwrapping of the delay profile on the probe samples it leaves
  free of data. On a single path with linear drift the tracked frequency on the first probes
  lags by 1 to 3 Hz (measured with `-v`, file 41, `--seed 2 --snr 30 --fdrift -3.5`). The phase
  ramp raises the residual of the true support, scored over the whole probe, about 3x above
  two aliased supports scored on short windows at the probe edges. One alias places the main
  path at delay -16 (fit 8.9 dB, no EOM). The timing loop centres the probe on the strongest
  one-tap path, so the search now admits only hypotheses that place the strongest delay at
  |d| <= L/2. The repro decodes with EOM, support 0 and 15, fit 29.1 dB.
- The weak tail on that channel still lands at +15 instead of -1: the same window bias
  decides between those two. It costs nothing measured; the residual comparison across
  hypotheses of different window length is still biased by residual frequency error.

### Branches and worktrees

- **Merged to master and pushed as branches:** `feat/110d-rx`, `feat/110d-walsh`,
  `feat/110d-qam`, `feat/110d-integrate`, `docs/110d-std-pdf`, `feat/110d-chan`,
  `feat/110d-8k`, `feat/110d-proofs`, `feat/110d-sync`, `feat/110d-eq`. None holds anything
  that master lacks.
- **This branch:** `feat/110d-stream`, worktree `m110a-wt-110d-stream`, not merged.
- **Worktree left in place:** `m110a-wt-110d-proofs` (`feat/110d-proofs`, merged). It holds
  an untracked `test/110d/__pycache__/`. Deleting that folder was refused by a permission
  denial, so the worktree was not removed. Nothing in it is unmerged.
- **Worktrees of other work:** `m110a-wt-rx-path`, `m110a-wt-mode-detect` and
  `m110a-wt-harris-*`. They do not belong to 110D work.

### Rebuild and rerun

The test set is not in the repository. It lives in the main checkout, in the ignored
`interop-110D-AppD-2026-09-08/` folder, called `<set>` below. Its `README.md` holds the
expected text.

```sh
cmake -S . -B build-110d -DCMAKE_BUILD_TYPE=Release
cmake --build build-110d --target m110d_rx m110d_chan -j2
build-110d/bin/m110d_rx --selftest            # selftest_mismatches=0
build-110d/bin/m110d_chan --selftest          # selftest: PASS

# clean 60 at 48 kHz, at 8 kHz, and at 8 kHz through stdin
python3 -I test/110d/score.py --rx build-110d/bin/m110d_rx --set <set> --jobs 2
python3 -I test/110d/score.py --rx build-110d/bin/m110d_rx --set <set> --jobs 2 --resample 8000 --workdir <scratch>/wav8k
python3 -I test/110d/score.py --rx build-110d/bin/m110d_rx --set <set> --jobs 2 --resample 8000 --workdir <scratch>/wav8k --stdin

# the 60 as one continuous --stream feed: as fast as it goes, at real-time pace, at 8 kHz; 1 h of noise
python3 -I test/110d/stream.py --rx build-110d/bin/m110d_rx --set <set>
python3 -I test/110d/stream.py --rx build-110d/bin/m110d_rx --set <set> --resample 8000 --workdir <scratch>/wav8k --pace 1
python3 -I test/110d/stream.py --rx build-110d/bin/m110d_rx --set <set> --resample 8000 --workdir <scratch>/wav8k --noise-snr 30 --duration 3600 --every 60

# channel matrices: default, or awgn, foff, drift, ppm, static, fading, band, all
python3 -I test/110d/channel.py --rx build-110d/bin/m110d_rx --chan build-110d/bin/m110d_chan --set <set> --jobs 2 --work <scratch>/chan [--matrix X] [--seeds N]

# a radio's audio filter: on its own over the 60 files, and on every condition of the default matrix
python3 -I test/110d/channel.py --rx build-110d/bin/m110d_rx --chan build-110d/bin/m110d_chan --set <set> --jobs 2 --work <scratch>/chan --matrix band --seeds 1 --files 1,2,...,60
python3 -I test/110d/channel.py --rx build-110d/bin/m110d_rx --chan build-110d/bin/m110d_chan --set <set> --jobs 2 --work <scratch>/chan --matrix default --bandpass 300:2700

# proofs files for the report page (t5.json, t6.json), from a verified master build
python3 -I test/110d/score.py --rx build-110d/bin/m110d_rx --set <set> --jobs 2 --proofs <inbox> --decoder-label "m110d_rx master @<commit>"
python3 -I test/110d/channel.py --rx build-110d/bin/m110d_rx --chan build-110d/bin/m110d_chan --set <set> --jobs 2 --work <scratch>/chan --proofs <inbox> --decoder-label "m110d_rx master @<commit>"
```

`<inbox>` is the greybeard-ale proofs inbox, `~/.local/share/greybeard-ale/proofs-inbox/`. Set
`TMPDIR` and `<scratch>` to a directory outside `/tmp`. `m110d_rx --eq off` gives the
pre-equaliser one-tap path.

### Unfinished

- The support residual comparison is biased toward short observation windows when the
  probe carries a residual frequency error (above). Placement of the strongest delay is
  constrained; weak taps are not.
- WID 0 has no equaliser (it has no probes). It still fails fading: 0/10 on Poor.
- WID 12 (256QAM) fails Poor at 30 dB. Table D-LXIV sets no Poor requirement for WID 11-12.
- static-wid2 fails on WID 7, 10 and 13. Its 9 ms spread exceeds what the K=32 probes cover.
  Table D-LXV pairs that channel with WID 2, which decodes.
- The equaliser thresholds were set on named cases, not swept. Fractional (T/2) spacing was
  not tried.

### Open questions

1. **WID 0 needs about 9 dB more than Table D-LXIV in AWGN** (measured; cause not
   determined). The tracking and equaliser work did not explain it.
2. **ITU good at 30 dB, WID 7.** On multipath the preamble clock fit is off by tens of ppm
   (measured). After tracking this cell fell from 4/10 to 3/10. The equaliser absorbs the
   error, and the cell now passes 10/10 (measured). The fit itself is unchanged.
3. **STD-AMB-001 fixed symbol 8.** On the continuous-PN files, PN index 256-287 matches no
   offset: 29/288 chips wrong (measured). This is a question for the transmitter's author.

### Next steps, in order

1. 256QAM (WID 12) through a 300 Hz low edge: 300-2700 Hz takes its EVM from -53.9 to
   -23.7 dB (measured). The cause was not investigated.
2. WID 0 in noise and fading: find the 9 dB.
3. A BER-style measurement (long runs, LONG interleaver) to compare with Table D-LXIV
   properly.
4. K=9 coding.
5. Streaming: the re-run interval grows with the segment, so a long transmission's block
   comes out up to 0.2 x its length late (3.5 s on a 19 s file, measured). An incremental
   decoder would remove the re-runs.

## Overview

`m110d_rx` decodes one 3 kHz Appendix D single-tone transmission from a WAV file [1], or with
`--stream` every transmission in a continuous feed on stdin (see "Streaming input"). It
reports the Waveform ID (WID), the interleaver, the constraint length, acquisition and EOM
status. It decodes the payload text for every waveform, WID 0 to 13: Walsh (WID 0), BPSK
(WID 1-5), QPSK (WID 6 and 13), 8PSK (WID 7), 16QAM (WID 8), 32QAM (WID 9), 64QAM (WID 10,
11) and 256QAM (WID 12).

It does not decode K=9 coding. For a K=9 transmission it reports the WID and the interleaver
only.

## Source document

The standard is filed in this repo at `docs/standards/MIL-STD-188-110D.pdf` [1].

- Title: *Interoperability and Performance Standards for Data Modems*, MIL-STD-188-110D,
  29 December 2017.
- Download: https://everyspec.com/MIL-STD/MIL-STD-0100-0299/download.php?spec=MIL-STD-188-110D.055856.pdf
- Distribution statement, as printed: "DISTRIBUTION STATEMENT A: Approved for public release;
  distribution unlimited."
- SHA-256: `aae90e308c8e69f11b013bda6a6cbf70858d5ef40db5a478641c95e9abac63d4`

## Build and run

```sh
cmake -S . -B build-110d -DCMAKE_BUILD_TYPE=Release
cmake --build build-110d --target m110d_rx -j2

# one file
build-110d/bin/m110d_rx <file.wav>

# the 60-file interop set; expected values come from <set>/README.md
python3 -I test/110d/score.py --rx build-110d/bin/m110d_rx --set <test-set dir> --jobs 2

# check the mini-probe base sequences against Tables D-XXIII and D-XXV, the WID 0
# scrambler against the D.5.1.4 example, and QAM points against Tables D-VIII and D-X
build-110d/bin/m110d_rx --selftest
```

The WAV must be 16-bit PCM at a sample rate that is a multiple of 2400 Hz (the test set is
48 kHz mono). The output is one `key=value` per line:

| key | meaning |
|---|---|
| `acquired`, `acq_metric`, `acq_time_s` | preamble found; normalised fixed-subsection correlation (1.000 = exact); time of the acquired super-frame |
| `freq_offset_hz`, `clock_ppm` | carrier offset at the acquired super-frame; receive clock error fitted over the preamble (positive: more samples per symbol) |
| `preamble`, `pn` | `nine-fixed` or `single-fixed` (D.5.2.1.3); preamble PN alignment that acquired, `restart` or `continuous` |
| `superframes`, `counts` | super-frames found and their down-counts (D.5.2.1.3.1) |
| `dcd` | 1 when every down-count passed parity and the WID checksum passed |
| `wid`, `interleaver`, `constraint_length`, `wid_checksum` | Waveform ID field (D.5.2.1.3.2, Tables D-XV to D-XVII) |
| `pn_chip_errors`, `fixed_symbol_chip_errors` | hard chip decisions in the acquired super-frame that disagree with the scrambling tables |
| `payload`, `payload_note` | `decoded` or `skipped`, and why |
| `blocks`, `eom`, `bytes`, `text` | interleaver blocks decoded, EOM seen, payload bytes up to the EOM (or every decoded byte without one); non-printing bytes are written `\xNN` |
| `reencode_agree`, `probe_quality` | share of soft coded bits the decoded path re-encodes with the same sign; mean normalised mini-probe correlation (WID 0, which has no probes: mean normalised correlation of the decided Walsh symbol) |
| `walsh_alt_last` | WID 0 only: blocks whose last channel symbol correlates better with the alternate Walsh set than the normal set, over blocks decoded |
| `boundary_probe_after_frames` | data frames followed by the cyclically shifted (long-interleaver boundary) mini-probe |
| `bits_per_symbol`, `evm_db`, `qam_scale` | WID 6-13 only: data bits per symbol; error vector magnitude of the data symbols against their decided constellation points; constellation scale used |
| `points_unused`, `point_max_err` | WID 6-13 only: transmitted constellation points (8PSK symbols, or QAM symbols after scrambling) never decided; largest distance from a received symbol to its decided table point, in probe-amplitude units |

## Measured pass table

This is the scorer's own output. It was run at merge commit `e0d079b` against
`interop-110D-AppD-2026-09-08`, with exit status 0.

```
 #  exp WID exp IL       got WID got IL       ID    text  EOM exp/got  PN          note
---------------------------------------------------------------------------------------
 1        0 short              0 short        PASS  PASS  on/1         restart     
 2        0 medium             0 medium       PASS  PASS  on/1         restart     
 3        0 long               0 long         PASS  PASS  on/1         restart     
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
24        6 ultra-short        6 ultra-short  PASS  PASS  on/1         restart     
25        6 short              6 short        PASS  PASS  on/1         restart     
26        6 medium             6 medium       PASS  PASS  on/1         restart     
27        6 long               6 long         PASS  PASS  on/1         restart     
28        7 ultra-short        7 ultra-short  PASS  PASS  on/1         restart     
29        7 short              7 short        PASS  PASS  on/1         restart     
30        7 medium             7 medium       PASS  PASS  on/1         restart     
31        7 long               7 long         PASS  PASS  on/1         restart     
32        8 ultra-short        8 ultra-short  PASS  PASS  on/1         restart     
33        8 short              8 short        PASS  PASS  on/1         restart     
34        8 medium             8 medium       PASS  PASS  on/1         restart     
35        8 long               8 long         PASS  PASS  on/1         restart     
36        9 ultra-short        9 ultra-short  PASS  PASS  on/1         restart     
37        9 short              9 short        PASS  PASS  on/1         restart     
38        9 medium             9 medium       PASS  PASS  on/1         restart     
39        9 long               9 long         PASS  PASS  on/1         restart     
40       10 ultra-short       10 ultra-short  PASS  PASS  on/1         restart     
41       10 short             10 short        PASS  PASS  on/1         restart     
42       10 medium            10 medium       PASS  PASS  on/1         restart     
43       10 long              10 long         PASS  PASS  on/1         restart     
44       11 ultra-short       11 ultra-short  PASS  PASS  on/1         restart     
45       11 short             11 short        PASS  PASS  on/1         restart     
46       11 medium            11 medium       PASS  PASS  on/1         restart     
47       11 long              11 long         PASS  PASS  on/1         restart     
48       12 ultra-short       12 ultra-short  PASS  PASS  on/1         restart     
49       12 short             12 short        PASS  PASS  on/1         restart     
50       12 medium            12 medium       PASS  PASS  on/1         restart     
51       12 long              12 long         PASS  PASS  on/1         restart     
52       13 ultra-short       13 ultra-short  PASS  PASS  on/1         restart     
53       13 short             13 short        PASS  PASS  on/1         restart     
54       13 medium            13 medium       PASS  PASS  on/1         restart     
55       13 long              13 long         PASS  PASS  on/1         restart     
56        3 long               3 long         PASS  PASS  on/1         continuous  STD-AMB-001
57       13 long              13 long         PASS  PASS  on/1         continuous  STD-AMB-001
58        3 long               3 long         PASS  PASS  on/1         restart     single-fixed
59        3 long               3 long         PASS  PASS  on/1         restart     long-preamble
60        3 long               3 long         PASS  PASS  off/0        restart     no-EOM
---------------------------------------------------------------------------------------
WID+interleaver correct: 60/60
text PASS (WID 0):       3/3
text PASS (WID 1-5):     24/24
text PASS (WID 6-13):    33/33
text PASS, EOM as expected (WID 0-13): 60/60
```

"ID" passes when the WID and the interleaver both match the README and the WID checksum
passes. "text" is scored for every WID, 0 to 13, and is a prefix match against the README's
expected text, which is the first ~100 characters of each message. The last line counts files
whose text passes and whose EOM matches the README's EOM column (seen for EOM-on, not seen for
file 60). File 60 has no EOM. It decodes as the message text followed
by zero bytes to the end of the 4096-bit long block (512 bytes), as its README row describes.

## What the test set settled (measured)

Each item below is a single decode run on the named file.

- **Preamble PN alignment, main matrix.** The receiver tries both readings and keeps the
  better. For files 1-55 and 58-60 that is the reading where the scrambling index restarts at
  0 for each Walsh channel symbol (D.5.2.1.1, D.5.2.1.4; the PN column above). On file 15
  every chip of the acquired super-frame matches
  (`pn_chip_errors=fixed:0/288,count:0/128,wid:0/160`). The continuous reading scores 0.323
  on files 13, 15 and 59. On file 58 the single fixed symbol uses PN indices 0 to 31 under
  either reading, so both score 1.000 and that file does not tell the two apart.
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
- **WID 6-13 payloads.** Files 24-55 and 57 decode to the README text with the EOM seen,
  re-encode agreement 1.0000, and EVM between -53.9 and -54.7 dB (`m110d_rx` on each file).
  File 57 (WID 13, STD-AMB-001 continuous PN) decodes the same as file 55.
- **QPSK scrambler steps.** Three generator shifts per QPSK data symbol decode the set, as
  for BPSK. One or two shifts do not: file 52 with `--psk-scr-shifts 1` gives no EOM and
  agreement 0.8340, with 2 no EOM and 0.8262. D.5.1.3 states 3 iterations for 8PSK only.
  For 8PSK the count is checked the same way (file 28: 1 and 2 shifts give no EOM).
- **QAM scrambler bits.** XOR of the symbol number with the rightmost N register cells, the
  rightmost cell as the least significant bit, N shifts per symbol (D.5.1.3), decodes WID
  8-12. No other reading was tried.
- **QAM amplitude.** The transmitter sends the Table D-VII to D-X points at the listed
  amplitudes, on the scale of the unit-amplitude mini-probes. With the channel gain taken
  from the probes and scale 1 the EVM is -54 dB on files 32, 36, 40, 44 and 48; scaling
  each frame to the constellation's RMS instead gives -35.4, -36.9, -34.9, -38.6 and
  -41.8 dB (`--qam-scale 0`). Scale 1 is the default. D.5.1 says nothing on the relative
  power of data and probe that this reading relies on; this is measured, not specified.
- **Constellation tables.** Tables D-IV, D-V and D-VII to D-X are images in the published
  PDF and were transcribed by hand (`src/m110d/appd_constellations.h`). Across files 24-55
  every point of every constellation is decided at least once (`points_unused=0` on at
  least one file per order), and no received symbol lies further than 0.0051 from its table
  point (`point_max_err`). So every entry of Tables D-VII to D-X is checked to about
  0.005, which is coarser than the six printed decimals. The Table D-IV and D-V
  transcodings are checked only through the decode: a wrong entry would corrupt the text.
- **24-symbol mini-probe.** WID 11 and 12 use the 13-symbol base of Table D-XXII(a)
  extended to 24, with the boundary shift of 6 (Table D-XXI). Probe quality is 1.000 and
  the shifted probe follows data frame 62 of 64 on files 47 and 51.
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
- **WID 0 data start.** Data channel symbols begin at the first symbol after the last
  preamble super-frame, with no mini-probe between (Tables D-XI and D-XII give N/A for
  WID 0; D.5.2.2). On files 1-3 the first block starts at symbol 1728 = 3 super-frames of 576
  symbols, and every block's mean normalised Walsh correlation is 1.000 (`m110d_rx -v`).
- **WID 0 scrambler reset.** The D.5.1.4 sequence restarts at each interleaver block. The
  reading that runs it on across blocks decodes block 0 of file 1, then block 1 correlates at
  0.172 and no EOM is found (temporary build, not kept). Files 2 and 3 (288 and 1152 coded
  bits per block, 4608 and 18432 chips) decode, so the wrap at 2048 values within a block is
  exercised as well.
- **WID 0 alternate Walsh set.** The last channel symbol of every block correlates better with
  the alternate set than the normal set: `walsh_alt_last=21/21`, `6/6` and `2/2` on files 1,
  2 and 3. The receiver places the block boundary by count from the data start; it does not
  search for the alternate symbol.
- **WID 0 di-bit order and code.** The first bit of the di-bit (MSB in the D.5.1.2.3.1 table)
  is taken as the earlier interleaver fetch, as D.5.1.2.1.2 states for QPSK. With that order,
  the Figure D-9 K=7 rate 1/2 code with no repetition or puncturing, and the D.5.3.3 fetch
  with the Table D-LI increments 11, 37 and 145, files 1-3 decode with re-encode agreement
  1.0000 and EOM seen.

## Standard sections implemented

| Section / table | Where |
|---|---|
| D.5.1, Table D-I: 2400 Bd, 1800 Hz sub-carrier, I = cos, Q = -sin | `to_baseband` in `src/m110d/appd_rx.h` |
| D.5.1.5: SRRC 35% matched filter | `srrc_taps` |
| D.5.1.2.1.1, Tables D-III, D-VI: BPSK transcoding, 8PSK points | `decode_payload`, `psk8` |
| D.5.1.3, Figure D-6: data scrambler, reset per data frame | `DataScrambler` in `src/m110d/appd_tables.h` |
| D.5.1.2.1.2, D.5.1.2.1.3, Tables D-IV, D-V: QPSK and 8PSK transcoding | `kQpskTranscode`, `k8pskTranscode` in `src/m110d/appd_constellations.h` |
| D.5.1.2.2, Tables D-VII to D-X: 16, 32, 64 and 256QAM points | `qam_point`, `tables_selftest_qam` |
| D.5.1.3: PSK mod-8 and QAM XOR descrambling, soft demapping | `demap_frame` in `src/m110d/appd_qam.h` |
| D.5.2.2, Tables D-XXI, D-XXII(a): 24-symbol mini-probe | `mini_probe` |
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
| D.5.1.2.3.1: WID 0 Walsh sets, normal and alternate (last di-bit of a block) | `kWalshData4`, `kWalshAlt8`, `decode_walsh` in `src/m110d/appd_walsh.h` |
| D.5.1.4: WID 0 trinomial (159, 31) scrambler, 2048 values, reset per block | `walsh_scrambler`, checked by `walsh_selftest` against the D.5.1.4 example |

Synchronisation. The standard specifies the signal and the performance (D.6.4, D.6.5), not
the receiver's method; these are this receiver's choices:

| Function | Method | Where |
|---|---|---|
| Frequency acquisition | Fixed-subsection search over 25 Hz bins, -100 to +100 Hz (coverage +-112.5 Hz), phase ramp on the chip reference, 32-chip coherent parts; sps/4 time grid refined to the sample | `search` |
| Frequency estimate | Bin offset, plus the phase step between 8-chip parts (+-150 Hz unambiguous), then between 32-chip parts (+-37.5 Hz) | `estimate_freq` |
| Clock ratio | Fixed-subsection peak of every super-frame (least-squares parabola over +-4 samples), straight-line fit, re-measured under the fitted ratio and refitted twice | `fit_clock`, `peak_offset` |
| Sampling | Linear interpolation of the 48 kHz matched-filter output at the fitted clock ratio plus a tracked offset | `at`, `spos`, `sample` |
| WID 1-13 frequency | Alpha-beta (0.5, 0.15) on the residual frequency and its drift per frame; probe-to-probe rotation unwrapped about the prediction; gain interpolated in magnitude under a linear phase ramp between probe centres | `decode_payload` |
| WID 1-13 timing | Alpha-beta (0.15, 0.01) on the probe timing; parabola on probe gain magnitude at +-sps/4; no update below probe quality 0.6 | `decode_payload` |
| WID 0 carrier | Second-order decision-directed loop (wn 0.15, zeta 0.707 per channel symbol), per-chip derotation, error from the largest-magnitude hypothesis, started from the last super-frame's gain | `decode_walsh` |
| WID 0 timing | Second-order decision-directed loop (0.1, 0.005 per channel symbol), parabola on the decided hypothesis at +-sps/4 | `decode_walsh` |

The TLC section (D.5.2.1.2) is not used. The receiver neither checks it nor counts its blocks.

## Channel testing

`m110d_chan` impairs one test WAV with a channel after Appendix E and writes a new 16-bit WAV
at the same rate. `test/110d/channel.py` runs a matrix of files x conditions x seeds through
it and `m110d_rx`. It scores each decode with the `score.py` rule: the text must start with
the README's expected text.

### How to run

```sh
cmake --build build-110d --target m110d_rx m110d_chan -j2

# simulator self-test after E.7 (SNR calibration, fading power and spectrum, shift, clock)
build-110d/bin/m110d_chan --selftest

# one file
build-110d/bin/m110d_chan --seed 1 --snr 10 --preset poor --foff 5 in.wav out.wav

# the default matrix: WID 0, 3, 13, 7 and 10 SHORT (rows 1, 13, 53, 29, 41), 3 seeds, ~45 s
python3 -I test/110d/channel.py --rx build-110d/bin/m110d_rx --chan build-110d/bin/m110d_chan \
    --set <test-set dir> --jobs 2

# other matrices: awgn, foff, drift, ppm, fading, static, or all; any README rows
python3 -I test/110d/channel.py ... --matrix awgn,ppm --files 1,13,53,29,41 --seeds 3 --csv runs.csv
```

Impaired WAVs are written under `$TMPDIR` (or `--work`) and deleted after each decode unless
`--keep` is given. A cell of the table is passes/seeds. Seed k gives the same noise and
fading draws for every file and condition, and the same impaired file on every run.

### Channel definitions

The chain, in order (`src/channel/appe_channel.h`):

| Step | Definition | Standard |
|---|---|---|
| Clock | Re-sampled on the receiver's clock, error `ppm + ppm_per_s * t`. Positive ppm: more samples per second, so the burst is longer and the carrier and symbol rate are lower by the same fraction. Kaiser-windowed sinc, 32 taps. | E.5.1 names 500 ppm as the simulator's own crystal limit |
| Analytic signal | FFT Hilbert transform | E.4.2 note, E.6.2 |
| Paths | Delay rounded to whole samples; power shares scaled to a total gain of 1 | E.5.2, E.5.4 (k) |
| Fading | Complex white Gaussian noise through `f(t) = exp(-pi^2 t^2 d^2)`, truncated at 1% of peak; Gaussian spectrum, sigma = d/2, so d is the two-sigma fading bandwidth. Taps computed at 128 d, cubic-interpolated to the sample rate. d = 0 is a static path. | E.5.4, Figure E-2 |
| Frequency offset | Constant, linear drift, or a triangle sweep, applied to the analytic signal (a shift, no image) | D.6.4, D.6.5 |
| Output | Real part | E.4.2 note |
| Noise | White Gaussian over 0 to fs/2. The variance puts `P / SNR` in the stated bandwidth (default 3000 Hz), where P is the input's average power over the burst. The burst runs from the first to the last 10 ms window within 30 dB of the strongest. | E.5.3, E.5.5; D.6.1 "measured in the specified bandwidth" |
| Level | Scaled down to a -1 dBFS peak only when it would clip (`out_gain_db`); `--gain` sets it instead | none |

Presets (`m110d_chan --list-presets`):

| Preset | Paths | Source |
|---|---|---|
| `awgn` | one static path | Table D-LXIV, AWGN channel |
| `poor` | 2 equal, 0 and 2 ms, d = 1 Hz | D.6.1 and Table D-LXIV notes ("ITU-R F.1487 Mid-Latitude-Disturbed") |
| `moderate` | 2 equal, 0 and 1 ms, d = 0.5 Hz | ITU-R F.1487 mid-latitude moderate. Not filed in this repo; the values are not checked against a copy |
| `good` | 2 equal, 0 and 0.5 ms, d = 0.1 Hz | ITU-R F.1487 mid-latitude quiet. Not filed in this repo; the values are not checked against a copy |
| `static-wid2`, `static-wid10`, `static-wid12` | static, equal power, (0, 3, 9), (0, 2, 4.5) and (0, 1.5) ms | Table D-LXV, 3 kHz rows |

`--path D:G[:F[:S]]` adds a custom path (delay ms, power dB, fading Hz, shift Hz).

Matrix conditions (`test/110d/channel.py`). "@30" is 30 dB SNR in 3 kHz, the D.6.4 SNR. It is
used to isolate the other impairments:

| Matrix | Conditions |
|---|---|
| `default` | clean; AWGN at D-LXIV+0, +6 and +12 dB; +10 and -30 Hz; +100 ppm; Poor |
| `awgn` | SNR at the file's Table D-LXIV AWGN value -9, -6, -3, 0, +3, +6, +9, +12, +15 and +20 dB |
| `foff` | +-2, 5, 10, 20, 30, 35, 40 and 75 Hz |
| `drift` | 0.5, 1, 2 and +-3.5 Hz/s; the D.6.5 triangle (+-75 Hz at 3.5 Hz/s, starting at 0 Hz rising, at t = 0 of the file) at 30 and 24 dB |
| `ppm` | +-10, 20, 30, 50, 100, 200, 300 and 500 ppm; 10 and 100 ppm/s drift |
| `fading` | good, moderate, Poor at 30 dB; Poor at the file's Table D-LXIV Poor value and +10 dB; Poor with no noise; D.6.4 (Poor, +-75 Hz, 30 dB) |
| `static` | the three Table D-LXV channels at 50 dB (D.6.2's SNR), each run on every file |

### Standard performance targets

Table D-LXIV gives the average SNR in 3 kHz for a coded BER of 1.0E-5. It applies with the
Long interleaver and a 20-super-frame preamble (D.6.1), measured over at least 60 minutes
(AWGN) or 5 hours (Poor). The matrix prints these values per file in its second header line.
Table D-LXV and D.6.2 ask for a BER of 1.0E-5 at 50 dB on the static channels. D.6.4 asks for
synchronisation held for 5 minutes at +-75 Hz on Poor at 30 dB. D.6.5 asks for a BER of 1.0E-5
at 24 dB under the +-75 Hz, 3.5 Hz/s sweep.

A pass here is one decode of a SHORT-interleaver burst of about 100 characters (about 800
bits), with a 3-super-frame preamble. Each file carries 3 s of silence around the burst
(test-set README). It is not a BER measurement and is not
comparable with these targets one for one. The matrix shows where a receiver breaks, and
whether a change moves that point.

### Baseline (measured)

These are the runner's own tables. They were run at commit `c10b5da`, whose receiver sources
(`src/m110d/`, `m110d_rx/`) are identical to master `32d4424`, with
`--matrix default,awgn,foff,drift,ppm,static --seeds 3` and `--matrix fading --seeds 10`,
both at `--jobs 2`. Both exited 0; they took 5 min 43 s and 2 min 52 s.

```
condition                 #1 W0 SH   #13 W3 SH  #53 W13 SH   #29 W7 SH  #41 W10 SH
D-LXIV AWGN / Poor dB      -6 / -1       3 / 7      6 / 11     13 / 19     21 / 31
----------------------------------------------------------------------------------
clean                          1/1         1/1         1/1         1/1         1/1
awgn D-LXIV+0             -6dB 0/3     3dB 3/3     6dB 3/3    13dB 3/3    21dB 3/3
awgn D-LXIV+6              0dB 1/3     9dB 3/3    12dB 3/3    19dB 3/3    27dB 3/3
awgn D-LXIV+12             6dB 3/3    15dB 3/3    18dB 3/3    25dB 3/3    33dB 3/3
foff +10 Hz @30                3/3         3/3         3/3         3/3         3/3
foff -30 Hz @30                3/3         3/3         3/3         3/3         3/3
ppm +100 @30                   0/3         3/3         3/3         3/3         0/3
poor @30                       0/3         1/3         1/3         1/3         0/3
awgn D-LXIV-9            -15dB 0/3    -6dB 0/3    -3dB 0/3     4dB 0/3    12dB 0/3
awgn D-LXIV-6            -12dB 0/3    -3dB 0/3     0dB 0/3     7dB 1/3    15dB 0/3
awgn D-LXIV-3             -9dB 0/3     0dB 3/3     3dB 2/3    10dB 3/3    18dB 3/3
awgn D-LXIV+3             -3dB 0/3     6dB 3/3     9dB 3/3    16dB 3/3    24dB 3/3
awgn D-LXIV+9              3dB 3/3    12dB 3/3    15dB 3/3    22dB 3/3    30dB 3/3
awgn D-LXIV+15             9dB 3/3    18dB 3/3    21dB 3/3    28dB 3/3    36dB 3/3
awgn D-LXIV+20            14dB 3/3    23dB 3/3    26dB 3/3    33dB 3/3    41dB 3/3
foff -75 Hz @30                0/3         0/3         0/3         0/3         0/3
foff -40 Hz @30                0/3         0/3         0/3         0/3         0/3
foff -35 Hz @30                0/3         0/3         0/3         0/3         0/3
foff -20 Hz @30                3/3         3/3         3/3         3/3         3/3
foff -10 Hz @30                3/3         3/3         3/3         3/3         3/3
foff -5 Hz @30                 3/3         3/3         3/3         3/3         3/3
foff -2 Hz @30                 3/3         3/3         3/3         3/3         3/3
foff +2 Hz @30                 3/3         3/3         3/3         3/3         3/3
foff +5 Hz @30                 3/3         3/3         3/3         3/3         3/3
foff +20 Hz @30                3/3         3/3         3/3         3/3         3/3
foff +30 Hz @30                3/3         3/3         3/3         3/3         3/3
foff +35 Hz @30                0/3         0/3         0/3         0/3         0/3
foff +40 Hz @30                0/3         0/3         0/3         0/3         0/3
foff +75 Hz @30                0/3         0/3         0/3         0/3         0/3
fdrift +0.5 Hz/s @30           3/3         3/3         3/3         3/3         3/3
fdrift +1 Hz/s @30             0/3         3/3         3/3         3/3         3/3
fdrift +2 Hz/s @30             0/3         3/3         3/3         3/3         0/3
fdrift +3.5 Hz/s @30           0/3         3/3         3/3         0/3         0/3
fdrift -3.5 Hz/s @30           0/3         3/3         3/3         0/3         0/3
sweep D.6.5 from 0 @30         0/3         3/3         3/3         0/3         0/3
sweep D.6.5 from 0 @24         0/3         3/3         3/3         0/3         0/3
ppm -500 @30                   0/3         0/3         0/3         0/3         0/3
ppm -300 @30                   0/3         0/3         0/3         0/3         0/3
ppm -200 @30                   0/3         0/3         3/3         3/3         0/3
ppm -100 @30                   0/3         3/3         3/3         3/3         0/3
ppm -50 @30                    0/3         3/3         3/3         3/3         0/3
ppm -30 @30                    0/3         3/3         3/3         3/3         3/3
ppm -20 @30                    3/3         3/3         3/3         3/3         3/3
ppm -10 @30                    3/3         3/3         3/3         3/3         3/3
ppm +10 @30                    3/3         3/3         3/3         3/3         3/3
ppm +20 @30                    3/3         3/3         3/3         3/3         3/3
ppm +30 @30                    0/3         3/3         3/3         3/3         3/3
ppm +50 @30                    0/3         3/3         3/3         3/3         0/3
ppm +200 @30                   0/3         0/3         3/3         3/3         0/3
ppm +300 @30                   0/3         0/3         0/3         0/3         0/3
ppm +500 @30                   0/3         0/3         0/3         0/3         0/3
ppm-drift +10/s @30            0/3         3/3         3/3         3/3         3/3
ppm-drift +100/s @30           0/3         0/3         0/3         0/3         0/3
static-wid2 @50                0/3         0/3         0/3         0/3         0/3
static-wid10 @50               0/3         0/3         0/3         0/3         0/3
static-wid12 @50               3/3         3/3         0/3         0/3         0/3
----------------------------------------------------------------------------------
runs: 830, text PASS: 477

condition                #1 W0 SH   #13 W3 SH  #53 W13 SH   #29 W7 SH  #41 W10 SH
D-LXIV AWGN / Poor dB     -6 / -1       3 / 7      6 / 11     13 / 19     21 / 31
---------------------------------------------------------------------------------
good @30                     3/10        9/10        8/10        4/10        1/10
moderate @30                 0/10        4/10        5/10        2/10        0/10
poor @30                     0/10        2/10        5/10        2/10        0/10
poor @D-LXIV poor       -1dB 0/10    7dB 2/10   11dB 4/10   19dB 2/10   31dB 0/10
poor @D-LXIV poor+10     9dB 0/10   17dB 2/10   21dB 4/10   29dB 2/10   41dB 0/10
poor noiseless               0/10        2/10        5/10        2/10        0/10
D.6.4 poor +75 Hz @30        0/10        0/10        0/10        0/10        0/10
D.6.4 poor -75 Hz @30        0/10        0/10        0/10        0/10        0/10
---------------------------------------------------------------------------------
runs: 400, text PASS: 70
```

Where each file breaks, read from the tables above:

| File | AWGN: lowest SNR with 3/3 and every step above | Frequency offset passes | Clock offset passes (both signs) | Drift passes | Poor, 10 seeds |
|---|---|---|---|---|---|
| 1, WID 0 | 3 dB (D-LXIV -6) | +-30 Hz | 20 ppm | 0.5 Hz/s | 0/10 at every SNR |
| 13, WID 3 | 0 dB (D-LXIV 3) | +-30 Hz | 100 ppm | 3.5 Hz/s, sweep | 2/10 |
| 53, WID 13 | 6 dB (D-LXIV 6) | +-30 Hz | 200 ppm | 3.5 Hz/s, sweep | 4/10 to 5/10 |
| 29, WID 7 | 10 dB (D-LXIV 13) | +-30 Hz | 200 ppm | 2 Hz/s | 2/10 |
| 41, WID 10 | 18 dB (D-LXIV 21) | +-30 Hz | 30 ppm | 1 Hz/s | 0/10 |

Measured detail behind the table. The acquisition, WID and probe-quality figures come from the
CSV (`--csv`) of an earlier `--matrix all --seeds 3` run of commit `12ca9dc`. That run used
coarser AWGN, offset and ppm steps; its conditions in common with the tables above give the
same cells.

- **Frequency offset.** Every file passes at +-30 Hz and fails at +-35 Hz and beyond, including
  the D.6.4 +-75 Hz condition. At +-40 and +-75 Hz each run reports `acquired=1`, a frequency
  estimate of 0.00 Hz, and a wrong WID or interleaver. The estimate comes from the phase step
  between 32-chip Walsh symbols at 2400 Bd (`src/m110d/appd_rx.h:424-431`). That step is
  ambiguous beyond +-37.5 Hz (inference from the code, not measured separately).
- **Poor channel.** The pass count is the same with no noise as at 30 dB, and the same at the
  D-LXIV Poor SNR and 10 dB above it, except WID 13 (4/10 against 5/10). So on Poor the fading
  realisation decides the result, not the noise. In the 3-seed run every failure on good,
  moderate and Poor acquired and read the WID correctly; the payload failed.
- **Clock offset.** The receiver's frequency estimate follows the carrier shift the clock error
  causes (-0.56 Hz at +300 ppm, 1800 Hz x 300e-6 = 0.54 Hz). Failures acquire and read the WID
  correctly, and mean probe quality falls (0.95 at +-100 ppm on WID 10, 0.22 to 0.26 at
  +-500 ppm on WID 3 to 13). This is consistent with the receiver having no symbol-timing tracking (see
  below; inferred).
- **Drift.** The estimate is the offset at the preamble (2.12 Hz at +1 Hz/s, burst at 2 s).
  Failing runs read the WID correctly.
- **Static multipath at 50 dB.** Table D-LXV's WID 2 and WID 10 channels fail on every file.
  The two-path WID 12 channel passes only the WID 0 and WID 3 files. Mean probe quality is
  0.15 to 0.24 on the three-path channels.
- **WID 0** needs 3 dB in AWGN, 9 dB above its Table D-LXIV value, and fails the Poor channel
  with no noise.

### With frequency and timing tracking (measured)

These are the runner's own tables for the receiver at commit `1f9b405` (sources in
`src/m110d/` and `m110d_rx/` as merged here), run with
`--matrix default,awgn,foff,drift,ppm,static --seeds 3` and `--matrix fading --seeds 10`,
both at `--jobs 2`. Both exited 0.

```
condition                 #1 W0 SH   #13 W3 SH  #53 W13 SH   #29 W7 SH  #41 W10 SH
D-LXIV AWGN / Poor dB      -6 / -1       3 / 7      6 / 11     13 / 19     21 / 31
----------------------------------------------------------------------------------
clean                          1/1         1/1         1/1         1/1         1/1
awgn D-LXIV+0             -6dB 0/3     3dB 3/3     6dB 3/3    13dB 3/3    21dB 3/3
awgn D-LXIV+6              0dB 3/3     9dB 3/3    12dB 3/3    19dB 3/3    27dB 3/3
awgn D-LXIV+12             6dB 3/3    15dB 3/3    18dB 3/3    25dB 3/3    33dB 3/3
foff +10 Hz @30                3/3         3/3         3/3         3/3         3/3
foff -30 Hz @30                3/3         3/3         3/3         3/3         3/3
ppm +100 @30                   3/3         3/3         3/3         3/3         3/3
poor @30                       0/3         1/3         1/3         1/3         0/3
awgn D-LXIV-9            -15dB 0/3    -6dB 0/3    -3dB 0/3     4dB 0/3    12dB 0/3
awgn D-LXIV-6            -12dB 0/3    -3dB 0/3     0dB 0/3     7dB 1/3    15dB 0/3
awgn D-LXIV-3             -9dB 0/3     0dB 3/3     3dB 2/3    10dB 3/3    18dB 3/3
awgn D-LXIV+3             -3dB 0/3     6dB 3/3     9dB 3/3    16dB 3/3    24dB 3/3
awgn D-LXIV+9              3dB 3/3    12dB 3/3    15dB 3/3    22dB 3/3    30dB 3/3
awgn D-LXIV+15             9dB 3/3    18dB 3/3    21dB 3/3    28dB 3/3    36dB 3/3
awgn D-LXIV+20            14dB 3/3    23dB 3/3    26dB 3/3    33dB 3/3    41dB 3/3
foff -75 Hz @30                3/3         3/3         3/3         3/3         3/3
foff -40 Hz @30                3/3         3/3         3/3         3/3         3/3
foff -35 Hz @30                3/3         3/3         3/3         3/3         3/3
foff -20 Hz @30                3/3         3/3         3/3         3/3         3/3
foff -10 Hz @30                3/3         3/3         3/3         3/3         3/3
foff -5 Hz @30                 3/3         3/3         3/3         3/3         3/3
foff -2 Hz @30                 3/3         3/3         3/3         3/3         3/3
foff +2 Hz @30                 3/3         3/3         3/3         3/3         3/3
foff +5 Hz @30                 3/3         3/3         3/3         3/3         3/3
foff +20 Hz @30                3/3         3/3         3/3         3/3         3/3
foff +30 Hz @30                3/3         3/3         3/3         3/3         3/3
foff +35 Hz @30                3/3         3/3         3/3         3/3         3/3
foff +40 Hz @30                3/3         3/3         3/3         3/3         3/3
foff +75 Hz @30                3/3         3/3         3/3         3/3         3/3
fdrift +0.5 Hz/s @30           3/3         3/3         3/3         3/3         3/3
fdrift +1 Hz/s @30             3/3         3/3         3/3         3/3         3/3
fdrift +2 Hz/s @30             3/3         3/3         3/3         3/3         3/3
fdrift +3.5 Hz/s @30           3/3         3/3         3/3         3/3         3/3
fdrift -3.5 Hz/s @30           3/3         3/3         3/3         3/3         3/3
sweep D.6.5 from 0 @30         3/3         3/3         3/3         3/3         3/3
sweep D.6.5 from 0 @24         3/3         3/3         3/3         3/3         3/3
ppm -500 @30                   3/3         3/3         3/3         3/3         3/3
ppm -300 @30                   3/3         3/3         3/3         3/3         3/3
ppm -200 @30                   3/3         3/3         3/3         3/3         3/3
ppm -100 @30                   3/3         3/3         3/3         3/3         3/3
ppm -50 @30                    3/3         3/3         3/3         3/3         3/3
ppm -30 @30                    3/3         3/3         3/3         3/3         3/3
ppm -20 @30                    3/3         3/3         3/3         3/3         3/3
ppm -10 @30                    3/3         3/3         3/3         3/3         3/3
ppm +10 @30                    3/3         3/3         3/3         3/3         3/3
ppm +20 @30                    3/3         3/3         3/3         3/3         3/3
ppm +30 @30                    3/3         3/3         3/3         3/3         3/3
ppm +50 @30                    3/3         3/3         3/3         3/3         3/3
ppm +200 @30                   3/3         3/3         3/3         3/3         3/3
ppm +300 @30                   3/3         3/3         3/3         3/3         3/3
ppm +500 @30                   3/3         3/3         3/3         3/3         3/3
ppm-drift +10/s @30            3/3         3/3         3/3         3/3         3/3
ppm-drift +100/s @30           3/3         3/3         3/3         3/3         3/3
static-wid2 @50                0/3         0/3         0/3         0/3         0/3
static-wid10 @50               0/3         0/3         0/3         0/3         0/3
static-wid12 @50               3/3         3/3         0/3         0/3         0/3
----------------------------------------------------------------------------------
runs: 830, text PASS: 740
```

```
condition                #1 W0 SH   #13 W3 SH  #53 W13 SH   #29 W7 SH  #41 W10 SH
D-LXIV AWGN / Poor dB     -6 / -1       3 / 7      6 / 11     13 / 19     21 / 31
---------------------------------------------------------------------------------
good @30                     8/10        9/10        8/10        3/10        1/10
moderate @30                 0/10        4/10        5/10        2/10        0/10
poor @30                     0/10        2/10        5/10        2/10        0/10
poor @D-LXIV poor       -1dB 0/10    7dB 2/10   11dB 4/10   19dB 2/10   31dB 0/10
poor @D-LXIV poor+10     9dB 0/10   17dB 2/10   21dB 4/10   29dB 2/10   41dB 0/10
poor noiseless               0/10        2/10        5/10        3/10        0/10
D.6.4 poor +75 Hz @30        0/10        2/10        5/10        2/10        0/10
D.6.4 poor -75 Hz @30        0/10        2/10        5/10        2/10        0/10
---------------------------------------------------------------------------------
runs: 400, text PASS: 93
```

Against the baseline above:

| Matrix | Baseline | Now | Rows worse |
|---|---|---|---|
| default, awgn, foff, drift, ppm, static (3 seeds) | 477/830 | 740/830 | none |
| foff | 135/240 | 240/240 | none |
| drift and D.6.5 sweep | 60/105 | 105/105 | none |
| ppm and ppm drift | 144/270 | 270/270 | none |
| default | 87/110 | 95/110 | none |
| awgn | every cell as now, except WID 0 at 0 dB: 1/3 | WID 0 at 0 dB: 3/3 | none |
| static | as baseline | as baseline | none |
| fading (10 seeds) | 70/400 | 93/400 | good @30, WID 7: 4/10 to 3/10 |

Measured detail:

- **Frequency.** Every file passes every offset to +-75 Hz. The estimate reads -75.01 Hz at
  -75 Hz and -39.99 Hz at -40 Hz (`--csv`). WID 7 at 30 dB also decodes at +100, -110 and
  +125 Hz (one seed each, `m110d_chan --foff`), beyond the +-112.5 Hz the bins cover.
- **Drift and D.6.5.** Every file passes +-3.5 Hz/s and the D.6.5 sweep at 24 dB.
- **Clock.** The fitted clock error reads 300.0 ppm at +300 ppm and -500.5 ppm at -500 ppm
  (WID 7, 30 dB). Without the re-measurement passes it read 263 and -439 ppm, 12.5 % low on
  every case tried. On a +100 ppm/s drift it reads about 236 ppm, the error over the
  preamble; the loops track the rest.
- **D.6.4.** Poor at +-75 Hz now passes as often as Poor at 0 Hz (WID 3 2/10, WID 13 5/10,
  WID 7 2/10). It was 0/10 on every file.
- **The one worse cell.** On ITU good (two paths 0.5 ms apart), the preamble clock fit reads
  between -48 and +6 ppm where the true error is 0 (WID 7, seeds 1-10), and WID 7 passes 3/10
  against 4/10 with the fit disabled (temporary build, not kept). Probe timing loop gains of
  0.5 / 0.1 cost one more seed on WID 7 and WID 10 there, so the loop runs at 0.15 / 0.01;
  those gains still pass +-500 ppm and +-100 ppm/s on WID 7, 10 and 13 (3 seeds each).
- **WID 0** still needs 3 dB in AWGN (D-LXIV -6 dB) and still fails Poor at every SNR. The
  tracking work did not find its cause.

## Equaliser (WID 1-13)

Source: `src/m110d/appd_eq.h` (CIR fit, block MMSE-DFE) and `AppDRx::setup_eq` /
`equalise_frame` in `src/m110d/appd_rx.h`. `m110d_rx --eq off` keeps the one-tap gain path,
and `--eq probe` skips the data-aided pass.

### Why this design

The frame gives the receiver a known mini-probe after every data block and one at the end of
the preamble (D.5.2.2; U and K per WID from Tables D-XI and D-XII). A probe is its base
sequence (Table D-XXI: length L = 13, 16 or 25 at 3 kHz) cyclically extended to K symbols, so
its first K-L symbols repeat its last K-L: a cyclic prefix. While the delay spread is at most
K-L symbols, the last L received probe symbols hold no data energy, and the probe gives the
channel impulse response (CIR) directly:

| WID | U | K | L | K-L, ms | Table D-LXV channel at 3 kHz |
|---|---|---|---|---|---|
| 1-2 | 48 | 48 | 25 | 9.6 | WID 2: 0 / 3.0 / 9.0 ms |
| 3-4 | 96 | 32 | 16 | 6.7 | |
| 5-10, 13 | 256 | 32 | 16 | 6.7 | WID 10: 0 / 2.0 / 4.5 ms |
| 11-12 | 360 | 24 | 13 | 4.6 | WID 12: 0 / 1.5 ms |

So every data block sits between two known probes, each with its own CIR estimate. The
equaliser equalises one block at a time with both probes known: a block MMSE
decision-feedback equaliser (LDL^H form), with decisions fed back from the end of the block,
next to the following probe, towards its start. It is a DFE rather than a linear equaliser
because the Table D-LXV channels are equal-power paths, whose spectral nulls a linear
equaliser fills only by enhancing the noise. Each symbol's error variance comes out of the
factorisation and weights its soft decisions into the existing Viterbi decoder.

### Steps

1. **Tracking (pass 1).** The frequency and timing loops above run unchanged over every probe
   position to the end of the input. A probe with one-tap correlation below 0.3 does not update
   the frequency loop.
2. **Support.** The probe-averaged periodic power-delay profile gives the delays that carry
   energy, modulo L. Each way of unwrapping it into a span of at most K-L delays that places
   the strongest delay at |d| <= L/2 is scored by a least-squares fit on the probe samples that
   span leaves free of data (the timing loop centres the probe on the strongest path, and the
   residual alone favours an alias a base period away when the probe carries a residual
   frequency error). Ties within 2.5x go to the shortest span. The chosen arc +-2 delays is
   then thresholded at 2.5x the noise of the periodic estimate.
3. **CIR per probe.** A least-squares fit on that support uses every clean probe sample (K for
   a single path, as the one-tap estimate did). The signal ends at the first two probes in a row
   whose fit explains less than 0.2 of what it can.
4. **Per block.** The CIR is interpolated linearly between the two probes, with the rotation
   between probe centres taken from the CIRs and unwrapped about the frequency loop's
   prediction. Block MMSE-DFE. Then a data-aided pass: with the first decisions as data, a
   least-squares CIR at the block centre gives a quadratic trajectory, and the block is
   equalised again. The pass with the lower decision error is kept.
5. **Soft decisions.** Weighted by the DFE's per-symbol error variance, plus any measured
   decision error above it over a +-16 symbol window.

Acquisition changed with it. On equal-power paths the nine-symbol fixed-section metric falls
to about 0.6, below the 0.7 at which the single-symbol shape is also searched, and the
single-symbol shape could then win by chance and give no valid down-count. The nine-symbol lock
is now tried as well, and the first lock that reads a valid preamble and WID is kept.

### Measured

Baseline is master `79de273`, re-measured here with the same runner, seeds and jobs; it
reproduces the tables above (static 6/45, fading 93/400). Matrices run one at a time
(`--matrix X --seeds 3`, fading `--seeds 10`, `--jobs 2`):

| Matrix | 79de273 | eq at 2d66ba3 | eq at master 1f02930 | eq at fix/110d-drift |
|---|---|---|---|---|
| default (110 runs) | 95 | 103 | 104 | 104 |
| awgn (150) | 111 | 116 | 116 | 116 |
| foff (240) | 240 | 240 | 240 | 240 |
| drift (105) | 105 | 105 | 100 | 105 |
| ppm (270) | 270 | 270 | 270 | 270 |
| static (45) | 6 | 36 | 36 | 36 |
| fading, 10 seeds (400) | 93 | 310 | 322 | 322 |

The last two columns are full runs of every matrix from fresh builds (`--matrix
default,awgn,foff,drift,ppm,static --seeds 3` and `--matrix fading --seeds 10`, `--jobs 3`).
They differ in five drift cells, each 2/3 on master and 3/3 on `fix/110d-drift` (files
1/13/53/29/41): fdrift +3.5 Hz/s @30 on WID 10; fdrift -3.5 Hz/s @30 on WID 13, 7 and 10;
sweep D.6.5 @30 on WID 10. Every other cell is equal.

Fading at `2d66ba3`, 10 seeds:

```
condition                #1 W0 SH   #13 W3 SH  #53 W13 SH   #29 W7 SH  #41 W10 SH
good @30                     8/10       10/10       10/10       10/10        8/10
moderate @30                 0/10        9/10       10/10       10/10        9/10
poor @30                     0/10       10/10       10/10        9/10        9/10
poor @D-LXIV poor       -1dB 0/10    7dB 9/10   11dB 9/10   19dB 8/10   31dB 9/10
poor @D-LXIV poor+10     9dB 0/10  17dB 10/10  21dB 10/10   29dB 9/10  41dB 10/10
poor noiseless               0/10       10/10       10/10        9/10        9/10
D.6.4 poor +75 Hz @30        0/10       10/10       10/10       10/10        9/10
D.6.4 poor -75 Hz @30        0/10       10/10       10/10        8/10        9/10
runs: 400, text PASS: 310
```

Static at `2d66ba3`: static-wid10 and static-wid12 pass 3/3 on every file. static-wid2 passes
WID 0 and WID 3 only: its 9 ms (21.6 symbols) spread is longer than the K-L = 16 symbols of
the K = 32 probes of WID 3-13, and Table D-LXV pairs it with WID 2 (K = 48). The WID 0 cells
improve through the acquisition change; the equaliser does not touch WID 0.

`c5e4eb7` (support refinement after `2d66ba3`):

- Clean set 60/60 on WID+interleaver and on text with EOM at 48 kHz and at 8 kHz
  (`score.py`, `--resample 8000`); selftest 0 mismatches.
- Default matrix 104/110, every cell at or above `79de273`.
- Reduced fading, 4 seeds, files 13, 53, 29, 41: 126/128 (the two misses are good @30 on WID
  10). Reduced awgn, 3 seeds, the same files: every cell as at `2d66ba3`.
- Table D-LXV channels on their own WIDs at 50 dB, seed 1 (`m110d_chan --preset`): WID 2 file
  9 on static-wid2, WID 10 files 41 and 42 on static-wid10, WID 12 file 49 on static-wid12 all
  decode. Every default file except WID 0 decodes static-wid10 and static-wid12. The fit's
  channel-to-noise reads 40 dB on static-wid2 / WID 2 and 37 dB on static-wid12 / WID 12.
- Poor with +100 ppm, static-wid10 with +200 ppm, and good with +10 ppm/s clock drift, at 30 or
  50 dB, decode on WID 7 and 10 (1-2 seeds each).

### Not done

- WID 0 has no equaliser (no probes): still 0/10 on Poor.
- WID 12 on Poor @30 still fails (2 seeds tried). Table D-LXIV gives no Poor requirement for
  WID 11-12.
- On equal paths 1.2 symbols apart at 30 dB, the 64QAM EVM is 4.5 dB worse than on one path;
  the MMSE-DFE bound for that channel is about 3 dB (inferred from the mean of ln(1 + cos)).
  Fractional (T/2) spacing was not tried.
- The tap threshold, the 2.5x tie, the 0.2 end-of-signal level and the +-16 symbol window were
  set on the cases named in the source comments, not swept.

## Input formats

- **WAV**: PCM 16, 24 or 32-bit, or IEEE float 32-bit; `WAVE_FORMAT_EXTENSIBLE` accepted;
  any channel count, channel 0 used. The sample rate is read from the `fmt ` chunk.
- **stdin**: `m110d_rx -` (or `--stdin`) reads the WAV from stdin. A data chunk whose length
  field is 0 or 0xFFFFFFFF, or runs past the end of the input (a streamed WAV), is read to
  EOF; the RIFF length field is ignored. The whole input is read before decoding starts. The
  output is the same as for a file, with `file=-`.
- **Streaming**: `m110d_rx --stream` decodes a continuous feed on stdin (see "Streaming
  input").
- **Rates**: a multiple of 2400 Hz (9600, 48000, 96000, ...) goes to the demodulator as is.
  Any other rate from 6840 Hz up (8000, 11025, 16000, 44100, ...) is resampled to 48000 Hz
  first. Below 6840 Hz the 110D band (180-3420 Hz) does not fit, and the file is refused.
- **Resampler** (`src/m110d/appd_input.h`): rational polyphase FIR, L/M = 48000/fs reduced by
  their gcd, Kaiser-windowed sinc, delay-compensated. With fmin = min(fs, 48000), the
  passband is 0-0.4375 fmin and the stopband starts at 0.5 fmin, with at least 100 dB of
  attenuation. `m110d_rx --resampler-report FS` measures the designed filter:

  | input | L/M | taps | passband | deviation | stopband | attenuation |
  |---|---|---|---|---|---|---|
  | 8000 Hz | 6/1 | 625 | 0-3500 Hz | 0.0001 dB | 4000-24000 Hz | 100.3 dB |
  | 16000 Hz | 3/1 | 313 | 0-7000 Hz | 0.0001 dB | 8000-24000 Hz | 100.4 dB |
  | 44100 Hz | 160/147 | 16641 | 0-19294 Hz | 0.0001 dB | 22050 Hz up | 102.8 dB |

  At 3420 Hz, the top of the 110D band, the 8 kHz filter loses 0.0001 dB.

### 8 kHz measurement

`score.py --resample 8000` resamples each clean file 48 -> 8 kHz with scipy `resample_poly`
and a separately designed Kaiser FIR (515 taps, passband 0-3400 Hz within 0.0001 dB,
stopband 4000 Hz up at 100.2 dB, 0.002 dB down at 3420 Hz), writes 16-bit PCM (0 samples
clipped) and decodes that. `--stdin` pipes each WAV to `m110d_rx -` with its RIFF and data
length fields set to 0xFFFFFFFF (`--stream-sizes ff`, the default) or 0 (`zero`).

Receiver at 4e43f17, scorer at 6a7f4f2:

| input | WID+interleaver | text WID 0 | text WID 1-5 | text WID 6-13 | text + EOM |
|---|---|---|---|---|---|
| 48 kHz file | 60/60 | 3/3 | 24/24 | 33/33 | 60/60 |
| 8 kHz file | 60/60 | 3/3 | 24/24 | 33/33 | 60/60 |
| 8 kHz stdin, lengths 0xFFFFFFFF | 60/60 | 3/3 | 24/24 | 33/33 | 60/60 |
| 8 kHz stdin, lengths 0 | 60/60 | 3/3 | 24/24 | 33/33 | 60/60 |

The 48 kHz table is byte-identical to the one before the input stage was added. At 8 kHz every
WID 6-13 file keeps its 48 kHz EVM to 0.1 dB (-53.9 to -54.7 dB, 256QAM included), and every
file keeps probe quality 1.000 and re-encode agreement 1.0000.

```
python3 -I test/110d/score.py --rx build-110d/bin/m110d_rx --set <set> --resample 8000 --workdir <dir>
python3 -I test/110d/score.py --rx build-110d/bin/m110d_rx --set <set> --resample 8000 --workdir <dir> --stdin
```

## Streaming input

`m110d_rx --stream` reads a WAV stream on stdin and decodes every transmission in it. It
prints each one as soon as it is complete. The design is in `src/m110d/appd_stream.h`:

- **Input.** The WAV header is read once. A data length of 0 or 0xFFFFFFFF is read to EOF,
  any other length to that length. Samples are read in 0.1 s pieces. A rate that is not a
  multiple of 2400 Hz goes through `StreamResampler`. It gives exactly what the file-mode
  resampler gives on the whole feed: 0 difference and equal length at 8000, 11025 and
  44100 Hz, on 3 s of noise fed in pieces of 37 to 1500 samples (measured).
- **Search.** `AppDRx::detect` runs the file-mode preamble search on each new second plus
  0.5 s of overlap. A lock counts only where the preamble reads: a valid down-count and WID
  checksum. The single-symbol fixed shape also needs metric 0.7. Its 32 chips pass 0.5 on
  noise in every window: median 0.565 and maximum 0.647 over 267 windows of 30 dB-feed noise
  (measured). Between transmissions 3 s of audio is kept.
- **Decode.** From 1.5 s before the lock to the newest sample, `AppDRx::run`, the file decoder,
  is re-run as audio arrives. It runs every 0.5 s until the preamble has passed, then every
  max(1 s, 0.2 x segment). The preamble search is confined to the lock, and then to the
  preamble the last run read (`RxOptions::acq_lo`, `acq_hi`). A later transmission in the
  segment therefore cannot take the lock, and the search does not grow with the segment.
- **Complete.** A transmission is printed when the EOM decodes. Without an EOM it is printed
  once 1 s of input has followed the last probe or Walsh block the decoder used, where the
  decoder found the signal lost (`RxResult::end_by_input` false). A K=9 transmission is
  printed 1 s after its preamble. One that reaches `--max-message` seconds (default 120) is
  printed as decoded so far, with `stream_note=cut`. Anything still open is printed at EOF.
  Search resumes where the printed transmission ended.
- **Output.** Each transmission is the file-mode block, opened by `message=N` and closed by
  an empty line. `acq_time_s` is the time in the feed, and `stream_end_s` is where the decoded
  signal ended. stdout is flushed after each block.
- **Memory.** Between transmissions it is a few MB. During one it is about 40 bytes per
  internal-rate sample of the segment: the input, its copy to baseband and the mixer product.
  At 48 kHz that is 1.9 MB per second, so the 120 s cap bounds it near 230 MB.
- **File mode is unchanged.** It searches the whole input as before. The new `RxResult`
  fields are not printed.

```sh
# a WAV stream on stdin (lengths 0xFFFFFFFF), any rate from 6840 Hz
producer | build-110d/bin/m110d_rx --stream [--max-message S] [-v]

# the 60-file set joined with gaps, at real-time pace and as fast as it goes; and 1 h of noise
python3 -I test/110d/stream.py --rx build-110d/bin/m110d_rx --set <set> [--resample 8000 --workdir <dir>] [--pace 1]
python3 -I test/110d/stream.py --rx build-110d/bin/m110d_rx --set <set> --resample 8000 --workdir <dir> \
    --noise-snr 30 --duration 3600 --every 60
```

### Measured

The feed is the 60 files in README order, each followed by 0.5, 1, 2 or 3 s of silence in turn
(570 s in all). Each file keeps its own leading and trailing silence. The noise feed is 1 h of
white Gaussian noise at 30 dB SNR in 3 kHz against each file's active power. One file is
placed every 60 s, 12 s into the minute, cycling through the 60. "In order" means block N
carries file N's WID, interleaver and text (score.py's prefix rule), with no block missing or
extra. RSS is the receiver's own `VmHWM`, read from `/proc`.

| feed | rate | pace | in order, correct | extra | wall | peak RSS |
|---|---|---|---|---|---|---|
| 60 files with gaps | 48 kHz | as fast as read | 60/60 | 0 | 134 s | 42.8 MB |
| 60 files with gaps | 8 kHz | as fast as read | 60/60 | 0 | 140 s | 48.7 MB |
| 60 files with gaps | 8 kHz | real time | 60/60 | 0 | 572 s | 48.5 MB |
| 1 h noise, a file a minute | 8 kHz | as fast as read | 60/60 | 0 | 1476 s | 49.7 MB |

- **Memory over the hour** (RSS sampled every 6 min of feed): 4, 37, 37, 37, 37, 37, 37, 37,
  18 and 22 MB. It does not grow with the feed.
- **Noise.** Over the hour the detector searched 3279 windows that held no preamble. Their best
  metric, always the single-symbol shape, was at most 0.697. Of 60 detections, 60 read a
  valid preamble and none was dropped.
- **Speed.** The hour of 8 kHz feed took 1476 s under `nice -n 10`, with two other decoders
  running: 2.4 times faster than real time. On noise every window runs four searches,
  because the single-symbol shape is searched whenever the nine-symbol shape scores below
  0.7.
- **Latency** at real-time pace: each block came out between 0.4 s before and 3.5 s after
  the end of its file (mean 0.4 s). File end includes the file's trailing silence, which
  is why a block can come out before it. The long ones are the long segments, where the
  re-run interval is 0.2 x segment: file 3 (WID 0 LONG) at 3.5 s, file 59 (long preamble) at
  2.4 s, and files 2, 7, 11 and 55 at 1.7 s.
- The 1 h run used the build before the commit that only changed the usage text.

## Band-limited channel

The waveform occupies about 180-3420 Hz. A radio's SSB audio filter is narrower.
`m110d_chan --bandpass LO:HI[:N]` adds one as step 7 of the chain (`src/channel/appe_channel.h`),
after the noise, so it filters signal and noise alike. The SNR is still stated before it, in
3 kHz. Its edges are Butterworth high-pass and low-pass, order N each (default 8, the
classic 8-pole crystal filter). They are -3 dB at LO and HI, made by the bilinear transform
with pre-warped corners. The filter is minimum-phase, so its group delay rises toward both
edges, as an analogue filter's does. `m110d_chan --selftest` checks it: 300-2700 Hz is
-0.0003 dB at 1500 Hz, -3.01 dB at 300 and 2700 Hz, -48.2 dB at 150 Hz and -50.4 dB at
5400 Hz. A tone through the chain matches the design to 0.05 dB.

`channel.py --matrix band` runs each filter on its own and at 30 dB. A 30 dB row with no
filter is the reference. `channel.py --bandpass LO:HI` adds the filter to every condition of
a matrix.

**All 60 files, one seed** (`--matrix band --seeds 1 --files 1,...,60`, `feat/110d-stream`).
Text pass out of 60, and the files that fail:

| condition | pass | failing files |
|---|---|---|
| awgn @30, no filter | 60/60 | - |
| 300-2700 Hz | 56/60 | 48-51 (WID 12, all four interleavers) |
| 300-2700 Hz @30 | 55/60 | 46 (WID 11 medium), 48-51 (WID 12) |
| 300-3000 Hz | 59/60 | 49 (WID 12 short) |
| 300-3000 Hz @30 | 57/60 | 48, 49, 50 (WID 12 ultra-short, short, medium) |
| 300-3300 Hz | 60/60 | - |
| 300-3300 Hz @30 | 57/60 | 48, 49, 50 (WID 12) |
| 200-3000 Hz | 60/60 | - |
| 200-3000 Hz @30 | 60/60 | - |

**Which waveforms survive.** WID 0-10 survive every filter tried, clean and at 30 dB: all
of their files decode. WID 11 survives all but one cell: file 46 at 300-2700 Hz and 30 dB.
WID 12 (256QAM) is the one that fails. The low edge matters more than the high one: 200-3000 Hz passes
everything, 300-3300 Hz loses only WID 12 at 30 dB, and 300-2700 Hz loses all four WID 12
(256QAM) files even with no noise. At 300-2700 Hz the 256QAM EVM is -23.7 dB, against
-53.9 dB clean. It is -25 dB at 300-3000 and 300-3300 Hz, and -31 to -33 dB at 200-3000 Hz.
WID 11 (64QAM, 12000 bps) is at -21.6 dB at 300-2700 Hz. It decodes without noise, and
at 30 dB it loses file 46.

**Default matrix through the filter** (`--matrix default --seeds 3 --bandpass 300:2700`, and
the same with `300:3000`, on files 1/13/53/29/41: WID 0, 3, 13, 7 and 10, SHORT). Both give
104/110, the same cells as with no filter (compared cell for cell against master's
default run). The two failing cells are the same as without a filter: WID 0 at -6 dB, and WID 0
on ITU Poor. The default files are WID 0, 3, 13, 7 and 10, and none of them uses 256QAM.

Not measured: a linear-phase (FIR) radio filter, a passband ripple, other filter orders, or a
real radio's audio path.

## Limits of what was tested

- The interop test set itself is clean digital audio straight from the transmitter. Every
  file has a probe quality of 1.000 and a frequency offset estimate of 0.00 Hz (or -0.01 Hz on
  the continuous-PN files). Channel testing above measures noise, fading, multipath, frequency
  offset and clock offset on five SHORT files only.
- WID 1-13 are equalised (see "Equaliser"). WID 0 is not, and still fails the fading
  channels.
- Frequency acquisition covers +-112.5 Hz by design. Larger offsets were tried on one file at
  30 dB only.
- The WID 6-13 soft bits are max-log distances with no noise-variance estimate. On a clean
  set that costs nothing; its effect at low SNR was not measured.
- Acquisition searches the whole file (or, with `--stream`, each new second of the feed), on
  an sps/4 grid in nine frequency bins, then refines to the sample.
- 8 kHz input was measured on the clean set resampled from 48 kHz only. A radio audio filter
  was simulated (see "Band-limited channel"); no real radio's audio path was measured.

## Open questions

1. What the STD-AMB-001 transmitter sends in fixed symbol 8, where the continuous index runs
   past 255 (measured above; not chased).
2. On a multipath channel the preamble clock fit is off by tens of ppm (measured above). With a
   three-super-frame preamble the fit cannot tell clock error from a multipath-shifted peak.
   The equaliser absorbs it: ITU good WID 7 went from 3/10 to 10/10, and Poor with +100 ppm
   decodes (measured). The fit itself is unchanged.
3. Why WID 0 needs 9 dB more than Table D-LXIV in AWGN (measured; cause not determined).

## Proofs files for a report page

`score.py --proofs DIR` writes `DIR/t5.json` and `channel.py --proofs DIR` writes
`DIR/t6.json`. Each holds a title, a decoder label (`m110d_rx @<short commit>` by default,
or `--decoder-label`), a note (`--proofs-note`) and a list of items with a name, a mode in
plain words, a status (`pass`, `partial`, `fail`, `none`), the decoded text and a one-line
detail. `t5.json` has one item per file of the 60-file set, with trailing zero padding
summarised as `[+ N padding bytes]`. `t6.json` has one item per channel condition, summed
over the files and seeds run. A text field that holds a path or host name is refused: the
script exits 3 and writes nothing. The tables and exit status of both scripts are unchanged.

## Credits

No code was taken from another project. The read-only reference
milwave-rs [2] was read during this work. It holds no Appendix D preamble, frame structure,
interleaver or tail-biting code, so nothing from it is used here. Its `src/walsh.rs` is a
Walsh-16 correlator with no D.5.1.4 scrambler; the WID 0 decoder is written from the standard.
Its 16, 32 and 64QAM tables (`src/unified/constellation.rs`) do not match Tables D-VII to
D-IX (for example its 16QAM symbols 1 and 2 are swapped, and its 64QAM symbol 13 is (1, 0)
where the table gives (0.588429, -0.588429)), and it has no 256QAM table; the constellations
here were transcribed from the standard's page images instead.

## References

[1] Department of Defense, *Interoperability and Performance Standards for Data Modems*,
MIL-STD-188-110D, Appendix D. Distribution Statement A, approved for public release.
Available: https://everyspec.com/MIL-STD/MIL-STD-0100-0299/MIL-STD-188-110D_55856/
Filed copy: `docs/standards/MIL-STD-188-110D.pdf`.

[2] HeroesLament, *milwave-rs*, MIT OR Apache-2.0. https://github.com/HeroesLament/milwave-rs
