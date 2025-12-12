---
applyTo: "src/**"
---

# Source Code - Core Implementation

## Purpose

This folder contains the core modem implementation including DSP, sync, channel simulation, and vocoder code.

## Folder Structure

| Folder | Purpose | Impact |
|--------|---------|--------|
| `m110a/` | Core M110A modem logic | Affects all TX/RX |
| `sync/` | Symbol and carrier recovery | Affects all RX |
| `dsp/` | Digital signal processing | Core algorithms |
| `channel/` | Channel simulation | Testing only |
| `equalizer/` | Adaptive equalizer | RX performance |
| `io/` | PCM file I/O | File handling |
| `modem/` | Modem state machine | TX/RX flow |
| `vocoder/` | Voice codec integration | Voice modes |
| `common/` | Shared utilities | Everything |

## Dependency Awareness

Before modifying code in `src/`, understand what depends on it:

- `src/sync/` → Used by `api/modem_rx.cpp` → Affects all RX
- `src/m110a/` → Used by both TX and RX chains
- `src/common/` → Used everywhere

## Rules

1. **Test After Changes**: Any change here should be verified with the baseline tests in `testing/interop/baseline/`.

2. **Document Algorithms**: Reference MIL-STD-188-110A sections in comments.

3. **Fixed-Point Preference**: Use fixed-point arithmetic where possible for DSP operations.

4. **C++17 Standard**: All code should compile with `-std=c++17`.
