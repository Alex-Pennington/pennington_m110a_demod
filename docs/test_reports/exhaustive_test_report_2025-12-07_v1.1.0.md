# M110A Modem Exhaustive Test Report

## Test Information
| Field | Value |
|-------|-------|
| **Date** | December 07, 2025 |
| **Version** | 1.1.0 |
| **Duration** | 180 seconds |
| **Iterations** | 16 |
| **Total Tests** | 910 |
| **Rating** | FAIR |

---

## Summary

| Metric | Value |
|--------|-------|
| **Overall Pass Rate** | 78.3% |
| **Total Passed** | 703 |
| **Total Failed** | 195 |

---

## Detailed Results by Category

| Category | Passed | Failed | Total | Pass Rate | Avg BER |
|----------|--------|--------|-------|-----------|--------|
| Clean Loopback | 151 | 6 | 157 | 96.2% | 0.00e+00 |
| AWGN Channel | 151 | 6 | 157 | 96.2% | 9.20e-05 |
| Multipath | 144 | 13 | 157 | 91.7% | 2.02e-02 |
| Freq Offset | 3 | 154 | 157 | 1.9% | 6.72e-01 |
| Message Sizes | 45 | 10 | 55 | 81.8% | 0.00e+00 |
| Random Data | 151 | 6 | 157 | 96.2% | 0.00e+00 |
| DFE Equalizer | 29 | 0 | 29 | 100.0% | 0.00e+00 |
| MLSE Equalizer | 29 | 0 | 29 | 100.0% | 1.21e-02 |

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
- **Frequency Offsets**: 0.5Hz, 1.0Hz, 2.0Hz, 5.0Hz

---

## Known Issues

- **Frequency Offset**: Pass rate remains low (~2%) - requires AFC implementation

