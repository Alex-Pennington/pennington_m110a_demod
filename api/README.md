# M110A Modem API

## Overview

Clean, thread-safe API layer for the MIL-STD-188-110A modem implementation.
**MS-DMT compatible** - tested against reference signals.

## Status: ✅ FULLY WORKING

| Component | Status | Notes |
|-----------|--------|-------|
| Types (Result<T>, Error) | ✅ Complete | |
| Configuration | ✅ Complete | TxConfig, RxConfig, builders |
| ModemTX | ✅ Working | All 11 modes (150-4800 bps) |
| ModemRX | ✅ Working | Auto mode detection |
| File I/O | ✅ Complete | PCM and WAV support |
| Loopback | ✅ Passing | 11/11 modes |
| MS-DMT Compat | ✅ Verified | Preamble, scrambler, probes |

### Supported Modes
- M150S, M150L (BPSK, 150 bps, rate 1/2 FEC)
- M300S, M300L (BPSK, 300 bps, rate 1/2 FEC)
- M600S, M600L (BPSK, 600 bps, rate 1/2 FEC)
- M1200S, M1200L (QPSK, 1200 bps, rate 1/2 FEC)
- M2400S, M2400L (8PSK, 2400 bps, rate 1/2 FEC)
- M4800S (8PSK, 4800 bps, uncoded)

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

## Quick Start

### One-Liner Encode/Decode
```cpp
#include "api/modem.h"
using namespace m110a::api;

// Encode and save
auto audio = encode("Hello World", Mode::M2400_SHORT);
save_wav("output.wav", audio.value());

// Load and decode  
auto samples = load_wav("input.wav");
auto result = decode(samples.value());
std::cout << result.as_string() << std::endl;
```

### Using ModemTX/ModemRX Classes
```cpp
// Transmitter
TxConfig tx_cfg;
tx_cfg.mode = Mode::M1200_SHORT;
tx_cfg.sample_rate = 48000.0f;
tx_cfg.carrier_freq = 1800.0f;
tx_cfg.amplitude = 0.8f;

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

## TX Configuration Options

```cpp
struct TxConfig {
    Mode mode;                    // Operating mode (required)
    float sample_rate = 48000.0f; // 8000 or 48000 Hz
    float carrier_freq = 1800.0f; // 500-3000 Hz
    float amplitude = 0.8f;       // 0.0-1.0
    bool include_preamble = true; // Add sync preamble
    bool include_eom = true;      // Add end-of-message
    bool use_pulse_shaping = false; // RRC filter (α=0.35)
};
```

## RX Configuration Options

```cpp
struct RxConfig {
    Mode mode = Mode::AUTO;       // AUTO for detection
    float sample_rate = 48000.0f;
    float carrier_freq = 1800.0f;
    Equalizer equalizer = Equalizer::NONE;
};
```

## Build

```bash
cd m110a_demod
g++ -std=c++17 -O2 -I . -I src api/*.cpp your_app.cpp -o your_app
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
    std::cerr << result.error().message << std::endl;
}
```

Error codes:
- 100-199: Configuration errors
- 200-299: TX errors
- 300-399: RX errors
- 400-499: I/O errors
- 500-599: Internal errors

## MS-DMT Compatibility

The implementation is compatible with MS-DMT (m188110a) reference:

1. **Preamble**: Exact match to MS-DMT format
   - Common segment (288 symbols)
   - D1/D2 mode identification
   - Countdown sequence

2. **Data Encoding**: Matches MS-DMT scrambler
   - Continuous scrambler across data + probes
   - Correct Gray code mapping
   - Proper interleaver block size

3. **Probe Symbols**: Inserted at correct positions
   - 32 data + 16 probe for M2400
   - 20 data + 20 probe for lower rates

See `docs/TX_CHAIN.md` for detailed pipeline documentation.
