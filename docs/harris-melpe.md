## MELPe

This section covers the part after the modem: payload bits to MELPe frames to audio, and
how `test/harris/score.py` decides whether the audio is speech.

### Coders available

| Tree | What it is | MELPe rates | Builds on Linux |
|---|---|---|---|
| `src/melpe_dist` | `melpe_api.h` plus two Win64 static archives (`libmelpe.a`, `libmelpe_api.a`). The object names (`melp_ana`, `lib600_*`, `bitorder600`) are those of the STANAG 4591 reference coder. No source, no licence line. | 2400, 1200, 600 (per the header) | No: Windows COFF objects |
| `src/vocoder` | C++ wrapper over `melpe_api.h`. A stub: it returns no data. | none | not built |
| `src/codec2_vocoder` | Command line over Codec2 (LGPL) [4]. Not MELPe. `extern/codec2` is an empty submodule. | none | not built |

So nothing in this repo decodes MELPe on Linux. The scorer runs the STANAG 4591 reference
coder's command line as an external program (`--melpe`); that coder is not part of this
repo.

### Frames

From STANAG 4591 [1] section 5.5 and Annexes D and M, and RFC 8130 [2] section 3.1:

| Rate | Bits per frame | Frame | Sync bit |
|---|---|---|---|
| 2400 bps | 54 | 22.5 ms | bit 54, alternates 0/1 from frame to frame |
| 1200 bps | 81 | 67.5 ms | bit 1, alternates 0/1 from frame to frame |
| 600 bps | 54 | 90 ms | none |

Bit 1 is transmitted first [1, s5.5.3]. At each rate one frame carries exactly the modem's
bits for one frame time (54 bits in 22.5 ms is 2400 bps, 81 in 67.5 ms is 1200 bps, 54 in
90 ms is 600 bps), so frames run back to back in the payload with no room for framing
bits. At 2400 and 1200 bps the sync bit gives the frame alignment; at 600 bps nothing in
the frame does.

How 110A carries the 600 and 1200 bps frames is not stated in the references at hand.
MIL-STD-188-110D Table XI [3] lists one voice code only, "2400 (Digital voice)", D1,D2 7,7,
and section 5.3.1.2 points voice digitisation at STANAG 4198 or 4591. NOT DETERMINABLE
from these documents; the frame alignment and bit order in the payload are found by
measurement (below).

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

### Speech measure

`melpe_frames.speech_measure` works on the decoded audio, in 22.5 ms hops with a 40 ms
window. A frame is voiced when its normalised autocorrelation peaks above 0.5 at a lag for
80-308 Hz, and the frame is within 40 dB of the loudest frames. `track` is the share of
frames that are voiced and within about 10 % of the previous frame's pitch: speech has a
continuing pitch contour, and a pitch drawn at random each frame does not.

Calibration: `score.py --calibrate` on 16 s of male speech (the Codec2 test samples hts1a,
hts2a and ve9qrp_10s [4]), coded and decoded through the reference coder:

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

- takes the rate from the preamble (the file's slug, or `rate` in a sidecar), and with
  `--search` tries both byte orders and every frame offset, using the sync bit to fix the
  offset at 2400 and 1200 bps when it alternates in at least 90 % of frames;
  `--search-all` adds every rate, frame reversal and inversion;
- passes a file when `track` >= 0.25 and beats every other choice it tried by 0.05.

### Running it

```
python3 -I -B test/harris/score.py --melpe <reference coder> [--search] \
    [--payload DIR] [--audio DIR] [--proofs DIR --set <test-set dir> --decoder TEXT]
python3 -I -B test/harris/score.py --melpe <reference coder> --calibrate <8 kHz s16le speech> ...
```

`--proofs DIR` writes `DIR/t7.json` and the audio it names under `DIR/t7-audio/`: the
decoded speech, and the recording resampled to 8 kHz.

A recording with several preambles ("600 #3 multiples" has four) has one payload file per
message, `<slug>_m<N>.bin`. Each message is searched and scored on its own and gets its
own table row. The recording keeps one proof item: `decoded` and `detail` give each
message's result in order, and the decoded speech is the messages joined with 0.5 s of
silence between them. The item is "pass" when every message passes, "partial" when every
message is at least partial, and "fail" otherwise. Text fields carry no file paths,
host names or frequencies. `ctest -L fast` runs `test/harris/test_harris.py` (packing, sync
search, the speech measure on synthetic signals, and the proof output), which needs no
MELPe coder.

### References

[1] NATO Standardization Agency, "The 600 bit/s, 1200 bit/s and 2400 bit/s NATO
interoperable narrow band voice coder," STANAG 4591, Ed. 1.

[2] V. Demjanenko and D. Satterlee, "RTP Payload Format for the Mixed Excitation Linear
Prediction Enhanced (MELPe) Codec," IETF RFC 8130, Mar. 2017.

[3] U.S. Department of Defense, "Interoperability and Performance Standards for Data
Modems," MIL-STD-188-110D (`docs/standards/MIL-STD-188-110D.pdf`).

[4] D. Rowe, codec2, GitHub repository drowe67/codec2, LGPL-2.1,
https://github.com/drowe67/codec2 (speech samples under `raw/`).
