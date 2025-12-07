# Equalizer Implementation Plan for Watterson Channel Robustness

## Current Status (December 7, 2025)

**Test Results:**
| Test | Status | BER | Issue |
|------|--------|-----|-------|
| 1. Basic Loopback | PASS | 0% | - |
| 2. AWGN 15dB | PASS | 0% | - |
| 3. Static Multipath | FAIL | 21% | DFE cold start |
| 4. Slow Fading | PASS | 0% | Fixed by early-stop |
| 5. CCIR Good | PASS | 2.75% | Fixed by early-stop |
| 6. CCIR Moderate | FAIL | 50% | No channel estimation |
| 7. 600bps Fading | FAIL | 49.6% | Phase tracking needed |
| 8. Profile Sweep | 1/3 | - | Combination issues |

## Root Cause Analysis

### Problem 1: DFE Cold Start
**Symptom:** Test 3 (Static Multipath) fails at 21% BER despite DFE being enabled

**Analysis:**
```
Signal flow: [Preamble 1440 sym] → [Data Frames: 32 unknown + 16 known repeating]
                                    ↑
                               DFE starts here with identity taps
```
- DFE is configured with ff_taps=11, fb_taps=5 (can handle ~2.4 symbol ISI)
- BUT: First 32 data symbols are processed decision-directed before ANY training
- With 1ms delay (48 samples = 2.4 symbols), errors propagate immediately

**Solution:** Pre-train DFE on preamble before processing data symbols

### Problem 2: MLSE Identity Channel
**Symptom:** MLSE equalization is effectively disabled

**Current Code (modem_rx.cpp:500-515):**
```cpp
// For now, use identity channel (no ISI) 
// TODO: Implement proper channel estimation from preamble/probes
std::vector<complex_t> channel(L, complex_t(0, 0));
channel[0] = complex_t(1, 0);  // Identity channel (main tap only)
```

**Note:** MLSE has a working `estimate_channel()` function using Least Squares!
It's just never being called.

**Solution:** Estimate channel from preamble symbols before MLSE processing

### Problem 3: Phase Tracking on Fading Channels
**Symptom:** Test 7 (600bps Fading) fails at 49.6% BER (essentially random)

**Analysis:**
- 600bps uses BPSK (2 symbols = 180° apart)
- Fading causes carrier phase drift between mini-frames
- Current phase is estimated once from preamble, not updated
- A 90° phase drift turns BPSK into noise

**Solution:** Implement data-aided phase tracking using probe symbols

---

## Implementation Phases

### Phase 1: Channel Estimation Infrastructure (Foundation)

**Goal:** Create reusable channel estimation that works for both DFE and MLSE

**New file:** `src/equalizer/channel_estimator.h`

```cpp
class ChannelEstimator {
public:
    struct Config {
        int num_taps = 5;          // CIR length to estimate
        float noise_floor = 0.001f; // Regularization
    };
    
    // Estimate CIR from known symbol pairs
    std::vector<complex_t> estimate(
        const std::vector<complex_t>& received,
        const std::vector<complex_t>& expected);
    
    // Compute estimated channel delay spread
    float delay_spread() const;
    
    // Get main tap index (for equalizer alignment)
    int main_tap_index() const;
};
```

**Algorithm:** Least Squares (already implemented in MLSE, extract to common)

**Test Plan:**
1. Known 2-path channel: h = [1.0, 0, 0.5] (1 symbol delay)
2. Verify estimated h matches within 5%

---

### Phase 2: DFE Preamble Pre-Training

**Goal:** Converge DFE taps before processing data

**Modifications to `api/modem_rx.cpp`:**

```cpp
std::vector<complex_t> apply_equalizer(
    const std::vector<complex_t>& symbols,
    const std::vector<complex_t>& preamble_symbols,  // NEW
    const ModeConfig& mode_cfg) 
{
    // ... 
    case Equalizer::DFE: {
        DFE dfe(dfe_cfg);
        
        // NEW: Pre-train on last 128 preamble symbols
        // These are known (use common_pattern generator)
        if (preamble_symbols.size() >= 128) {
            auto known = generate_preamble_training(128);
            auto training_input = last_n(preamble_symbols, 128);
            dfe.train(training_input, known);
        }
        
        // Then process data symbols as before...
    }
}
```

**Key insight:** The preamble has known structure (288 common symbols).
We can generate the expected symbols and train the DFE before data starts.

**Expected impact:** Test 3 (Static Multipath) should improve significantly

---

### Phase 3: MLSE Channel Initialization

**Goal:** Use actual channel estimate instead of identity

**Modifications to `api/modem_rx.cpp`:**

```cpp
case Equalizer::MLSE_L2:
case Equalizer::MLSE_L3: {
    int L = (config_.equalizer == Equalizer::MLSE_L2) ? 2 : 3;
    MLSEEqualizer mlse(MLSEConfig{.channel_memory = L});
    
    // NEW: Estimate channel from preamble
    if (preamble_symbols.size() >= 64) {
        auto known = generate_preamble_training(64);
        auto training_input = last_n(preamble_symbols, 64);
        mlse.estimate_channel(known, training_input);
    }
    
    // Equalize with real channel estimate
    std::vector<int> detected = mlse.equalize(symbols);
    // ...
}
```

**Expected impact:** MLSE will actually equalize multipath now

---

### Phase 4: Adaptive Phase Tracking

**Goal:** Track phase drift on fading channels

**New component:** Phase tracking in probe processing

**Strategy:**
```
For each mini-frame:
  1. Process 32 unknown (data) symbols with current phase
  2. Process 16 known (probe) symbols
  3. Measure phase error from probe symbols
  4. Update phase estimate for next mini-frame
```

**New file:** `src/sync/phase_tracker.h`

```cpp
class PhaseTracker {
public:
    struct Config {
        float alpha = 0.1f;      // Phase tracking bandwidth
        float max_rate = 0.01f;  // Max phase change per symbol
    };
    
    // Update phase estimate from known symbol
    void update(complex_t received, complex_t expected);
    
    // Get current phase correction
    complex_t correction() const;
    
    // Apply correction to symbol
    complex_t correct(complex_t symbol) const;
};
```

**Integration:** Add to DFE processing loop between probe blocks

**Expected impact:** Test 7 (600bps Fading) should improve

---

### Phase 5: Integration and Testing

**Goal:** Combine all improvements and validate

**Test matrix:**
| Change | Test 3 | Test 6 | Test 7 |
|--------|--------|--------|--------|
| Baseline | 21% | 50% | 49.6% |
| + DFE pretrain | <5%? | 50%? | 49.6%? |
| + MLSE channel | <5%? | <10%? | 49.6%? |
| + Phase track | <5%? | <10%? | <10%? |

---

## Implementation Order

1. **Channel Estimator** (shared infrastructure)
2. **DFE Pre-training** (quickest win for Test 3)
3. **Test & Measure**
4. **MLSE Channel Init** (if MLSE mode needed)
5. **Phase Tracker** (for deep fading)
6. **Final Integration**

## Files to Create/Modify

### New Files:
- `src/equalizer/channel_estimator.h` - CIR estimation

### Modified Files:
- `api/modem_rx.cpp` - Add preamble passing, training calls
- `src/m110a/msdmt_decoder.h` - Expose preamble symbols in result
- `src/equalizer/dfe.h` - May need train() method fixes

## Timeline Estimate
- Phase 1: 1-2 hours (extract/refactor)
- Phase 2: 2-3 hours (DFE training + test)
- Phase 3: 1-2 hours (MLSE hookup)
- Phase 4: 3-4 hours (phase tracker)
- Phase 5: 1-2 hours (integration)

**Total: ~10-14 hours of work**

## Next Step
Start with Phase 1: Create `channel_estimator.h` and verify it works on synthetic data.
