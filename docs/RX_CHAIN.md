# MIL-STD-188-110A Modem RX Chain Documentation

## Overview

The receiver chain consists of:
1. **Preamble Detection** - Correlates against known preamble pattern
2. **Symbol Extraction** - Brain Modem compatible demodulation
3. **Phase Tracking** - Corrects frequency offsets (when equalizer=NONE)
4. **Equalization** - DFE or MLSE for multipath compensation
5. **Viterbi Decoding** - FEC decoding with deinterleaving

## Equalizer Options

### NONE
No equalization. Suitable for AWGN channels with no multipath.

### DFE (Default)
Decision Feedback Equalizer with preamble pretraining and probe-aided adaptation.

**Algorithm Options:**
- **Standard LMS** (default): Fixed step size, conservative tuning (Î¼=0.005)
- **NLMS** (`use_nlms=true`): Normalized LMS, step size adapts to signal power

**When to use NLMS:**
- Time-varying channels (CCIR Moderate/Poor)
- Unknown or varying signal levels
- Fast fading conditions

**NLMS Performance (vs standard LMS):**
| Channel | LMS | NLMS | Improvement |
|---------|-----|------|-------------|
| CCIR Good @ 20dB | 38% | 32% | 16% |
| CCIR Moderate @ 20dB | 45% | 40% | 12% |

**Preamble Pretraining:**
- Uses first 288 preamble symbols (common segment) to initialize DFE
- Provides known reference before data starts
- Two training passes for better convergence
- Results in ~60% BER improvement on CCIR Good @ 18 dB

**Probe Adaptation:**
- Feedforward filter: 11 taps (~4.5ms at 2400 baud)
- Feedback filter: 5 taps
- LMS adaptation: Î¼=0.005 (conservative for fading)

**Best for:** General HF channels with mild to moderate fading.

### MLSE_L2 / MLSE_L3
Maximum Likelihood Sequence Estimation using Viterbi algorithm.
- L2: 8 states, L3: 64 states
- Uses probe symbols for channel estimation (Least Squares)
- Theoretically optimal for multipath detection

**Best for:** Channels where channel estimation is accurate.

## Phase Tracking

Adaptive phase tracking corrects slow frequency drift and small carrier offsets.

**When applied:** Only when `equalizer = Equalizer::NONE`

**Why not with DFE/MLSE?** 
- DFE handles phase via adaptive filters
- MLSE incorporates phase in channel model
- Phase tracking can interfere with equalizer adaptation on fading channels

**Performance:**
| Condition | Without PT | With PT |
|-----------|------------|---------|
| 0 Hz offset | 0% BER | 0% BER |
| 1.5 Hz offset | 30% BER | 0% BER |

### Configuration

```cpp
RxConfig cfg;
cfg.equalizer = Equalizer::DFE;   // Default - best general purpose
cfg.use_nlms = true;              // Enable NLMS for fading channels
cfg.phase_tracking = true;        // Enabled by default (applies when eq=NONE)
```

## Performance Summary

| Channel | NONE | DFE | MLSE_L2 | Notes |
|---------|------|-----|---------|-------|
| Clean/AWGN | 0% | 0% | 0% | All work |
| CCIR Good @ 20 dB | 0.4% | 0.5% | 0% | Similar |
| CCIR Good @ 18 dB (avg) | 31% | 12% | - | DFE 60% better |
| CCIR Moderate | 51% | 51% | 51% | Use lower rates |
| 1.5 Hz offset + PT | 0% | 51% | 51% | PT helps |

**Note:** DFE with preamble pretraining shows significant improvement on 
moderately challenging channels (CCIR Good @ 18 dB).

## Recommendations

1. **Default:** Use DFE for best general-purpose performance
2. **Frequency offset:** Use NONE + phase_tracking if only freq offset (no fading)
3. **Severe fading:** Reduce data rate (M600S instead of M2400S)
4. **Clean channel:** Any equalizer works

## EOM (End of Message) Marker

The EOM marker indicates clean transmission end per MIL-STD-188-110A.

### TX Side
- 4 flush frames appended after data
- Each frame: scrambled zeros (data) + normal probes
- Enabled by default: `TxConfig.include_eom = true`

### RX Side  
- Detected by counting trailing zeros in decoded data
- Threshold: 40+ trailing zero bytes
- `DecodeResult.eom_detected` indicates if EOM found
- Zero padding from EOM is automatically stripped

### Configuration

```cpp
// TX
TxConfig tx_cfg;
tx_cfg.include_eom = true;  // Default - add EOM marker

// RX
DecodeResult result = rx.decode(samples);
if (result.eom_detected) {
    // Clean transmission end detected
}
```

### Limitations
- Short messages (<100 bytes) may not reliably detect EOM
  due to interleaver padding also producing trailing zeros
- EOM detection is probabilistic, not guaranteed

## Files

- `api/modem_rx.cpp` - Main RX implementation
- `src/equalizer/dfe.h` - DFE implementation
- `src/dsp/mlse_equalizer.h` - MLSE implementation
- `src/dsp/phase_tracker.h` - Phase tracking PLL
