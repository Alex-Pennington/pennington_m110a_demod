# Harris-mode transmitter (600H, 1200H, 2400V)

Goal: a transmitter whose output a Harris RT-1694D accepts, so known 600H payloads can be played
to the radio and its synthesiser output recorded (`docs/harris-melpe.md` gives the modem facts).

Labels: **read** = read from code or a standard; **measured** = measured with a command;
**guessed** = an inference not yet measured.

## State

### Done

- Read the TX path (`api/modem_tx.cpp`, `src/modem/m110a_codec.h`) and the standard's text on the
  EOM, the flush and the block-boundary known symbols (110D §5.3.2.2.3, §5.3.2.2.4, §5.3.2.3.1,
  §5.3.2.3.2, §5.3.2.3.7.1.2). The results are under *What TX does today* and *What 110C requires*.
- Built the tree on this branch. No sources are changed.

### Verified

- Baseline `ctest` on the unchanged tree: at least 18 of 19 tests passed (measured). The last
  test, `iq_sweep_mistune`, was still running at handoff.

### Not done

- No TX change. 600H, 1200H and 2400V transmit as before, without D1/D2 block probes, the EOM or
  the flush.
- No loopback ctests for the Harris modes.
- No step-1 operator file (600_1 re-modulated). No comparison with the recording.
- No measurement of what follows the EOM in the recordings (how many blocks, what content).
- Probe-set paper design: not started.
- Fallback audio set (silence, pitch sweep, level steps, vowel and hiss, known audio) and its
  README: not started. No candidate public-domain speech named yet.
- No files were generated for the operator.

## What TX does today (read, `api/modem_tx.cpp`, `src/modem/m110a_codec.h`)

- **Modes:** 150S to 2400L, 4800S, 2400V (D1, D2 7, 7), 600H (5, 7) and 1200H (5, 6). 75S and 75L
  return "M75 modes not yet supported". `M4800_LONG` maps to 4800S.
- **Preamble:** `BrainPreambleEncoder::encode(mode_index, is_long)`. Short modes send three
  frames, counting down 2, 1, 0. With `include_leading_symbols`, 288 extra symbols go before the
  preamble: a 2-symbol burst, 8 silent symbols, 2 ramp symbols and 276 scrambled 8PSK.
- **Input:** bytes only, turned into bits LSB first. A payload whose length is not a whole number
  of bytes cannot be sent bit-exact.
- **FEC:** K=7, rate 1/2, then 6 zero tail bits. The coded stream is padded with zeros to the
  interleaver block.
- **Known segments:** every one is scrambled zeros, including the two before each new interleaver
  block, where 110C requires D1 and D2.
- **EOM:** not sent. With `include_eom`, `generate_eom` adds four frames of scrambled zero data
  and zero probes. That is not the 32-bit pattern 4B65A5B2, so `eom=1` is not tested on own-TX
  loopback.
- **Flush:** none beyond the 6 tail bits and the zero padding of the last block. 110C asks for
  144 zero bits plus the rest of the block.
- **Output:** real audio at `sample_rate` with the carrier at `carrier_freq`, shaped by a
  root-raised-cosine filter (alpha 0.35, 6 symbols each side). Optional raised-cosine ramp-up.

## What 110C requires (read, MIL-STD-188-110D text, same clauses as 110C)

- **EOM** (§5.3.2.2.3, §5.3.2.3.1): 4B65A5B2, most significant digit first, fed to the FEC
  encoder right after the last data bit.
- **Flush** (§5.3.2.3.2): 144 zero bits, plus enough zeros to complete the interleaver block that
  holds the last coder flush bit.
- **Known data** (§5.3.2.3.7.1.2): the two known segments before each new interleaver block
  (1440 symbols short) carry D1 and then D2: the 8-symbol Walsh pattern twice (16 symbols), then
  4 zeros when the segment is 20 symbols long. The pattern goes through the scrambler like the
  zeros it replaces. `test/harris/harris_score.cpp` `dpat()` and `fse()` use this form.

## Next steps (in order)

1. Measure in the recordings what follows the EOM: the decoded bits after the EOM to the end of
   the signal, the number of blocks, and whether the last block carries D1/D2 probes.
2. Add a bit-level encode path (payload bits verbatim, then the EOM, then the flush as measured)
   and D1/D2 in the block-boundary known segments, for 600H, 1200H and 2400V. Keep RX unchanged
   and add byte-exact loopback ctests per mode.
3. Re-modulate 600_1's payload bits. Its dump is 442 bytes, so the EOM sits somewhere in bits
   3529 to 3536. Read `eom_bit` from `harris_score` first: the bits between the trailer's end
   (3522) and the EOM must go out verbatim. Write the 48 kHz, 16-bit mono WAV with 2 s of quiet before and 1 s after, matched
   to the recording's level, and run the comparison.
4. Probe-set design, then the fallback audio set.
