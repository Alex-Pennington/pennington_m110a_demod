# MS-DMT TCP/IP Protocol for Interoperability Testing

## Network Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        MS-DMT                               │
│                                                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐ │
│  │ Data Port   │  │ Control Port│  │ Discovery (UDP)     │ │
│  │ TCP 4998    │  │ TCP 4999    │  │ UDP 5000            │ │
│  │             │  │             │  │                     │ │
│  │ Binary data │  │ Commands &  │  │ Broadcasts "helo"   │ │
│  │ TX/RX       │  │ Status msgs │  │ for auto-discovery  │ │
│  └─────────────┘  └─────────────┘  └─────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

| Port | Protocol | Purpose |
|------|----------|---------|
| **4998** | TCP | **Data Port** - Raw binary message bytes (TX and RX) |
| **4999** | TCP | **Control Port** - ASCII commands and status messages |
| **5000** | UDP | **Discovery** - Broadcasts "helo" datagrams |

---

## Connection Sequence

```python
# 1. Connect to control port
ctrl_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
ctrl_sock.connect(('localhost', 4999))

# 2. Connect to data port
data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
data_sock.connect(('localhost', 4998))

# 3. Wait for ready signal on control port
response = ctrl_sock.recv(1024)
# Look for: "MODEM READY"
```

---

## Control Port Commands (Port 4999)

All commands are ASCII, newline-terminated (`\n`).

### Set Data Rate
```
CMD:DATA RATE:<mode>
```

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

### Transmit Buffer
```
CMD:SENDBUFFER
```
Transmits all data buffered on data port 4998.

**Response sequence:**
```
STATUS:TX:TRANSMIT
... (transmission occurs) ...
STATUS:TX:IDLE
```

### Kill Transmission
```
CMD:KILL TX
```
Aborts ongoing transmission immediately.

### Enable TX Recording
```
CMD:RECORD TX:ON
CMD:RECORD TX:OFF
```
Records TX audio to PCM files in `./tx_pcm_out/`

**Response:** `OK:RECORD TX:ON` or `OK:RECORD TX:OFF`

### Set Recording Prefix
```
CMD:RECORD PREFIX:<prefix>
```
Sets filename prefix for recorded PCM files.

**Response:** `OK:RECORD PREFIX:<prefix>`
**Output:** `./tx_pcm_out/<prefix>_YYYYMMDD_HHMMSS_mmm.pcm`

### RX Audio Injection (KEY FOR TESTING!)
```
CMD:RXAUDIOINJECT:<filepath>
```
Injects a PCM audio file directly into the RX modem for decoding. **This is the primary mechanism for interoperability testing.**

**Response sequence:**
```
OK:RXAUDIOINJECT:STARTED:<filepath>
STATUS:RX:<mode>           # e.g., "STATUS:RX:600 BPS SHORT"
... (decoded data appears on port 4998) ...
STATUS:RX:NO DCD
OK:RXAUDIOINJECT:COMPLETE:<sample_count> samples
```

**Errors:**
```
ERROR:RXAUDIOINJECT:FILE NOT FOUND:<path>
ERROR:RXAUDIOINJECT:CANNOT OPEN:<path>
```

---

## Status Messages (Async from MS-DMT)

| Message | Meaning |
|---------|---------|
| `MODEM READY` | Modem initialized and ready |
| `STATUS:TX:TRANSMIT` | Transmission started |
| `STATUS:TX:IDLE` | Transmission complete |
| `STATUS:RX:<mode>` | DCD acquired, mode detected |
| `STATUS:RX:NO DCD` | Signal lost / end of message |

**RX mode format:** `<rate> BPS <interleave>`
Examples: `600 BPS SHORT`, `2400 BPS LONG`, `75 BPS SHORT`

---

## Data Port (4998) - Binary

- **No framing** - raw bytes only
- **No line terminators** - pure binary stream
- **TX:** Send bytes → `CMD:SENDBUFFER` triggers transmission
- **RX:** Decoded bytes appear after `STATUS:RX:<mode>`

---

## PCM Audio File Format

| Parameter | Value |
|-----------|-------|
| **Sample Rate** | 48000 Hz |
| **Bit Depth** | 16-bit signed integer |
| **Byte Order** | Little-endian |
| **Channels** | Mono (1 channel) |
| **Format** | Raw PCM (no WAV header) |

**Duration:** `seconds = file_bytes / 96000`

---

## Interoperability Testing Scenarios

### Scenario 1: MS-DMT TX → PhoenixNest RX (Validate MS-DMT TX)

```
┌──────────────┐    PCM File    ┌──────────────────┐
│   MS-DMT     │ ─────────────→ │  PhoenixNest     │
│   (TX)       │                │  (RX/Decode)     │
└──────────────┘                └──────────────────┘
```

**Steps:**
```
# 1. Start MS-DMT: MS-DMT.exe --testdevices
# 2. Connect to ports 4998 and 4999

[CTRL] → CMD:DATA RATE:600S
[CTRL] ← OK:DATA RATE:600S
[CTRL] → CMD:RECORD TX:ON
[CTRL] ← OK:RECORD TX:ON
[CTRL] → CMD:RECORD PREFIX:phoenix_test
[CTRL] ← OK:RECORD PREFIX:phoenix_test
[DATA] → <binary test message>
[CTRL] → CMD:SENDBUFFER
[CTRL] ← STATUS:TX:TRANSMIT
[CTRL] ← STATUS:TX:IDLE

# 3. PCM file saved to: ./tx_pcm_out/phoenix_test_*.pcm
# 4. Feed this PCM to PhoenixNest RX
# 5. Compare decoded output to original message
```

### Scenario 2: PhoenixNest TX → MS-DMT RX (Validate PhoenixNest TX)

```
┌──────────────────┐    PCM File    ┌──────────────┐
│  PhoenixNest     │ ─────────────→ │   MS-DMT     │
│  (TX/Generate)   │                │   (RX)       │
└──────────────────┘                └──────────────┘
```

**Steps:**
```
# 1. Generate PCM with PhoenixNest (48kHz, 16-bit LE, mono)
# 2. Start MS-DMT: MS-DMT.exe --testdevices
# 3. Connect to ports 4998 and 4999

[CTRL] ← MODEM READY
[CTRL] → CMD:RXAUDIOINJECT:C:\path\to\phoenix_tx.pcm
[CTRL] ← OK:RXAUDIOINJECT:STARTED:C:\path\to\phoenix_tx.pcm
[CTRL] ← STATUS:RX:600 BPS SHORT      # Mode detected!
[DATA] ← <decoded binary bytes>        # Data appears here
[CTRL] ← STATUS:RX:NO DCD             # End of signal
[CTRL] ← OK:RXAUDIOINJECT:COMPLETE:105600 samples

# 4. Compare decoded data to original message
```

### Scenario 3: Full Loopback via PCM (MS-DMT TX → MS-DMT RX)

**MS-DMT doesn't have internal loopback**, but you can achieve the same result:

```
TX → Record PCM → Inject PCM → RX → Compare
```

```
# 1. Generate TX audio
[CTRL] → CMD:DATA RATE:600S
[CTRL] → CMD:RECORD TX:ON
[CTRL] → CMD:RECORD PREFIX:loopback_test
[DATA] → HELLO WORLD
[CTRL] → CMD:SENDBUFFER
[CTRL] ← STATUS:TX:IDLE

# 2. Inject the recorded file back into RX
[CTRL] → CMD:RXAUDIOINJECT:./tx_pcm_out/loopback_test_*.pcm
[CTRL] ← STATUS:RX:600 BPS SHORT
[DATA] ← HELLO WORLD
[CTRL] ← STATUS:RX:NO DCD

# 3. Verify decoded matches original
```

---

## Full Compatibility Test Matrix

Test all 12 modes in both directions:

```python
modes = ['75S', '75L', '150S', '150L', '300S', '300L', 
         '600S', '600L', '1200S', '1200L', '2400S', '2400L']

for mode in modes:
    # Test 1: MS-DMT TX → PhoenixNest RX
    msdmt_tx_pcm = generate_msdmt_tx(mode, test_message)
    phoenix_decoded = phoenix_decode(msdmt_tx_pcm)
    assert phoenix_decoded == test_message
    
    # Test 2: PhoenixNest TX → MS-DMT RX  
    phoenix_tx_pcm = phoenix_generate(mode, test_message)
    msdmt_decoded = inject_and_decode(phoenix_tx_pcm)
    assert msdmt_decoded == test_message
```

---

## Quick Command Reference

### Minimal TX Sequence
```
1. Connect ports 4998/4999
2. Wait for "MODEM READY"
3. CMD:DATA RATE:600S
4. CMD:RECORD TX:ON
5. Send data to port 4998
6. CMD:SENDBUFFER
7. Wait for STATUS:TX:IDLE
8. Collect PCM from ./tx_pcm_out/
```

### Minimal RX Test Sequence
```
1. Connect ports 4998/4999
2. Wait for "MODEM READY"
3. CMD:RXAUDIOINJECT:<pcm_path>
4. Wait for STATUS:RX:<mode>
5. Read decoded data from port 4998
6. Wait for STATUS:RX:NO DCD
```

---

## Launch Command for Testing

```powershell
MS-DMT.exe --testdevices
```

This uses mock audio/serial devices - no hardware required. Perfect for automated interoperability testing.
