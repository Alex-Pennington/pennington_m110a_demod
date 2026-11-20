# m110a_rx: SDR I/Q to decoded bytes

`m110a_rx` reads MIL-STD-188-110A from SDR I/Q, from a file or from stdin.

## Chain

1. **Tune.** Move the dial (the USB suppressed carrier) from `+offset` Hz to 0 Hz. With `--ppm`, also correct the receiver's clock error.
2. **Decimate.** `IQSource` brings the input down to 48 kHz complex.
3. **Filter.** Keep the upper sideband, 300 to 3300 Hz.
4. **Demodulate.** Take the real part. That is the audio, with the 110A carrier at 1800 Hz.
5. **Find the preamble.** Search for the 288-symbol common preamble across the whole stream, so a message is found at any start time and any carrier offset within ±37 Hz. Frames 480 symbols apart chain into one preamble. For a given mode, the message starts at the last frame minus 2 frames (short modes) or 23 frames (long modes), so a missed first frame does no harm.
6. **Find the end.** The message ends after 1.5 s below a threshold set 35% of the way from the noise level before the preamble up to the preamble level, or after `--max-secs`. With no noise before the preamble, the threshold is 45% of the preamble level.
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

`--ppm P` is the receiver clock error in ppm, with + meaning the clock runs fast. A receiver whose clock is 1.09 ppm slow takes `--ppm -1.09`. The dial is then looked for at `offset − dial × P × 1e-6` Hz.

`--mode` is required. `AUTO` is passed through to the receiver's mode detection.

## Output

stdout carries one block per preamble found:

```
msg=1 start_s=2.008 end_s=4.500 mode=600S preamble_hits=3 acq_metric=0.96 acq_foff_hz=+0.01 success=1 eom=1 bytes=54 snr_db=6.8 err=""
  text="THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890"
  hex=5448...
```

| Field | Meaning |
|---|---|
| `start_s` | Start of the first preamble frame, in seconds |
| `preamble_hits` | Preamble frames detected |
| `acq_metric` | Preamble correlation: 1 is clean, about 0.16 is noise |
| `acq_foff_hz` | Carrier offset measured on the preamble |
| `success`, `snr_db`, `err` | The decoder's own report |
| `eom=1` | The EOM was found and the bytes stop before it |

`success=1` without `eom=1` is not proof of a good decode.

## Exit status

| Status | When |
|---|---|
| 0 | A message decoded. With `--expect TEXT`: a message decoded to exactly TEXT, with EOM |
| 1 | Otherwise |
| 2 | Usage error |
