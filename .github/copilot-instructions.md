# MIL-STD-118-110A Modem Project

## Project Overview
This project implements a MIL-STD-118-110A HF modem with modulation/demodulation capabilities for military standard communications.

## Coding Guidelines
- Use C11/C17 standard for core signal processing
- Follow MISRA C guidelines where applicable for safety-critical code
- Use fixed-point arithmetic for DSP operations where possible
- Document all signal processing algorithms with references to MIL-STD-118-110A sections

## Project Structure
- `src/` - Source files for modem implementation
- `include/` - Header files
- `test/` - Unit tests
- `docs/` - Documentation and specifications

## Build System
- CMake-based build system
- Support for cross-compilation to embedded targets

## Key Components
- PSK modulator/demodulator (8-PSK, QPSK, BPSK)
- Interleaver/de-interleaver
- FEC encoder/decoder (convolutional codes)
- Symbol timing recovery
- Carrier recovery
- AGC (Automatic Gain Control)
- Equalizer
