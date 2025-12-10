# M110A Server - MS-DMT Compatible Network Interface

## Overview

This server provides an MS-DMT compatible network interface for the M110A modem.
It allows external applications to interface with the modem via TCP sockets,
using the same protocol as the MS-DMT reference implementation.

## Network Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      M110A Server                           │
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

## Building

### Windows (MinGW)

```bash
cd server
g++ -std=c++17 -O2 -I.. -o m110a_server.exe main.cpp msdmt_server.cpp ../api/modem_tx.cpp ../api/modem_rx.cpp -lws2_32
g++ -std=c++17 -O2 -I.. -o test_client.exe test_client.cpp -lws2_32
```

### Linux

```bash
cd server
g++ -std=c++17 -O2 -I.. -o m110a_server main.cpp msdmt_server.cpp ../api/modem_tx.cpp ../api/modem_rx.cpp -lpthread
g++ -std=c++17 -O2 -I.. -o test_client test_client.cpp
```

## License Activation (`license_manager` flow)

The server performs a hardware-locked license check on startup using the shared `license_manager` activation flow (`m110a::LicenseManager` in `src/common/license.h`).

1. Run `license_gen.exe --hwid` (or call `LicenseManager::get_hardware_id()` in your tooling) to capture the hardware ID reported by `license_manager`.
2. Go to https://www.organicengineer.com/projects to request a license key that matches the hardware ID.
3. Save the received key as `license.key` beside `m110a_server.exe` (or wherever you launch the binary from).
4. Launch the server once; `license_manager` automatically validates the key and prints the status if there is a mismatch or expiration.

Embedding applications can perform the same validation explicitly:

```cpp
#include "src/common/license.h"

LicenseInfo info;
auto status = LicenseManager::load_license_file("license.key", info);
if (status != LicenseStatus::VALID) {
	throw std::runtime_error(LicenseManager::get_status_message(status));
}
```

The server refuses to process modem traffic until this flow completes successfully.

## Usage

### Starting the Server

```bash
# Basic usage
./m110a_server

# Test mode (no hardware required)
./m110a_server --testdevices

# Custom ports
./m110a_server --data-port 4998 --control-port 4999

# Quiet mode
./m110a_server --quiet
```

### Command Line Options

| Option | Description |
|--------|-------------|
| `--testdevices` | Run with mock audio devices |
| `--data-port N` | Set data port (default: 4998) |
| `--control-port N` | Set control port (default: 4999) |
| `--discovery-port N` | Set discovery port (default: 5000) |
| `--no-discovery` | Disable UDP discovery |
| `--output-dir DIR` | Set PCM output directory |
| `--quiet` | Reduce logging output |
| `--help` | Show help |

## Protocol Reference

### Commands (to server)

| Command | Description |
|---------|-------------|
| `CMD:DATA RATE:<mode>` | Set data rate (75S, 150S, 300S, 600S, 1200S, 2400S, etc.) |
| `CMD:SENDBUFFER` | Transmit buffered data |
| `CMD:RECORD TX:ON/OFF` | Enable/disable TX recording |
| `CMD:RECORD PREFIX:<name>` | Set recording filename prefix |
| `CMD:RXAUDIOINJECT:<path>` | Inject PCM file for RX decoding |
| `CMD:KILL TX` | Abort transmission |

### Responses (from server)

| Response | Description |
|----------|-------------|
| `MODEM READY` | Server ready for commands |
| `OK:<cmd>:<details>` | Command succeeded |
| `ERROR:<cmd>:<details>` | Command failed |
| `STATUS:TX:TRANSMIT` | Transmission started |
| `STATUS:TX:IDLE` | Transmission complete |
| `STATUS:RX:<mode>` | RX mode detected |
| `STATUS:RX:NO DCD` | Signal lost |

### Data Rate Modes

| Mode | Bits/sec | Interleave |
|------|----------|------------|
| `75S` | 75 | Short |
| `75L` | 75 | Long |
| `150S` | 150 | Short |
| `150L` | 150 | Long |
| `300S` | 300 | Short |
| `300L` | 300 | Long |
| `600S` | 600 | Short |
| `600L` | 600 | Long |
| `1200S` | 1200 | Short |
| `1200L` | 1200 | Long |
| `2400S` | 2400 | Short |
| `2400L` | 2400 | Long |

## Example Session

```python
import socket

# Connect to server
ctrl = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
ctrl.connect(('localhost', 4999))

data = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
data.connect(('localhost', 4998))

# Wait for MODEM READY
print(ctrl.recv(1024))  # b'MODEM READY\n'

# Set data rate
ctrl.send(b'CMD:DATA RATE:600S\n')
print(ctrl.recv(1024))  # b'OK:DATA RATE:600S\n'

# Send message
data.send(b'Hello, World!')

# Trigger transmission
ctrl.send(b'CMD:SENDBUFFER\n')
# Receive: STATUS:TX:TRANSMIT, STATUS:TX:IDLE
```

## Testing

Run the test client against the server:

```bash
# Terminal 1: Start server
./m110a_server --testdevices

# Terminal 2: Run test client
./test_client
```

> **Note:** The test binaries also invoke the `license_manager` activation path. Ensure the license flow above has produced a valid `license.key` before running the tests or they will exit after printing the hardware ID instructions.

## Files

| File | Description |
|------|-------------|
| `msdmt_server.h` | Server header with API |
| `msdmt_server.cpp` | Server implementation |
| `main.cpp` | Server executable entry point |
| `test_client.cpp` | Test client for protocol verification |

## Integration with Modem API

The server uses the `m110a::api` modem library internally:

```cpp
// TX: Data → PCM samples
m110a::api::TxConfig cfg;
cfg.mode = m110a::api::Mode::M600_SHORT;
auto samples = m110a::api::encode(data, cfg);

// RX: PCM samples → Data
m110a::api::RxConfig rx_cfg;
auto result = m110a::api::decode(samples, rx_cfg);
```

## Compatibility

This server implements the MS-DMT v3.00 Beta 2.22 protocol and should be
compatible with any client designed for the MS-DMT reference implementation.
