# MS-DMT Integration Guide

## Overview

MS-DMT (MIL-STD Data Modem Terminal) is a software modem implementing the MIL-STD-188-110A PSK protocol for HF radio communications. This guide explains how external applications can interface with MS-DMT for testing and integration.

---

## Network Architecture

MS-DMT exposes three network interfaces:

```
┌─────────────────────────────────────────────────────────────┐
│                        MS-DMT                               │
│                                                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐ │
│  │ Data Port   │  │ Control Port│  │ Discovery (UDP)     │ │
│  │ TCP 4998    │  │ TCP 4999    │  │ UDP 5000            │ │
│  │             │  │             │  │                     │ │
│  │ Binary data │  │ Commands &  │  │ Broadcasts "helo"   │ │
│  │ in/out      │  │ Status msgs │  │ for auto-discovery  │ │
│  └─────────────┘  └─────────────┘  └─────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

| Port | Protocol | Purpose |
|------|----------|---------|
| **4998** | TCP | Data port - raw message bytes (TX and RX) |
| **4999** | TCP | Control port - commands and status messages |
| **5000** | UDP | Discovery - broadcasts "helo" datagrams |

---

## Connection Sequence

### 1. Discovery (Optional)

MS-DMT broadcasts UDP datagrams on port 5000 to announce its presence:

```
UDP Broadcast → Port 5000
Payload: "helo"
```

Your application can listen for these to auto-discover MS-DMT instances on the network.

### 2. TCP Connection

Connect to both TCP ports:

```python
import socket

# Connect to control port first
ctrl_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
ctrl_sock.connect(('localhost', 4999))

# Connect to data port
data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
data_sock.connect(('localhost', 4998))
```

### 3. Wait for Ready

After connecting, wait for the `MODEM READY` message on the control port:

```python
response = ctrl_sock.recv(1024)
if b'MODEM READY' in response:
    print("Modem is ready for commands")
```

---

## Data Port (TCP 4998)

The data port carries raw message bytes - this is where your actual message content flows.

### Sending Data (TX)

Send raw bytes to the data port. MS-DMT buffers them until you send `CMD:SENDBUFFER`:

```python
# Send message bytes
message = b'THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG'
data_sock.send(message)
```

### Receiving Data (RX)

Decoded bytes from received signals appear on the data port:

```python
# Read decoded data (blocking)
data_sock.settimeout(10.0)
received = data_sock.recv(4096)
print(f"Received: {received}")
```

**Important:** Data port is binary - no line terminators or framing. You receive exactly what was transmitted.

---

## Control Port (TCP 4999)

The control port handles commands and status messages. All messages are ASCII text, newline-terminated.

### Message Format

```
Commands (to MS-DMT):    CMD:<command>:<parameters>\n
Status (from MS-DMT):    STATUS:<category>:<details>\n
Responses (from MS-DMT): OK:<command>:<details>\n
                         ERROR:<command>:<details>\n
```

### Command Reference

#### Set Data Rate
```
CMD:DATA RATE:<mode>
```
| Mode | Description |
|------|-------------|
| `75S` | 75 bps, short interleave |
| `75L` | 75 bps, long interleave |
| `150S` | 150 bps, short interleave |
| `150L` | 150 bps, long interleave |
| `300S` | 300 bps, short interleave |
| `300L` | 300 bps, long interleave |
| `600S` | 600 bps, short interleave |
| `600L` | 600 bps, long interleave |
| `1200S` | 1200 bps, short interleave |
| `1200L` | 1200 bps, long interleave |
| `2400S` | 2400 bps, short interleave |
| `2400L` | 2400 bps, long interleave |

**Example:**
```
Send:    CMD:DATA RATE:600S
Receive: OK:DATA RATE:600S
```

#### Transmit Buffered Data
```
CMD:SENDBUFFER
```
Triggers transmission of all data buffered on the data port.

**Example:**
```
Send:    CMD:SENDBUFFER
Receive: STATUS:TX:TRANSMIT
         ... (transmission occurs) ...
Receive: STATUS:TX:IDLE
```

#### Enable/Disable TX Recording
```
CMD:RECORD TX:ON
CMD:RECORD TX:OFF
```
Records transmitted audio to PCM files in `./tx_pcm_out/` directory.

**Example:**
```
Send:    CMD:RECORD TX:ON
Receive: OK:RECORD TX:ON
```

#### Set Recording Filename Prefix
```
CMD:RECORD PREFIX:<prefix>
```
Sets a prefix for recorded PCM filenames (e.g., mode name).

**Example:**
```
Send:    CMD:RECORD PREFIX:test_600S
Receive: OK:RECORD PREFIX:test_600S
```
Files saved as: `./tx_pcm_out/test_600S_20251207_143022_123.pcm`

#### Inject RX Audio (Testing)
```
CMD:RXAUDIOINJECT:<filepath>
```
Feeds a PCM file into the RX modem for decoding. Used for testing without live radio signals.

**Example:**
```
Send:    CMD:RXAUDIOINJECT:D:\recordings\test.pcm
Receive: OK:RXAUDIOINJECT:STARTED:D:\recordings\test.pcm
         STATUS:RX:600 BPS SHORT
         ... (decoded data appears on data port) ...
         STATUS:RX:NO DCD
Receive: OK:RXAUDIOINJECT:COMPLETE:105600 samples
```

#### Kill Transmission
```
CMD:KILL TX
```
Immediately aborts any ongoing transmission.

---

## Status Messages

MS-DMT sends status messages asynchronously on the control port:

### Transmission Status
```
STATUS:TX:TRANSMIT    # Transmission started
STATUS:TX:IDLE        # Transmission complete
```

### Reception Status
```
STATUS:RX:75 BPS SHORT     # DCD acquired, mode detected
STATUS:RX:600 BPS LONG     # Different mode example
STATUS:RX:NO DCD           # Signal lost / end of message
```

### Modem Ready
```
MODEM READY           # Modem initialized and ready for commands
```

---

## Complete Protocol Example

Here's a full session showing TX and RX:

```
# Application connects to ports 4998 and 4999
# ─────────────────────────────────────────────

[CTRL 4999] ← MODEM READY

# Configure modem for 600 bps short interleave
[CTRL 4999] → CMD:DATA RATE:600S
[CTRL 4999] ← OK:DATA RATE:600S

# Enable recording
[CTRL 4999] → CMD:RECORD TX:ON
[CTRL 4999] ← OK:RECORD TX:ON

# Set filename prefix
[CTRL 4999] → CMD:RECORD PREFIX:mytest
[CTRL 4999] ← OK:RECORD PREFIX:mytest

# Send message data
[DATA 4998] → THE QUICK BROWN FOX

# Trigger transmission
[CTRL 4999] → CMD:SENDBUFFER
[CTRL 4999] ← STATUS:TX:TRANSMIT

# ... audio is generated, radio transmits ...

[CTRL 4999] ← STATUS:TX:IDLE

# Later, to test RX by injecting the recorded file:
[CTRL 4999] → CMD:RXAUDIOINJECT:C:\tx_pcm_out\mytest_20251207.pcm
[CTRL 4999] ← OK:RXAUDIOINJECT:STARTED:C:\tx_pcm_out\mytest_20251207.pcm
[CTRL 4999] ← STATUS:RX:600 BPS SHORT

# Decoded data appears on data port
[DATA 4998] ← THE QUICK BROWN FOX

[CTRL 4999] ← STATUS:RX:NO DCD
[CTRL 4999] ← OK:RXAUDIOINJECT:COMPLETE:105600 samples
```

---

## PCM File Format

All PCM files use this format:

| Parameter | Value |
|-----------|-------|
| Sample Rate | 48000 Hz |
| Bit Depth | 16-bit signed integer |
| Byte Order | Little-endian |
| Channels | Mono (1 channel) |
| Format | Raw PCM (no WAV header) |

### File Size Calculation
```
file_size_bytes = duration_seconds × 48000 × 2
```

Example: 10 seconds of audio = 960,000 bytes

### Reading/Writing PCM in Python
```python
import struct
import numpy as np

def read_pcm(filename):
    """Read PCM file as numpy array of int16"""
    with open(filename, 'rb') as f:
        data = f.read()
    return np.frombuffer(data, dtype=np.int16)

def write_pcm(filename, samples):
    """Write numpy array of int16 to PCM file"""
    samples = np.array(samples, dtype=np.int16)
    with open(filename, 'wb') as f:
        f.write(samples.tobytes())

# Example: Generate 1 second of silence
silence = np.zeros(48000, dtype=np.int16)
write_pcm('silence.pcm', silence)
```

### Reading/Writing PCM in C/C++
```cpp
#include <fstream>
#include <vector>

std::vector<int16_t> read_pcm(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<int16_t> samples(size / 2);
    file.read(reinterpret_cast<char*>(samples.data()), size);
    return samples;
}

void write_pcm(const std::string& filename, const std::vector<int16_t>& samples) {
    std::ofstream file(filename, std::ios::binary);
    file.write(reinterpret_cast<const char*>(samples.data()), 
               samples.size() * sizeof(int16_t));
}
```

---

## Testing Your Implementation Against MS-DMT

### Test 1: Validate Your TX

Your application generates MIL-STD-188-110A audio → MS-DMT decodes it

```python
# 1. Start MS-DMT: MS-DMT.exe --testdevices
# 2. Connect and wait for MODEM READY
# 3. Inject your PCM file:

ctrl.send(b'CMD:RXAUDIOINJECT:C:\\my_tx_output.pcm\n')

# 4. Wait for status messages:
#    - STATUS:RX:<mode>  (should match your TX mode)
#    - STATUS:RX:NO DCD  (end of signal)

# 5. Read decoded bytes from data port
decoded = data.recv(4096)

# 6. Compare to your original message
assert decoded == original_message
```

### Test 2: Validate Your RX

MS-DMT generates reference audio → Your application decodes it

```python
# 1. Start MS-DMT: MS-DMT.exe --testdevices
# 2. Configure and generate TX:

ctrl.send(b'CMD:DATA RATE:600S\n')
ctrl.send(b'CMD:RECORD TX:ON\n')
ctrl.send(b'CMD:RECORD PREFIX:reference\n')
data.send(b'TEST MESSAGE 1234567890')
ctrl.send(b'CMD:SENDBUFFER\n')

# 3. Wait for STATUS:TX:IDLE
# 4. Find PCM file in ./tx_pcm_out/reference_*.pcm
# 5. Feed into your RX implementation
# 6. Compare decoded output to "TEST MESSAGE 1234567890"
```

### Test 3: Full Compatibility Matrix

Run both tests for all 12 modes to ensure full compatibility:

```python
modes = ['75S', '75L', '150S', '150L', '300S', '300L', 
         '600S', '600L', '1200S', '1200L', '2400S', '2400L']

for mode in modes:
    test_tx_against_msdmt_rx(mode)
    test_rx_against_msdmt_tx(mode)
```

---

## Command-Line Arguments

| Argument | Description |
|----------|-------------|
| (none) | Normal operation with GUI |
| `rlba` | Bypass VOX PTT test (development) |
| `--autotest` | Automated testing mode, uses real audio devices |
| `--testdevices` | Use mock devices - no hardware required |

**For integration testing, use:**
```bash
MS-DMT.exe --testdevices
```

This launches MS-DMT with null audio/serial devices, perfect for automated testing without any hardware.

---

## Error Handling

### Connection Errors
- If connection refused: MS-DMT may not be running
- If connection times out: Check firewall settings

### Command Errors
| Error | Meaning |
|-------|---------|
| `ERROR:RXAUDIOINJECT:FILE NOT FOUND:<path>` | PCM file doesn't exist |
| `ERROR:RXAUDIOINJECT:CANNOT OPEN:<path>` | Permission denied |

### RX Decode Failures
- `STATUS:RX:NO DCD` immediately after inject → Invalid signal format
- No data on data port → Signal not decoded (check PCM format)

---

## Quick Reference

### Minimum TX Sequence
```
1. Connect to ports 4998 and 4999
2. Wait for "MODEM READY"
3. CMD:DATA RATE:<mode>
4. Send data to port 4998
5. CMD:SENDBUFFER
6. Wait for STATUS:TX:IDLE
```

### Minimum RX Test Sequence
```
1. Connect to ports 4998 and 4999
2. Wait for "MODEM READY"
3. CMD:RXAUDIOINJECT:<pcm_file>
4. Wait for STATUS:RX:<mode>
5. Read decoded data from port 4998
6. Wait for STATUS:RX:NO DCD
```

---

## Contact & Support

This integration guide is for MS-DMT v3.00 Beta 2.22.

For the MIL-STD-188-110A protocol specification details, see:
- `MIL-STD-188-110A-Reference.md` in the project repository
