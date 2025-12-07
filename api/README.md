# M110A Modem API

## Overview

Clean, thread-safe API layer for the MIL-STD-188-110A modem implementation.
**MS-DMT compatible** - tested against reference signals.

## Status: ✅ WORKING

| Component | Status | Notes |
|-----------|--------|-------|
| Types (Result<T>, Error) | ✅ Complete | |
| Configuration | ✅ Complete | TxConfig, RxConfig, builders |
| ModemTX | ✅ Working | All 11 modes (150-4800 bps) |
| ModemRX | ✅ Working | Auto mode detection |
| File I/O | ✅ Complete | PCM and WAV support |
| Loopback | ✅ Passing | 11/11 modes |

### Supported Modes
- M150S, M150L (BPSK, 150 bps)
- M300S, M300L (BPSK, 300 bps)
- M600S, M600L (BPSK, 600 bps)
- M1200S, M1200L (QPSK, 1200 bps)
- M2400S, M2400L (8PSK, 2400 bps)
- M4800S (8PSK uncoded, 4800 bps)

### Not Yet Supported
- M75S, M75L (Walsh coded)

## Files

```
api/
├── modem.h          # Main include (convenience functions)
├── modem_types.h    # Result<T>, Error, Mode, Equalizer
├── modem_config.h   # TxConfig, RxConfig, builders
├── modem_tx.h       # ModemTX class interface
├── modem_tx.cpp     # ModemTX implementation
├── modem_rx.h       # ModemRX class interface  
├── modem_rx.cpp     # ModemRX implementation
├── test_api.cpp     # API tests
└── README.md        # This file
```

## Usage Examples

### Quick Encode/Decode
```cpp
#include "api/modem.h"
using namespace m110a::api;

// Encode
auto audio = encode("Hello World", Mode::M2400_SHORT);
if (audio.ok()) {
    save_wav("output.wav", audio.value());
}

// Decode  
auto samples = load_wav("input.wav");
auto result = decode(samples.value());
if (result.success) {
    std::cout << result.as_string() << std::endl;
}
```

### Using ModemTX/ModemRX Classes
```cpp
// Transmitter
TxConfig tx_cfg;
tx_cfg.mode = Mode::M1200_SHORT;
tx_cfg.sample_rate = 48000.0f;

ModemTX tx(tx_cfg);
auto result = tx.encode({0x48, 0x65, 0x6c, 0x6c, 0x6f});

// Receiver
RxConfig rx_cfg;
rx_cfg.mode = Mode::AUTO;  // Auto-detect
rx_cfg.sample_rate = 48000.0f;

ModemRX rx(rx_cfg);
auto decode_result = rx.decode(samples);
```

### File Operations
```cpp
// Encode to file
encode_to_file({'T','e','s','t'}, "output.wav", Mode::M2400_SHORT);

// Decode from file
auto result = decode_file("input.pcm");
```

## Build

```bash
cd m110a_demod
g++ -std=c++17 -O2 -I . -I src api/*.cpp -o modem_api_test
```

## Thread Safety

All public methods are thread-safe:
- Internal mutex protects state
- Safe to call from multiple threads
- No static mutable state

## Error Handling

Uses Result<T> pattern (no exceptions):
```cpp
Result<Samples> result = tx.encode(data);
if (result.ok()) {
    use(result.value());
} else {
    handle_error(result.error());
}
```

Error codes are categorized:
- 100-199: Configuration errors
- 200-299: TX errors
- 300-399: RX errors
- 400-499: I/O errors
- 500-599: Internal errors
