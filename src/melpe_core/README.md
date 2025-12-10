# MELPe Codec - Self-Contained Export

NATO STANAG 4591 Mixed Excitation Linear Prediction (MELPe) voice codec.

## Features

- **Bit Rates**: 600, 1200, 2400 bps
- **Audio Format**: 8000 Hz, 16-bit signed PCM, mono
- **Fixed-Point**: All DSP in Q15 format (no floating point required)

## Quick Start

### Using CMake (Windows/Linux/macOS)

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Using Make (Linux/macOS)

```bash
make
```

### Using MinGW (Windows)

```bash
mingw32-make
```

## Integration

### Link the Static Library

```c
#include "melpe_api.h"

// Create encoder (2400 bps with noise pre-processor)
melpe_encoder_t *enc = melpe_encoder_create(2400, true);

// Create decoder
melpe_decoder_t *dec = melpe_decoder_create(2400);

// Encode: PCM samples → bitstream
int16_t samples[180];  // 22.5 ms at 8 kHz
uint8_t bits[7];       // 54 bits packed
int bytes = melpe_encoder_process(enc, samples, 180, bits, sizeof(bits));

// Decode: bitstream → PCM samples
int16_t output[180];
int decoded = melpe_decoder_process(dec, bits, 7, output, 180);

// Cleanup
melpe_encoder_destroy(enc);
melpe_decoder_destroy(dec);
```

## Frame Sizes

| Rate | Samples/Frame | Bytes/Frame | Duration |
|------|---------------|-------------|----------|
| 2400 | 180 | 7 | 22.5 ms |
| 1200 | 540 | 11 | 67.5 ms |
| 600 | 720 | 7 | 90.0 ms |

## Files

| File | Description |
|------|-------------|
| `melpe_api.h` | **Main API header** - include this |
| `melpe_api.c` | Streaming encoder/decoder wrapper |
| `melp_ana.c` | Encoder analysis |
| `melp_syn.c` | Decoder synthesis |
| `melp_chn.c` | Channel coding |
| `sc1200.h` | Core codec definitions |
| `lib600_*.c` | 600 bps specific code |

## Command-Line Tool

After building, use `melpe_cli` for testing:

```bash
# Encode
./melpe_cli -i input.raw -o encoded.bin -r 2400

# Decode  
./melpe_cli -i encoded.bin -o output.raw -r 2400 -d

# Loopback (encode+decode)
./melpe_cli -i input.raw -o output.raw -r 2400 -l
```

Input/output files are raw PCM: 16-bit signed, little-endian, 8000 Hz, mono.

## License

This implementation is based on the public STANAG 4591 specification.
