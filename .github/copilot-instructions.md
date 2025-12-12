# MIL-STD-188-110A Modem Project

## ⚠️ FROZEN BLOCKS - DO NOT MODIFY ⚠️

**This is the most important rule in this project.**

Code sections marked with `FROZEN BLOCK` comments are protected and must NOT be modified without explicit permission from the project owner. These blocks contain critical, tested, and stable code.

### Frozen Block Format
```
# ============================================================
# FROZEN BLOCK: <Name> - DO NOT MODIFY WITHOUT PERMISSION
# ============================================================
```

Or in C/C++:
```cpp
// ============================================================
// FROZEN BLOCK: <Name> - DO NOT MODIFY WITHOUT PERMISSION
// ============================================================
```

### Rules
1. **NEVER** modify code inside a frozen block
2. **NEVER** delete or rename frozen block markers
3. If changes are absolutely necessary, ask the user first
4. When adding new code, add it OUTSIDE frozen blocks
5. Frozen blocks exist in: `build.ps1`, and may exist in other critical files

### Current Frozen Blocks in build.ps1
- Parameters
- Help Text
- Version Management
- version.h Generation
- Build Configuration
- Main Build Logic
- Build Targets
- Brain Server Build
- Build Execution
- Target Filtering
- Build Summary

---

## 📁 Path-Specific Instructions

This project uses GitHub's official path-specific custom instructions system. Additional rules for specific folders are defined in `.github/instructions/*.instructions.md` files with glob patterns.

Current path-specific instructions:
- `api.instructions.md` → `api/**` - Public API, version.h is auto-generated
- `extern.instructions.md` → `extern/**` - External dependencies, DO NOT MODIFY
- `src.instructions.md` → `src/**` - Core implementation, dependency awareness
- `testing.instructions.md` → `testing/**` - Test framework and interop testing rules

These are automatically applied when working on files matching the glob patterns.

---

## Project Overview
This project implements a MIL-STD-188-110A HF modem with modulation/demodulation capabilities for military standard communications.

## Coding Guidelines
- Use C++17 standard for core signal processing
- Follow MISRA C guidelines where applicable for safety-critical code
- Use fixed-point arithmetic for DSP operations where possible
- Document all signal processing algorithms with references to MIL-STD-118-110A sections

## Project Structure
- `src/` - Source files for modem implementation
- `api/` - Public API headers and modem interface
- `server/` - TCP server implementation
- `extern/` - External dependencies (brain_core, codec2)
- `test/` - Unit tests and test GUI
- `testing/` - Interop testing scripts
- `docs/` - Documentation and specifications
- `release/bin/` - Build output (executables)

## Build System
- PowerShell-based build system (`build.ps1`)
- All executables output to `release/bin/`
- Uses MinGW g++ compiler with C++17

## Key Components
- PSK modulator/demodulator (8-PSK, QPSK, BPSK)
- Interleaver/de-interleaver
- FEC encoder/decoder (convolutional codes)
- Symbol timing recovery
- Carrier recovery
- AGC (Automatic Gain Control)
- Equalizer
