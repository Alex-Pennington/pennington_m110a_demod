# MS-DMT TCP Protocol Summary

**Version:** MS-DMT v3.00 Beta 2.22 (M110A Implementation)  
**Date:** December 2025  
**Protocol Compliance:** ✅ All 29 command tests pass

---

## Ports

| Port | Protocol | Description |
|------|----------|-------------|
| 4998 | TCP | **Data Port** - Binary message payload (TX/RX) |
| 4999 | TCP | **Control Port** - ASCII commands and status |
| 5000 | UDP | **Discovery** - Broadcasts "helo" for auto-discovery |

---

## Connection Flow

```
1. Connect TCP to localhost:4999 (control)
2. Connect TCP to localhost:4998 (data)
3. Wait for "MODEM READY" on control port
4. Send commands, receive status
```

---

## Control Port Commands

All commands are ASCII, newline-terminated (`\n`).

### CMD:DATA RATE:\<mode\>
Set modem mode before transmission.

| Mode | Rate | Interleave |
|------|------|------------|
| `75S` | 75 bps | Short |
| `75L` | 75 bps | Long |
| `150S` | 150 bps | Short |
| `150L` | 150 bps | Long |
| `300S` | 300 bps | Short |
| `300L` | 300 bps | Long |
| `600S` | 600 bps | Short |
| `600L` | 600 bps | Long |
| `1200S` | 1200 bps | Short |
| `1200L` | 1200 bps | Long |
| `2400S` | 2400 bps | Short |
| `2400L` | 2400 bps | Long |

**Response:** `OK:DATA RATE:<mode>`  
**Error:** `ERROR:DATA RATE:INVALID MODE: <mode>`

---

### CMD:SENDBUFFER
Transmit all data buffered on data port.

**Response sequence:**
```
STATUS:TX:TRANSMIT
STATUS:TX:IDLE
OK:SENDBUFFER:<bytes> bytes FILE:<pcm_path>
```

**Note:** When recording is enabled, the response includes the PCM file path.

**Empty buffer:** `OK:SENDBUFFER:EMPTY`

---

### CMD:KILL TX
Abort ongoing transmission immediately.

**Response:**
```
STATUS:TX:IDLE
OK:KILL TX
```

---

### CMD:RECORD TX:ON / CMD:RECORD TX:OFF
Enable/disable TX audio recording to PCM files.

**Response:** `OK:RECORD TX:ON` or `OK:RECORD TX:OFF`

**Output directory:** `./tx_pcm_out/`

---

### CMD:RECORD PREFIX:\<prefix\>
Set filename prefix for recorded PCM files.

**Example:** `CMD:RECORD PREFIX:test_600S`  
**Response:** `OK:RECORD PREFIX:test_600S`  
**Filename:** `./tx_pcm_out/test_600S_20251207_143022_123.pcm`

---

### CMD:RXAUDIOINJECT:\<filepath\>
Inject PCM file into RX modem for decoding (testing).

**Example:** `CMD:RXAUDIOINJECT:./tx_pcm_out/test.pcm`

**Response sequence:**
```
OK:RXAUDIOINJECT:STARTED:<filepath>
STATUS:RX:<mode>           (e.g., STATUS:RX:600 BPS SHORT)
... (decoded data on port 4998) ...
STATUS:RX:NO DCD
OK:RXAUDIOINJECT:COMPLETE:<sample_count> samples
```

**With channel simulation enabled:**
```
OK:RXAUDIOINJECT:COMPLETE:<sample_count> samples (channel sim applied)
```

**Errors:**
- `ERROR:RXAUDIOINJECT:FILE NOT FOUND:<path>`

---

## Channel Simulation Commands (Testing Extension)

These commands enable channel impairment simulation for RX testing.  
Impairments are applied during `CMD:RXAUDIOINJECT` operations.

### CMD:CHANNEL CONFIG
Display current channel simulation configuration.

**Response:**
```
OK:CHANNEL CONFIG:CHANNEL CONFIG:
  Enabled: YES/NO
  AWGN: ON/OFF (SNR=<db>dB)
  Multipath: ON/OFF (delay=<samples> samples, gain=<value>)
  FreqOffset: ON/OFF (<hz>Hz)
```

---

### CMD:CHANNEL PRESET:\<preset\>
Apply a predefined channel configuration.

| Preset | SNR | Multipath | Freq Offset | Description |
|--------|-----|-----------|-------------|-------------|
| `GOOD` / `GOOD_HF` | 25 dB | 12 samples, 0.3 | 2 Hz | Good HF conditions |
| `MODERATE` / `MODERATE_HF` | 18 dB | 24 samples, 0.4 | 5 Hz | Moderate HF |
| `POOR` / `POOR_HF` | 15 dB | 48 samples, 0.5 | NONE* | Poor HF (1 Hz fading) |
| `CCIR_GOOD` | 20 dB | 6 samples, 0.2 | 1 Hz | CCIR good channel |
| `CCIR_MODERATE` | 15 dB | 12 samples, 0.35 | 3 Hz | CCIR moderate |
| `CCIR_POOR` | 10 dB | 24 samples, 0.5 | 5 Hz | CCIR poor channel |
| `CLEAN` / `OFF` | - | - | - | No impairments |

\* *Note: POOR_HF has no frequency offset due to AFC limitations. Frequency offsets ≥3 Hz cause decode failure.*

**Example:** `CMD:CHANNEL PRESET:MODERATE`  
**Response:** `OK:CHANNEL PRESET:MODERATE_HF (SNR=18dB, multipath=24/0.4, freq_offset=5Hz)`

---

### CMD:CHANNEL AWGN:\<snr_db\>
Enable AWGN (Additive White Gaussian Noise) with specified SNR.

**Example:** `CMD:CHANNEL AWGN:20`  
**Response:** `OK:CHANNEL AWGN:Enabled with SNR=20dB`

---

### CMD:CHANNEL MULTIPATH:\<delay_samples\>
Enable multipath simulation with specified delay.

**Example:** `CMD:CHANNEL MULTIPATH:24`  
**Response:** `OK:CHANNEL MULTIPATH:Enabled with delay=24 samples, gain=0.5`

**Optional gain:** `CMD:CHANNEL MULTIPATH:24,0.3` sets delay=24, gain=0.3

---

### CMD:CHANNEL FREQOFFSET:\<hz\>
Enable frequency offset simulation.

**Example:** `CMD:CHANNEL FREQOFFSET:5`  
**Response:** `OK:CHANNEL FREQOFFSET:Enabled with offset=5Hz`

---

### CMD:CHANNEL OFF
Disable all channel impairments.

**Response:** `OK:CHANNEL OFF:All channel impairments disabled`

---

## Status Messages (Async)

MS-DMT sends these asynchronously on the control port:

| Message | Meaning |
|---------|---------|
| `MODEM READY` | Modem initialized, ready for commands |
| `STATUS:TX:TRANSMIT` | Transmission started |
| `STATUS:TX:IDLE` | Transmission complete |
| `STATUS:RX:<mode>` | DCD acquired, mode detected |
| `STATUS:RX:NO DCD` | Signal lost / end of message |

**RX mode format:** `<rate> BPS <interleave>`  
Examples: `600 BPS SHORT`, `2400 BPS LONG`, `75 BPS SHORT`

---

## Data Port (4998)

- **Binary** - no framing, no line terminators
- **TX:** Send raw bytes, then `CMD:SENDBUFFER` on control port
- **RX:** Read decoded bytes when `STATUS:RX:<mode>` received

---

## PCM File Format

| Parameter | Value |
|-----------|-------|
| Sample Rate | 48000 Hz |
| Bit Depth | 16-bit signed |
| Byte Order | Little-endian |
| Channels | Mono |
| Format | Raw PCM (no header) |

**Duration calculation:** `seconds = file_bytes / 96000`

---

## Command-Line Flags

| Flag | Description |
|------|-------------|
| `--testdevices` | Mock audio/serial devices (no hardware) |
| `--autotest` | Auto-test mode with real devices |
| `rlba` | Bypass VOX test |

**For testing:** `m110a_server.exe --testdevices`

---

## Quick Examples

### Transmit Message
```
→ [CTRL] CMD:DATA RATE:600S
← [CTRL] OK:DATA RATE:600S
→ [DATA] <binary message bytes>
→ [CTRL] CMD:SENDBUFFER
← [CTRL] STATUS:TX:TRANSMIT
← [CTRL] STATUS:TX:IDLE
← [CTRL] OK:SENDBUFFER:42 bytes FILE:./tx_pcm_out/test.pcm
```

### Receive via PCM Injection
```
→ [CTRL] CMD:RXAUDIOINJECT:./tx_pcm_out/test.pcm
← [CTRL] OK:RXAUDIOINJECT:STARTED:./tx_pcm_out/test.pcm
← [CTRL] STATUS:RX:600 BPS SHORT
← [DATA] <decoded binary bytes>
← [CTRL] STATUS:RX:NO DCD
← [CTRL] OK:RXAUDIOINJECT:COMPLETE:105600 samples
```

### Record TX to PCM
```
→ [CTRL] CMD:RECORD TX:ON
← [CTRL] OK:RECORD TX:ON
→ [CTRL] CMD:RECORD PREFIX:mytest
← [CTRL] OK:RECORD PREFIX:mytest
→ [DATA] <message bytes>
→ [CTRL] CMD:SENDBUFFER
← [CTRL] STATUS:TX:TRANSMIT
← [CTRL] STATUS:TX:IDLE
← [CTRL] OK:SENDBUFFER:128 bytes FILE:./tx_pcm_out/mytest_20251207_143022_123.pcm
```

### Loopback Test with Channel Simulation
```
→ [CTRL] CMD:DATA RATE:2400S
← [CTRL] OK:DATA RATE:2400S
→ [CTRL] CMD:RECORD TX:ON
← [CTRL] OK:RECORD TX:ON
→ [DATA] Hello World
→ [CTRL] CMD:SENDBUFFER
← [CTRL] STATUS:TX:TRANSMIT
← [CTRL] STATUS:TX:IDLE
← [CTRL] OK:SENDBUFFER:11 bytes FILE:./tx_pcm_out/..._20251207_200000_000.pcm

→ [CTRL] CMD:CHANNEL PRESET:MODERATE
← [CTRL] OK:CHANNEL PRESET:MODERATE_HF (SNR=18dB, ...)

→ [CTRL] CMD:RXAUDIOINJECT:./tx_pcm_out/..._20251207_200000_000.pcm
← [CTRL] OK:RXAUDIOINJECT:STARTED:...
← [CTRL] STATUS:RX:2400 BPS SHORT
← [DATA] Hello World
← [CTRL] STATUS:RX:NO DCD
← [CTRL] OK:RXAUDIOINJECT:COMPLETE:61440 samples (channel sim applied)

→ [CTRL] CMD:CHANNEL OFF
← [CTRL] OK:CHANNEL OFF:All channel impairments disabled
```

---

## Response Format Summary

| Type | Format |
|------|--------|
| Success | `OK:<command>:<details>` |
| Error | `ERROR:<command>:<details>` |
| Status | `STATUS:<category>:<details>` |
| Ready | `MODEM READY` |

---

## Command Summary Table

| Command | Description |
|---------|-------------|
| `CMD:DATA RATE:<mode>` | Set data rate (75S/L to 2400S/L) |
| `CMD:SENDBUFFER` | Transmit buffered data |
| `CMD:KILL TX` | Abort transmission |
| `CMD:RECORD TX:ON/OFF` | Enable/disable TX recording |
| `CMD:RECORD PREFIX:<prefix>` | Set recording filename prefix |
| `CMD:RXAUDIOINJECT:<path>` | Inject PCM for RX decode |
| `CMD:CHANNEL CONFIG` | Show channel simulation config |
| `CMD:CHANNEL PRESET:<name>` | Apply channel preset |
| `CMD:CHANNEL AWGN:<snr>` | Enable AWGN noise |
| `CMD:CHANNEL MULTIPATH:<delay>` | Enable multipath |
| `CMD:CHANNEL FREQOFFSET:<hz>` | Enable frequency offset |
| `CMD:CHANNEL OFF` | Disable all impairments |

---

## Test Programs

| Program | Description |
|---------|-------------|
| `test_all_commands.exe` | Protocol compliance test (29 tests) |
| `test_loopback.exe` | TX→PCM→RX loopback verification |
| `exhaustive_server_test.exe` | Multi-mode/channel exhaustive test |
| `test_channel_cmds.exe` | Channel simulation command test |

**Run server for testing:** `m110a_server.exe --testdevices`
