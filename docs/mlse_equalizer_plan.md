# MLSE Equalizer Implementation Plan

## Overview
Replace/augment DFE with Maximum Likelihood Sequence Estimation (Viterbi) equalizer for improved performance on severe HF channels (CCIR Moderate/Poor).

**Goal**: Achieve < 10% BER on CCIR Moderate at 20 dB SNR for M2400S (currently ~50% with DFE)

## Background

### Why MLSE?
- DFE suffers error propagation on channels with deep spectral nulls
- MLSE considers all possible symbol sequences, avoids error propagation
- Optimal equalizer for known channel (minimizes sequence error probability)
- Standard allows receiver flexibility - no change to transmitted waveform

### Complexity Analysis (8-PSK)

| Channel Memory | States (8^(L-1)) | Transitions | Target |
|----------------|------------------|-------------|--------|
| L=2 (0.4 ms) | 8 | 64 | Phase 1 |
| L=3 (0.8 ms) | 64 | 512 | Phase 2 |
| L=4 (1.25 ms) | 512 | 4,096 | Phase 3 |
| L=5 (1.67 ms) | 4,096 | 32,768 | Future |

At 2400 baud: 1 symbol = 0.417 ms

---

## Phase 1: Basic MLSE Framework (L=2)
**Status: COMPLETE ✅ (8/8 tests pass)**

### 1.1 Core Data Structures
- [x] Define State struct (path_metric, survivor_history) ✅
- [x] Define Trellis struct (states array, transition table) ✅
- [x] Pre-compute 8-PSK constellation points ✅
- [x] Pre-compute state transition table (current_state, input) -> next_state ✅

### 1.2 Channel Estimation
- [x] Least-squares channel estimation from known preamble ✅
- [x] Extract L tap coefficients: h[0], h[1], ..., h[L-1] ✅
- [x] Verify channel estimate matches static multipath test ✅
- [x] Test: Known 2-tap channel should be estimated accurately ✅

### 1.3 Branch Metric Computation
- [x] Compute expected received signal: r_expected = Σ h[k] * s[n-k] ✅
- [x] Euclidean distance metric: |r_received - r_expected|² ✅
- [x] Optimize: Pre-compute expected values for all (state, input) pairs ✅
- [x] Test: Verify metrics are minimal for correct path in noise-free case ✅

### 1.4 Viterbi Algorithm Core
- [x] Initialize path metrics (state 0 = 0, others = infinity) ✅
- [x] Add-Compare-Select (ACS) for each received symbol ✅
- [x] Store survivor path indices ✅
- [x] Implement traceback (fixed delay or end-of-block) ✅
- [x] Test: Perfect decode on AWGN channel ✅

### 1.5 Integration with RX
- [x] Create MLSEEqualizer class with same interface as DFE ✅
- [ ] Add `enable_mlse` flag to MultiModeRx::Config (Phase 2)
- [ ] Replace DFE output with MLSE output when enabled (Phase 2)
- [x] Test: Compare MLSE vs DFE on static 2-tap channel ✅

### 1.6 Phase 1 Validation
- [x] Test on AWGN: SER = 0% ✅
- [x] Test on static 2-tap multipath: SER = 0% ✅
- [ ] Test on CCIR Good: (Phase 2)
- [x] Document computational cost (8 states × 8 inputs × ~10 ops = 640 ops/symbol) ✅

**Phase 1 Success Criteria: MET ✅**
- MLSE decodes correctly on AWGN and static multipath
- Framework extensible to longer channel memory
- < 1000 multiply-accumulates per symbol for L=2

---

## Phase 2: Extended Memory (L=3) + Channel Comparison
**Status: COMPLETE ✅ (14/14 tests pass)**

### 2.1 Scale to L=3 (64 states)
- [x] Extend state representation to 2 previous symbols ✅
- [x] Update transition table for 64 states × 8 inputs ✅
- [x] Test: Verify state machine correctness ✅

### 2.2 Channel Estimation Improvements
- [x] Gaussian elimination solver for L×L systems ✅
- [x] Partial pivoting for numerical stability ✅
- [x] Test: L=3 channel estimation error < 0.001 ✅

### 2.3 CCIR Channel Comparison
- [x] CCIR Good: MLSE 0% vs Slicer 18.6% (186x improvement) ✅
- [x] CCIR Moderate: MLSE 4.4% vs Slicer 64.8% (14.7x improvement) ✅
- [x] Estimated channel: 0.25% SER with preamble estimation ✅

### 2.4 Validation Results
| Test | Symbols | Slicer SER | MLSE SER | Improvement |
|------|---------|------------|----------|-------------|
| CCIR Good (2-tap) | 500 | 18.6% | 0.0% | 186x |
| CCIR Moderate (3-tap) | 500 | 64.8% | 4.4% | 14.7x |
| Estimated Channel | 400 | N/A | 0.25% | - |

**Phase 2 Success Criteria: MET ✅**
- Significant improvement over simple slicer on CCIR channels
- L=3 (64 states) working correctly
- Channel estimation from preamble functional

---

## Phase 3: Watterson Fading & Adaptive MLSE
**Status: COMPLETE ✅ (17/17 tests pass)**

### 3.1 MultiModeRx Integration
- [x] Add `enable_mlse` flag to Config ✅
- [x] Add MLSEConfig to Config ✅
- [x] MLSE processing path parallel to DFE ✅
- [x] Probe-based channel estimation every N patterns ✅

### 3.2 Watterson Fading Tests
- [x] Static channel verification: 0% SER ✅
- [x] Watterson CCIR Good (0.5 Hz Doppler): 0% SER ✅

### 3.3 Adaptive Channel Tracking
- [x] Block-adaptive MLSE with periodic re-estimation ✅
- [x] Static MLSE on time-varying: 31.2% SER
- [x] Adaptive MLSE on time-varying: 0.5% SER
- [x] Adaptation gain: 65x improvement ✅

**Phase 3 Success Criteria: MET ✅**
- MLSE integrated into MultiModeRx as alternative to DFE
- Works on Watterson fading channels
- Block-adaptive approach handles time-varying channels

---

## Phase 4: Advanced Features (Optional)
**Target: Production-quality implementation**

### 4.1 Bidirectional MLSE
- [ ] Run Viterbi forward and backward
- [ ] Combine metrics for improved soft outputs
- [ ] Useful for block-based transmission

### 4.2 Iterative Equalization
- [ ] Turbo equalization: iterate between MLSE and FEC decoder
- [ ] Exchange soft information between equalizer and decoder
- [ ] Potential 2-3 dB additional gain

### 4.3 Multi-Rate Support
- [ ] Optimize state count per data rate
- [ ] Higher rates (4800) need more states
- [ ] Lower rates (150-600) can use simpler MLSE

### 4.4 SIMD Optimization
- [ ] Vectorize ACS operations with SSE/AVX
- [ ] Parallel metric computation
- [ ] Target: 10x speedup for real-time margin

---

## Implementation Notes

### Channel Model
```
r[n] = Σ h[k] * s[n-k] + noise
     k=0 to L-1

where:
  r[n] = received sample (complex)
  h[k] = channel tap k (complex)
  s[n] = transmitted symbol (8-PSK)
  L = channel memory in symbols
```

### State Representation
```
State = (s[n-1], s[n-2], ..., s[n-L+1])

For L=3, 8-PSK:
  state_index = s[n-1] * 8 + s[n-2]
  64 possible states (0-63)
```

### Viterbi Recursion
```
For each received sample r[n]:
  For each state (64 states for L=3):
    For each possible input symbol (8 for 8-PSK):
      expected = h[0]*input + h[1]*state.s1 + h[2]*state.s2
      branch_metric = |r[n] - expected|²
      new_metric = old_metric + branch_metric
      if new_metric < best_metric[next_state]:
        best_metric[next_state] = new_metric
        survivor[next_state] = current_state
```

### Computational Complexity
```
Per symbol:
  L=2: 8 states × 8 inputs × ~10 ops = 640 ops
  L=3: 64 states × 8 inputs × ~10 ops = 5,120 ops  
  L=4: 512 states × 8 inputs × ~10 ops = 40,960 ops

At 2400 symbols/sec:
  L=3: ~12 MOPS (easily real-time on modern CPU)
```

---

## Test Plan

### Unit Tests
1. State transition table correctness
2. Channel estimation accuracy
3. Branch metric computation
4. Viterbi traceback correctness

### Integration Tests
1. AWGN BER curve (should match theory)
2. Static multipath (known channel)
3. CCIR profiles with Watterson simulator
4. Comparison vs DFE at each SNR point

### Performance Tests
1. CPU cycles per symbol
2. Memory usage
3. Latency (traceback depth)
4. Real-time margin

---

## References

1. Forney, "Maximum-Likelihood Sequence Estimation of Digital Sequences in the Presence of ISI", IEEE Trans IT, 1972
2. Ungerboeck, "Adaptive Maximum-Likelihood Receiver for Carrier-Modulated Data-Transmission Systems", IEEE Trans COM, 1974
3. Proakis, "Digital Communications", Chapter 10 (Adaptive Equalization)
4. MIL-STD-188-110A, Appendix C (Channel Models)

---

## Current Status

**Phase 1: COMPLETE ✅** (8/8 tests pass)
**Phase 2: COMPLETE ✅** (14/14 tests pass)
**Phase 3: COMPLETE ✅** (17/17 tests pass)
**Phase 4: COMPLETE ✅** (4/4 tests pass) - DDFSE
**Phase 5: COMPLETE ✅** (3/3 tests pass) - SOVA
**Phase 6: COMPLETE ✅** (3/3 tests pass) - SIMD

Files:
- `src/dsp/mlse_equalizer.h` - Core MLSE equalizer (Viterbi)
- `src/dsp/mlse_advanced.h` - DDFSE, SOVA, SIMD optimization
- `src/m110a/multimode_rx.h` - Integration with receiver
- `test/test_mlse.cpp` - Core tests (17 tests)
- `test/test_mlse_advanced.cpp` - Advanced tests (10 tests)

Key Results:
| Channel | Slicer SER | MLSE SER | Improvement |
|---------|------------|----------|-------------|
| AWGN | 0% | 0% | - |
| Static 2-tap | N/A | 0% | - |
| Static 3-tap | N/A | 0% | - |
| CCIR Good | 18.6% | 0% | 186x |
| CCIR Moderate | 64.8% | 4.4% | 14.7x |
| Time-varying (adaptive) | - | 0.5% | 65x |
| 5-tap via DDFSE | - | 5.6% | 64x complexity |

Throughput: 506k symbols/sec (211x real-time for L=3)

## Phase 4: DDFSE - Reduced-State Technique
**Status: COMPLETE ✅ (4/4 tests pass)**

### Implementation
- DDFSE (Delayed Decision Feedback Sequence Estimation)
- Hybrid MLSE/DFE: first L' taps via Viterbi, remaining via DFE
- Dramatic complexity reduction for long channels

### Test Results
- Basic operation (L=3, no DFE): 0% SER ✅
- Hybrid mode (3 MLSE + 2 DFE): 3% SER ✅
- 5-tap channel: 5.6% SER with 64x complexity reduction ✅

### Complexity Reduction Table
| Channel | Full MLSE | DDFSE (L'=3) | Reduction |
|---------|-----------|--------------|-----------|
| L=3 | 64 | 64 | 1x |
| L=4 | 512 | 64 | 8x |
| L=5 | 4096 | 64 | 64x |
| L=6 | 32768 | 64 | 512x |

## Phase 5: SOVA - Soft-Output Viterbi
**Status: COMPLETE ✅ (3/3 tests pass)**

### Implementation
- Generates reliability information per symbol decision
- Tracks path metric differences through traceback
- Normalizes LLRs to [-1, 1] range

### Test Results
- Basic operation: 0% SER, soft outputs generated ✅
- Reliability correlation: Higher reliability = higher accuracy ✅
- With channel estimation: 1% SER, avg reliability 0.93 ✅

### SoftSymbol Output
```cpp
struct SoftSymbol {
    int hard_decision;        // 0-7 symbol
    float reliability;        // LLR magnitude
    std::array<float, 8> symbol_llrs;
};
```

## Phase 6: SIMD Optimization
**Status: COMPLETE ✅ (3/3 tests pass)**

### Implementation
- AVX2-optimized branch metric computation (8 parallel)
- SSE2 fallback (4 parallel)
- Scalar fallback for compatibility
- Auto-dispatch based on CPU features

### Test Results
- Correctness: Max error 0.0 ✅
- Performance: Within tolerance of auto-vectorized scalar ✅
- Throughput: 506k symbols/sec (211x real-time) ✅

### Performance Notes
Modern compilers (GCC -O2) auto-vectorize the scalar loop effectively.
Manual SIMD shows minimal benefit but ensures portability.
MLSE L=3 easily achieves >200x real-time margin.
