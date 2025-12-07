# AFC (Automatic Frequency Control) Implementation Plan

## Problem Statement

The exhaustive test shows **~2% pass rate** for frequency offset tests (0.5Hz to 5Hz offsets).
This is the primary remaining issue blocking higher overall pass rates.

**Current State:**
- Frequency offset tolerance: Effectively ~0.5 Hz maximum
- v1.1.0 exhaustive test: 3/157 passed (1.9%) for frequency offset category
- All other categories: 82-100% pass rates

---

## Root Cause Analysis

### Current Frequency Handling

| Component | Location | Capability | Limitation |
|-----------|----------|------------|------------|
| `CarrierRecovery` | `src/sync/carrier_recovery.h` | Decision-directed PLL, ±50Hz theoretical | Requires good initial estimate |
| `FrequencySearchDetector` | `src/sync/frequency_search_detector.h` | Brute-force ±50Hz in 5Hz steps | Slow, coarse resolution |
| `PhaseTracker` | `src/equalizer/phase_tracker.h` | 2nd-order PLL, ±10Hz max | Too narrow, phase-only |
| `PreambleDetector` | `src/m110a/preamble_detector.h` | Phase rotation between peaks | Not currently used for AFC |

### Why Current Approach Fails

1. **No AFC loop in receiver chain** - Frequency estimate from preamble is not applied
2. **PhaseTracker bandwidth too narrow** - Limited to ±10Hz, tests go up to ±5Hz but accumulate
3. **No continuous frequency tracking** - Only phase correction during data, frequency drifts uncorrected
4. **Open-loop after preamble** - Frequency estimate made once, never updated

---

## Proposed AFC Architecture

### Two-Stage AFC Design

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         STAGE 1: ACQUISITION                             │
│                      (During Preamble - Coarse AFC)                      │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│   Raw Samples ──► FFT-Based ──► Coarse Freq ──► NCO Correction          │
│                   Estimator      Estimate        (±50 Hz range)          │
│                   (±50 Hz)       (1 Hz res)                              │
│                                                                          │
│   Method: FFT of preamble correlation, find peak offset from 1800 Hz    │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         STAGE 2: TRACKING                                │
│                      (During Data - Fine AFC)                            │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│   Corrected ──► Decision-Directed ──► Fine Freq ──► NCO Trim            │
│   Symbols       Phase/Freq PLL        Update        (±5 Hz range)        │
│                 (probe-aided)         per frame                          │
│                                                                          │
│   Method: Probe symbols provide known reference for PLL training         │
│           Update NCO frequency every frame (32 symbols @ 2400 baud)      │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Implementation Details

### Stage 1: FFT-Based Coarse AFC (Acquisition)

**Location:** New file `src/sync/afc.h`

**Algorithm:**
1. During preamble, extract ~480 symbols (200ms) of known pattern
2. Correlate with expected preamble to get complex correlation
3. Perform FFT on correlation output to find frequency peak
4. Peak offset from DC = frequency error
5. Apply correction to NCO before data demodulation

**Key Parameters:**
```cpp
struct AFCConfig {
    float search_range_hz = 50.0f;     // ±50 Hz search
    float resolution_hz = 0.5f;        // 0.5 Hz resolution
    int fft_size = 512;                // FFT size for freq estimation
    float acquisition_threshold = 0.7f; // Correlation threshold
};
```

**Pseudocode:**
```cpp
float estimate_frequency_offset(const std::vector<std::complex<float>>& correlation) {
    // Zero-pad correlation to FFT size
    std::vector<std::complex<float>> fft_in(fft_size, {0, 0});
    std::copy(correlation.begin(), correlation.end(), fft_in.begin());
    
    // Perform FFT
    auto fft_out = fft(fft_in);
    
    // Find peak (excluding DC)
    int peak_bin = find_peak_bin(fft_out, 1, fft_size/2);
    
    // Parabolic interpolation for sub-bin accuracy
    float refined_bin = parabolic_interpolate(fft_out, peak_bin);
    
    // Convert bin to Hz
    float freq_offset = refined_bin * sample_rate / fft_size;
    
    return freq_offset;
}
```

### Stage 2: Probe-Aided Fine AFC (Tracking)

**Location:** Extend `src/equalizer/phase_tracker.h` or new `src/sync/afc.h`

**Algorithm:**
1. Every frame, extract probe symbols (16-20 known symbols)
2. Compute phase difference between received and expected probes
3. Estimate frequency from phase slope across probe block
4. Update NCO frequency with filtered estimate
5. Apply 2nd-order loop for smooth tracking

**Key Parameters:**
```cpp
struct AFCTrackingConfig {
    float loop_bandwidth_hz = 2.0f;    // Tracking loop bandwidth
    float damping = 0.707f;            // Critical damping
    float max_freq_update_hz = 5.0f;   // Max per-frame frequency change
    bool use_probe_only = true;        // Only update on probe symbols
};
```

**Pseudocode:**
```cpp
void update_frequency_from_probes(
    const std::vector<std::complex<float>>& received_probes,
    const std::vector<std::complex<float>>& expected_probes,
    float symbol_rate
) {
    // Compute phase for each probe
    std::vector<float> phase_errors;
    for (size_t i = 0; i < received_probes.size(); i++) {
        auto error = received_probes[i] * std::conj(expected_probes[i]);
        phase_errors.push_back(std::arg(error));
    }
    
    // Unwrap phases
    unwrap_phase(phase_errors);
    
    // Linear regression to get slope = frequency error
    float freq_error_rad_per_sym = linear_fit_slope(phase_errors);
    float freq_error_hz = freq_error_rad_per_sym * symbol_rate / (2 * M_PI);
    
    // Apply loop filter
    float filtered_freq = loop_filter.update(freq_error_hz);
    
    // Update NCO
    nco.adjust_frequency(filtered_freq);
}
```

---

## Integration Points

### 1. Modify `modem_rx.cpp`

```cpp
// In decode() function, after preamble detection:

// Stage 1: Coarse AFC
AFCConfig afc_config;
AFC afc(afc_config);
float coarse_freq_offset = afc.estimate_from_preamble(preamble_samples);
nco.adjust_frequency(coarse_freq_offset);

// ... existing code ...

// Stage 2: Fine AFC (in frame processing loop)
for (each frame) {
    // Extract and demodulate symbols
    auto symbols = demodulate_frame(...);
    
    // Extract probe symbols
    auto probes = extract_probes(symbols, frame_structure);
    
    // Update AFC from probes
    afc.update_from_probes(probes, expected_probes);
    
    // Apply any frequency correction
    nco.adjust_frequency(afc.get_frequency_correction());
    
    // Continue with equalization and decoding
    ...
}
```

### 2. Modify `RxConfig` in `modem_config.h`

```cpp
struct RxConfig {
    // ... existing fields ...
    
    // AFC Configuration
    bool enable_afc = true;
    float afc_search_range_hz = 50.0f;
    float afc_tracking_bandwidth_hz = 2.0f;
    bool afc_probe_aided = true;
};
```

---

## Implementation Order

### Phase 1: Coarse AFC (Highest Impact)
1. Create `src/sync/afc.h` with FFT-based frequency estimator
2. Add FFT utility functions (or use existing if available)
3. Integrate into `modem_rx.cpp` after preamble detection
4. Test with ±5Hz, ±10Hz, ±20Hz offsets

**Expected Result:** Should handle ±20Hz offsets reliably

### Phase 2: Fine AFC / Tracking
1. Extend AFC class with probe-aided tracking
2. Implement 2nd-order tracking loop
3. Update frequency estimate every frame during data
4. Handle mode-specific probe structures (M2400 vs M1200 vs M600)

**Expected Result:** Should handle frequency drift during transmission

### Phase 3: Optimization
1. Tune loop bandwidths for different SNR conditions
2. Add adaptive bandwidth based on estimated SNR
3. Optimize FFT size and resolution tradeoffs
4. Handle edge cases (very low SNR, large initial offset)

---

## Test Plan

### Unit Tests (`test/test_afc.cpp`)
```cpp
// Test coarse AFC accuracy
test_coarse_afc_5hz_offset();
test_coarse_afc_10hz_offset();
test_coarse_afc_20hz_offset();
test_coarse_afc_50hz_offset();

// Test fine AFC tracking
test_fine_afc_1hz_drift();
test_fine_afc_with_noise();
test_fine_afc_probe_aided();

// Test combined AFC
test_combined_afc_pipeline();
```

### Integration Tests
- Modify `exhaustive_test.cpp` frequency offset tests
- Target: >90% pass rate for frequency offset category
- Test offsets: 0.5Hz, 1Hz, 2Hz, 5Hz, 10Hz, 20Hz

---

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| AFC adds latency | Use overlap-save FFT, pipeline with other processing |
| False lock at wrong frequency | Use correlation threshold, multiple hypothesis testing |
| AFC instability at low SNR | Reduce bandwidth adaptively, freeze updates below threshold |
| Mode-specific probe differences | Handle all modes in probe extraction logic |

---

## Success Criteria

| Metric | Current | Target |
|--------|---------|--------|
| Freq offset pass rate | 1.9% | >90% |
| Overall exhaustive pass rate | 78.3% | >90% |
| Max tolerable offset | ~0.5 Hz | ±20 Hz |
| Frequency drift tolerance | None | ±2 Hz/sec |

---

## Files to Create/Modify

### New Files
- `src/sync/afc.h` - Main AFC implementation
- `test/test_afc.cpp` - AFC unit tests

### Modified Files
- `api/modem_rx.cpp` - Integrate AFC into decode pipeline
- `api/modem_config.h` - Add AFC configuration options
- `src/equalizer/phase_tracker.h` - Possibly extend for tracking
- `test/exhaustive_test.cpp` - Update frequency offset tests

---

## Estimated Effort

| Phase | Effort | Priority |
|-------|--------|----------|
| Phase 1: Coarse AFC | 2-3 hours | HIGH |
| Phase 2: Fine AFC | 2-3 hours | MEDIUM |
| Phase 3: Optimization | 1-2 hours | LOW |
| Testing & Tuning | 1-2 hours | HIGH |

**Total: 6-10 hours**

---

## References

- MIL-STD-188-110A Section 5.4 (Synchronization)
- `src/sync/carrier_recovery.h` - Existing carrier recovery
- `src/sync/frequency_search_detector.h` - Existing frequency search
- `src/m110a/preamble_detector.h` - Preamble correlation

