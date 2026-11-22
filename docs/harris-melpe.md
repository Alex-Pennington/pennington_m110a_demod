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

What the D1, D2 values mean [1, Table XI], [2, Table XI]:

- **7, 7** (melp2400) is *2400 (Digital voice)*. Table XI defines it for the short setting only.
- **5, 7** (600 files, melp600) and **5, 6** (1200 files) are not modes in Table XI. The note
  to Table XI says: "The D1, D2 combinations 5,6 and 5,7 are reserved for application-specific
  use" [1], [2], [3]. None of 110C, 110C Change 1 or 110D defines what they carry.
  So these are Harris's own codes. The file names pair 5,7 with 600 bps and 5,6 with 1200 bps.

Before this work, `m110a_rx` AUTO had no entry for 7,7, 5,7 or 5,6, so it reported "Mode
detection failed" on all eight.

## References

[1] *Interoperability and Performance Standards for Data Modems*, MIL-STD-188-110C,
    Department of Defense, 23 Sep. 2011, §5.3.2.3.7.2.1 and Table XI. Distribution A. Not
    filed in this repo.

[2] *Interoperability and Performance Standards for Data Modems*, MIL-STD-188-110C w/Change 1,
    Department of Defense, 3 Jan. 2012, Table XI. Distribution A. Not filed in this repo.

[3] *Interoperability and Performance Standards for Data Modems*, MIL-STD-188-110D,
    Department of Defense, 2017, Table XI. Filed in this repo at
    `docs/standards/MIL-STD-188-110D.pdf`.
