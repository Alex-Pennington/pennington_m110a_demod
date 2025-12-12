# MIL-STD-188-110A TCP Protocol Reference

**Version:** tcp_server_base v1.1.0  
**Date:** December 2025  
**Servers:** Phoenix Nest, Brain Core (both use tcp_server_base)

---

## Server Comparison

| Property | Phoenix Nest | Brain Core |
|----------|-------------|------------|
| Control Port | 4999 | 3999 |
| Data Port | 4998 | 3998 |
| Sample Rate | 48000 Hz | 9600 Hz |
| Ready Message | `MODEM READY` | `READY:G4GUO Core (tcp_base)` |
| Executable | `m110a_server.exe` | `brain_tcp_server.exe` |

**Cross-modem testing:** When feeding Phoenix Nest output to Brain Core, decimate 5:1 (48000→9600).

---

## Connection Flow

```
1. Connect TCP to control port (PN:4999 or BC:3999)
2. Connect TCP to data port (PN:4998 or BC:3998)
3. Wait for READY message (contains "READY")
4. Send commands with CMD: prefix, receive responses
5. Keep connections open for entire session (persistent connections)
```

**IMPORTANT:** Both ports must be connected. Commands go to control port, binary data to data port.

---

## Control Port Commands

All commands are ASCII, newline-terminated (`\n`).  
**All commands MUST start with `CMD:` prefix.**

### CMD:DATA RATE:\<mode\>
Set modem mode for TX/RX.

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

**Example:** `CMD:DATA RATE:600S`  
**Response:** `OK:DATA RATE:600 BPS SHORT`  
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

**Phoenix Nest Response:** `OK:KILL TX`  
**Brain Core Response:** `OK:TX KILLED`

---

### CMD:RESET MDM
Reset modem state, clear TX buffer.

**Response:** `OK:RESET`

---

### CMD:RECORD TX:ON / CMD:RECORD TX:OFF
Enable/disable TX audio recording to PCM files.

**Response:** `OK:RECORD TX:ON` or `OK:RECORD TX:OFF`

**Output directory:** `./tx_pcm_out/`

---

### CMD:RECORD PREFIX:\<prefix\>
Set filename prefix for recorded PCM files.

**Example:** `CMD:RECORD PREFIX:test_600S`  
**Phoenix Nest Response:** `OK:RECORD PREFIX:test_600S`  
**Brain Core Response:** `OK:PREFIX:test_600S`  
**Filename:** `./tx_pcm_out/test_600S_20251207_143022_123.pcm`

---

### CMD:RXAUDIOINJECT:\<filepath\>
Inject PCM file into RX modem for decoding (testing).

**Example:** `CMD:RXAUDIOINJECT:./tx_pcm_out/test.pcm`

**Response sequence:**
```
OK:RXAUDIOINJECT:STARTED:<filepath>
STATUS:RX:<mode>           (e.g., STATUS:RX:600 BPS SHORT)
... (decoded data on data port) ...
STATUS:RX:NO DCD
OK:RXAUDIOINJECT:COMPLETE:<sample_count> samples
```

**Errors:**
- `ERROR:RXAUDIOINJECT:FILE NOT FOUND:<path>`

---

### CMD:SET EQUALIZER:\<type\> (Phoenix Nest only)
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
**Error:** `ERROR:SET EQUALIZER:UNKNOWN: <type>`

---

## Query Commands

### CMD:QUERY:STATUS
Get current modem status.

**Response:** `STATUS:IDLE TX_MODE:<mode> TX_BUF:<bytes>`

### CMD:QUERY:MODES
Get list of supported modes.

**Response:** `MODES:75S,75L,150S,150L,300S,300L,600S,600L,1200S,1200L,2400S,2400L`

### CMD:QUERY:VERSION
Get server version.

**Phoenix Nest Response:** `VERSION:<m110a version>`  
**Brain Core Response:** `VERSION:v1.1.0-tcp_base`

### CMD:QUERY:HELP
Get list of available commands.

**Response:** `COMMANDS:DATA RATE,SENDBUFFER,RESET MDM,KILL TX,RECORD TX:ON/OFF,RECORD PREFIX,RXAUDIOINJECT,QUERY:*`

### CMD:QUERY:PCM OUTPUT (Brain Core only)
Get PCM output directory.

**Response:** `PCM OUTPUT:./tx_pcm_out/`

---

## Status Messages (Async)

Servers send these asynchronously on the control port:

| Message | Meaning |
|---------|---------|
| `MODEM READY` | Phoenix Nest ready |
| `READY:G4GUO Core (tcp_base)` | Brain Core ready |
| `STATUS:TX:TRANSMIT` | Transmission started |
| `STATUS:TX:IDLE` | Transmission complete |
| `STATUS:RX:<mode>` | DCD acquired, mode detected |
| `STATUS:RX:NO DCD` | Signal lost / end of message |

**Note:** To detect ready state, check if response contains "READY".

**RX mode format:** `<rate> BPS <interleave>`  
Examples: `600 BPS SHORT`, `2400 BPS LONG`, `75 BPS SHORT`

---

## Data Port

- **Binary** - no framing, no line terminators
- **TX:** Send raw bytes, then `CMD:SENDBUFFER` on control port
- **RX:** Read decoded bytes when `STATUS:RX:<mode>` received

---

## PCM File Format

| Parameter | Phoenix Nest | Brain Core |
|-----------|-------------|------------|
| Sample Rate | 48000 Hz | 9600 Hz |
| Bit Depth | 16-bit signed | 16-bit signed |
| Byte Order | Little-endian | Little-endian |
| Channels | Mono | Mono |
| Format | Raw PCM (no header) | Raw PCM (no header) |

**Duration calculation:**  
- Phoenix Nest: `seconds = file_bytes / 96000`  
- Brain Core: `seconds = file_bytes / 19200`

---

## Command-Line Flags

**Phoenix Nest (`m110a_server.exe`):**
| Flag | Description |
|------|-------------|
| `--testdevices` | Mock audio/serial devices (no hardware) |
| `--autotest` | Auto-test mode with real devices |
| `rlba` | Bypass VOX test |

**Brain Core (`brain_tcp_server.exe`):**
| Flag | Description |
|------|-------------|
| (none) | Starts with defaults |

**For testing:** Run servers in separate terminal windows (not VS Code terminals).

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
| Error | `ERROR:<command>:<details>` or `ERROR:UNKNOWN COMMAND` |
| Status | `STATUS:<category>:<details>` |
| Ready (PN) | `MODEM READY` |
| Ready (BC) | `READY:G4GUO Core (tcp_base)` |

**Error handling:** Commands without `CMD:` prefix return `ERROR:INVALID:Must start with CMD:` (Phoenix Nest).

---

## Command Summary Table

| Command | Description | PN | BC |
|---------|-------------|:--:|:--:|
| `CMD:DATA RATE:<mode>` | Set data rate (75S/L to 2400S/L) | ✅ | ✅ |
| `CMD:SENDBUFFER` | Transmit buffered data | ✅ | ✅ |
| `CMD:KILL TX` | Abort transmission | ✅ | ✅ |
| `CMD:RESET MDM` | Reset modem state | ✅ | ✅ |
| `CMD:RECORD TX:ON/OFF` | Enable/disable TX recording | ✅ | ✅ |
| `CMD:RECORD PREFIX:<prefix>` | Set recording filename prefix | ✅ | ✅ |
| `CMD:RXAUDIOINJECT:<path>` | Inject PCM for RX decode | ✅ | ✅ |
| `CMD:SET EQUALIZER:<type>` | Set equalizer type | ✅ | ❌ |
| `CMD:QUERY:STATUS` | Get modem status | ✅ | ✅ |
| `CMD:QUERY:MODES` | Get supported modes | ✅ | ✅ |
| `CMD:QUERY:VERSION` | Get version | ✅ | ✅ |
| `CMD:QUERY:HELP` | Get command list | ✅ | ✅ |
| `CMD:QUERY:PCM OUTPUT` | Get PCM output dir | ❌ | ✅ |

**Note:** Channel simulation commands (CHANNEL CONFIG, CHANNEL PRESET, etc.) may be available in Phoenix Nest but not currently in tcp_server_base.

---

## Test Programs

| Program | Description |
|---------|-------------|
| `test_client.exe` | Simple C++ test client for Phoenix Nest |
| `test_pn_to_bc.exe` | Cross-modem test: PN TX → BC RX |

**Location:** `testing/interop/`  
**Build:** `g++ -o test_client.exe test_client.cpp -lws2_32`

---

## Cross-Modem Testing

When testing Phoenix Nest TX → Brain Core RX:
1. Phoenix Nest outputs 48000 Hz PCM
2. Brain Core expects 9600 Hz PCM
3. **Decimate 5:1** before feeding to Brain Core

```cpp
// Simple 5:1 decimation (average every 5 samples)
for (int i = 0; i + 5 <= pn_samples.size(); i += 5) {
    int32_t sum = 0;
    for (int j = 0; j < 5; j++) sum += pn_samples[i + j];
    bc_samples.push_back(sum / 5);
}
```
