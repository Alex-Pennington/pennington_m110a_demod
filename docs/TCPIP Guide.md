# MS-DMT TCP Protocol Summary

**Version:** MS-DMT v3.00 Beta 2.22 (M110A Implementation)  
**Date:** December 2025  
**Protocol Compliance:** ✅ All 57 command tests pass

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

**Errors:**
- `ERROR:RXAUDIOINJECT:FILE NOT FOUND:<path>`

---

### CMD:SET EQUALIZER:\<type\>
Set the equalizer type used during RX demodulation.

| Type | Description |
|------|-------------|
| `NONE` | No equalization |
| `DFE` | Decision Feedback Equalizer (default) |
| `DFE_RLS` | DFE with Recursive Least Squares adaptation |
| `MLSE_L2` | Maximum Likelihood Sequence Estimation (L=2) |
| `MLSE_L3` | MLSE with longer constraint length (L=3) |
| `MLSE_ADAPTIVE` | Adaptive MLSE |
| `TURBO` | Turbo equalizer |

**Example:** `CMD:SET EQUALIZER:DFE`  
**Response:** `OK:SET EQUALIZER:DFE`  
**Error:** `ERROR:SET EQUALIZER:UNKNOWN: <type> (valid: NONE, DFE, DFE_RLS, MLSE_L2, MLSE_L3, MLSE_ADAPTIVE, TURBO)`

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
| `CMD:SET EQUALIZER:<type>` | Set equalizer (NONE/DFE/DFE_RLS/MLSE_L2/MLSE_L3/MLSE_ADAPTIVE/TURBO) |
| `CMD:CHANNEL CONFIG` | Show channel simulation config |
| `CMD:CHANNEL PRESET:<name>` | Apply channel preset |
| `CMD:CHANNEL AWGN:<snr>` | Enable AWGN noise |
| `CMD:CHANNEL MULTIPATH:<delay>[,<gain>]` | Enable multipath |
| `CMD:CHANNEL FREQOFFSET:<hz>` | Enable frequency offset |
| `CMD:CHANNEL OFF` | Disable all impairments |
| `CMD:RUN BERTEST:<input>[,<output>]` | Apply channel impairments to PCM file |

---

## Test Programs

| Program | Description |
|---------|-------------|
| `test_all_commands.exe` | Protocol compliance test (57 tests) |
| `test_loopback.exe` | TX→PCM→RX loopback verification |
| `exhaustive_test.exe` | Multi-mode/channel exhaustive test |
| `test_channel_cmds.exe` | Channel simulation command test |

**Run server for testing:** `m110a_server.exe --testdevices`
