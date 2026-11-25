# m110a_rx: SDR I/Q to decoded bytes

`m110a_rx` reads MIL-STD-188-110A from SDR I/Q, from a file or from stdin.

## Chain

1. **Tune.** Move the dial (the USB suppressed carrier) from `+offset` Hz to 0 Hz. With `--ppm`, also correct the receiver's clock error: the dial's position, and after step 4 the audio's sample rate.
2. **Decimate.** `IQSource` brings the input down to 48 kHz complex.
3. **Filter.** Keep the upper sideband, 300 to 3300 Hz.
4. **Demodulate.** Take the real part. That is the audio, with the 110A carrier at 1800 Hz.
5. **Find the preamble.** Search for the 288-symbol common preamble across the whole stream, so a message is found at any start time and any carrier offset within ±37 Hz. Frames 480 symbols apart chain into one preamble. With `AUTO`, decoding starts at the first frame seen; the receiver reads that frame's countdown, so any frame is a valid entry point. With a forced mode, the message starts at the last frame minus 2 frames (short modes) or 23 frames (long modes). A fade, or two propagation paths that move the correlation peak by more than 2 symbols from frame to frame, splits one preamble into several chains (on Watterson moderate and poor, 14 to 16 chains for one 24-frame preamble). The chains that start inside the first chain's preamble and on its frame grid, within ±10 symbols of a whole number of frames, are one message. Inside the preamble means at most P − 1 frames after the first chain, where P is the preamble length of the mode that both decodes read, or 24 when either is unread. Each chain is decoded and one message is reported. Its bytes are the ones most of the chains with an EOM decoded: right bytes come out the same from several entry points, wrong ones differ. Ties go to bytes a chain with success carries, then to the better decode. Without an EOM on any chain it is the better decode: success, then the EOM, then the best worst block, then the best `quality_db`. The message is reported from the best decode carrying those bytes, so `success=1` means one of those chains cleared the gate. Near the gate one chain's word is not enough: when the preamble split into chains and the reported decode clears its block threshold by less than 0.75 dB, a third of the chains with an EOM (at least 2) must have decoded the same bytes, or the message is `success=0`. A message with a single chain is judged by the gate alone. A short message that follows within 24 frames stays a message of its own.
6. **Find the end.** The decode window ends 0.5 s after a power step: the 0.3 s and the 1 s before it are both at least 3 dB above every 0.5 s mean of the 4 s after it. Power is measured in 0.1 s blocks counted from the first preamble frame, so the end does not depend on the audio before the preamble. A slow fade has no step, and a fast fade or dropout comes back up within the 4 s, so neither ends the message. The noise rising after the step, as a receiver's AGC recovers, does not stop it being a step. Without a step, the window runs to the next message's preamble, to `--max-secs`, or to the end of the input. A preamble that starts less than 24 frames after this message's first frame may be its own, split into separate chains by a fast fade, so it does not end the window. Inside the window, `ModemRX` ends the bytes at the EOM, or without one at the last interleaver block that the probes cover. A fade that stays 3 dB or more down for 4 s with a sharp start still ends the window. A window with no power step at its end that is longer than 60 s (at about 0 dB SNR the end is a 3 dB step, which the test can miss) is decoded over its first 30 s first. That decode is kept when it found the EOM, read no mode, or its signal ends at least two interleaver blocks before the 30 s; otherwise the whole window is decoded. `end_s` is then the end of the 30 s.
7. **Decode.** Scale the message to the reference level, about 7000 RMS on the int16 scale, and decode it with `ModemRX`. Cut the output at the EOM (0x4B65A5B2).

## Build

```
cmake -S . -B build && cmake --build build -j
```

The binary is `build/bin/m110a_rx`.

## Inputs

| Input | Format | Rate, offset and dial |
|---|---|---|
| `PREFIX.cs16` | int16 interleaved I/Q, as written by `rsp2_stream.py --record PREFIX` | Read from `PREFIX.json`: `rate_readback`, `wwv_hz − tuner_hz_readback`, `wwv_hz` |
| `.cf32` | float32 interleaved I/Q | Give them on the command line |
| `.iqr` | 64-byte header, then int16 I/Q | Rate from the header |
| `-` | stdin, cs16 unless `--format cf32` | Give `--rate` and `--offset` |

Command-line options override the sidecar.

`--ppm P` is the receiver clock error in ppm, with + meaning the clock runs fast. A receiver whose clock is 1.09 ppm slow takes `--ppm -1.09`. The dial is then looked for at `offset − dial × P × 1e-6` Hz, and the audio is resampled by 1 + P × 1e-6, because a clock P ppm fast also takes P ppm more samples per second and moves the symbol timing. With `--dial 0` only the sample clock is corrected. `--ppm` is the receiver's clock only. The sender's symbol clock is followed by the data grid's drift tracking (docs/m110a-timing-steps.md), and `--ppm` does not change it. On a 2400L message whose receiver clock ran 200 ppm fast (12 dB), only `--ppm 200` decoded it: exact, q 13.2 dB. At ±40 ppm the decode is exact without it, and the flag raises q from 11.3 to 13.2 dB (2400L) and from 9.9 to 13.0 dB (1200L).

`--mode` defaults to `AUTO`: the receiver reads the mode from the preamble's D1 and D2. Give a mode, such as `--mode 600L`, to force it.

## Output

stdout carries one block per message: one per preamble, its chains merged (see Find the preamble):

```
msg=1 start_s=1.505 end_s=4.805 mode=600S preamble_hits=3 acq_metric=0.95 acq_foff_hz=+0.08 success=1 quality_db=10.9 quality_threshold_db=2.8 eom=1 bytes=64 snr_db=3.1 d1=6 d2=6 countdown=2 blocks=2 block_quality_db=10.3,10.4 block_quality_min_db=10.3 block_quality_threshold_db=2.75 preamble_chains=1 chains_agreeing=1 err=""
  text="DE AAM402 THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890"
  hex=4445...
```

| Field | Meaning |
|---|---|
| `start_s` | Start of the first preamble frame, in seconds |
| `preamble_hits` | Preamble frames detected, over all its chains |
| `acq_metric` | Preamble correlation: 1 is clean, about 0.16 is noise |
| `acq_foff_hz` | Carrier offset measured on the preamble |
| `mode` | The mode decoded: read from D1/D2 with `AUTO`, otherwise the one forced |
| `quality_db` | Probe-symbol SNR as the codec sees it, over the first 1440 data symbols |
| `blocks` | Interleaver blocks the message occupies: up to the one holding the EOM, or without an EOM the whole blocks the signal covers |
| `block_quality_db` | Quality of each of those blocks, comma-separated, or `-`: the flat-channel SNR with the same expected coded-bit error rate as the block (`docs/m110a-quality-gate.md`) |
| `block_quality_min_db`, `block_quality_threshold_db` | The worst block, and the least it must reach for the mode |
| `preamble_chains` | Preamble chains merged into this message (see Find the preamble) |
| `chains_agreeing` | Chains, with an EOM, that decoded these same bytes (1 when no chain found the EOM) |
| `success` | 1 only when `quality_db` reaches `quality_threshold_db` and `block_quality_min_db` reaches `block_quality_threshold_db` for the mode. 110A has no CRC, so this is a confidence threshold, not proof |
| `snr_db`, `err` | The decoder's other reports |
| `eom=1` | The EOM was found and the bytes stop before it |

Trust a message when it shows both `success=1` and `eom=1`.

## Exit status

| Status | When |
|---|---|
| 0 | A message decoded. With `--expect TEXT`: a message decoded to exactly TEXT, with EOM |
| 1 | Otherwise |
| 2 | Usage error |
