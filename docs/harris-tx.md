# Harris-mode transmitter (600H, 1200H, 2400V)

Goal: a transmitter whose output a Harris RT-1694D accepts, so known 600H payloads can be played
to the radio and its synthesiser output recorded (`docs/harris-melpe.md` gives the modem facts).
This is transmit code only. It writes files and keys no radio.

Labels: **read** = read from code or a standard; **measured** = measured with a command;
**guessed** = an inference not yet measured.

## What TX does for 600H, 1200H and 2400V

`M110ACodec::encode_message_bits` (`src/modem/m110a_codec.h`), through `ModemTX::encode_bits`
(`api/modem_tx.cpp`). `ModemTX::encode(bytes)` takes the same path for these three modes, bytes
LSB first. Every other mode goes through `encode_bits_with_probes`, which also sends D1/D2 and,
with `include_eom`, the EOM and flush. 2400V is 2400S's waveform with D1, D2 = 7, 7: 8PSK,
32 data and 16 known symbols per frame, 40 × 72 short interleave.

- **Input:** payload bits, each 0 or 1, in the order the FEC encoder takes them. Any length.
- **Message end:** one continuous K=7 rate 1/2 stream: the payload, the EOM 4B65A5B2 most
  significant digit first [110D §5.3.2.3.1], 144 zero bits, then zeros to the end of the
  interleaver block (360 information bits per block at 600H, 720 at 1200H, 1440 at 2400V)
  [§5.3.2.3.2]. No other
  tail is added (read).
- **Known symbols:** in the last two known segments of every 1440-symbol block, the 8-symbol Walsh
  pattern of D1 and then D2 twice, then 4 zeros, through the scrambler like the zeros elsewhere
  [§5.3.2.3.7.1.2]. The last block carries them too, as the radio does (measured, below).
- **Stream:** the short preamble (three frames, countdown 2, 1, 0), then data and probes,
  modulated as one symbol stream. Root-raised-cosine shaping (alpha 0.35) leaves no gap between
  preamble and data.
- **Length:** 1440 × (1 + ceil((payload bits + 176) / bits per block)) symbols, preamble included.
  The radio's recordings have the same block counts (measured, below).
- `include_eom` is ignored on this path: the EOM is always sent. The 288 Brain-modem leading symbols
  still follow `include_leading_symbols`.

### `m110a_tx`

`build/bin/m110a_tx [--mode 600H|1200H|2400V] (--bits FILE | --bytes FILE) [--frame] OUT.wav` writes a
48 kHz 16-bit mono WAV: by default 2 s of silence, the signal (carrier 1800 Hz, root-raised-cosine,
no leading symbols), then 1 s of silence. `--match WAV` sets the signal RMS to a recording's.
`--symbols OUT` writes the 8PSK position (0-7) of every symbol from the first preamble symbol.

`--frame` takes whole vocoder units (60 bits at 600H, 81 at 1200H, 54 at 2400V) and frames them
as the radio does: the 54-bit header and the units. 600H and 1200H then get the trailer words W
and W' and zeros to a multiple of 16 bits. 2400V gets 108 ones and zeros to a multiple of 8 bits.

- Measured: in the ten 600H and 1200H streams the zero fill is to a multiple of 16 bits (600_1
  pads 14 bits, 600_2 pads 2). From each of the nine that end in W, W' (600_x, 1200_x), `--frame`
  rebuilds the stream bit for bit from its units.
- Measured, melp2400 (the one 2400V recording): 15288 bits = the header, 280 units of 54 bits,
  108 ones from bit 15174, then 6 zeros. 15288 is a multiple of 8 and not of 16. From its 280
  units, `--frame --mode 2400V` rebuilds the stream bit for bit. The byte fill rests on this one
  message.
- The trailer start at 15174 is a reading: the run of ones is 109 bits (15173-15281), and the
  first of them is taken as the last bit of unit 280. What supports it (measured,
  `test/harris/blind/claims2400.py`): 15174 and the header end (bit 54) are both on the 54-bit
  grid. The alternating column 5 alternates in all 279 frame steps, the first and the last frame
  included. Column 53, where that 109th one would sit, is 1 in 71 % of frames. Starting the
  trailer at 15173 leaves a 53-bit last unit.
- Corrected 2026-11-22: this section used to say the fill is to 16 bits "in all eleven streams".
  melp2400 is the exception (above).
- Not covered: melp600's form (108 ones as trailer, stream bit 54 = 0). Send it with `--bits`.

## Verified

- **Loopback, ctest `harris_tx_loopback`** (`test/harris/tx_loopback.py`, label `iq`, needs numpy
  and scipy). For each of 600H, 1200H and 2400V, it runs three cases from a fixed seed: 23 framed
  random units, a byte payload that fills four blocks exactly, and 301 bytes. Each goes through
  m110a_tx, a WAV, I/Q and `m110a_rx` AUTO. Every case must name the mode and D1, D2, report
  `success=1 eom=1`, return the payload bytes identically, and have the symbol count above.
  Measured: 9/9 (2400V: 4320, 7200 and 4320 symbols, mode 2400V, D1, D2 = 7, 7).
- **`test_table_xi_modes`:** the in-process ModemTX → AUTO loopback of 600H and 1200H now runs on
  this path and still requires the exact payload.

## Step 1: recorded messages re-made by this modem

`test/harris/tx_step1.py` re-makes every 600H, 1200H and 2400V message in the Harris set. It takes the
message's trusted payload bits (the modem stage's `<slug>.bits`) and transmits them at the
recording's level. Then (a) `m110a_rx` decodes our WAV, and (b) `harris_score --compare` compares
symbols. For (b), the receiver makes its own hard decisions on every symbol from the first
preamble symbol, on the recording and on our WAV, and the script counts how many agree with our
transmitted symbol stream. The receiver decides without looking at our stream: preamble and probes
get 8-ary decisions, data the nearest point of the mode's constellation. It adapts on the known
288-symbol common segment once before it decides. The work files go under `build/` and stay out
of git.

```sh
python3 -I -B test/harris/tx_step1.py --rx build/bin/m110a_rx --tx build/bin/m110a_tx \
    --score build/bin/harris_score --set <recordings> --payload <payload dir> --work build/step1/all
```

Measured, one-tap front end (`--eq 0`). melp600 is also given with the 13-tap equalizer
(`--eq 6`), which that narrow-band file needs:

| Message | (a) m110a_rx on our WAV | Recording vs our symbols | Preamble | Data | Zero probes | D1/D2 probes |
|---|---|---|---|---|---|---|
| 600_1 | success=1 eom=1, 442 bytes identical | 17278/17280 | 1440/1440 | 7920/7920 | 7566/7568 | 352/352 |
| 600_2 | identical | 15838/15840 | all | all | 6878/6880 | all |
| 600_3 m1 | identical | 12958/12960 | all | all | 5502/5504 | all |
| 600_3 m2 | identical | 10078/10080 | all | all | 4126/4128 | all |
| 600_3 m3 | identical | 7198/7200 | all | all | 2750/2752 | all |
| 600_3 m4 | identical | 5758/5760 | all | all | 2062/2064 | all |
| 1200_1 | identical | 21590/21600 | all | all | 9628/9632 | 442/448 |
| 1200_2 | identical | 14398/14400 | all | all | 6190/6192 | all |
| 1200_3 | identical | 14398/14400 | all | all | 6190/6192 | all |
| melp600, `--eq 6` | identical | 23026/23040 | 1439/1440 | 10800/10800 | 10313/10320 | 474/480 |
| melp2400, `--eq 6` | success=1 eom=1, 1911 bytes identical | 17250/17280 | 1439/1440 | 10545/10560 | 4923/4928 | 343/352 |
| melp2400, `--eq 0` | identical | 15964/17280 | 1407/1440 | 9774/10560 | 4452/4928 | 331/352 |

Our own WAV against our symbols: every symbol agrees, in all eleven. The 600H and 1200H rows
were re-measured with 2400V on the path and are unchanged.

Where the recordings diverge from our stream (measured):

- **The end.** In eight messages the only two disagreements are the last two symbols of the
  transmission. There the recording's level falls to 0.25 and then 0.05 of a symbol, while
  ours stays at 1.0. The radio's signal stops about two symbols before the last probe segment ends.
  1200_1 stops 11 symbols early (all ten of its disagreements are symbols 21589 to 21599), and
  melp600 about 9 (9 of its 14), and melp2400 about 8: its mean |y| falls from 0.75 to 0.03 in
  the last 8 symbols, where 9 of its 30 disagreements sit (symbols 17270 to 17278). NOT
  DETERMINABLE from the recordings whether the radio, its
  audio path or the recording cuts them. This TX sends the full last frame.
- **melp600's scattered errors.** The other 5 of its 14 are spread over the message, at its 10.7 %
  probe symbol error rate. With one tap it agrees on 21935/23040, with all data symbols still
  agreeing. That is the file's narrow passband, not the stream.
- **melp2400's scattered errors.** The other 21 of its 30 are spread over the message (at most 3
  in any 96-symbol stretch). 15 are data symbols, at the file's 9.3 % probe symbol error rate
  and 703-2918 Hz passband, against a 2400-baud signal. The receiver still decodes the same
  bytes from the recording. With one tap, 15964/17280 agree. After the last symbol the recording
  falls to its noise floor: 0.1 s RMS 126 to 314 against about 5800 during the message.
- **Nothing else.** The 1440-symbol preamble, every data symbol, every D1/D2 segment and the
  number of blocks agree. That includes the final block, whose content is the EOM, the 144-bit
  flush and the zero fill. After the last symbol, the recordings fall to noise within 8 symbols.
  So the radio sends nothing after the last block that this receiver sees (measured).

## Not proven

- **That the radio accepts our WAV.** No RT-1694D has been played this file. The symbols match.
  The analogue side was not compared: pulse shape, exact carrier (the recordings read +2.5 Hz
  against our 1800 Hz, which is the radio or the recording chain), audio passband and level
  through the operator's chain.
- **What goes before the preamble.** Our WAV starts the preamble straight after silence. The
  comparison begins at the first preamble symbol, so nothing earlier in the recordings was
  compared.
- The slow ctest sweeps (`-L slow`) were not run. They exercise the receiver on the MS-DMT
  reference recordings, not this TX.

## Next steps (in order)

1. Probe-set paper design for the radio play-back route (`docs/harris-melpe.md`), then the
   fallback audio set.
2. If the radio rejects our WAV: compare the analogue side (spectrum, pulse shape, start of
   transmission) against a recording before changing the symbol stream.
