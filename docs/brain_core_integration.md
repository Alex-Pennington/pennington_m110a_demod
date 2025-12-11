# Brain_Core Integration Guide

## Overview

The `brain_core` submodule provides Paul Shortino's MIL-STD-188-110A modem implementation (Cm110s class) which we integrate for interoperability testing against PhoenixNest.

## Architecture

```
extern/
  brain_core/           <- Git submodule
    m188110a/
      Cm110s.h          <- Main modem class
      *.cpp             <- Implementation files
  brain_wrapper.h       <- PhoenixNest wrapper API

test/
  interop_test.cpp      <- Bidirectional compatibility tests
  test_gui/
    server.h            <- HTTP server with /run-interop endpoint
    html_content.h      <- Web UI with streaming matrix
```

## Critical Implementation Details

### 1. Stack Overflow Prevention

The Cm110s class contains massive static arrays:
```cpp
int tx_bit_array[400000];  // 1.6MB!
```

**Solution**: Always heap-allocate:
```cpp
class Modem {
    Cm110s* modem_;  // Pointer, not value
    Modem() : modem_(new Cm110s()) { ... }
    ~Modem() { delete modem_; }
};
```

### 2. Status Callback Required

Brain crashes at 1920 sample boundary (one frame) if no status callback registered.

**Solution**: Register dummy callback:
```cpp
static void status_callback_static(ModemStatus status, void* data) {
    (void)status; (void)data;  // Ignore, just prevents crash
}

// In constructor:
modem_->register_status(status_callback_static);
```

### 3. Windows Header Conflicts

Two conflicts with Windows SDK:

**std::byte collision** (rpcndr.h `typedef unsigned char byte`):
```cpp
// Include Brain/Windows headers BEFORE C++ stdlib
#include "extern/brain_wrapper.h"  // FIRST
#include <iostream>                // THEN stdlib
```

**ERROR macro** (wingdi.h):
```cpp
// In modem_rx.h, renamed:
enum class RxState { DECODE_ERROR };  // Was: ERROR
```

### 4. Sample Rate Conversion

- Brain native: 9600 Hz
- PhoenixNest: 48000 Hz
- Ratio: 5:1

Wrapper handles decimation/interpolation automatically.

### 5. Mode Mapping

```cpp
const std::map<int, int> PN_TO_BRAIN = {
    {PN::M75S, ::M75S},   {PN::M75L, ::M75L},
    {PN::M150S, ::M150S}, {PN::M150L, ::M150L},
    // ... etc
};
```

## Building

### Interop Test
```powershell
.\build.ps1 -Target interop
```

### Test GUI
```powershell
cd test\test_gui
.\build.bat
```

## Streaming JSON Protocol

### interop_test.exe --json

Events sent to stdout:
```json
{"event":"start","total_tests":36,"message_size":54}
{"event":"result","mode":"600S","pn_brain":false,"brain_pn":true,"auto":false,"detected":"---"}
{"event":"complete","passed":9,"total":36,"elapsed":112.4}
```

### Server /run-interop

Streams SSE events:
```
data: {"output":"Testing 600S...","type":"interop_result","mode":"600S","pn_brain":false,"brain_pn":true}
```

### Web UI

Matrix updates in real-time via `handleInteropEvent()`:
- Green ✓ = PASS
- Red ✗ = FAIL
- Gray ○ = Pending

## Current Test Results (2024-12-11)

| Direction | Pass Rate |
|-----------|-----------|
| PN TX → Brain RX | 0/12 (0%) |
| Brain TX → PN RX | 9/12 (75%) |
| Auto-detect | 0/12 (0%) |

### Known Issues

1. PN encoder may have subtle differences from Brain
2. Auto-detect often returns wrong mode (e.g., "1200L" for 75S)
3. Higher data rates (2400S/L) have interop issues

## Next Steps

1. Debug PN encoder differences
2. Fix mode auto-detection
3. Add to CI pipeline
4. Test with external brain_core server (TCP mode)
