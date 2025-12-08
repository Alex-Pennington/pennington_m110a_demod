# MIL-STD-118-110A HF Modem

A high-performance implementation of the MIL-STD-118-110A High Frequency (HF) modem standard for military communications.

## Features

- **Complete Mode Support**: All 6 data rates (75, 150, 300, 600, 1200, 2400 bps) with short and long interleavers
- **Advanced Equalization**: Multiple equalizer types (None, LMS, DFE, MLSE) for robust performance
- **Parallel Test Execution**: Multi-threaded test harness with up to 16 threads for efficient validation
- **Web-Based GUI**: Interactive test interface at `http://localhost:8080`
- **MS-DMT Compatible Server**: Network interface on TCP ports 4998/4999/5000
- **Comprehensive API**: Direct C++ API and TCP/IP server interface

## Quick Start

### Build

```powershell
.\build.ps1 -Target all
```

Build targets:
- `all` - Build all executables
- `unified` - Test suite only
- `gui` - Web GUI only
- `server` - Modem server only

### Run Server & GUI

```powershell
# Start modem server (ports 4998/4999/5000)
.\server\m110a_server.exe

# Start test GUI (http://localhost:8080)
.\test\test_gui.exe
```

### Run Tests

```powershell
# Single test
.\test\exhaustive_test.exe --mode 2400S --eq DFE -n 1

# Multiple modes/equalizers
.\test\exhaustive_test.exe --modes 2400S,1200S --eqs DFE,NONE -n 5

# Parallel execution (4 threads)
.\test\exhaustive_test.exe --modes 2400S,1200L,600S --parallel 4 -n 10

# Progressive test suite
.\test\exhaustive_test.exe --progressive -n 50
```

## Architecture

### Core Components

- **PSK Modulator/Demodulator**: 8-PSK, QPSK, BPSK support
- **FEC Encoder/Decoder**: Convolutional codes per MIL-STD-188-110A
- **Interleaver/De-interleaver**: Short (4.48s) and Long (17.92s) modes
- **Symbol Timing Recovery**: Gardner timing error detector
- **Carrier Recovery**: Costas loop with frequency tracking
- **AGC**: Automatic Gain Control
- **Equalizers**: 
  - LMS (Least Mean Squares)
  - DFE (Decision Feedback Equalizer)
  - MLSE (Maximum Likelihood Sequence Estimation)

### Test Framework

- **Direct API Backend**: In-process modem testing
- **Server Backend**: TCP/IP testing via MS-DMT compatible interface
- **Parallel Execution**: ThreadPool-based test distribution
- **Web GUI**: Real-time test monitoring with SSE streaming

## Project Structure

```
api/              - Public API headers
src/              - Core modem implementation
  m110a/          - Mode-specific implementations
  dsp/            - DSP utilities
  equalizer/      - Equalizer implementations
  sync/           - Synchronization components
test/             - Test suite and GUI
server/           - TCP/IP server interface
docs/             - Documentation
refrence_pcm/     - Reference test vectors
```

## Performance

Typical test performance with parallel execution:
- **Sequential**: ~42s for 22 tests
- **Parallel (4 threads)**: ~11s for 22 tests (3.8x speedup)

Bottleneck: Modem `decode()` processing time

## Version

Current version: **1.2.0+build.57** (turbo branch)

See `docs/VERSION_ITERATION_INSTRUCTIONS.md` for versioning details.

## Documentation

- [API Documentation](api/README.md)
- [Implementation Plan](docs/IMPLEMENTATION_PLAN.md)
- [Test Matrix](docs/M110A_MODES_AND_TEST_MATRIX.md)
- [Project Status](docs/PROJECT_STATUS.md)
- [Development Journal](docs/development_journal.md)

## Requirements

- **Compiler**: MSVC with C++17 support
- **Build System**: PowerShell 5.1+, CMake (optional)
- **Runtime**: Windows x64

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.

## Standards Compliance

Implements **MIL-STD-188-110A** Appendix C (Annex C) serial-tone HF modem specifications.

## Contact

Repository: [pennington_m110a_demod](https://github.com/Alex-Pennington/pennington_m110a_demod)
