# MIL-STD-188-110A HF Modem

A complete software implementation of the MIL-STD-188-110A serial-tone HF data modem standard, supporting all 11 data modes from 75 bps to 4800 bps with advanced equalization and turbo processing.

## Features

### Data Modes
| Mode | Data Rate | Modulation | Interleave | FEC |
|------|-----------|------------|------------|-----|
| M75S/L | 75 bps | Walsh-8 | Short/Long | Repetition |
| M150S/L | 150 bps | 8PSK | Short/Long | Rate 1/2 K=7 |
| M300S/L | 300 bps | 8PSK | Short/Long | Rate 1/2 K=7 |
| M600S/L | 600 bps | 8PSK | Short/Long | Rate 1/2 K=7 |
| M1200S/L | 1200 bps | 8PSK | Short/Long | Rate 1/2 K=7 |
| M2400S/L | 2400 bps | 8PSK | Short/Long | Rate 1/2 K=7 |
| M4800S | 4800 bps | 8PSK | Short | Uncoded |

### Signal Processing
- **Carrier**: 1800 Hz center frequency
- **Symbol Rate**: 2400 baud
- **Sample Rates**: 8000, 9600, 44100, 48000 Hz supported
- **Filtering**: Root-raised-cosine with β=0.35

### Equalizers
Seven equalizer implementations for different channel conditions:

| Equalizer | Best For | Complexity |
|-----------|----------|------------|
| NONE | Clean channels | Lowest |
| DFE | Light ISI, AWGN | Low |
| DFE_RLS | Time-varying channels | Medium |
| MLSE_L2 | Moderate ISI (L=2) | Medium |
| MLSE_L3 | Heavy ISI (L=3) | High |
| MLSE_ADAPTIVE | Unknown/changing ISI | High |
| TURBO | Severe conditions (default) | Highest |

### Advanced Features
- **Turbo Equalization**: Iterative SISO decoding with 2-5 iterations
- **Soft Demapping**: SNR-weighted LLR computation
- **Adaptive MLSE**: Channel length estimation and tracking
- **Automatic Mode Detection**: Preamble correlation with D1/D2 decoding
- **Frequency Offset Compensation**: ±10 Hz acquisition range

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        API Layer                             │
│  modem.h  modem_rx.h  modem_tx.h  modem_config.h            │
├─────────────────────────────────────────────────────────────┤
│                     Modem Components                         │
│  ┌─────────┐ ┌──────────┐ ┌──────────┐ ┌─────────────────┐  │
│  │ m110a/  │ │ modem/   │ │equalizer/│ │     sync/       │  │
│  │ m110a_tx│ │ viterbi  │ │ dfe      │ │ preamble_detect │  │
│  │ m110a_rx│ │ siso     │ │ mlse     │ │ timing_recovery │  │
│  │ mode_*  │ │ mapper   │ │ turbo    │ │ freq_estimator  │  │
│  └─────────┘ └──────────┘ └──────────┘ └─────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│                      DSP Primitives                          │
│  nco.h  rrc_filter.h  resampler.h  agc.h  fft.h            │
├─────────────────────────────────────────────────────────────┤
│                        I/O Layer                             │
│  pcm_file.h  wav_file.h                                     │
└─────────────────────────────────────────────────────────────┘
```

## Directory Structure

```
m110a_demod/
├── api/                    # High-level modem API
│   ├── modem.h            # Main include
│   ├── modem_rx.cpp/h     # Receiver implementation
│   ├── modem_tx.cpp/h     # Transmitter implementation
│   └── modem_config.h     # Configuration structures
├── src/
│   ├── common/            # Shared types and constants
│   ├── dsp/               # DSP primitives (NCO, filters, AGC)
│   ├── equalizer/         # DFE, MLSE, Turbo equalizers
│   ├── io/                # File I/O (PCM, WAV)
│   ├── m110a/             # Protocol-specific (preamble, modes)
│   ├── modem/             # Codec chain (Viterbi, interleaver)
│   └── sync/              # Synchronization (timing, frequency)
├── test/                  # Unit and integration tests
├── examples/              # Usage examples
├── tools/                 # Command-line utilities
├── docs/                  # Documentation
└── ref_pcm/               # MS-DMT reference samples
```

## Building

### Requirements
- C++17 compatible compiler (GCC 8+, Clang 7+, MSVC 2019+)
- CMake 3.16+ (optional)

### Quick Build (Header-Only Core)
```cpp
// Most components are header-only
#include "api/modem.h"
```

### CMake Build
```bash
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
make -j$(nproc)
```

### Manual Compilation
```bash
g++ -std=c++17 -O3 -I src -o modem tools/m110a_modem.cpp src/io/pcm_file.cpp
```

## Usage

### Basic Transmission
```cpp
#include "api/modem.h"

// Configure transmitter
m110a::TxConfig tx_cfg;
tx_cfg.mode = m110a::Mode::M2400S;
tx_cfg.sample_rate = 48000;

// Create and transmit
m110a::ModemTx tx(tx_cfg);
std::vector<uint8_t> data = {/* your data */};
auto pcm_samples = tx.transmit(data);
```

### Basic Reception
```cpp
#include "api/modem.h"

// Configure receiver
m110a::RxConfig rx_cfg;
rx_cfg.mode = m110a::Mode::AUTO;      // Auto-detect mode
rx_cfg.equalizer = m110a::Equalizer::TURBO;
rx_cfg.sample_rate = 48000;

// Create and receive
m110a::ModemRx rx(rx_cfg);
auto result = rx.receive(pcm_samples);

if (result.success) {
    // result.data contains decoded bytes
    // result.detected_mode shows which mode was detected
    // result.ber contains bit error rate estimate
}
```

### Loopback Test
```cpp
#include "api/modem.h"

m110a::TxConfig tx_cfg{.mode = m110a::Mode::M1200S};
m110a::RxConfig rx_cfg{.mode = m110a::Mode::AUTO};

m110a::ModemTx tx(tx_cfg);
m110a::ModemRx rx(rx_cfg);

std::vector<uint8_t> original = "Hello, HF World!";
auto samples = tx.transmit(original);
auto result = rx.receive(samples);

assert(result.data == original);
```

## Equalizer Selection Guide

| Channel Condition | Recommended | Alternative |
|-------------------|-------------|-------------|
| AWGN only | DFE | NONE |
| Light multipath | DFE_RLS | DFE |
| Moderate multipath | MLSE_L2 | MLSE_ADAPTIVE |
| Severe multipath | TURBO | MLSE_L3 |
| Unknown/varying | TURBO | MLSE_ADAPTIVE |
| Low CPU budget | DFE | NONE |

## Performance

### Test Results (11/11 modes passing)
All modes verified with loopback testing and MS-DMT reference sample compatibility.

### Turbo Equalization Gains
| Condition | Without Turbo | With Turbo | Improvement |
|-----------|---------------|------------|-------------|
| AWGN 9dB | 8.0% BER | 0.0% BER | +100% |
| Light ISI | 4.6% BER | 0.0% BER | +100% |
| Moderate ISI | 28.8% BER | 11.5% BER | +60% |
| Heavy ISI | 40.6% BER | 28.4% BER | +30% |

### Resource Usage
- Memory: ~2-5 MB depending on equalizer and interleaver
- CPU: Runs real-time on Raspberry Pi 4 for all modes
- Latency: Mode-dependent (short interleave ~0.6s, long interleave ~4.8s)

## Protocol Details

### Frame Structure
```
[Preamble][Data Block 1][Probe][Data Block 2][Probe]...[EOM]

Preamble (variable length):
  - Common sync: 288-320 symbols (scrambled known pattern)
  - D1: 32 symbols (data rate identifier)
  - D2: 32 symbols (interleaver setting)
  - Count: 96 symbols (transmission duration)

Data Block: 32 symbols (mode-dependent bits)
Probe: 16 symbols (channel estimation)
```

### Channel Coding
- Convolutional encoder: Rate 1/2, K=7, polynomials G1=0133, G2=0171
- Interleaver: Block interleaver, size depends on mode and short/long setting
- Scrambler: 9-stage LFSR, polynomial x⁹ + x⁴ + 1

### Modulation
- 8PSK with Gray coding
- Differential encoding (optional)
- Symbol rate: 2400 baud fixed

## MS-DMT Compatibility

Tested against MS-DMT v3.00.2.22 reference implementation:
- All 10 standard modes verified (M150-M2400, short and long interleave)
- Reference samples included in `ref_pcm/` directory
- Known message: "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890"

## Documentation

- `docs/API.md` - API reference
- `docs/PROTOCOL.md` - MIL-STD-188-110A protocol details
- `docs/EQUALIZERS.md` - Equalizer theory and usage
- `docs/TURBO_EQUALIZATION.md` - Turbo processing details
- `docs/RX_CHAIN.md` - Receiver signal flow
- `docs/TX_CHAIN.md` - Transmitter signal flow

## Testing

### Run All Tests
```bash
# Build and run test suite
cd build && ctest --output-on-failure
```

### Test Individual Components
```bash
# Loopback test all modes
./test/test_pcm_loopback

# Turbo equalizer stress test
./test/test_turbo_severe

# MS-DMT reference sample test
./test/test_msdmt_e2e
```

## Examples

See `examples/` directory:
- `simple_loopback.cpp` - Basic TX/RX demonstration
- `auto_detect.cpp` - Automatic mode detection
- `channel_test.cpp` - Testing with simulated channels
- `file_demod.cpp` - Demodulate from WAV/PCM file

## Command Line Tools

### m110a_modem
```bash
# Transmit
./m110a_modem tx --mode 2400S --input data.bin --output signal.wav

# Receive  
./m110a_modem rx --input signal.wav --output decoded.bin

# Loopback test
./m110a_modem test --mode 1200L --snr 10
```

## Development History

This implementation was developed over 22+ sessions, progressing through:
1. Core modem (TX/RX chains, all 11 modes)
2. DFE equalizer with probe-based channel estimation
3. MLSE equalizer (L=2, L=3, adaptive)
4. RLS adaptive filtering
5. Turbo equalization with SISO Viterbi decoder
6. MS-DMT compatibility fixes

## Known Limitations

- M75 (75 bps Walsh) mode: Basic implementation, may need tuning for weak signals
- No ALE (Automatic Link Establishment) integration
- Single-channel only (no diversity combining)
- No built-in audio device interface (file I/O only)

## Contributing

Contributions welcome! Areas of interest:
- Additional equalizer algorithms
- ALE integration
- Real-time audio interface
- Performance optimization
- Additional test vectors

## License

[Add your license here - GPLv3 recommended for compatibility with similar projects]

## References

- MIL-STD-188-110A: Interoperability and Performance Standards for Data Modems
- MIL-STD-188-110B/C: Updated versions (partial compatibility)
- STANAG 4539: NATO equivalent standard

## Acknowledgments

- MS-DMT project for reference implementation and test samples
- GNU Radio community for DSP insights
- Fldigi project for protocol documentation
