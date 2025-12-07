# Equalizer Guide

## Overview

HF channels introduce multipath propagation and fading that cause Inter-Symbol Interference (ISI). Equalizers compensate for these channel effects.

## Equalizer Options

| Equalizer | Complexity | Best For |
|-----------|------------|----------|
| NONE | O(1) | Clean AWGN channels |
| DFE | O(N) | General HF, mild fading |
| MLSE_L2 | O(64) | Moderate multipath |
| MLSE_L3 | O(512) | Severe multipath |

## NONE (No Equalization)

Simply passes symbols through without modification.

**When to use:**
- Clean channels (line-of-sight, cables)
- AWGN-only conditions
- Testing/debugging

**Configuration:**
```cpp
RxConfig cfg;
cfg.equalizer = Equalizer::NONE;
cfg.phase_tracking = true;  // Enable for frequency offset
```

**Note:** Enable `phase_tracking` to handle small frequency offsets.

---

## DFE (Decision Feedback Equalizer)

The default equalizer. Uses feedforward and feedback filters to cancel ISI.

### Architecture

```
Input → [FF Filter] → (+) → Decision → Output
                       ↑
         [FB Filter] ←─┘
```

- **Feedforward (FF):** 11 taps, cancels precursor ISI
- **Feedback (FB):** 5 taps, cancels postcursor ISI using decisions

### Adaptation

**Standard LMS:**
```
w[k] += μ × error × conj(x[k])
```
- μ_ff = 0.005 (conservative)
- μ_fb = 0.002

**NLMS (Normalized LMS):**
```
w[k] += μ / (δ + ||x||²) × error × conj(x[k])
```
- Normalizes by input power
- Faster convergence on fading
- μ_ff = 0.3, μ_fb = 0.15 (larger since normalized)

### Training

1. **Preamble Pretraining:** First 288 symbols of preamble (known sequence)
2. **Probe Training:** 16 known probe symbols every frame
3. **Decision-Directed:** Data symbols use hard decisions

### Configuration

```cpp
RxConfig cfg;
cfg.equalizer = Equalizer::DFE;
cfg.use_nlms = false;  // Standard LMS (default)

// For fading channels:
cfg.use_nlms = true;   // Enable NLMS
```

### Performance

| Channel | LMS | NLMS |
|---------|-----|------|
| CCIR Good @ 20dB | 38% BER | 32% BER |
| CCIR Moderate @ 20dB | 45% BER | 40% BER |

NLMS provides **10-16% improvement** on fading channels.

---

## MLSE (Maximum Likelihood Sequence Estimation)

Viterbi algorithm finds most likely transmitted sequence given channel model.

### Theory

Models channel as FIR filter:
```
y[n] = h[0]×s[n] + h[1]×s[n-1] + ... + h[L-1]×s[n-L+1] + noise
```

Viterbi algorithm:
1. Maintains state = last L-1 symbols
2. For each new symbol, computes metrics for all state transitions
3. Keeps best path to each state
4. Traces back to find ML sequence

### Variants

**MLSE_L2 (L=2):**
- 8 states (8^1 = 8)
- Handles 1-symbol ISI
- Good for short delay spread

**MLSE_L3 (L=3):**
- 64 states (8^2 = 64)
- Handles 2-symbol ISI
- Better for longer delay spread
- 8× more computation

### Channel Estimation

Uses probe symbols (Least Squares):
```
H = (X^H × X)^(-1) × X^H × Y
```

Where X is Toeplitz matrix of transmitted probes, Y is received probes.

### Configuration

```cpp
RxConfig cfg;
cfg.equalizer = Equalizer::MLSE_L2;  // 8 states
// or
cfg.equalizer = Equalizer::MLSE_L3;  // 64 states
```

### Performance

| Channel | DFE | MLSE_L2 |
|---------|-----|---------|
| CCIR Good @ 20dB | 0.5% | 0% |
| Static multipath | 5% | 0.1% |

MLSE excels when channel estimation is accurate.

### Limitations

- Requires accurate channel estimate
- Single estimate per frame (doesn't track fast fading)
- Higher complexity than DFE

---

## Phase Tracking

2nd-order PLL for frequency offset correction.

### When Applied

**Only when `equalizer = NONE`**

DFE and MLSE handle phase internally:
- DFE: Adaptive filters absorb phase
- MLSE: Phase in channel model

Applying phase tracking with DFE/MLSE can cause interference.

### Configuration

```cpp
RxConfig cfg;
cfg.equalizer = Equalizer::NONE;
cfg.phase_tracking = true;  // Enable PLL
```

### Performance

| Condition | Without PT | With PT |
|-----------|------------|---------|
| 0 Hz offset | 0% BER | 0% BER |
| 1.5 Hz offset | 30% BER | 0% BER |

---

## Selection Guide

### Decision Tree

```
Is channel clean (AWGN only)?
├─ Yes → NONE + phase_tracking
└─ No → Is fading fast?
         ├─ Yes → DFE + use_nlms
         └─ No → Is delay spread long?
                  ├─ Yes → MLSE_L3
                  └─ No → DFE or MLSE_L2
```

### Recommendations by Channel

| Channel Type | Recommended |
|--------------|-------------|
| Cable/Clean | NONE + PT |
| AWGN | NONE + PT |
| CCIR Good | DFE |
| CCIR Good (fast fading) | DFE + NLMS |
| CCIR Moderate | DFE + NLMS, lower rate |
| CCIR Poor | Lower rate, DFE + NLMS |
| Static multipath | MLSE_L2 or MLSE_L3 |

### Performance Tips

1. **Start with DFE** - good default
2. **Enable NLMS** for fading conditions
3. **Try MLSE** if DFE struggles with multipath
4. **Reduce data rate** for severe conditions
5. **Don't use phase_tracking with DFE/MLSE**

---

## Implementation Details

### DFE Files
- `src/equalizer/dfe.h` - DFE class
- `api/modem_rx.cpp` - `apply_dfe_equalization()`

### MLSE Files
- `src/dsp/mlse_equalizer.h` - MLSE class
- `api/modem_rx.cpp` - `apply_mlse_equalization()`

### Phase Tracker Files
- `src/dsp/phase_tracker.h` - PLL implementation
- `api/modem_rx.cpp` - Phase correction logic

---

## Benchmarks

Test conditions: M2400_SHORT, 100 bytes, 10 trials average

### CCIR Good @ 20 dB
| Equalizer | BER |
|-----------|-----|
| NONE | 30% |
| DFE | 0.5% |
| DFE+NLMS | 0.3% |
| MLSE_L2 | 0% |

### CCIR Moderate @ 20 dB
| Equalizer | BER |
|-----------|-----|
| NONE | 50% |
| DFE | 45% |
| DFE+NLMS | 40% |
| MLSE_L2 | 50% |

Note: MLSE degrades on fast fading due to single-frame channel estimate.

### Clean + 1.5 Hz Offset
| Equalizer | BER |
|-----------|-----|
| NONE | 30% |
| NONE + PT | 0% |
| DFE | 0% |
