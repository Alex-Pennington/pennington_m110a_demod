# MS-DMT TCP Protocol Summary

**Version:** MS-DMT v3.00 Beta 2.22  
**Date:** December 2025

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

---

### CMD:SENDBUFFER
Transmit all data buffered on data port.

**Response:**
```
STATUS:TX:TRANSMIT
... (transmission) ...
STATUS:TX:IDLE
```

---

### CMD:KILL TX
Abort ongoing transmission immediately.

**Response:** (transmission stops)

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

**Example:** `CMD:RXAUDIOINJECT:D:\recordings\test.pcm`

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
- `ERROR:RXAUDIOINJECT:CANNOT OPEN:<path>`

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

**For testing:** `MS-DMT.exe --testdevices`

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
```

### Receive via PCM Injection
```
→ [CTRL] CMD:RXAUDIOINJECT:C:\test.pcm
← [CTRL] OK:RXAUDIOINJECT:STARTED:C:\test.pcm
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
(PCM saved to ./tx_pcm_out/mytest_*.pcm)
```

---

## Response Format Summary

| Type | Format |
|------|--------|
| Success | `OK:<command>:<details>` |
| Error | `ERROR:<command>:<details>` |
| Status | `STATUS:<category>:<details>` |
| Ready | `MODEM READY` |
