---
applyTo: "hfchansim/**"
---

# HF Channel Simulator Instructions

## Overview

This folder contains the HF channel simulator for testing modem performance under realistic propagation conditions.

## Purpose

The HF channel simulator adds realistic impairments to audio signals:
- Multipath propagation (multiple delayed signal paths)
- Fading (Rayleigh/Rician)
- Doppler spread
- Noise (AWGN, atmospheric, man-made)
- Frequency offset

## Key File

- `main.cpp` - HF channel simulator implementation

## Usage

```bash
hfchansim.exe <input.pcm> <output.pcm> [options]
```

## Channel Models

Based on ITU-R F.1487 and CCIR 520 recommendations:
- Good conditions: Low delay spread, minimal fading
- Moderate conditions: Medium multipath, some fading
- Poor conditions: Severe multipath, deep fades

## Rules

1. **Preserve signal integrity** - Don't clip or saturate signals
2. **Use realistic parameters** - Base on ITU/CCIR standards
3. **Document channel profiles** - Explain what conditions each simulates
4. **Keep reproducible** - Use seed for random number generation

## Testing

When modifying the channel simulator:
1. Test with known-good PCM files
2. Verify output signal levels are reasonable
3. Compare with reference channel implementations
