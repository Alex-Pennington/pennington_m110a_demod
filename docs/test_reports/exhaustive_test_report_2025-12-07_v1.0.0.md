# M110A Modem Exhaustive Test Report

## Test Information
| Field | Value |
|-------|-------|
| **Date** | December 7, 2025 |
| **Version** | 1.0.0 |
| **Duration** | 600 seconds (10 minutes) |
| **Iterations** | 51 |
| **Total Tests** | 3,037 |
| **Branch** | master |
| **Tester** | Automated Test Suite |

---

## Summary

| Metric | Value |
|--------|-------|
| **Overall Pass Rate** | 77.7% |
| **Total Passed** | 2,328 |
| **Total Failed** | 669 |
| **Core Modem Pass Rate** | 93.4%* |

*Excluding frequency offset tests which have a known issue

---

## Detailed Results by Category

| Category | Passed | Failed | Total | Pass Rate | Avg BER |
|----------|--------|--------|-------|-----------|---------|
| Clean Loopback | 484 | 20 | 504 | 96.0% | 0.00e+00 |
| AWGN Channel | 484 | 20 | 504 | 96.0% | 1.34e-04 |
| Multipath | 453 | 51 | 504 | 89.9% | 2.71e-02 |
| Freq Offset | 0 | 504 | 504 | 0.0% | 7.00e-01 |
| Message Sizes | 234 | 51 | 285 | 82.1% | 2.66e-06 |
| Random Data | 481 | 23 | 504 | 95.4% | 7.75e-06 |
| DFE Equalizer | 96 | 0 | 96 | **100.0%** | 0.00e+00 |
| MLSE Equalizer | 96 | 0 | 96 | **100.0%** | 9.52e-03 |

---

## Test Configuration

### Modes Tested
- M75_SHORT, M75_LONG (Walsh orthogonal coding)
- M150_SHORT, M150_LONG (BPSK 8x repetition)
- M300_SHORT, M300_LONG (BPSK 4x repetition)
- M600_SHORT, M600_LONG (BPSK 2x repetition)
- M1200_SHORT, M1200_LONG (QPSK)
- M2400_SHORT, M2400_LONG (8-PSK)
- M4800_SHORT (8-PSK uncoded)

### Channel Conditions Tested
- **SNR Levels**: 30dB, 25dB, 20dB, 15dB, 12dB
- **Multipath Delays**: 10, 20, 30, 40, 48, 60 samples (at 48kHz)
- **Echo Gain**: -6dB (0.5 linear)
- **Frequency Offsets**: 0.5Hz, 1Hz, 2Hz, 5Hz, 10Hz

### Equalizer Settings
- **DFE**: ff_taps=11, fb_taps=5, mu_ff=0.005, mu_fb=0.002
- **MLSE**: L=3 (64 states), traceback_depth=20

---

## Key Findings

### Strengths ✓
1. **DFE Equalizer**: Perfect 100% pass rate on multipath tests
2. **MLSE Equalizer**: 100% pass rate with low average BER (9.52e-03)
3. **Clean Loopback**: 96% pass rate across all modes
4. **AWGN Performance**: 96% pass rate with very low BER (1.34e-04)
5. **Random Data**: 95.4% pass rate proves robustness to arbitrary data

### Areas for Improvement ✗
1. **Frequency Offset Compensation**: 0% pass rate - phase tracker not compensating
2. **M75 Walsh Modes**: Contributing to ~4% failure rate in clean loopback
3. **Extreme Multipath**: Some failures at longer delays (60+ samples)

---

## Comparison: DFE vs MLSE

| Metric | DFE | MLSE |
|--------|-----|------|
| Pass Rate | 100% | 100% |
| Average BER | 0.00e+00 | 9.52e-03 |
| Recommendation | **Preferred** | Backup |

DFE consistently outperforms MLSE on this modem implementation.

---

## Related Test Results

### Reference PCM Decode
- **Result**: 10/10 passed
- All standard modes decode correctly from reference files

### PCM Loopback
- **Result**: 11/11 passed  
- All modes encode/decode through PCM file correctly

### Static Multipath (DFE)
| Delay (sym) | BER (None) | BER (DFE) | BER (MLSE) |
|-------------|------------|-----------|------------|
| 0.5 | 0% | 0% | 0% |
| 1.0 | 20% | 0% | 0% |
| 1.5 | 20% | 0% | 1% |
| 2.0 | 24% | 0% | 8% |
| 2.4 | 28% | 0% | 12.5% |
| 3.0 | 22% | 0% | 26% |

---

## Recommendations

1. **Priority 1**: Fix frequency offset compensation in phase tracker
2. **Priority 2**: Investigate M75 Walsh mode failures
3. **Priority 3**: Consider adaptive DFE parameters for extreme multipath

---

## Test Environment

- **OS**: Windows
- **Compiler**: g++ (MinGW-w64)
- **C++ Standard**: C++17
- **Optimization**: -O2
- **Sample Rate**: 48,000 Hz
- **Carrier Frequency**: 1,800 Hz
- **Symbol Rate**: 2,400 baud

---

## Changelog

### v1.0.0 (2025-12-07)
- Initial exhaustive test report
- MLSE bugs fixed (constellation angle, preamble pretraining)
- DFE preamble pretraining implemented
- All reference PCM files decode correctly
