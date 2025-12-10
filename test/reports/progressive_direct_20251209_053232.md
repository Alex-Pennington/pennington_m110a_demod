# M110A Modem Progressive Test Report

## Test Information
| Field | Value |
|-------|-------|
| **Version** | 1.2.0 |
| **Branch** | turbo |
| **Build** | 117 |
| **Commit** | 7bd4b42 |
| **Build Date** | 2025-12-09 05:32:04 |
| **Backend** | Direct API |
| **Mode Detection** | KNOWN |
| **Test Date** | December 09, 2025 05:32 |
| **Duration** | 9 seconds |
| **Tests** | SNR Freq Multipath |

---

## Key

### Modes
- **S (Short)**: 0.6 second interleave - lower latency, less robust
- **L (Long)**: 4.8 second interleave - higher latency, more robust
- Data rates: 75, 150, 300, 600, 1200, 2400 BPS

### Min SNR (dB)
- Minimum Signal-to-Noise Ratio at which the mode decodes correctly
- **Lower is better** (negative values = works below noise floor)
- Value of **30.0 dB** = mode failed even at maximum SNR (broken)

### Max Freq Offset (Hz)
- Maximum frequency offset the AFC (Automatic Frequency Control) can correct
- **Higher is better** (more tolerance to tuning errors)
- Value of **0.0 Hz** = mode failed even with no offset (broken)

### Max Multipath (samples/ms)
- Maximum multipath delay spread the equalizer can handle
- **Higher is better** (more tolerance to HF channel distortion)
- Samples @ 48kHz sample rate (1 sample = 0.0208 ms)
- Value of **0** = mode failed even with clean channel (broken)

---

## Equalizer: DFE

| Mode | Min SNR (dB) | Max Freq (Hz) | Max Multipath |
|------|:------------:|:-------------:|:-------------:|
| 600S | -5.00 | +/-2.50 | 195 (4.06 ms) |

---

*Report generated automatically by M110A Test Suite*
