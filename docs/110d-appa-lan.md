# MIL-STD-188-110D Appendix A: the LAN interface (`m110_lan`)

`m110_lan` puts the Appendix A LAN interface in front of this repo's 110A modem. A DTE drives
it over the TCP data socket (A.5.1) or over the UDP data socket (A.5.2). There is no radio:
transmit audio goes to a WAV file or to stdout, and receive audio comes from a file or stdin.

Source: `m110_lan/main.cpp` (the server) and `m110_lan/appa_proto.h` (packets, CRC, FEC).
Tests: `test/lan/test_appa_proto.cpp` (ctest `test_appa_proto`), `test/lan/appa_dte.py`
(ctest `appa_lan_dte`), `test/lan/appa_rx_stream.py` (ctest `appa_rx_stream`, the receiver
against real-time audio) and `test/lan/appa_m75.py` (ctest `appa_lan_75`, 75S and 75L both ways
and the TCP chunk framing of every mode). The last two need numpy and are skipped without it.

The spec is `docs/standards/MIL-STD-188-110D.pdf`, Appendix A, PDF pages 46-83 (printed
pages 41-78). Eighteen of those pages are scanned images, so `pdftotext` returns nothing for
them. They were read as rendered images. They hold A.5.1.1, Tables A-I to A-IX, Figures A-9
and A-10, and most of A.5.2.

**Measured** means a test in this repo checks it, and the test is named. **Inferred** means
the behaviour is this implementation's reading of the appendix, and nothing here tests it
against another implementation. No Appendix A equipment from another vendor was available,
so every byte-level check is against the appendix text, not against a second implementation.

## Run it

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
build/bin/m110_lan --mode 600S --tx-out tx_%d.wav --rx-in -      # receive audio on stdin
```

| Option | Meaning |
|---|---|
| `--mode M` | Transmit waveform: 75S 75L 150S 150L 300S 300L 600S 600L 1200S 1200L 2400S 2400L 4800S. The default is 600S. The receiver reads each burst's mode from its preamble whatever `--mode` says. The 75 bps finder, which reaches below 0 dB, runs only with `--mode 75S` or `75L` (see "The modem behind the interface"). |
| `--tx-out FILE\|-` | Transmit audio. A file gets one WAV per transmission (`%d` in the name numbers them, and without it each transmission overwrites the last). `-` writes raw s16le 48 kHz mono to stdout. |
| `--tx-format wav\|raw` | The default is `wav` for a file and `raw` for stdout. |
| `--rx-in FILE\|-` | Receive audio: a 16-bit mono 48 kHz WAV, or raw s16le 48 kHz. `-` reads stdin. A regular file is read once a DTE is connected; a pipe or other stream is read from the start, and a burst that ends with no DTE is dropped. |
| `--port P` | TCP data socket port. The default is 3000 (A.4.2). |
| `--rci-port P` / `--udp-port P` | Control connection and UDP data socket ports. The defaults are 3001 and 3002. |
| `--duplex full\|half-tx\|half-rx` | Full duplex, or half duplex as transmitter master or receiver master (A.5.1.2.7). |
| `--rx-fast` | Deliver received data as soon as it is decoded, without the air-rate pacing (see "The modem behind the interface"). |
| `--rx-gap S` | A burst ends after S seconds (default 0.5) below its own end level (see "The modem behind the interface"). |
| `--rx-max S` | The longest burst, in seconds (default 120). |
| `--rx-threshold X` | Preamble correlation metric, 0 to 1, that starts a burst (default 0.45). |
| `--rx-low-quality` | Deliver the bytes of a block the decoder scores below its quality gate (default off). See below. |
| `--rx-squelch X` | Accepted and has no effect: bursts are found by their preamble, not by level. |
| `--tx-lead S`, `--tx-tail S` | Silence before and after each transmission (default 0.5 s and 1.0 s). |
| `--tx-queue BYTES` | Transmit queue size (default 1 MiB). |
| `--min-latency MS`, `--max-latency MS` | The Initial Setup socket latency fields (default 100 and 5000). |
| `--verbose` | Log each accepted connection with its handshake age. |

Logs go to stderr. The 110A decoder prints to stdout, so fd 1 is pointed at stderr at
startup. Audio for `--tx-out -` goes to a copy of the original stdout, so its bytes stay clean.

Transmit audio is scaled to 0.2136 RMS (7000/32768, the level `modem_tx_wav` uses). It is
written whole and then renamed, so a reader never sees half a file.

### The UDP control connection

A.5.2.1 requires a TCP control connection (RCI) before any UDP traffic, and leaves its protocol
outside the appendix. `m110_lan` uses newline-terminated text on `--rci-port`:

| DTE sends | Modem answers | Effect |
|---|---|---|
| `UDP <port>` | `OK UDP <modem udp port>` | The DTE's UDP address is the control connection's source address plus `<port>` (A.5.2.1, A.5.2.6.1). |
| `CONTROL` | `OK CONTROL` | Control mode on: the In Control status bit is set (Table A-XII), and data is accepted. |
| `RELEASE` | `OK RELEASE` | Control mode off. |

Closing the control connection ends the UDP session.

### Choosing the data rate: the modem is configured, the DTE is told

**Appendix A gives the DTE no way to choose the data rate or the interleaver.** The rate and
blocking factor are "obtained from the waveform configuration (user configured)" (A.5.1.2.1
item 2), set by "a mechanism beyond the scope of this Appendix" (A.5.1.2.1 item 1), and go
modem to DTE only: Transmit Setup (A.5.1.1.9, Figure A-7), Carrier Detect (A.5.1.1.8) and the
UDP Status Reply (A.5.2.4.1). No payload command (Table A-II) or UDP packet type (Table A-X)
carries a rate from the DTE, and A.5.1.2.4 allows the modem to drop the TCP connection when its
configuration changes.

So whatever drives `m110_lan` (an operator, or the MARS core's configuration) sets the
transmit waveform with `--mode`. For 75 bps: `--mode 75S` or `--mode 75L`. The DTE then reads
it from the Transmit Setup packet of the connection handshake: Tx rate 75, Tx blocking factor 45
(75S) or 360 (75L) (measured, `appa_lan_75` phase `tx75`). A DTE that waits for a rate
command of its own, or sends one, will not get one. Received bursts are reported at the rate
their preamble names, whatever `--mode` is.

## Ownership: one DTE (A.4.1)

The modem holds one DTE at a time, across both interfaces. While a DTE holds the TCP data
socket or the control connection, a new connection on either port is accepted and closed at
once, with no bytes sent.

One thread accepts on both ports. On each wake it takes every pending connection from both,
then hands the slot out in the order the handshakes completed. The order comes from TCP_INFO
`tcpi_last_ack_recv` (ms before now, 1 ms resolution at HZ=1000). All the ages are read
against one clock tick: they are read again until two passes agree. Two handshakes completed
in the same millisecond can't be ordered from user space, so that tie goes to the control
connection.

Measured with `--verbose`, which logs each accept with its age and its drain: 1000 rounds of
"control connection, then TCP connection", on 6 cores.

| Acceptor | Idle machine | All cores busy |
|---|---|---|
| One thread per port | not run | 95 of 300 gave the TCP DTE the modem |
| One thread, ages read one by one | 7 of 1000 | 2 of 300 |
| One thread, ages read against one tick | 0 of 1000 | 0 of 1000 |

The middle row's failures were a clock tick between two reads, which made the later-read
socket look older.

## TCP data socket (A.5.1): field-by-field

Every row marked Measured is checked byte by byte in `appa_lan_dte` (phase `tcp` or `half`)
or `test_appa_proto`.

| Item | Spec | Support | Status |
|---|---|---|---|
| Configurable port, 3000 suggested | A.4.2 | `--port`, default 3000 | Measured (the test uses a free port) |
| Reader thread blocked only on socket input | A.4.3 | One reader thread per connection; timers in their own thread | Inferred (structure, not a test) |
| Network byte order | A.4.4 | All multi-byte fields | Measured |
| Header: preamble 49 50 55, type, payload size, header CRC | A.5.1.1.1, Figure A-2 | Yes | Measured |
| Packet types DATA 00, CONNECT 01, CONNECT ACK 02, ERROR FF | Table A-I | All four | Measured |
| Unused type gives ERROR | A.5.1.1.1 | ERROR, then close (A.5.1.2.3) | Measured (type 05) |
| Payload at most 4086 bytes; payload CRC over payload only; none when empty | A.5.1.1, A.5.1.1.3 | Yes | Measured |
| CONNECT / CONNECT ACK version 12 | A.5.1.1.2 | Yes | Measured |
| Payload commands 00-06, 08-0B | Table A-II | All. 07 and above 0B, and modem-only commands from the DTE, give ERROR and close | Measured (07) |
| Data Transfer: 00, order, 12-byte ID, 0-4072 data bytes | A.5.1.1.5, Figure A-3 | Both ways | Measured |
| Packet order FIRST ONLY 1, FIRST AND LAST 2, CONTINUATION 3, LAST 4 | Table A-III | Both ways | Measured |
| Unique packet ID per Data Transfer packet | A.5.1.1.5.2 | 12-byte counter from the modem; the DTE's ID is echoed in NACKs | Measured |
| Tx_Data_NAK: 04, cause, NACKed ID | A.5.1.1.6, Figure A-4 | Yes | Measured |
| NACK cause 0 not armed, 2 missing first, 3 multiple first | Table A-IV | Yes | Measured |
| NACK cause 1 transmit underrun | Table A-IV | Never sent: the critical-bytes mechanism is off (see below) | Not applicable |
| Tx_Status: 05, state, FIFO space, fill, critical ms, critical bytes | A.5.1.1.7, Figure A-5 | Yes | Measured |
| States FLUSHED 1 to DRAINING FORCED 6 | Table A-V | 1-5 reached. 6 only after an undecodable UDP set; on TCP, 6 needs an underrun, which never happens | Measured (1-5) |
| Carrier Detect: 08, state, Rx rate, Rx blocking factor | A.5.1.1.8, Figure A-6, Table A-VI | Yes | Measured |
| Transmit Setup: 09, Tx rate, Tx blocking factor | A.5.1.1.9, Figure A-7 | Yes, from `--mode` | Measured (600S: 600, 360; 75S: 75, 45; 75L: 75, 360) |
| Initial Setup: 0A, RTT, min/max latency, sync flag, async fields | A.5.1.1.10, Figure A-8, Tables A-VII to A-IX | RTT measured over the probe. Sync flag 0 (async), 8 data bits, 1 stop, no parity, DATA ONLY | Measured |
| Initial connection: CONNECT both ways, ACK both ways, 3 s limits, version check | A.5.1.2.1 | Yes | Measured (3 s silent DTE; version 11) |
| CONNECTION_PROBE with a 6 s limit, then Initial Setup, Transmit Setup, Tx Status FLUSHED, Carrier Detect | A.5.1.2.1 | Yes, in that order | Measured (6 s limit and order) |
| RTT above the maximum socket latency drops the connection | A.5.1.1.10 ("may") | Yes | Inferred (not tested) |
| Keep-alive after 2 s with nothing sent | A.5.1.2.2 | Yes | Measured |
| 30 s with no DATA packet closes the connection | A.5.1.2.2 | Yes | Measured (closes at 30.0 s) |
| A bad CRC is dropped silently | A.5.1.2.3 | Yes; the parser resynchronises on the next preamble | Measured (header and payload CRC) |
| Protocol error gives ERROR and close | A.5.1.2.3 | Yes; ERROR carries no payload | Measured |
| Transmitter: ARM, pre-fill, START, DRAINING_OK on LAST, FLUSHED when done | A.5.1.2.5, Figure A-9 | Yes | Measured |
| Status at least every 2 s in STARTED or DRAINING | A.5.1.2.5 step 8 | Every 1 s, and on every queue change | Inferred (not timed) |
| No insert while free space is below the maximum packet data; the DTE's socket blocks | A.5.1.2.5 step 10 | The reader thread stops reading until space frees | Inferred (not tested) |
| Receiver: CARRIER DETECTED, data in blocking-factor chunks, NO CARRIER | A.5.1.2.6, Figure A-10 | Yes; a chunk that is not a whole number of bytes: see "Blocking factor" | Measured (`appa_lan_dte`; `appa_lan_75` at 75S, 75L, 150S, 300S and 600S to 4800S) |
| ABORT_RECEPTION: a LAST packet, then NO CARRIER | A.5.1.2.6 step 6 | LAST with no data | Measured |
| Half duplex, transmitter master | A.5.1.2.7.1 | `--duplex half-tx` | Measured |
| Half duplex, receiver master | A.5.1.2.7.2 | `--duplex half-rx` | Measured |
| Synchronous socket mode | A.5.1.2.8.1 | Not offered | See "Not implemented" |
| Async standard mode (start/stop/parity over the air) | A.5.1.2.8.2.1 | Not offered | See "Not implemented" |
| Async data-only mode | A.5.1.2.8.2.2 | The only mode offered | Measured (as reported) |
| Configuration change while connected | A.5.1.2.4 | Configuration is fixed at startup | Not applicable |

## UDP data socket (A.5.2): field-by-field

| Item | Spec | Support | Status |
|---|---|---|---|
| Control connection first; UDP only to and from that DTE | A.5.2.1, A.5.2.6.1 | Yes; datagrams from any other address and port are ignored | Measured |
| Packet: 4-byte header, payload, CRC over header and payload, at most 1226 bytes | A.5.2.2, Figure A-11 | Yes | Measured |
| Header: version 1 (4 bits), type (4 bits), session (24 bits); only version 1 processed | A.5.2.3, Figure A-12 | Version in the high nibble | Measured (version 2 ignored) |
| Types 1-8 | Table A-X | All. A DTE-to-modem packet of a modem-only type is ignored | Measured (1-8) |
| Session ID | A.5.2.3 | Adopted from the DTE's packets; a new one resynchronises | Inferred (see ambiguities) |
| Status Reply, 32 bytes, Figure A-13 fields | A.5.2.4.1 | Every field | Measured (sequence, transfer ID, reserved, error, status, dropped, rates, blocking factors) |
| Status on change or every 500 ms; sequence modulo 65536 | A.5.2.4.1 | Yes | Measured (0.50-0.51 s) |
| Error vector bit 4 NOT IN CONTROL | Figure A-14, Table A-XI | Yes | Measured |
| Error vector bits 0-3 (TX/RX overrun and underrun) | Table A-XI | Never set: no real-time buffers to overrun | Not applicable |
| Status vector: receiver state, carrier detect, transmitter state, in control | Figure A-15, Tables A-XII to A-XIV | Yes | Measured |
| Packets Dropped | A.5.2.4.1 | Lost packets of the current inbound transfer | Measured (6 = 6) |
| Jitter | A.5.2.4.1 | Standard deviation over 100 packets of arrival minus timestamp, in ms | Inferred (not tested) |
| Transmit queue fill and space | A.5.2.4.1, A.5.2.6.5 step 5 | Queued bytes plus 1200 per undecoded set buffered | Inferred |
| Encoded Data (16 + data) / Enhanced Encoded Data (20 + data) | A.5.2.4.2, A.5.2.4.3, Figures A-16, A-18 | Both ways; the modem uses Enhanced for the first set | Measured |
| Control vector: first set, last set, holdoff | Figure A-17, Table A-XV | Holdoff keeps the transmitter in QUEUEING | Measured (first and last); holdoff Inferred |
| FEC type 1 and interleaver type 1 only | A.5.2.4.2 | Other types ignored | Measured (types sent); rejection Inferred |
| Timestamp | A.5.2.4.2 | The appendix's code (see ambiguities) | Inferred |
| Modem Command Request / Ack; ABORT RECEPTION 1; others ignored | A.5.2.4.4, A.5.2.4.5, Table A-XVI | Yes; a repeated sequence number is acked again and not re-run | Measured |
| RS with erasures, GF(16) mod x^4+x+1, printed g(x) | A.5.2.5 | Yes | Measured (generator coefficients; every erasure pattern up to N-K) |
| Interleaver: rows of nibbles, packets as columns | A.5.2.5, Tables A-XVIII to A-XX | Yes | Measured (the appendix's ABCD...P example, byte for byte) |
| N, K by data rate | Table A-XVII | Modem to DTE: (6,2) up to 2400, (7,4) to 28800, (8,5) above. DTE to modem: any N, K the packets carry | Measured (6,2) |
| L = min(ceil(B/(K*8)), 1200) * N | Figure A-22 | Yes | Measured (600S: 23 bytes per packet) |
| EOM 4B 65 A5 B2 26 D2 D3 69, zero padding, removed on receipt | A.5.2.5.2 | Yes | Measured |
| Ping every 5 s; 30 s without PING REPLY locks out data | A.5.2.6.2 | Yes | Measured (ping and reply); lockout Inferred |
| Bad CRC, unknown type, wrong length or duplicate ignored; late packets discarded | A.5.2.6.3 | Yes | Measured (CRC, version) |
| Undecodable set: end of message, DRAINING | A.5.2.6.3 | Data so far is transmitted, DRAINING_FORCED internally (DRAINING on the wire) | Inferred |
| Transmitter states FLUSHED, QUEUEING, STARTED, DRAINING | A.5.2.6.5, Figure A-23 | Yes | Measured |
| Receiver: carrier detect 1, Enhanced first set, last-set flag, carrier 0, FLUSHED | A.5.2.6.6, Figure A-24 | Yes | Measured |
| Sync/async data socket modes | A.5.2.6.8 | As TCP: async data-only only | See "Not implemented" |

Measured transfers (`appa_lan_dte`, 600S): 200 bytes sent with 6 of 30 packets withheld were
recovered by the FEC, transmitted to a WAV and fed back. The same 200 bytes came back as 30
Enhanced/Encoded Data packets whose every codeword the test's own RS encoder confirms. A
500-byte reception was aborted by command mid-way.

## The modem behind the interface

- **Transmit.** Data queued from the DTE is encoded by `ModemTX` once the message is complete:
  LAST on TCP, or the last interleaver set on UDP. The audio is written then, and the
  transmitter returns to FLUSHED. The transmitter does not run in real time, because there is
  no radio to pace it. So FIFO Critical Milliseconds and Critical Bytes are 0, which turns off
  the A.5.1.2.5 step 11 underrun mechanism, and the A.5.1.2.8 data-rate pacing obligation on
  the DTE has no effect.
- **Receive.** Three threads, so that nothing waits on a decode:
  - The **reader** reads the input 10 ms at a time into a queue. It blocks only on the input,
    so a decode never stops audio being read.
  - The **scanner** mixes the audio to baseband at 9600 Hz (root raised cosine, 0.35) and
    correlates it with the 110A common preamble (`PreambleCorrelator`, `src/sync/preamble_acq.h`),
    so a burst is found by its preamble, not by its level. On a peak at or above
    `--rx-threshold` it reads D1 and D2 from that frame: a coherent match of each 32-symbol block
    against the eight Walsh patterns, after the correlator's carrier-offset estimate is taken
    out, summed over up to three frames until one pattern leads the next by 2:1. The pair gives
    the mode (MIL-STD-188-110 Table XI). CARRIER DETECTED then goes up with that mode's rate and
    blocking factor (A.5.1.2.6 step 2). A peak whose mode cannot be read is ignored. So is one
    that reads 75S or 75L when the modem is configured for 75 bps, because the 75 bps finder
    (below) takes those. The burst
    ends when the mean level over `--rx-gap` falls below its end level, at a later preamble, at
    `--rx-max`, or at the end of the input. The end level is the geometric mean of the burst's
    level over its first preamble frame and the quietest 0.1 s of the second before it, measured
    in the signal's band, and never above 1/30 of the burst's level.
  - **With `--mode 75S` or `--mode 75L`**, the scanner also finds **75 bps bursts** with the
    75 bps acquisition (`m75::acquire`, `src/m110a/m75_acq.h`, docs/m110a-75bps.md). It
    integrates the whole preamble (3 frames for 75S, 24 for 75L), so it reaches far below the
    correlator above. A sliding form of its coarse search (`m75::SlidingAcq`, same file) scores
    every possible end of preamble once, as the audio arrives. Every 0.5 s, when that search
    holds an end in the last 6 s of input (from the end of the last burst on) at 0.75 of the
    threshold or more, `m75::acquire` runs on those 6 s. A preamble is taken when it is
    found (significance at least 9), its D1/D2 read 7,5 or 5,5 clearly (best code 1.1 times the
    next), its countdown is the one D1/D2 name (3 frames for 75S, 24 for 75L), and two runs
    0.5 s apart agree on where it ends. The countdown and agreement rules are there because a
    window that ends inside a 75L preamble reads a frame boundary inside it as the end
    (measured at -6 dB: 4 frames in, as a 3-frame countdown with z 23.7). CARRIER DETECTED goes
    up then, at 75 bps and blocking factor 45 or 360, so for 75 bps it follows the end of the
    preamble, as A.5.1.2.6 step 2 says ("fully detected a preamble").
    The finder costs CPU all the time the receiver is idle, so under any other `--mode` it is
    not built. A 75 bps preamble the correlator reads then starts a burst like any other mode
    (found near 0 dB and above, ended by level, decoded AUTO), as before 75 bps transmit
    existed. Measured (`appa_lan_75`): configured for 600S, a clean 75S burst is delivered
    exact.

    Idle receive CPU, 30 s of white noise in 300-3300 Hz fed in real time, process user plus
    system time from `/proc` (one core = 100%; the host's load average was 18-27 throughout):

    | Configured `--mode` | master (no 75 bps) | finder always on, `acquire` every 0.5 s | finder only for 75 bps, behind `SlidingAcq` (now) |
    |---|---|---|---|
    | 75S | refused | 36.2% | 7.2% |
    | 75L | refused | 36.3% | 7.3% |
    | 150S | 2.6% | 35.2% | 2.6% |
    | 600S | 2.7% | 36.6% | 2.5% |
    | 2400S | 2.6% | 34.2% | 2.6% |

    `SlidingAcq` alone, on 120 s of noise, measured as a standalone loop: 6.5% of one core.
    Its highest significance on that noise was 6.3, under the 6.75 that runs `m75::acquire`.
    On a 75L preamble at -6 dB its significance matched `m75::acquire`'s on the same windows
    to 0.1 (76.0 against 76.1).
    The burst is then followed set by set as the audio arrives, with the decoder's own matched
    filter and Walsh RAKE. Each interleaver block is decoded as it completes, and the burst
    ends at the EOM. Without an EOM it ends when no set has carried signal for one interleaver
    block plus `--rx-gap`, at `--rx-max`, or at the end of the input. A level threshold does not
    end it, because at negative SNR the level hardly changes.
  - The **decoder** decodes each burst the correlator finds as its audio arrives, from 0.25 s
    before the preamble found, with the 110A streaming receiver (ModemRX `start`,
    `push_samples`, `take_blocks`, `finish`; AUTO, DFE; docs/m110a-streaming-rx.md). The
    scanner hands it the burst's audio as it reads it. Each interleaver block goes to the modem
    once it is decoded, which is once the next block is in: the Viterbi traceback reaches into
    the next block, and that block is deinterleaved whole. The burst's last block goes when its
    EOM is decoded. The bytes are those `decode()` gives over the same audio, but for the soft
    demapper's SNR (measured on the probes received so far rather than on all of them;
    docs/m110a-streaming-rx.md says how often that changes a byte). One burst is decoded at a
    time. A 75 bps burst from the 75 bps finder is decoded whole when it ends, as the mode its
    preamble read (forced, so ModemRX refuses it if the preamble contradicts it).
- **Carrier.** CARRIER DETECTED stays up from preamble sync through the decode and the delivery
  (Table A-VI: "synchronized on a preamble or is processing data from the air"). The data goes
  one blocking factor at a time as it decodes. NO CARRIER follows the last packet once the
  burst is also over on the air (its level gone, or the next preamble): the last data comes at
  the EOM, while the far transmitter may still be sending its flush bits and its tail. A DTE
  that answers at NO CARRIER then does not key over it. greybeard-ale's 5066 loopback, whose
  audio relay deafens a keyed side, lost every answer to that before the carrier waited. A chunk
  goes once all its bytes are decoded. It goes as LAST only when the decoder has said no more
  will come, and it holds back only until then: before the last block, the decoder reports a
  block once at least one more byte is known to follow it. Delivery is paced at the air rate:
  chunk i (i >= 1) goes no sooner than i - 1 blocking factors after the first. Fed in real time,
  the stream delivers each chunk as it decodes. With input faster than the air (a file), or a
  whole-burst decode, the first two chunks go at once and then one per blocking factor.
  `--rx-fast` drops the pacing. A burst with no bytes sends NO CARRIER with no data packets. One
  reception is reported at a time: a burst found while the last is still being decoded or
  delivered gets its CARRIER DETECTED after that one's NO CARRIER.
- **Measured** (`appa_rx_stream`): CARRIER DETECTED arrives 0.15 s after the preamble's first
  sample goes into the input, and no audio is dropped while a 32 s 600S burst is decoded. That
  burst's data arrives during the burst. See "Data during the burst" below for the timings.
  A 600S burst in noise band-limited to 300-3300 Hz is found at -2 dB SNR (carrier, then NO
  CARRIER: the 110A receiver's own mode detection fails there, so nothing is decoded), and
  found and decoded exact at 2 dB.
- **Measured, 75 bps** (`appa_lan_75` phase `rx75`, input fed in real time 10 ms at a time,
  4 s of input before the burst, `--rx-fast`): see the table below. Every case delivered the
  60-byte "DE AAM402 ..." message exact, with no carrier on the 4 s of input before the burst
  and no receive audio dropped. -6 dB is white noise in 300-3300 Hz with the SNR over that band.
  The decoder's own reach is lower (ModemRX to -8 dB AWGN for every seed tried,
  docs/m110a-75bps.md); this test does not sweep it.

  | Case | CARRIER DETECTED after the preamble's last sample | Burst end | Bytes |
  |---|---|---|---|
  | 75S clean | 1.09 s | EOM | 60 exact |
  | 75S -6 dB, 2 noise seeds | 1.14 s, 1.09 s | EOM | 60 exact, both |
  | 75L clean | 0.95 s | EOM | 60 exact |
  | 75L -6 dB, 2 noise seeds | 0.99 s, 0.99 s | EOM | 60 exact, both |

  The carrier time is two finder runs 0.5 s apart, plus the wait for the next 0.5 s mark.
  The test allows 2.7 s. When `m75::acquire` ran on every 0.5 s step, the same cases measured
  1.19 to 2.72 s, because the scanner fell behind real time. TCP packets: 75S 5, 6, 5, 6, 6,
  5, 6, 6, 5, 6, 4 bytes; 75L 45, 15.

- **Data during the burst.** Measured with `appa_rx_stream` phase `stream`: a 32 s 600S burst
  (2252 bytes) fed in real time, 10 ms at a time, `--rx-fast`, the host's load average 12.
  Times are against the end of the burst's signal. "Per block" is each data packet against the
  end of its 0.6 s interleaver block on the air:

  | Build | First data | Last data, NO CARRIER | Per block (min / median / max) | Decoder time |
  |---|---|---|---|---|
  | whole-burst decode (origin/master) | +21.05 s | +22.08 s | 22.09 / 36.57 / 51.05 s | 20.4 s |
  | streaming (this build) | -29.28 s | +0.40 s | 0.41 / 0.73 / 1.01 s | 1.5 s |

  A block can go no sooner than one interleaver span after its end (0.6 s at 600S): it needs
  the next block whole, which also covers the Viterbi's 34-bit traceback (0.06 s at 600 bps).
  One span plus the traceback is 0.66 s. The median, 0.73 s, is one span plus the data grid's
  lookahead: the timing-step search settles a frame's grid 8 frames later (0.13 s at 600S),
  which it must wait for to give `decode()`'s bytes. The rest is the decoder's own time under
  load. The last block goes 0.41 s after the end, once its EOM is decoded.
  A 75 bps burst from the 75 bps finder is still decoded whole when it ends.
- **Below the quality gate.** Each block is judged as it decodes. It passes when its
  `block_quality_db` clears the mode's block threshold and the burst's `quality_db` (the first
  1440 data symbols) clears the mode's threshold (docs/m110a-quality-gate.md). The first block
  below the gate ends the delivery. The bytes of the blocks before it have already gone. Its
  own bytes and the rest do not go. **Appendix A has no field to mark received data as suspect,
  or to say a reception was cut short.** The Data Transfer payload is order, packet ID and data
  (Figure A-3). The Carrier State is NO CARRIER or CARRIER DETECTED only (Table A-VI). The UDP
  Error Vector, Status Vector and Control Vector define no such bit, and their reserved bits
  "shall always be 0" (Tables A-XI to A-XV). So the DTE learns of it only as a message that
  ends early. On TCP, the last chunk before the failing block goes as LAST, then NO CARRIER.
  When that chunk has already gone, the reception ends as ABORT_RECEPTION ends one (A.5.1.2.6
  step 6): a LAST packet with no data, then NO CARRIER. A gate that fails on the first block
  sends NO CARRIER with no data packets. On UDP the bytes before the
  block are framed with the EOM, and the set holding it carries the last-set flag. The modem's
  log line ("below the quality gate ... delivery ends after N bytes") says what happened. The
  DTE needs its own integrity check, such as STANAG 5066's D_PDU CRCs. A 5066 node sees a cut
  short as a D_PDU whose CRC fails, or one that never ends, and its ARQ repeats what was lost.
  With `--rx-low-quality` a block below the gate is delivered like any other, and the log line
  "BELOW THE QUALITY GATE, delivered" gives its quality and the thresholds. Bytes refused for
  another reason are not delivered even then: no bytes, or a forced 75 bps decode whose
  preamble names another mode.
- **Blocking factor** (Transmit Setup, Carrier Detect, status): the data rate times the 110A
  interleaver span, 0.6 s short and 4.8 s long. For 600S that is 360 bits, and 45 bytes per TCP
  data packet. Inferred: MIL-STD-188-110 gives the interleaver spans, and Appendix A says only
  that the value "is tied to the interleaver length".

### A blocking factor that is not a whole number of bytes

75S has a blocking factor of 45 bits, 150S 90 bits and 300S 180 bits. What the appendix says:

- the blocking factor is the "chunk size in bits", and data "shall be transferred to the DTE in
  data chunks equal to this value" (A.5.1.1.8; A.5.1.1.9 for transmit; A.5.2.4.1 and A.5.2.4.3
  for UDP);
- "Data is always exchanged in the data packets using 8-bit byte alignment" (A.5.1.2.8.2), and
  the Data Transfer payload carries whole bytes (A.5.1.1.5, Figure A-3);
- the UDP interleaver length rounds up: L = min(ceil(B / (K x 8)), 1200) x N (Figure A-22).

**It does not say how a TCP chunk that ends inside a byte is framed** (Ambiguity 11). Here,
packet i of a reception carries the bytes that end inside chunk i: bytes floor(bf x i / 8) up
to floor(bf x (i + 1) / 8), the last packet taking what is left. So one packet goes per blocking
factor, each carries whole bytes, and the bytes go at the air rate. For 75S the packets carry
5, 6, 5, 6, 6, 5, 6, 6 bytes, 45 bytes per 8 chunks. A whole-byte blocking factor gives bf/8
bytes per packet, as before. UDP follows Figure A-22 as written: 75S packs 3 bytes per packet,
6 user bytes per interleaver set of (6,2).

Before this rule, every packet carried ceil(bf/8) bytes, which ran 150S and 300S above their
air rate (75 bps had no data path then):

| Mode | Blocking factor (bits) | Before: bytes per packet | Before: delivery rate | Now: bytes per packet | Now: delivery rate |
|---|---|---|---|---|---|
| 75S | 45 | (refused) | - | 5 6 5 6 6 5 6 6, repeating | 75 bps |
| 75L | 360 | (refused) | - | 45 | 75 bps |
| 150S | 90 | 12 | 160 bps | 11 11 11 12, repeating | 150 bps |
| 150L | 720 | 90 | 150 bps | 90 | 150 bps |
| 300S | 180 | 23 | 306.7 bps | 22 23, repeating | 300 bps |
| 300L, 600S, 600L, 1200S, 1200L, 2400S, 2400L, 4800S | 1440, 360, 2880, 720, 5760, 1440, 11520, 2880 | bf/8 | the air rate | bf/8, unchanged | the air rate |

The rates are bytes x 8 per chunk time (bf / rate), without `--rx-fast`. Measured
(`appa_lan_75` phase `framing`, packets timed at the DTE): 150S 148.8 bps, 300S 296.6 bps, both
100 and 120 bytes exact after a round trip through the modem. 600S, 1200S, 2400S, 4800S and 600L
delivered packet for packet as the earlier rule gave them. The measured rates sit about 1% under
the air rate because each packet waits for a 20 ms timer tick.

## What is refused, and why

The appendix gives the DTE **no field for choosing a waveform**. Rate and blocking factor go
modem to DTE only (A.5.1.1.9: "user configured"; A.5.1.2.1 item 2). So a waveform without a
data path here is refused where it would be configured, on the command line. `m110_lan` exits
with status 2 and names the supported list. Nothing is ever reported for a waveform it cannot
run.

| Waveform | Why refused |
|---|---|
| 4800L | Not a 110A mode; `ModemTX` maps it to 4800S. |
| 110D (Appendix C/D) waveforms | No 110D transmitter in this repo. See below for receive. |

On the wire, the appendix's own refusal mechanisms are used as written: NACK causes (Table
A-IV), ERROR and close (A.5.1.2.3), ignored UDP packets and commands (A.5.2.4.4, A.5.2.6.3).

## Not implemented

- **The 110D receiver.** `m110d::AppDRx` (`src/m110d/appd_rx.h`) is header-only and could be
  called on each burst. It is left out because nothing here can verify it: the repo has no
  110D transmitter for a loopback, and the 110D interop recordings `docs/110d-appd.md`
  describes are not in the repo. A WID-to-data-rate table for the Rx Data Rate field would
  also be needed.
- **Synchronous socket mode and async standard mode** (A.5.1.2.8, A.5.2.6.8). The 110A path
  takes bytes and returns bytes aligned from the start of the message, which is what DATA ONLY
  MODE describes. That is the only mode offered, and the Initial Setup packet says so. Standard
  mode would put start, stop and parity bits over the air. Synchronous mode makes the DTE align
  bits itself. Neither is built.
- **Transmit underrun** (NACK cause 1, DRAINING_FORCED on TCP, error vector bits 0-3). The
  transmitter is not real time; see above.
- **Configuration change during a connection** (A.5.1.2.4, A.5.2.6.4). Configuration is fixed
  at startup.

## Ambiguities in Appendix A

1. **CRC listing.** A.5.3's `CalculateCRC16` has no `return nCrc;`. The implementation returns
   the register, which is what the text describes. `test_appa_proto` checks the listing against
   the polynomial computed a second way.
2. **The 3200 bps and faster rows of Table A-XVII have N-K = 3**, as does the (7,4) example in
   A.5.2.5.1, but the only generator printed has degree 4. Here n-k = 3 uses roots a^1..a^3.
   This applies only to 4800S data sent modem to DTE over UDP, and to any DTE that sends
   (7,4) or (8,5).
3. **Codeword symbol order.** Table A-XVIII lists parity first ({XXXABCD}). Here position i is
   the coefficient of x^i, so parity is the low-order remainder of the systematic code. The
   appendix does not say which end is high-order. With erasures only, any consistent choice
   decodes, but another vendor's parity bytes would differ if it read this the other way.
4. **Timestamp units.** A.5.2.4.2 says milliseconds, but its code computes
   `(sec % 3600) * 1000000 + usec`, which is microseconds. The code is followed.
5. **Session ID.** "Selected by the initiating device", the DTE, yet "the modem may increment
   this number". Here the modem sends its own random ID until the DTE's first packet, then uses
   the DTE's. A change by the DTE resynchronises.
6. **"Rejected"** (A.4.1) and the **ERROR packet payload** (Table A-I) are not specified. Here
   a rejected connection is closed with no bytes, and ERROR carries no payload.
7. **Table A-XXI's "Interleaver" column** (1 at 50-150 bps, 4 at 600, 15 at 2400 ...) is
   defined nowhere and does not match L/N from Figure A-22 (23 bytes at 600S). The modem
   follows Figure A-22 for what it sends, and accepts any per-packet length the DTE uses.
8. **A.5.1.2.7.2 says "TRANSFER_STARTED command"**, which Table A-II does not define. It is read
   as TRANSMIT_START (0x02).
9. **Data after LAST** in the same transmission has no NACK cause. Here it gets cause 2,
   MISSING FIRST PACKET: a new transmission needs a FIRST.
10. **Table A-XVI "ABORT RECEPTION ... switch to a FLUSHED state"** describes the receiver
    state (Table A-XIII). The receiver returns to FLUSHED and carrier detect 0.
11. **A blocking factor that is not a whole number of bytes** (75S 45 bits, 150S 90, 300S 180).
    A.5.1.1.8 sends data "in data chunks equal to this value" (bits), and A.5.1.2.8.2 keeps the
    data packets byte-aligned. Nothing says how a chunk that ends inside a byte is framed on the
    TCP data socket. Here each chunk's packet carries the bytes that end inside it (see "A
    blocking factor that is not a whole number of bytes"). Another vendor might round each
    chunk up, which runs above the air rate, or pack bits across bytes, which DATA ONLY MODE does
    not describe.
