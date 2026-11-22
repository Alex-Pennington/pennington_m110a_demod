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
- Generator polynomials: G1=0x6D, G2=0x4F (G4GUO Brain Core compatible)
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

With `include_eom` (the default), `ModemTX::encode` ends the message as MIL-STD-188-110
5.3.2.2.3-4 and 5.3.2.3.1-2 require. After the last data bit, the FEC encoder takes the
EOM 4B65A5B2, most significant digit first, then 144 zero flush bits, then zeros to the end
of the interleaver block that holds the last flush bit. At 4800 bps the same bits go out
uncoded. This is the ending other 110A modems scan for. `m110a_rx` and `ModemRX` cut the
decoded data at the EOM, so the payload comes back with nothing after it.

With `include_eom = false`, the data ends at its interleaver block and no EOM is sent.

`ModemTX::encode_bits` (600H, 1200H) always sends the EOM (docs/harris-tx.md).

`ModemRX` still reads the older `ModemTX` ending, four frames of zero data with no EOM.
When it finds no EOM, it strips the trailing zero bytes as before.

### 9. Modulation

The preamble, the data with its probes, and the message end are modulated as one symbol
stream, with or without pulse shaping. The 288 leading symbols (`include_leading_symbols`)
are modulated on their own, before the preamble.

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

**With RRC Pulse Shaping** (`use_pulse_shaping`):
- SRRC filter (alpha 0.35, 6 symbols each side), the receiver's matched filter
  (`SRRC_ALPHA`, `SRRC_SPAN_SYMBOLS`, src/common/constants.h)
- The filter adds 12 symbols of tail to each stream it shapes. Shaping the preamble, the data
  and the message end separately would put those 12 symbols between them, and the receiver
  would read the data 12 symbols late.

Samples per symbol is `int(sample_rate / 2400)`: 20 at 48 kHz. At 8 kHz that is 3, so the
symbol rate would be 2667 Bd. Use 48 kHz.

## Measured performance

These figures are ModemTX to `m110a_rx` (AUTO) at 48 kHz, through test/tx/tx_loopback.py
(ctest `tx_loopback_*`). Noise is white, with SNR measured in 3 kHz.

- **RRC pulses:** every standard mode, 150S to 4800S, decodes to the exact payload with
  `success=1` and `eom=1`, clean and at 10 dB. 4800S passes at 18 dB.
- **Rectangular pulses:** the same holds for 150S to 2400L, clean and at 10 dB. The pulses
  are not matched to the receiver's SRRC filter, and the ISI they leave caps the clean probe
  SNR at about 14 dB, against 55 dB for RRC with no equalizer.

4800 bps is uncoded 8PSK at 2400 Bd. Es/N0 = SNR(3 kHz) + 0.97 dB, and BER is about
(2/3)·Q(sqrt(2·Es/N0)·sin(pi/8)). That gives 8e-4 at 14 dB, 4.5e-5 at 16 dB and 1e-5 at
16.8 dB. MIL-STD-188-110 Table XVI asks 1e-3 at 17 dB on the fixed channel.

ModemTX to ModemRX at 4800S, 14,400 bits per point:

| SNR | RRC, DFE | RRC, no EQ | rect, DFE | rect, no EQ |
|---|---|---|---|---|
| 14 dB | 1.7e-3 | 4.2e-3 | 1.7e-2 | 2.3e-2 |
| 16 dB | 1.4e-4 | 4.2e-4 | 9.0e-3 | 1.5e-2 |
| 18 dB | 0 | 7e-5 | 5.0e-3 | 1.0e-2 |
| 30 dB | 0 | 0 | 5.6e-4 | 3.1e-3 |

With RRC pulses the modem is within about 0.7 dB of theory and meets Table XVI. With
rectangular pulses it has a BER floor near 5e-4, and 4800S needs about 30 dB to come back
bit-exact.

## Configuration

```cpp
TxConfig tx_cfg;
tx_cfg.mode = Mode::M2400_SHORT;
tx_cfg.sample_rate = 48000.0f;     // or 8000
tx_cfg.carrier_freq = 1800.0f;     // 500-3000 Hz
tx_cfg.amplitude = 0.8f;           // 0.0-1.0
tx_cfg.include_preamble = true;    // Add sync preamble
tx_cfg.include_eom = true;         // EOM 4B65A5B2 + flush (section 8)
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
