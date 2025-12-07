# MIL-STD-188-110A HF Modem

A complete C++17 implementation of the MIL-STD-188-110A HF data modem standard, compatible with MS-DMT and other compliant modems.

## Features

- **Full Mode Support**: 75-4800 bps data rates
- **MS-DMT Compatible**: Verified interoperability
- **Robust Equalization**: DFE, MLSE, and adaptive phase tracking
- **HF Channel Simulation**: Watterson fading model with CCIR profiles
- **Clean API**: Simple encode/decode interface with Result<T> error handling
- **Thread-Safe**: All API classes are safe for concurrent use

## Quick Start

### Transmit
```cpp
#include "api/modem.h"
using namespace m110a::api;

// Create transmitter
TxConfig tx_cfg;
tx_cfg.mode = Mode::M2400_SHORT;
tx_cfg.sample_rate = 48000.0f;

ModemTX tx(tx_cfg);

// Encode message
std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o'};
auto result = tx.encode(data);

if (result.ok()) {
    auto& samples = result.value();
    // Write to audio device or file
}
```

### Receive
```cpp
#include "api/modem.h"
using namespace m110a::api;

// Create receiver
RxConfig rx_cfg;
rx_cfg.mode = Mode::AUTO;  // Auto-detect mode
rx_cfg.sample_rate = 48000.0f;
rx_cfg.equalizer = Equalizer::DFE;

ModemRX rx(rx_cfg);

// Decode audio
auto result = rx.decode(samples);

if (result.success) {
    std::cout << "Mode: " << mode_name(result.mode) << "\n";
    std::cout << "Data: " << result.as_string() << "\n";
    std::cout << "EOM:  " << (result.eom_detected ? "Yes" : "No") << "\n";
}
```

## Supported Modes

| Mode | Data Rate | Modulation | Interleave | FEC |
|------|-----------|------------|------------|-----|
| M75_SHORT/LONG | 75 bps | Walsh/8-PSK | S/L | Rate 1/2 |
| M150_SHORT/LONG | 150 bps | BPSK | S/L | Rate 1/2 |
| M300_SHORT/LONG | 300 bps | QPSK | S/L | Rate 1/2 |
| M600_SHORT/LONG | 600 bps | 8-PSK | S/L | Rate 1/2 |
| M1200_SHORT/LONG | 1200 bps | 8-PSK | S/L | Rate 1/2 |
| M2400_SHORT/LONG | 2400 bps | 8-PSK | S/L | Rate 1/2 |
| M4800_SHORT | 4800 bps | 8-PSK | S | Uncoded |

## Equalizer Options

| Equalizer | Best For | Notes |
|-----------|----------|-------|
| `NONE` | Clean channels | Use with phase_tracking=true |
| `DFE` | General HF | Default, good all-around |
| `MLSE_L2` | Multipath | 8-state Viterbi |
| `MLSE_L3` | Severe multipath | 64-state Viterbi |

**NLMS Mode**: Set `rx_cfg.use_nlms = true` for improved performance on fast-fading channels (10-16% BER improvement).

## Building

```bash
cd m110a_demod

# Build and run tests
g++ -std=c++17 -O2 -I . -I src test/test_msdmt_compat.cpp \
    api/modem_tx.cpp api/modem_rx.cpp -o test_compat
./test_compat

# Build example
g++ -std=c++17 -O2 -I . -I src examples/simple_loopback.cpp \
    api/modem_tx.cpp api/modem_rx.cpp -o loopback
./loopback
```

## Documentation

- [API Reference](API.md) - ModemTX/RX class documentation
- [TX Chain](TX_CHAIN.md) - Transmitter pipeline details
- [RX Chain](RX_CHAIN.md) - Receiver pipeline and equalization
- [Equalizers](EQUALIZERS.md) - DFE/MLSE theory and usage
- [Protocol](PROTOCOL.md) - MIL-STD-188-110A specification notes

## Project Structure

```
m110a_demod/
├── api/                 # Public API
│   ├── modem.h          # Main include (includes all)
│   ├── modem_tx.{h,cpp} # Transmitter
│   ├── modem_rx.{h,cpp} # Receiver
│   ├── modem_types.h    # Result<T>, Error, enums
│   └── modem_config.h   # TxConfig, RxConfig
├── src/                 # Internal implementation
│   ├── modem/           # Codec, scrambler, mapper
│   ├── dsp/             # NCO, filters, timing
│   ├── m110a/           # Mode config, preamble
│   ├── equalizer/       # DFE implementation
│   └── channel/         # Watterson, AWGN
├── test/                # Test suite
├── examples/            # Usage examples
└── docs/                # Documentation
```

## Performance

### Clean Channel (AWGN @ 20 dB)
All modes: 0% BER

### HF Fading (CCIR Good @ 20 dB)
- Without equalization: ~30% BER
- With DFE: ~0.5% BER
- With MLSE_L2: ~0% BER

### HF Fading (CCIR Moderate @ 20 dB)
- Recommend using lower data rates (M600 or below)
- Enable NLMS for best results

## Test Results

```
MS-DMT Compatibility: 11/11 ✓
PCM Loopback:         11/11 ✓
Watterson HF:         19/19 ✓
EOM Detection:         5/5 ✓
Total:                46/46 ✓
```

## License

This implementation is provided for educational and research purposes.
MIL-STD-188-110A is a U.S. military standard.

## Acknowledgments

- MS-DMT reference implementation for compatibility verification
- CCIR reports for HF channel models
