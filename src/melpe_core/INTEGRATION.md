# MELPe Codec Integration Guide

## Overview

This is a self-contained export of the **NATO STANAG 4591 MELPe** (Mixed Excitation Linear Prediction enhanced) voice codec. It provides military-grade voice encoding at extremely low bit rates suitable for HF/VHF radio communications.

## Specifications

| Parameter | Value |
|-----------|-------|
| **Bit Rates** | 600, 1200, 2400 bps |
| **Sample Rate** | 8000 Hz |
| **Sample Format** | 16-bit signed PCM, mono |
| **Frame Duration** | 22.5 ms (2400), 67.5 ms (1200), 90 ms (600) |
| **Latency** | 1 frame + processing |
| **Arithmetic** | Fixed-point Q15 (no FPU required) |

## Frame Sizes

| Rate | Samples/Frame | Bits/Frame | Bytes/Frame |
|------|---------------|------------|-------------|
| 2400 bps | 180 | 54 | 7 |
| 1200 bps | 540 | 81 | 11 |
| 600 bps | 720 | 54 | 7 |

## Directory Structure

```
melpe_export/
├── CMakeLists.txt      # CMake build system
├── Makefile            # GNU Make build system
├── README.md           # Quick start guide
├── INTEGRATION.md      # This file
│
├── melpe_api.h         # ★ PRIMARY API HEADER
├── melpe_api.c         # Streaming encoder/decoder wrapper
│
├── sc1200.h            # Core codec definitions
├── sc600.h             # 600 bps definitions
│
├── melp_ana.c          # Encoder analysis
├── melp_syn.c          # Decoder synthesis
├── melp_chn.c          # Channel coding
├── melp_sub.c          # Common subroutines
│
├── dsp_sub.c/h         # DSP subroutines
├── fft_lib.c/h         # FFT library
├── lpc_lib.c/h         # LPC analysis
├── pitch.c/h           # Pitch detection
├── npp.c/h             # Noise pre-processor
│
├── lib600_*.c/h        # 600 bps specific modules
├── var600_*.c          # 600 bps variables
├── cbk600_*.c          # 600 bps codebooks
│
└── build/              # Build output directory
    ├── libmelpe.a      # Core codec library
    ├── libmelpe_api.a  # Streaming API library
    └── melpe_cli.exe   # Command-line tool
```

## Building

### CMake (Recommended - Cross Platform)

```bash
mkdir build && cd build
cmake .. -G "MinGW Makefiles"   # Windows with MinGW
cmake .. -G "Unix Makefiles"    # Linux/macOS
cmake --build .
```

### GNU Make (Linux/macOS)

```bash
make
```

### Visual Studio

```bash
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

## API Reference

### Include Header

```c
#include "melpe_api.h"
```

### Create Encoder

```c
melpe_encoder_t* melpe_encoder_create(int rate, bool enable_npp);
```

- `rate`: Bit rate (2400, 1200, or 600)
- `enable_npp`: Enable noise pre-processor (recommended for real audio)
- Returns: Encoder handle or NULL on error

### Create Decoder

```c
melpe_decoder_t* melpe_decoder_create(int rate);
```

- `rate`: Bit rate (must match encoder)
- Returns: Decoder handle or NULL on error

### Encode Audio

```c
int melpe_encoder_process(
    melpe_encoder_t *enc,
    const int16_t *samples,
    int num_samples,
    uint8_t *output,
    int output_size
);
```

- `samples`: Input PCM audio (16-bit signed, 8 kHz)
- `num_samples`: Number of input samples
- `output`: Buffer for encoded bitstream
- `output_size`: Size of output buffer
- Returns: Number of bytes written (0 if buffering, -1 on error)

### Decode Audio

```c
int melpe_decoder_process(
    melpe_decoder_t *dec,
    const uint8_t *bits,
    int num_bytes,
    int16_t *output,
    int output_size
);
```

- `bits`: Encoded bitstream
- `num_bytes`: Number of input bytes
- `output`: Buffer for decoded PCM samples
- `output_size`: Size of output buffer in samples
- Returns: Number of samples decoded (-1 on error)

### Cleanup

```c
void melpe_encoder_destroy(melpe_encoder_t *enc);
void melpe_decoder_destroy(melpe_decoder_t *dec);
```

### Helper Functions

```c
int melpe_get_frame_samples(int rate);  // Samples per frame
int melpe_get_frame_bytes(int rate);    // Bytes per encoded frame
```

## Integration Examples

### Example 1: Simple Encode/Decode Loop

```c
#include "melpe_api.h"
#include <stdio.h>

int main() {
    const int RATE = 2400;
    const int FRAME_SAMPLES = 180;
    const int FRAME_BYTES = 7;
    
    // Create encoder and decoder
    melpe_encoder_t *enc = melpe_encoder_create(RATE, true);
    melpe_decoder_t *dec = melpe_decoder_create(RATE);
    
    int16_t pcm_in[FRAME_SAMPLES];
    int16_t pcm_out[FRAME_SAMPLES];
    uint8_t bitstream[FRAME_BYTES];
    
    FILE *fin = fopen("input.raw", "rb");
    FILE *fout = fopen("output.raw", "wb");
    
    while (fread(pcm_in, sizeof(int16_t), FRAME_SAMPLES, fin) == FRAME_SAMPLES) {
        // Encode
        int encoded = melpe_encoder_process(enc, pcm_in, FRAME_SAMPLES, 
                                            bitstream, sizeof(bitstream));
        if (encoded > 0) {
            // Decode
            int decoded = melpe_decoder_process(dec, bitstream, encoded,
                                                pcm_out, FRAME_SAMPLES);
            if (decoded > 0) {
                fwrite(pcm_out, sizeof(int16_t), decoded, fout);
            }
        }
    }
    
    melpe_encoder_destroy(enc);
    melpe_decoder_destroy(dec);
    fclose(fin);
    fclose(fout);
    return 0;
}
```

### Example 2: Streaming with Callbacks

```c
#include "melpe_api.h"

void on_encoded(const uint8_t *bits, int num_bytes, void *user_data) {
    // Send bits over radio channel
    radio_transmit(bits, num_bytes);
}

void setup_encoder() {
    melpe_encoder_t *enc = melpe_encoder_create(2400, true);
    melpe_encoder_set_callback(enc, on_encoded, NULL);
    
    // Feed audio samples continuously
    // Callback fires automatically when frame is ready
}
```

### Example 3: CMake Integration

In your project's CMakeLists.txt:

```cmake
# Add MELPe as subdirectory
add_subdirectory(melpe_export)

# Link to your target
add_executable(my_radio_app main.c)
target_link_libraries(my_radio_app PRIVATE melpe_api)
```

### Example 4: Direct Static Library Linking

```bash
# Compile your application
gcc -o my_app my_app.c -I./melpe_export -L./melpe_export/build -lmelpe_api -lmelpe -lm
```

## Platform Notes

### Windows
- Use MinGW-w64 or MSVC
- No special dependencies

### Linux
- Requires only libc and libm
- Tested on x86_64 and ARM

### Embedded
- Pure C99, no dynamic allocation in core codec
- Fixed-point arithmetic (Q15)
- No FPU required
- Typical RAM: ~20 KB per encoder/decoder instance

## Audio Format Conversion

### From WAV to Raw PCM

```bash
ffmpeg -i input.wav -f s16le -ar 8000 -ac 1 input.raw
```

### From Raw PCM to WAV

```bash
ffmpeg -f s16le -ar 8000 -ac 1 -i output.raw output.wav
```

### Using SoX

```bash
sox input.wav -r 8000 -b 16 -c 1 -e signed-integer input.raw
sox -r 8000 -b 16 -c 1 -e signed-integer output.raw output.wav
```

## Troubleshooting

### No audio output
- Verify input is 8000 Hz, 16-bit signed, mono
- Check frame alignment (samples must be multiple of frame size)
- Ensure encoder and decoder rates match

### Distorted audio
- Disable NPP if input is already clean
- Check for sample rate mismatch
- Verify PCM format (little-endian on x86)

### Build errors
- Ensure C99 compiler support
- Check all source files are present (108 files)
- Verify include paths

## Performance

Typical performance on modern hardware:

| Platform | Rate | Encode | Decode |
|----------|------|--------|--------|
| x86_64 3GHz | 2400 | 0.5ms/frame | 0.3ms/frame |
| ARM Cortex-A53 | 2400 | 2ms/frame | 1.5ms/frame |
| ARM Cortex-M4 | 2400 | 15ms/frame | 10ms/frame |

All rates are well under real-time on any modern processor.

## License

This implementation is based on the public NATO STANAG 4591 specification.

## Version History

- **1.0** - Initial self-contained export
  - Supports 600/1200/2400 bps
  - Streaming API with callbacks
  - Fixed-point implementation
