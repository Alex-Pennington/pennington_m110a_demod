# MIL-STD-188-110A Transmitter Chain

## Overview

The transmitter converts data bytes to audio samples suitable for HF transmission.

```
Data â†’ FEC â†’ Interleave â†’ Scramble â†’ Map â†’ Insert Probes â†’ Preamble â†’ Modulate
```

## Signal Flow

### 1. FEC Encoding

Rate 1/2 convolutional code (except M4800 uncoded):
- Constraint length: K=7
- Generator polynomials: G1=0x6D, G2=0x4F (Brain Modem compatible)
- 2 output bits per input bit

```
Input:  [d0, d1, d2, ...]
Output: [c0a, c0b, c1a, c1b, c2a, c2b, ...]
```

### 2. Bit Repetition (Low-Rate Modes)

For 150/300/600 bps, coded bits are repeated:

| Mode | Repetition |
|------|------------|
| 150 bps | 16Ã— |
| 300 bps | 8Ã— |
| 600 bps | 4Ã— |
| 1200+ bps | 1Ã— (none) |

### 3. Interleaving

Block interleaver with helical write/sequential read:

| Interleave | Rows | Cols | Block Size |
|------------|------|------|------------|
| SHORT | 40 | 576 | 23,040 bits |
| LONG | 40 | 4608 | 184,320 bits |

Data is padded to block size boundary.

### 4. Scrambling

12-bit LFSR scrambler (continuous across entire transmission):

```cpp
// Polynomial: x^12 + x^11 + x^10 + x^9 + x^7 + x^4 + 1
uint16_t state = 0x0BAD;  // Initial state
uint8_t bit = ((state >> 11) ^ (state >> 10) ^ 
               (state >> 9) ^ (state >> 6) ^ 
               (state >> 3) ^ state) & 1;
state = (state << 1) | bit;
```

Scrambler output (0-7) is added mod 8 to Gray-coded tribits.

### 5. Symbol Mapping

Tribits mapped to 8-PSK constellation via Gray code:

```
Tribit â†’ Gray â†’ Phase
  0   â†’   0  â†’   0Â°
  1   â†’   1  â†’  45Â°
  2   â†’   3  â†’ 135Â°
  3   â†’   2  â†’  90Â°
  4   â†’   7  â†’ 315Â°
  5   â†’   6  â†’ 270Â°
  6   â†’   4  â†’ 180Â°
  7   â†’   5  â†’ 225Â°
```

### 6. Probe Insertion

Known symbols inserted every frame for channel estimation:

| Mode | Data | Probes | Frame |
|------|------|--------|-------|
| 150-2400 bps | 32 | 16 | 48 |
| 4800 bps | 20 | 20 | 40 |
| 75 bps | 32 | 0 | 32 (Walsh) |

Probes are scrambler output only (data = 0).

### 7. Preamble Generation

Three segments, each 0.2 seconds (480 symbols):

**Segment 1-2:** Common sync pattern
- 9 blocks Ã— 32 symbols
- Pattern determined by D-sequence
- Same for all modes (enables sync before mode known)

**Segment 3:** Mode-specific
- D1/D2 values encode data rate and interleave
- Allows receiver to auto-detect mode

### 8. EOM (End of Message)

4 flush frames appended after data:
- Data portion: scrambled zeros
- Probe portion: normal scrambler sequence
- Signals clean transmission end

### 9. Modulation

Baseband symbols upconverted to audio:

```cpp
// Without pulse shaping
for (symbol : symbols) {
    for (i = 0; i < samples_per_symbol; i++) {
        carrier = nco.next();  // cos + j*sin
        output[n++] = symbol.real * carrier.real 
                    - symbol.imag * carrier.imag;
    }
}
```

**With RRC Pulse Shaping:**
- SRRC filter (Î±=0.35, 6 symbol span)
- Better spectral efficiency
- Requires matched filter in receiver

## Configuration

```cpp
TxConfig tx_cfg;
tx_cfg.mode = Mode::M2400_SHORT;
tx_cfg.sample_rate = 48000.0f;     // or 8000
tx_cfg.carrier_freq = 1800.0f;     // 500-3000 Hz
tx_cfg.amplitude = 0.8f;           // 0.0-1.0
tx_cfg.include_preamble = true;    // Add sync preamble
tx_cfg.include_eom = true;         // Add end marker
tx_cfg.use_pulse_shaping = false;  // RRC filtering
```

## Timing

| Mode | Symbol Rate | Samples/Symbol (48kHz) |
|------|-------------|------------------------|
| All | 2400 baud | 20 |

### Transmission Duration

```
Duration = Preamble + Data + EOM
         = 0.6s + (symbols / 2400) + 0.08s

// M2400S, 100 bytes:
// FEC: 200 bytes â†’ 1600 bits
// Symbols: 1600/3 â‰ˆ 534
// Frames: 534/32 = 17 â†’ 17Ã—48 = 816 symbols
// Duration: 0.6 + 0.34 + 0.08 = 1.02 seconds
```

## Example

```cpp
#include "api/modem.h"
using namespace m110a::api;

TxConfig cfg;
cfg.mode = Mode::M1200_SHORT;
cfg.sample_rate = 48000.0f;
cfg.include_eom = true;

ModemTX tx(cfg);

std::vector<uint8_t> message = {'H', 'E', 'L', 'L', 'O'};
auto result = tx.encode(message);

if (result.ok()) {
    auto& samples = result.value();
    // samples.size() â‰ˆ 48000 * 0.8 seconds
    // Write to audio device or file
}
```

## Files

- `api/modem_tx.cpp` - TX implementation
- `src/modem/m110a_codec.h` - FEC, interleaver, mapping
- `src/m110a/brain_preamble.h` - Preamble generation
- `src/modem/scrambler.h` - Scrambler implementation
