# 110A streaming receiver (ModemRX start / push_samples / take_blocks / finish)

`ModemRX::decode()` takes a whole burst and returns its bytes. A burst's bytes then reach
the DTE a decode time after the burst ends: for a 32 s 600S burst, 11.5 s on an idle host
and 21 s at a load average of 12 (docs/110d-appa-lan.md). The streaming receiver decodes the
burst as its samples arrive. It
reports the preamble once it is read, and each interleaver block once it is decoded. Its
bytes are `decode()`'s, but for one difference, described below.

Source: `api/modem_rx.h` (API), `api/modem_rx.cpp` (`StreamState` and the `stream_*`
functions). Tests: `test/rx/stream_vs_whole.cpp` (one buffer decoded both ways) and
`test/rx/stream_compare.py` (the sets below). `m110_lan` uses it (docs/110d-appa-lan.md).

## The API

```cpp
ModemRX rx(cfg);
rx.start();                                   // one burst
for (each chunk of audio) {
    rx.push_samples(chunk);
    StreamSync s;
    if (rx.take_sync(s)) ...                  // once: the mode, D1/D2, the carrier offset
    for (auto& b : rx.take_blocks()) ...      // in order; b.last ends the message
}
DecodeResult r = rx.finish();                 // the rest, and decode()'s verdict on it all
for (auto& b : rx.take_blocks()) ...
```

The samples from `start()` to `finish()` are the buffer `decode()` would be given. Samples
after the EOM are ignored. A `BlockRecord` carries:

- `index`: the interleaver block, from 0;
- `data`: the bytes that end inside the block's info bits, bytes floor(B i / 8) to
  floor(B (i + 1) / 8), B the info bits per block, cut at the EOM. That is the Appendix A
  chunk rule for a blocking factor of B (docs/110d-appa-lan.md);
- `block_quality_db`: the block's quality, and the mode's threshold for it;
- `quality_db`: the first 1440 data symbols, and the mode's threshold for it, as `decode()`
  reports them (docs/m110a-quality-gate.md);
- `passes`: both clear their thresholds. `below_quality`: when not, whether quality is the only
  reason;
- `last`: no block follows. `eom`: the EOM ended the message.

A block that is not the last is reported only when at least one more byte is known to follow
it. A receiver can therefore mark a packet as not the last without waiting for the next
block. The last block is the one holding the message's last byte. An EOM-only block after it
is judged in `finish()`'s verdict and reported as no record.

## How it decodes

Each step `decode()` runs over the whole buffer is run as the samples arrive. Each step has
one implementation, which `decode()` and the stream share:

| Step | Shared code | What it waits for |
|---|---|---|
| Cut before the first preamble | `BrainDecoder::scan_first_preamble` | its correlator window |
| Two-stage AFC | `BrainDecoder::coarse_afc`, `fine_offsets`, `fine_afc_metric` | the first 788 symbols (coarse) and 488 (fine) after the cut, at each candidate offset |
| Mixer and matched filter | `BasebandFilter` | half the filter (120 samples) |
| Preamble, mode, countdown, carrier line | `BrainDecoder::read_header` | the end of the preamble |
| Data grid and timing steps | `BrainDecoder::DataGrid` | 8 frames past a frame (16 where a step is found) |
| Level | `symbol_gain` | the preamble |
| Phase tracking | `PhaseTracking` | the frame |
| DFE | `FrameDfe` | 5 symbols past the frame |
| Demap, deinterleave, Viterbi | `M110ACodec::demap_frames`, `block_viterbi_input`, `ViterbiDecoder` | the interleaver block |
| EOM, signal end, quality, verdict | `cut_at_eom`, `signal_frame`, `judge` | the message's blocks |

Each wait is the step's own lookahead. A step that reads past the samples it has waits,
instead of reading the buffer's end as `decode()` would. So each step gives the same output
as over the whole buffer. At `finish()` the steps run to the end of the samples, as
`decode()` does.

A block is decoded once the next one is in. The Viterbi decoder decides each bit 34 bits
later (its traceback), so a block's last bits need the next block's first bits. The
deinterleaver needs a whole block before it gives any bit of it. So block i is reported one
interleaver span after its end (0.6 s short, 4.8 s long), plus the data grid's 8 frames
(0.13-0.16 s). The block holding the EOM is reported once the EOM is decided, which takes
no further block. Without an EOM, a block is reported once the next block is known to be
inside the signal (`signal_frames()` reaches past its end). The rest wait for `finish()`,
which cuts the message to the whole blocks the signal covers, as `decode()` does.

**The one difference: the soft demapper's SNR.** `decode()` weights the soft bits by the SNR
of every probe in the buffer, including the frames after the signal ends. The stream weights
each block by the SNR of the probes up to and including that block. The coded modes keep the
bytes the same in every case measured. 4800S is uncoded: each bit is the sign of a soft
value truncated to an integer, so a scale change moves the bits nearest zero. See the counts
below.

**What it does not stream.** These are decoded whole by `decode()` at `finish()` and then
reported block by block: 75 bps (its own demodulator, `m75::decode`); a burst whose mode
`decode()` would find only below the correlator's reach; the MLSE and turbo equalizers; the
legacy single-stage AFC. The bytes are then `decode()`'s, and the records come only at
`finish()`.

## Measured

`test/rx/stream_compare.py` (stream_vs_whole per case, 10 ms a push, a Release build of
this branch). "Same" means the same bytes, success and EOM both ways. "Exact" means the
payload sent.

| Set | Cases | Same | Exact: whole / stream | Success: whole / stream | Differ, and of those exact either way |
|---|---|---|---|---|---|
| AWGN (the quality gate's set, plus 75S/75L) | 188 | 184 | 132 / 133 | 137 / 137 | 4 (4800S at 14 and 16 dB), 1 |
| CCIR moderate and poor, 5 and 10 dB, seeds 1-5 | 180 | 138 | 97 / 97 | 98 / 98 | 42, 0 |
| Net 300L bursts 1-4 (the audio m110a_rx decoded) | 4 | 3 | - | 3 / 3 | 1 (burst 1), - |

- **4800S** (uncoded) at 14 dB: three cases differ with both decodes wrong. At 16 dB one
  case differs: the stream's bytes are exact and the whole decode's are not.
- **CCIR**: no case is exact one way and wrong the other, and none succeeds one way and fails
  the other. 40 of the 42 that differ fail both ways (decode quality or a block below its
  threshold). Two are the 600S wrong-byte successes the quality gate lets through
  (docs/m110a-quality-gate.md), wrong both ways. One, 2400L moderate 10 dB seed 3, is a
  garbage decode (quality 4.3 dB against 7.75 dB): the whole decode found an EOM pattern in
  it and the stream did not, so it gives 200 bytes against none.
- **Net burst 1**: the same verdict (block 9 at 2.0 dB under 2.25 dB) and the same byte
  count. 41 bytes differ, all in blocks 2 and 9 (counting from 1, as
  docs/m110a-quality-gate.md does; `BlockRecord::index` 1 and 8). Those are the two blocks
  `reencode_check.py` finds do not fit the signal. Bursts 2-4 are the same.
- **75S and 75L** are decoded whole (see above), so they are the same by construction.
- Every case: the blocks' bytes put together are the stream's result bytes. When the whole
  decode succeeds, every block reported clears the gate (AWGN 137 of 137, CCIR 98 of 98, net 3
  of 3). No failing decode has every one of its blocks clear it.

**Whole-burst decode() unchanged.** `decode()` now runs on the shared steps above. Over the
372 audio files of these three sets, it gives the same `DecodeResult` as origin/master
7aae91b, field for field: the bytes (hashed), success, the EOM flags, SNR, carrier offset,
`quality_db`, every block's quality, the thresholds, D1/D2, the countdown and the error
text. Its stdout lines (`[RX PREAMBLE]`, `[RX] extract_data`) are the same too (a test
program per build, linked against each build's `libm110a_modem.a`). The reference decode set
prints the same output on both builds (`test_decode_quality`, 51 cases; `test_mode_detect`,
20). ctest passes 49 of 49 on this branch and on origin/master (run on voror, an idle host,
each with 5 jobs; on saga, loaded, the branch also passed 49 of 49).

**CPU.** One case per mode (the AWGN set's cleanest, seed 1), the 32 s 600S burst
`appa_rx_stream` sends, and net burst 2. Run one at a time on voror, an idle host. Process CPU
seconds of `decode()` on this branch and on origin/master, and of the stream (start to
finish, 10 ms a push). Same bytes in every row:

| Input | Audio s | decode() master | decode() branch | Stream | Stream / decode() |
|---|---|---|---|---|---|
| 2400S clean | 3.3 | 0.93 | 0.95 | 0.17 | 0.18 |
| 2400L clean | 11.1 | 3.18 | 3.21 | 0.29 | 0.09 |
| 1200S 7 dB | 3.9 | 1.11 | 1.12 | 0.18 | 0.16 |
| 1200L 7 dB | 11.1 | 3.15 | 3.22 | 0.28 | 0.09 |
| 600S 4 dB | 5.1 | 1.46 | 1.47 | 0.20 | 0.14 |
| 600L 4 dB | 11.1 | 3.15 | 3.18 | 0.28 | 0.09 |
| 4800S 18 dB | 2.5 | 0.69 | 0.69 | 0.17 | 0.25 |
| 300S 0 dB | 8.1 | 2.33 | 2.34 | 0.28 | 0.12 |
| 300L 0 dB | 15.9 | 4.58 | 4.65 | 0.40 | 0.09 |
| 150S 0 dB | 14.1 | 4.12 | 4.16 | 0.37 | 0.09 |
| 150L 0 dB | 20.7 | 6.02 | 6.04 | 0.48 | 0.08 |
| 75S clean (decoded whole) | 26.1 | 5.03 | 5.14 | 4.63 | 0.90 |
| 75L clean (decoded whole) | 30.3 | 5.78 | 5.94 | 5.34 | 0.90 |
| 600S, 2252 bytes | 32.7 | 10.32 | 10.18 | 0.71 | 0.07 |
| Net 300L burst 2 (.f32) | 93.0 | - | 24.28 | 1.60 | 0.07 |

`decode()` reads the whole buffer 25 times through the matched filter: once for the cut and
twelve AFC candidates in each of its two passes. The stream runs one filter over the burst.
It runs the AFC candidates only over the first 0.2 s after the cut. The stream's cost is
about 2% of the audio's duration, against about 30% for `decode()`.
A 75 bps burst is decoded whole, after the stream has run its first steps: 0.9 of the
whole-burst cost here. On the loaded host (saga), the AWGN set took 557 s of CPU whole and
331 s streamed, the 75 bps cases included; the CCIR set took 527 s and 76 s.

**Latency in m110_lan**: docs/110d-appa-lan.md, "Data during the burst".

**STANAG 5066 over m110_lan** (greybeard-ale `tests/test_s5066_modem.py` at its saga/main
61104ee: two s5066-node instances through Appendix A, a 2048-byte ARQ U_PDU, 600S). Run on
voror, origin/master's m110_lan and then this branch's. Every case passes on both; the U_PDU
arrives exact once:

| Case | Set-up s, master / branch | A's confirm s, master / branch | Recovery s, master / branch |
|---|---|---|---|
| clean | 14.8 / 11.7 | 63.2 / 48.0 | - |
| 10 dB | 14.4 / 11.9 | 62.5 / 48.6 | - |
| 6 dB | 14.1 / 11.9 | 62.2 / 48.6 | - |
| answer lost | 14.8 / 11.7 | 116.7 / 89.1 | 53.9 / 40.4 |
| data lost | 14.8 / 11.7 | 132.7 / 117.5 | 84.5 / 72.2 |

The node B gets the first D_PDU of A's 32.7 s data burst 27.9 s before that burst's keyed
end, and the D_PDU of a 2.7-3.3 s burst 0.7-1.5 s before its end (from the run on saga, the
node logs and the relay's keying times). The test's own "end-to-data" column looks for a burst's first D_PDU only after
the burst ends, so with streaming it finds the next burst's and no longer measures that.
The node's timers were left as greybeard-ale sets them for whole-burst decoding
(`ack_wait_tx_pct 100`, `link_timeout_ms 120000`).
