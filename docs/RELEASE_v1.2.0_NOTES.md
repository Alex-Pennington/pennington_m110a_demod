# M110A Modem v1.2.0 Release Notes

**Build:** 92  
**Commit:** 7bd4b42  
**Date:** December 8, 2024  
**Branch:** turbo

## Overview

This release packages the M110A MIL-STD-188-110A compatible modem as a production-ready software product with hardware-locked licensing system.

## Key Features

### Modem Capabilities
- **Supported Modes:** 75, 150, 300, 600, 1200, 2400 BPS (Short/Long interleaver)
- **Modulation:** BPSK, QPSK, 8-PSK with Walsh modulation
- **FEC:** Convolutional coding with Viterbi decoding
- **Interleaving:** Short (0.72s) and Long (4.48s) variants
- **AFC:** Automatic Frequency Correction ±2 Hz
- **Equalization:** DFE, RLS, MLSE (2-3 tap), Turbo equalizer
- **Channel Simulation:** AWGN, Poor HF, Flutter fading (Watterson model)

### Licensing System
- **Hardware Fingerprinting:** CPU-based hardware ID
- **License Key Format:** `CUSTOMER-HWID-EXPIRY-CHECKSUM`
- **Validation:** Startup license check with expiration enforcement
- **Trial Mode:** 30-day trial with single channel limitation (framework ready)

### Components

#### 1. m110a_server.exe (553 KB)
TCP/IP network server for modem operations.

**Ports:**
- Data: 4998
- Control: 4999
- Discovery: UDP 5000 (optional)

**Features:**
- License validation on startup
- MS-DMT protocol compatible
- Channel simulation support
- Graceful shutdown handling

**Usage:**
```bash
m110a_server.exe [--port N] [--no-discovery]
```

#### 2. exhaustive_test.exe (586 KB)
Comprehensive test suite for all modem modes.

**Features:**
- License validation on startup
- Direct API or server backend
- Progressive testing (SNR, frequency offset, multipath)
- Reference PCM validation
- Equalizer comparison
- Parallel execution support
- CSV results export

**Usage:**
```bash
exhaustive_test.exe [--server] [--channel TYPE] [--mode MODE] [--csv FILE]
```

## Release Package Structure

```
release/
├── bin/
│   ├── m110a_server.exe      # TCP/IP modem server
│   ├── exhaustive_test.exe   # Test suite
│   ├── test_gui.exe          # Web-based GUI (port 8080)
│   └── melpe_vocoder.exe     # MELPe voice codec
├── docs/
│   ├── PROTOCOL.md           # TCP/IP protocol reference
│   └── CHANNEL_SIMULATION.md # Channel impairment guide
├── examples/
│   ├── refrence_pcm/         # Reference PCM files (12 modes)
│   │   ├── tx_*.pcm          # Brain modem TX reference
│   │   └── *_metadata.json   # Mode metadata
│   └── melpe_test_audio/     # MELPe test audio files
├── license.key.template
├── INSTALL.md
├── QUICKSTART.md
├── README.md
├── EULA.md
└── RELEASE_INFO.txt
```

## Installation

1. Extract release package
2. Obtain license:
   - Run: `m110a_server.exe` to display your hardware ID
   - Go to https://www.organicengineer.com/projects to obtain a license key using your hardware ID
   - Receive `license.key` file
3. Place `license.key` in `bin/` directory
4. Run `m110a_server.exe` or `exhaustive_test.exe`

## Known Limitations

### AFC Range
The current AFC implementation is optimized for ±2 Hz frequency offset:
- **Design Range:** ±2 Hz (preamble-based correlation)
- **Tested Range:** ±5 Hz with degraded performance
- **Recommendation:** Ensure transmitter/receiver frequency stability

**Technical Background:**
- AFC method: Preamble correlation with frequency search
- Search granularity: 0.5 Hz steps
- Maximum acquisition: ±10 Hz (with reduced performance beyond ±5 Hz)
- Build 83-86 validation: 62.1% pass rate on ±2 Hz test set

**Future Enhancement:**
To achieve robust ±10 Hz operation, the following approaches are recommended:
- Pilot tone tracking (24-40 hour implementation)
- Decision-directed frequency tracking
- FFT-based coarse acquisition (requires integration with preamble fine search)

See `docs/AFC_ROOT_CAUSE.md` for detailed analysis.

### Performance Characteristics

**Test Results (Build 86):**
- **Ideal Channel:** 62.1% pass rate across all modes
- **foff_0hz:** 70.2% pass (baseline)
- **foff_2hz:** 67.7% pass (good)
- **foff_5hz:** 0.0% pass (outside AFC range)
- **Best Mode:** 1200L (80-85% pass on ideal/foff_0hz)
- **Challenging Modes:** 75L, 150L (longer symbols, more sensitive to frequency error)

**Channel Simulation:**
- AWGN: Full functionality
- Poor HF: Watterson model with moderate performance degradation
- Flutter: High Doppler spread, may exceed AFC tracking

## Build Information

**Compiler:** MinGW g++ 14.2.0 (x86_64-w64-mingw32)  
**C++ Standard:** C++17  
**Optimization:** -O2  
**Libraries:** WinSock2 (ws2_32)

**Build Flags:**
```
-std=c++17 -O2 -I. -Isrc -D_USE_MATH_DEFINES
```

## Version History

### v1.2.0 (Build 92) - December 8, 2024
- **Licensing System:** Hardware-locked licensing with CPU fingerprinting
- **Release Package:** Complete distribution with documentation
- **AFC Investigation:** Characterized ±2 Hz working range (Builds 83-86)
- **Server Integration:** License validation in m110a_server.exe
- **Test Integration:** License validation in exhaustive_test.exe

### v1.1.0 (Build 82) - December 7, 2024
- Turbo equalizer integration
- MLSE equalizer improvements
- Channel simulation enhancements

### v1.0.0 (Build 70-75) - November 2024
- Initial MS-DMT compatible release
- All modes functional
- Basic AFC and equalization

## Testing

### Exhaustive Test Suite
Run all modes with ideal channel:
```bash
cd bin
exhaustive_test.exe
```

### Progressive Testing
Test SNR sensitivity:
```bash
exhaustive_test.exe --prog-snr --csv results.csv
```

Test frequency offset:
```bash
exhaustive_test.exe --prog-freq --csv freq_results.csv
```

Test multipath:
```bash
exhaustive_test.exe --prog-multipath --csv multipath_results.csv
```

### Server-Based Testing
```bash
# Terminal 1
m110a_server.exe

# Terminal 2
exhaustive_test.exe --server --channel poor
```

## Documentation

- **INSTALL.md:** Installation instructions
- **QUICKSTART.md:** Quick start guide
- **API.md:** Programming interface reference
- **PROTOCOL.md:** MS-DMT protocol specification
- **CHANNEL_SIMULATION.md:** Channel modeling details
- **EULA.md:** End User License Agreement

## Licensing

**License Type:** Proprietary, hardware-locked  
**Activation:** Required for all executables  
**Trial:** 30-day trial mode supported (framework ready)  
**Support:** support@phoenixnestsoftware.com

**License Key Format:**
```
CUSTOMER-HWID-EXPIRY-CHECKSUM
Example: ACME01-A3B4C5D6-20261231-9F8E7D6C
```

## Support

For technical support, licensing inquiries, or bug reports:
- **Email:** support@phoenixnestsoftware.com
- **Documentation:** See `docs/` directory
- **Test Reports:** Check `test_reports/` for detailed logs

## Credits

**Developer:** Phoenix Nest Software  
**Standard:** MIL-STD-188-110A  
**Protocol Compatibility:** MS-DMT (MIL-STD Data Modem Terminal)

## Legal

© 2024 Phoenix Nest Software  
All rights reserved.

This software is protected by copyright and requires a valid license for operation.
Unauthorized distribution or use is prohibited.

See EULA.md for complete terms and conditions.

---

**Build System Version:** 1.2.0  
**Git Branch:** turbo  
**Git Commit:** 7bd4b42  
**Build Date:** 2024-12-08 20:02:42
