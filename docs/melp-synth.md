# MELP parameter-domain synthesiser (`src/melp_synth`)

`melp_synth` turns MELP frame parameters into 8 kHz 16-bit PCM. It contains no quantiser
codebook and no bitstream decoder. Its input is the parameters a decoder holds after
dequantisation. It takes 22.5 ms frames (180 samples, MIL-STD-3005 / STANAG 4591 [1]) and
25 ms frames (200 samples, the Harris MELP 600 frame [2]).

It was written from the published descriptions only [1]–[3]. No code from any MELP or
MELPe implementation was read. The reference coder used for comparison below was run as
an external program only.

## State

**Done and verified, with the numbers in this document:**

- The codebook-free synthesiser. It covers every stage of [1] A.5.3, takes 22.5 ms and
  25 ms frames, and has two excitation models.
- The CLI, `melp_synth`.
- Two fast ctests that use no private tool or data. Their measured output is under Tests.
- The test-only analysis path and comparison, `test/melp_synth/validate.py`, measured on
  16 s of public Codec2 speech against the original and against the reference coder's
  2400 bit/s decode. The figures are under Measured comparison.

**Tables entered: none.**

- Coordinator ruling (relayed): the quantiser tables printed in STANAG 4591 may go into the
  repo, taken only from the STANAG 4591 PDF.
- Measured from `pdftotext -layout` of that PDF (130 pages): the document prints none of
  the codebooks. Section 5.3 cites "Tables C-1 to C-4" for the LSF MSVQ, and Annex C says
  only that the codebooks "are available in electronic form" from the CNSC and NC3A
  websites. Page C-2 is blank.
- The only numeric tables in the PDF are Table A-2 (pulse dispersion, 65 coefficients,
  not entered: see Choices) and the Annex E Tukey window tables (noise preprocessor, not
  needed for synthesis).
- So no LSF MSVQ, Fourier magnitude VQ or 1200 bit/s codebook can be entered under those
  terms, and no table has been checked.

**Not done:**

- The 2400 and 1200 bit/s bitstream decoder. It is blocked on the codebooks above.
- Decoding the Harris 1200 #1–#3 payloads.
- Decoding the reference coder's `-m A` bitstreams.
- The vendor anchor pair (h_noise to h9).

All four need the codebooks. The codebook-free fields (pitch, gains, voicing, jitter flag,
Hamming protection, bit order) can be decoded from the printed standard. They are not
built, because without the LSFs the output cannot be checked against anything.

## What it implements

| Stage | Source | Notes |
|---|---|---|
| Parameter defaults, unvoiced mode, voicing 0001 exception | [1] A.5.3.1 | pitch 50, jitter 25 %, voicing 0, Fourier magnitudes 1 for unvoiced frames |
| LSF ordering and 50 Hz minimum separation | [1] A.5.2.14 | applied to every input frame |
| Noise attenuation of G1, G2 | [1] A.5.3.2, Eq. A-10 | estimator steps of 3 dB/s up, 12 dB/s down, scaled to the frame length |
| Pitch-synchronous interpolation | [1] A.5.3.3, Eqs. A-11, A-12 | gain from G2p→G1→G2 over the two half frames; the other parameters by t0/N; high-pitch onset and gain-onset exceptions |
| Mixed excitation, band-pass model | [1] A.5.1, A.5.2.4, F.1.1.7 | one pulse per period (inverse DFT of the Fourier magnitudes) and white noise, through five shaping filters weighted by voicing |
| Mixed excitation, harmonic model | [1] A.5.3.4, Eqs. A-13, A-14, Table A-1; D.4.8 | frequency-domain mixing around the cutoff F |
| Aperiodic pulses (jitter) | [1] A.5.3.4 | T = pitch × (1 + jitter × U(−1, 1)), rounded, clamped 20–160 |
| Adaptive spectral enhancement | [1] A.5.3.5, Eqs. A-15, A-16 | A(αz⁻¹)/A(βz⁻¹)·(1 + µz⁻¹), α = 0.5p, β = 0.8p |
| LPC synthesis | [1] A.5.3.6 | direct form 1/A(z), A(z) = 1 + Σaᵢz⁻ⁱ |
| Gain | [1] A.5.3.7, Eq. A-19 | per-period RMS scaling, ramped over 10 samples |
| Pulse dispersion | [1] A.5.3.8 | 65-tap FIR, derived (see below) |
| Loop control | [1] A.5.3.9 | the period that crosses a frame end carries into the next frame |
| Postfilter (optional, `--postfilter`) | [1] A.5.3.10, Eqs. A-20 to A-23 | then 2nd-order Butterworth low-pass 3800 Hz and high-pass 60 Hz |

The band-pass model is the default. `--excitation harmonic` selects the harmonic model.

### Choices the standard leaves open

These are this implementation's own choices. They are marked `CHOICE` in
`src/melp_synth/melp_synth.cpp`.

- **Shaping filters.** Each is a difference of 65-tap Hamming-windowed sinc low-passes at
  the band edges of [1] A.5.2.4 (500, 1000, 2000 and 3000 Hz). The five filters sum to a
  delayed unit impulse, so pulse and noise take complementary shares in each band.
- **Pulse dispersion.** [1] A.5.3.8 describes the filter as "derived from a
  spectrally-flattened triangle pulse". Its Table A-2 coefficients are not copied. The
  filter here is an asymmetric 65-sample triangle (rising over 45 samples, falling over
  20), with its 65-point DFT set to unit magnitude and transformed back. It has unit energy.
- **Harmonic model.** The standard says the transition frequencies FL and FH come from "an
  empirically derived algorithm" and does not give it. This implementation uses
  FL = 0.90 F and FH = 1.03 F, which lie inside the published ranges [0.85F, 0.98F] and
  [F, 1.05F]. The mixed-band gain g is 0.9, 0.8, 0.7 and 0.6 at F = 500, 1000, 2000 and
  4000 Hz, so g falls as F rises, as the text requires. φ0 centres the pulse in its period.
- **LSF separation.** The pseudo code in [1] A.5.2.14 prints only the `f[i+1] += s2`
  update. The matching `f[i] -= s1` is applied as well, because s1 is otherwise unused.
- **Initial state.** The first frame interpolates from itself. The noise estimate starts
  at 10 dB.
- **Pulse and noise level.** The pulse and the noise each have unit RMS before shaping.
  The gain stage sets the absolute level.

## Parameter file

The input is text, one item per line, and `#` starts a comment:

```
frame_ms 22.5            # or 25; or "frame_len 180|200"
frame pitch=80 g1=50 g2=50 vbp=1,1,1,0,0 lsf=300,650,780,1150,1300,2000,2450,2650,3200,3600 fm=1.4,1.2,1.1,1,1,0.9,0.9,0.8,0.8,0.8 ap=0
frame pitch=0 g1=40 g2=38 vbp=0,0,0,0,0 lsf=400,900,1400,1800,2200,2600,2900,3200,3500,3750
```

The fields are:

- `pitch`: samples, 20–160, where 0 marks an unvoiced frame.
- `g1`, `g2`: the two gains in dB, the RMS of 16-bit PCM ([1] Eq. A-6).
- `vbp`: the five band voicing decisions or strengths, band 1 being 0–500 Hz.
- `lsf`: ten LSFs in Hz.
- `fm`: ten Fourier magnitudes, optional (default all 1).
- `ap`: the aperiodic flag, optional.

Run it as follows:

```
melp_synth [--frame-ms 22.5|25] [--excitation bandpass|harmonic] [--postfilter]
           [--no-noise-att] [--no-ase] [--no-dispersion] [--seed N] params.txt out.wav
```

An output name ending in `.raw` gives headerless s16le.

## Tests

`ctest -L fast` runs two tests, `test_melp_synth` and `melp_synth_cli`. Both use only
fixed parameter sequences defined in the repo.

`test_melp_synth` runs 30 voiced frames at pitch 80 and 50 dB, then 30 unvoiced frames at
44 dB, then 30 aperiodic voiced frames. It runs this sequence for both excitation models
and both frame lengths, and checks:

- the output length (90 × N samples);
- steady-state RMS within 1.5 dB of the commanded gain;
- a normalised autocorrelation peak above 0.6 within ±1 lag of the commanded pitch;
- an unvoiced peak below 0.35;
- an aperiodic peak below the periodic one.

It also checks:

- `lsf_to_lpc` against LSFs found by scanning P(e^jω) and Q(e^jω);
- the sign convention of k1;
- LSF separation;
- the Table A-1 cutoff rule;
- seed determinism;
- the parameter parser.

`melp_synth_cli` runs the CLI on `test/melp_synth/fixed.params`.

Measured output of `test_melp_synth`:

```
bandpass 22.5 ms   N=180  len=16200  voiced 49.11 dB r=1.000@80  unvoiced 43.43 dB r=0.082  aperiodic r=0.109@64
bandpass 25 ms     N=200  len=18000  voiced 49.11 dB r=1.000@80  unvoiced 43.53 dB r=0.064  aperiodic r=0.169@63
harmonic 22.5 ms   N=180  len=16200  voiced 49.11 dB r=0.999@80  unvoiced 43.33 dB r=0.085  aperiodic r=0.232@51
harmonic 25 ms     N=200  len=18000  voiced 49.15 dB r=0.999@80  unvoiced 43.33 dB r=0.063  aperiodic r=0.221@54
```

The voiced level is 0.9 dB under the commanded 50 dB. This is measured and not explained
here. One candidate is the pulse dispersion filter, which has unit energy as a 65-point
circular filter but not at every harmonic of a pitch-80 pulse train.

## Measured comparison

`test/melp_synth/validate.py` holds a test-only analysis path written from [1] A.5.2. It
is not an encoder: nothing is quantised. Its stages are:

- LPC from a 200-sample Hamming window at the frame end, with 0.994 bandwidth expansion;
- LSFs from the roots of P and Q;
- integer pitch on the 1 kHz low-passed speech, with a doubling check;
- five band voicing strengths with the envelope term;
- peakiness forcing;
- the aperiodic flag;
- pitch-adaptive G1 and G2;
- Fourier magnitudes of the residual.

The script writes the parameters, synthesises them, and compares the result with the
original and with the reference MELPe coder's 2400 bit/s decode (`-r 2400 -m C`, with the
noise preprocessor and with `-p`).

**Speech:** 16 s of the Codec2 test samples hts1a, hts2a and ve9qrp_10s [4], male, 8 kHz.
This is the same speech that `test/harris/score.py --calibrate` uses on feat/harris-melpe.

**Measures:**

- **env:** the mean RMS difference of cepstrally smoothed (20-coefficient) log spectra over
  125–3800 Hz on active frames, with each frame's level removed.
- **env+lvl:** the same difference with the level kept.
- **V/UV:** voicing agreement on active frames.
- **f0<10%:** the share of frames voiced in both whose pitch agrees within 10 %.
- **|df0|:** the median pitch error in semitones.
- **segSNR:** segmental SNR. For any vocoder this is near or below 0 dB, and it is listed
  only to show that.
- The last six columns are `speech_measure`, `modulation` and `energy_contour` from
  feat/harris-melpe `test/harris/melpe_frames.py`, run on the concatenated output.

The rows below compare each output with the original, measured 2026-11-22:

| Output | lag | env dB | env+lvl dB | V/UV | f0<10% | \|df0\| st | voiced | track | f0 Hz | mod | r1 | depth dB |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| original | 0 | 0 | 0 | 1 | 1 | 0 | 0.44 | 0.32 | 96 | 0.53 | 0.88 | 13.7 |
| synth band-pass, 22.5 ms | 56–74 | 5.01 | 6.47 | 0.83 | 0.86 | 0.30 | 0.42 | 0.29 | 101 | 0.51 | 0.90 | 13.6 |
| synth band-pass + postfilter | 58–74 | 5.32 | 7.37 | 0.82 | 0.85 | 0.30 | 0.43 | 0.29 | 101 | 0.51 | 0.90 | 13.7 |
| synth harmonic, 22.5 ms | 60–72 | 4.71 | 6.23 | 0.82 | 0.84 | 0.32 | 0.44 | 0.31 | 99 | 0.51 | 0.90 | 13.6 |
| synth harmonic + postfilter | 60–74 | 5.09 | 7.22 | 0.82 | 0.84 | 0.32 | 0.45 | 0.31 | 100 | 0.51 | 0.90 | 13.7 |
| synth band-pass, 25 ms | 54–70 | 5.00 | 6.34 | 0.83 | 0.85 | 0.23 | 0.42 | 0.30 | 99 | 0.52 | 0.90 | 13.7 |
| synth harmonic, 25 ms | 60–70 | 4.70 | 6.10 | 0.82 | 0.83 | 0.20 | 0.43 | 0.30 | 98 | 0.52 | 0.90 | 13.6 |
| reference coder 2400 | 282–296 | 6.21 | 7.92 | 0.79 | 0.92 | 0.13 | 0.46 | 0.29 | 99 | 0.57 | 0.88 | 14.9 |
| reference coder 2400 `-p` | 208–222 | 5.74 | 6.71 | 0.84 | 0.94 | 0.13 | 0.45 | 0.33 | 98 | 0.54 | 0.90 | 14.2 |

The rows below compare each synth output with the reference coder's 2400 decode (noise
preprocessor on), after both are aligned to the original:

| Output | env dB | env+lvl dB | V/UV | f0<10% | \|df0\| st |
|---|---|---|---|---|---|
| synth band-pass, 22.5 ms | 5.41 | 6.87 | 0.80 | 0.79 | 0.37 |
| synth band-pass + postfilter | 5.11 | 6.67 | 0.80 | 0.79 | 0.37 |
| synth harmonic, 22.5 ms | 5.18 | 6.73 | 0.76 | 0.82 | 0.34 |
| synth harmonic + postfilter | 4.93 | 6.60 | 0.76 | 0.82 | 0.35 |
| synth band-pass, 25 ms | 5.41 | 6.83 | 0.78 | 0.79 | 0.36 |
| synth harmonic, 25 ms | 5.14 | 6.63 | 0.80 | 0.77 | 0.36 |

What the tables show:

- The synthesiser fed with unquantised parameters is as close to the original in spectral
  envelope as the reference coder's quantised 2400 decode (4.7–5.3 dB against
  5.7–6.2 dB). The two figures are not a like-for-like comparison, because the synth row
  has no quantisation error and the coder row does.
- The pitch agreement is lower for the synth (0.83–0.86 of frames within 10 %) than for the
  coder (0.92–0.94). This is the test analysis path's simplified pitch estimator: it has
  no fractional pitch and no long-term average pitch. It is not the synthesiser.
- 25 ms frames measure the same as 22.5 ms frames.
- The postfilter raises the distance to the original by about 0.3–0.4 dB and lowers the
  distance to the reference coder by about 0.25 dB.
- Delay against the original: 54–74 samples for the synth (the 32-sample shaping filter,
  dispersion and interpolation), and 208–296 for the coder.

To reproduce:

```
python3 -I -B test/melp_synth/validate.py --synth build/bin/melp_synth --work <scratch> \
    --melpe <reference coder> --measures <copy of melpe_frames.py> hts1a.raw hts2a.raw ve9qrp_10s.raw
```

## What is missing

- **A bitstream decoder.** Bitstream decoding needs the LSF MSVQ (four stages, 128/64/64/64
  × 10) and the Fourier magnitude VQ (256 × 10). The 1200 bit/s mode also needs its own
  pitch, LSF, interpolation and gain codebooks ([1] Annex D). [1] cites "Tables C-1 to C-4"
  for the MSVQ (§5.3), but its Annex C prints none of the codebooks. It reads: "The
  codebooks used by STANAG 4591 are available in electronic form from the CNSC website ...
  and from the NC3A web site". Page C-2 is blank.

  The fields that need no codebook can be decoded from the standard as printed:

  - pitch and voicing mode ([1] §5.3.1, Table 1);
  - the gains (uniform quantisers, [1] A.5.2.16);
  - band voicing;
  - the jitter flag;
  - the Hamming protection ([1] A.5.2.19);
  - the bit order ([1] Table 3; [3]).
- The Table A-2 pulse dispersion coefficients (see Choices).
- Frame erasure and frame repeat ([1] A.5.3.1). These belong to a bitstream decoder.
- Bit exactness against any reference. It is not a goal, because the harmonic-model
  transition rule and the dispersion filter are this implementation's own.
- The 600 bit/s parameter decoding of the Harris frames. Only the 25 ms frame length is
  supported here.

## References

[1] *The 600 bit/s, 1200 bit/s and 2400 bit/s NATO Interoperable Narrow Band Voice
    Coder*, NATO STANAG 4591, Ed. 1, Annex A §A.5.1–A.5.3, Annex C, Annex D §D.4.8,
    Annex F. Not filed in this repo.

[2] M. W. Chamberlain, "A 600 bps MELP vocoder for use on HF channels," in *Proc. IEEE
    MILCOM 2001*, p. 447 ff. (25 ms frame, four-frame block of 100 ms). Not filed in this
    repo.

[3] V. Demjanenko, D. Satterlee, "RTP Payload Format for the Mixed Excitation Linear
    Prediction Enhanced (MELPe) Codec," IETF RFC 8130, 2017.
    https://www.rfc-editor.org/rfc/rfc8130

[4] D. Rowe, codec2, GitHub repository drowe67/codec2, LGPL-2.1,
    https://github.com/drowe67/codec2 (speech samples under `raw/`). Not filed in this
    repo.
