# M110A Modem Exhaustive Test Report

## Test Information
| Field | Value |
|-------|-------|
| **Date** | December 07, 2025 |
| **Version** | 1.0.0 |
| **Duration** | 124 seconds |
| **Iterations** | 10 |
| **Total Tests** | 59 |
| **Rating** | GOOD |

---

## Summary

| Metric | Value |
|--------|-------|
| **Overall Pass Rate** | 86.4% |
| **Total Passed** | 51 |
| **Total Failed** | 8 |

---

## Detailed Results by Category

| Category | Passed | Failed | Total | Pass Rate | Avg BER |
|----------|--------|--------|-------|-----------|--------|
| Clean Loopback | 10 | 0 | 10 | 100.0% | 0.00e+00 |
| AWGN Channel | 10 | 0 | 10 | 100.0% | 0.00e+00 |
| Multipath | 10 | 0 | 10 | 100.0% | 0.00e+00 |
| Freq Offset | 2 | 8 | 10 | 20.0% | 2.22e-01 |
| Message Sizes | 5 | 0 | 5 | 100.0% | 0.00e+00 |
| Random Data | 10 | 0 | 10 | 100.0% | 0.00e+00 |
| DFE Equalizer | 2 | 0 | 2 | 100.0% | 0.00e+00 |
| MLSE Equalizer | 2 | 0 | 2 | 100.0% | 1.74e-02 |

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

