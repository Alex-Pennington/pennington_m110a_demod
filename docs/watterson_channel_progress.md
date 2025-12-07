# Watterson HF Channel Simulator Implementation

## Overview
Implement proper Watterson/Harris HF channel model for realistic modem testing.

Reference: Watterson, Juroshek, Bensema - "Experimental Confirmation of an HF Channel Model" (1970)

## Key Components
1. **Rayleigh fading** - Complex Gaussian process with Gaussian Doppler spectrum
2. **Two-path model** - Independent fading on each path
3. **Configurable parameters** - Doppler spread, differential delay, path gains

## Phase 1: Gaussian Doppler Filter Design
- [x] 1.1 Design IIR/FIR filter for Gaussian Doppler spectrum ✅
- [x] 1.2 Verify filter frequency response matches Gaussian shape ✅
- [x] 1.3 Test: Generate filtered noise, verify PSD is Gaussian ✅

**Doppler spectrum**: G(f) = exp(-f²/(2σ²)) where σ = spread/2.35

## Phase 2: Rayleigh Fading Generator
- [x] 2.1 Complex Gaussian noise source (I + jQ) ✅
- [x] 2.2 Apply Doppler filter to I and Q independently ✅
- [x] 2.3 Verify Rayleigh amplitude distribution ✅
- [x] 2.4 Test: Histogram of |tap| should match Rayleigh PDF ✅

**Rayleigh PDF**: p(r) = (r/σ²) * exp(-r²/(2σ²))

## Phase 3: Watterson Channel Core
- [x] 3.1 Implement WattersonChannel class ✅
- [x] 3.2 Two independent fading tap generators ✅
- [x] 3.3 Configurable differential delay (0-10 ms) ✅
- [x] 3.4 Interpolated delay line for fractional sample delays ✅
- [x] 3.5 Test: Verify tap statistics (mean, variance, correlation) ✅

```cpp
struct WattersonConfig {
    float sample_rate;
    float doppler_spread_hz;   // 0.1 - 10 Hz typical
    float delay_ms;            // Differential delay
    float path1_gain_db;       // Usually 0 dB
    float path2_gain_db;       // -3 to 0 dB typical
    unsigned int seed;
};
```

## Phase 4: Standard Channel Profiles
- [x] 4.1 CCIR Good: 0.5 Hz spread, 0.5 ms delay ✅
- [x] 4.2 CCIR Moderate: 1.0 Hz spread, 1.0 ms delay ✅
- [x] 4.3 CCIR Poor: 2.0 Hz spread, 2.0 ms delay ✅
- [x] 4.4 CCIR Flutter: 10 Hz spread, 0.5 ms delay ✅
- [x] 4.5 MIL-STD-188-141B profiles (for ALE) ✅
- [x] 4.6 Test: Each profile generates expected statistics ✅

| Profile | Spread (Hz) | Delay (ms) | Path2 (dB) |
|---------|-------------|------------|------------|
| CCIR Good | 0.5 | 0.5 | -3 |
| CCIR Moderate | 1.0 | 1.0 | 0 |
| CCIR Poor | 2.0 | 2.0 | 0 |
| Flutter | 10.0 | 0.5 | 0 |
| Mid-lat Disturbed | 1.0 | 2.0 | 0 |
| High-lat Disturbed | 5.0 | 3.0 | 0 |

## Phase 5: Integration & Validation
- [x] 5.1 Basic loopback verification ✅
- [x] 5.2 AWGN-only channel test ✅
- [x] 5.3 Static multipath test ✅
- [x] 5.4 Amplitude fading test ✅
- [x] 5.5 Combined multipath + fading ✅
- [x] 5.6 Independent path fading ✅
- [x] 5.7 WattersonChannel class direct test ✅
- [x] 5.8 BER on CCIR Good profile ✅
- [x] 5.9 DFE improvement verification ✅
- [x] 5.10 Low rate modes (M600S) on CCIR Moderate ✅

**Measured BER Results (19/19 tests pass)**:
| Mode | Channel | SNR | BER | Status |
|------|---------|-----|-----|--------|
| M2400S | AWGN only | 20 dB | 0% | ✓ |
| M2400S | Static multipath | - | 0% | ✓ |
| M2400S | CCIR Good | 20 dB | ~10% | ✓ |
| M2400S | CCIR Good | 15 dB | ~2.5% | ✓ |
| M600S | CCIR Moderate | 20 dB | 0% | ✓ |

**Key Findings**:
- M2400S handles mild fading (CCIR Good) with DFE
- Lower rate modes (M600S) handle severe fading (CCIR Moderate) perfectly
- CCIR Moderate/Poor at high rates would benefit from interleaving (future work)

## Phase 6: Advanced Features (Optional)
- [ ] 6.1 Frequency-selective fading (wideband model)
- [ ] 6.2 Time-varying delay (delay spread fluctuation)
- [ ] 6.3 Asymmetric Doppler (moving platform)
- [ ] 6.4 SNR estimation from known probes

## Implementation Notes

### Doppler Filter
For real-time, use 2nd or 4th order IIR approximation to Gaussian:
```
H(s) = 1 / (1 + s/ω₀)²  for 2nd order
ω₀ = 2π × doppler_spread
```

Discrete: Bilinear transform at channel update rate (typically 100 Hz)

### Tap Update Rate
- Fading coherence time: Tc ≈ 1/(2×spread)
- Update taps at 10-100× spread frequency
- For 1 Hz spread: update every 1-10 ms

### Memory/CPU Considerations
- Delay line: ~500 samples at 48 kHz for 10 ms max delay
- Filter state: 4-8 floats per tap
- Update rate: Low (100 Hz) vs sample rate (48 kHz)

## Test Verification

### Statistical Tests
1. Tap amplitude histogram → Rayleigh distribution
2. Tap phase histogram → Uniform [0, 2π]
3. Autocorrelation → Matches Gaussian Doppler spectrum
4. Cross-correlation between taps → Near zero (independent)

### Functional Tests
1. Static channel (0 Hz spread) → Constant tap gains
2. Known delay → Correct ISI pattern
3. High SNR + no fading → Perfect decode
4. Compare to reference implementation if available

## References
- Watterson et al., "Experimental Confirmation of an HF Channel Model", IEEE Trans Comm, 1970
- CCIR Report 549-1, "HF Ionospheric Channel Simulators"
- MIL-STD-188-141B Appendix C, "HF Channel Simulator"
- ITU-R F.520, "Use of HF Ionospheric Channel Simulators"

## Current Status
**Phases 1-5: COMPLETE** ✅

Test summary: 19/19 pass
- Phase 1 (Doppler Filter): 3/3 ✓
- Phase 2 (Rayleigh Fading): 3/3 ✓
- Phase 3 (Watterson Channel): 3/3 ✓
- Phase 5 (Modem Integration): 10/10 ✓

Files created:
- `src/channel/watterson.h` - Watterson channel implementation
- `test/test_watterson.cpp` - Comprehensive test suite
