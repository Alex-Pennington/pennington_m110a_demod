# M110A Test GUI - Refactored

Web-based GUI for M110A Modem exhaustive testing.

## File Structure

```
test/test_gui/
├── main.cpp           - Entry point
├── server.h           - HTTP server core
├── utils.h            - Common utilities
├── brain_client.h     - Paul Brain modem TCP client
├── pn_client.h        - PhoenixNest server manager & client
├── test_config.h      - Test configuration structures
├── html_content.h     - Embedded HTML/CSS/JS
├── build.bat          - Build script (Windows)
└── README.md          - This file
```

## Features

### Run Tests Tab
- **Backend Selection**: Direct API (fast) or TCP Server (integration testing)
- **Parallelization**: 1-8 workers, batch size, parallel mode
- **Presets**: Quick (1 min), Standard (3 min), Extended (10 min), Overnight (60 min)
- **Modes**: All 13 M110A modes (75S-4800S) with quick-select
- **Test Categories**: Clean loopback, AWGN, Multipath, Freq Offset, Message Sizes, Random Data, DFE, MLSE
- **Channel Parameters**: SNR levels (10-30 dB), Multipath delays, Echo gain, Freq offsets
- **Message Options**: Custom test message, variable sizes
- **Output Options**: Report generation, CSV export, verbose mode, PCM saving
- **Real-time Results**: Live progress, pass/fail stats, BER metrics

### Cross-Modem Interop Tab
- PhoenixNest server management
- Paul Brain modem connection
- Brain → PhoenixNest tests
- PhoenixNest → Brain tests
- Full compatibility matrix

### Reports Tab
- List saved test reports
- View markdown reports in browser
- Export results (Markdown, CSV, JSON)

## Building

### Windows (MinGW)
```batch
cd test\test_gui
build.bat
```

### Manual Build
```batch
g++ -std=c++17 -O2 -I../.. -I../../src -I../../src/common -D_USE_MATH_DEFINES -o test_gui.exe main.cpp -lws2_32 -lshell32
```

## Usage

```batch
test_gui.exe [--port 8080]
```

Opens browser to http://localhost:8080

## Integration Notes

This is a header-only implementation for simplicity. All code is in .h files
except for main.cpp which provides the entry point.

To add new features:
1. **New routes**: Add handler in `server.h::handle_client()`
2. **New UI elements**: Modify HTML in `html_content.h`
3. **New test types**: Add to `test_config.h` and implement in server
4. **Protocol changes**: Modify `brain_client.h` or `pn_client.h`
