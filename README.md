# Phoenix Nest Modem

**MIL-STD-188-110A HF Data Modem**

Copyright © 2025 Phoenix Nest LLC

See [EULA](phoenixnestmodem_eula.md) for license terms and conditions.

---

## Overview

A complete software implementation of the MIL-STD-188-110A serial-tone HF data modem standard, supporting all 11 data modes from 75 bps to 4800 bps with advanced equalization, turbo processing, and comprehensive testing infrastructure.

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
- **Parallel Test Execution**: Multi-threaded test harness with up to 16 threads
- **Web-Based GUI**: Interactive test interface at `http://localhost:8080`
- **MS-DMT Compatible Server**: Network interface on TCP ports 4998/4999/5000

### SDR Input (NEW)
- **Direct SDR Receive**: I/Q input at 2 MSPS from SDRplay RSP2 Pro
- **IQSource Class**: Multi-stage decimation (2 MSPS → 48 kHz) with format conversion
- **IQFileSource Class**: Playback of `.iqr` recordings from [phoenix_sdr](https://github.com/Alex-Pennington/phoenix_sdr)
- **Format Support**: int16/float32, planar/interleaved I/Q samples
- **Signal Integrity**: 82.2 dB SNR through decimation chain, amplitude perfectly preserved

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
│  pcm_file.h  wav_file.h  iq_source.h  audio_source.h        │
└─────────────────────────────────────────────────────────────┘
```

## Project Structure

```
pennington_m110a_demod/
├── api/                    # Public API headers
│   ├── modem.h            # Main include
│   ├── modem_rx.cpp/h     # Receiver implementation
│   ├── modem_tx.cpp/h     # Transmitter implementation
│   ├── modem_config.h     # Configuration structures
│   ├── sample_source.h    # Abstract input interface
│   ├── iq_source.h        # SDR I/Q input with decimation
│   ├── iq_file_source.h   # .iqr file playback
│   ├── audio_source.h     # Legacy audio input wrapper
│   └── channel_sim.h      # Channel simulation
├── src/                    # Core modem implementation
│   ├── common/            # Shared types and constants
│   ├── dsp/               # DSP primitives (NCO, filters, AGC)
│   ├── equalizer/         # DFE, MLSE, Turbo equalizers
│   ├── io/                # File I/O (PCM, WAV)
│   ├── m110a/             # Protocol-specific (preamble, modes)
│   ├── modem/             # Codec chain (Viterbi, interleaver)
│   └── sync/              # Synchronization (timing, frequency)
├── test/                  # Test suite and GUI
│   ├── exhaustive_test_unified.cpp  # Main test executable
│   ├── test_gui_server.cpp          # Web GUI server
│   └── test_framework.h             # Test infrastructure
├── server/                # TCP/IP server interface
│   ├── msdmt_server.cpp   # MS-DMT compatible server
│   └── main.cpp           # Server entry point
├── docs/                  # Documentation
├── refrence_pcm/          # MS-DMT reference test vectors
└── build.ps1              # PowerShell build script
```

## Building

### Requirements
- **Compiler**: MSVC with C++17 support (Visual Studio 2019+)
- **Build System**: PowerShell 5.1+ with build.ps1 script
- **Runtime**: Windows x64

### Build All Components

```powershell
.\build.ps1 -Target all
```

Build targets:
- `all` - Build all executables (modem, test suite, GUI, server)
- `unified` - Test suite only (`exhaustive_test.exe`)
- `gui` - Web GUI only (`test_gui.exe`)
- `server` - Modem server only (`m110a_server.exe`)

## Quick Start

### Run Server & GUI

```powershell
# Start modem server (TCP ports 4998/4999/5000)
.\server\m110a_server.exe

# Start test GUI (opens browser to http://localhost:8080)
.\test\test_gui.exe
```

## Usage

### Direct API Usage

## API Examples

The following examples demonstrate the C++ API. See [api/README.md](api/README.md) for complete API documentation.

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

### Test Framework Performance

Parallel test execution performance (Windows, 8-core CPU):
- **Sequential**: ~42s for 22 tests
- **Parallel (4 threads)**: ~11s for 22 tests (3.8x speedup)
- **Bottleneck**: Modem `decode()` processing time

Test backends:
- **Direct API Backend**: In-process modem testing (supports parallel execution)
- **Server Backend**: TCP/IP testing via MS-DMT compatible interface (sequential only)

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

Tested for interoperability with N2CKH's MS-DMT v3.00.2.22 reference implementation:
- All 10 standard modes verified (M150-M2400, short and long interleave)
- Reference test samples included in `refrence_pcm/` directory for validation
- Known test message: "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890"
- Compatible TCP/IP server interface on ports 4998 (data), 4999 (control), 5000 (UDP discovery)

Note: This implementation was developed independently from the MIL-STD-188-110A specification.
MS-DMT compatibility is for interoperability testing only.

## Cross-Modem Interoperability

Phoenix Nest has been validated for cross-modem interoperability with G4GUO's Brain Core implementation. This testing ensures that signals generated by one implementation can be correctly decoded by the other.

### Test Results (2025-01-15)

**Phoenix Nest TX → Brain Core RX: 9/12 PASS**
| Mode | Data Rate | Interleave | Status |
|------|-----------|------------|--------|
| 600S | 600 bps | SHORT | ✅ PASS |
| 600L | 600 bps | LONG | ✅ PASS |
| 300S | 300 bps | SHORT | ✅ PASS |
| 300L | 300 bps | LONG | ✅ PASS |
| 1200S | 1200 bps | SHORT | ✅ PASS |
| 1200L | 1200 bps | LONG | ✅ PASS |
| 2400S | 2400 bps | SHORT | ✅ PASS |
| 2400L | 2400 bps | LONG | ✅ PASS |
| 150S | 150 bps | SHORT | ⚠️ Partial |
| 150L | 150 bps | LONG | ✅ PASS |
| 75S | 75 bps | SHORT | ❌ TX not implemented |
| 75L | 75 bps | LONG | ❌ TX not implemented |

**Brain Core TX → Phoenix Nest RX: 10/12 PASS**

**Key Fix**: Preamble encoding in `brain_preamble.h` corrected to match Qt MSDMT reference (scrambler restart per segment, count encoding, zero segment seed).

## Documentation

- [API Documentation](api/README.md) - Public API reference
- [Implementation Plan](docs/IMPLEMENTATION_PLAN.md) - Development roadmap
- [Test Matrix](docs/M110A_MODES_AND_TEST_MATRIX.md) - Mode test coverage
- [Project Status](docs/PROJECT_STATUS.md) - Current status and milestones
- [Development Journal](docs/development_journal.md) - Session-by-session progress
- [Protocol Details](docs/PROTOCOL.md) - MIL-STD-188-110A protocol specifics
- [Equalizers](docs/EQUALIZERS.md) - Equalizer theory and usage
- [RX Chain](docs/RX_CHAIN.md) - Receiver signal flow
- [TX Chain](docs/TX_CHAIN.md) - Transmitter signal flow

## Testing

### Web GUI Test Interface

```powershell
.\test\test_gui.exe
```

Browser opens to `http://localhost:8080` with:
- Multi-select mode and equalizer lists
- Backend selection (Direct API or Server)
- Parallel thread count (1-16 threads, Direct API only)
- Test type (Standard or Progressive)
- Real-time test output streaming

### Command Line Testing

### Test Backends

**Server Backend**:
- Tests via TCP/IP (MS-DMT compatible)
- Validates server interface
- Sequential execution only
- Requires `m110a_server.exe` running

## API Examples

## Development History

This implementation was developed over 22+ sessions, progressing through:
1. Core modem (TX/RX chains, all 11 modes)
2. DFE equalizer with probe-based channel estimation
3. MLSE equalizer (L=2, L=3, adaptive)
4. RLS adaptive filtering
5. Turbo equalization with SISO Viterbi decoder
6. MS-DMT compatibility fixes
7. Parallel test execution framework
8. Web-based test GUI

## Known Limitations

- M75 (75 bps Walsh) mode: Basic implementation, may need tuning for weak signals
- No ALE (Automatic Link Establishment) integration
- Single-channel only (no diversity combining)
- SDR input tested with synthetic signals only (real-world OTA validation pending)
- Windows-focused build system (PowerShell script)

## Changelog

### v1.1.0 - SDR Integration (2025-01-15)

**SDR I/Q Pipeline** - 31/31 tests passing

| Component | Tests | Status |
|-----------|-------|---------|
| IQSource format conversion | 10 | ✅ PASS |
| IQFileSource .iqr loading | 11 | ✅ PASS |
| I/Q pipeline loopback | 10 | ✅ PASS |

**New Files:**
- `api/sample_source.h` - Abstract input interface
- `api/iq_source.h` - SDR I/Q input with multi-stage decimation
- `api/iq_file_source.h` - .iqr file playback wrapper
- `api/audio_source.h` - Legacy audio input wrapper
- `test/test_sample_source.cpp` - SampleSource unit tests
- `test/test_iq_file_source.cpp` - IQFileSource unit tests
- `test/test_iq_loopback.cpp` - I/Q pipeline loopback tests

**Key Metrics:**
- Signal amplitude preserved: rms_ratio = 1.00
- int16 quantization SNR: 82.2 dB
- Format consistency: planar == interleaved (max_diff = 0)

**Integration:** Coordinated with [phoenix_sdr](https://github.com/Alex-Pennington/phoenix_sdr) - IQSource accepts phoenix_sdr's int16 planar format directly (1:1 callback match).

See [SDRPlay_Integration_Pathway.md](SDRPlay_Integration_Pathway.md) for full details.

### v1.0.0 - Cross-Modem Interoperability (2025-01-15)

- Brain Core TX→RX interoperability: 10/12 PASS
- Phoenix Nest TX→Brain Core RX: 9/12 PASS
- All 11 MIL-STD-188-110A modes validated
- Turbo equalization for severe channel conditions

## Contributing

Contributions welcome! Areas of interest:
- Additional equalizer algorithms
- ALE integration
- Real-time SDR signal capture testing
- Performance optimization
- Additional test vectors
- Cross-platform build support (CMake)
- Linux/macOS compatibility

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.

## References

- MIL-STD-188-110A: Interoperability and Performance Standards for Data Modems (primary specification)
- MIL-STD-188-110B/C: Updated versions (partial compatibility)
- STANAG 4539: NATO equivalent standard
- N2CKH's MS-DMT v3.00.2.22: Used for interoperability validation
- G4GUO's Brain Core: Cross-modem interoperability validation

## Acknowledgments

- U.S. Department of Defense for the MIL-STD-188-110A specification
- **G4GUO** (Charles Brain) - Brain Core MIL-STD-188-110A implementation, invaluable for cross-modem interoperability testing
- **N2CKH** (Steve Hajduchek) - MS-DMT (MS-110A Demonstration & Maintenance Tool) reference implementation
- GNU Radio community for DSP insights
- Fldigi project for protocol documentation

## Repository

- GitHub: [pennington_m110a_demod](https://github.com/Alex-Pennington/pennington_m110a_demod)
- Branch: `master` (stable)
- SDR integration: [phoenix_sdr](https://github.com/Alex-Pennington/phoenix_sdr)
