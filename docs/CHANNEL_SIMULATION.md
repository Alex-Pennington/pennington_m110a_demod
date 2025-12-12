# HF Channel Simulation

This document describes the HF channel simulation models implemented in the M110A modem project, used for testing modem performance under realistic propagation conditions.

## Overview

HF (High Frequency, 3-30 MHz) radio propagation is characterized by:

- **Multipath propagation**: Signals arrive via multiple ionospheric reflections
- **Rayleigh fading**: Random amplitude variations due to multipath interference
- **Doppler spread**: Frequency dispersion from moving ionospheric layers
- **Delay spread**: Time dispersion from path length differences
- **Additive noise**: Atmospheric, man-made, and receiver noise

The M110A project implements industry-standard channel models to simulate these effects.

## Channel Models

### 1. Watterson Model

The primary channel model, based on:

> Watterson, C.C., Juroshek, J.R., Bensema, W.D., "Experimental Confirmation of an HF Channel Model," IEEE Transactions on Communication Technology, Vol. COM-18, No. 6, December 1970.

#### Structure

```
Input Signal ──┬──► Path 1 (Rayleigh Fading) ──────────────┬──► Output
               │                                            │
               └──► Delay ──► Path 2 (Rayleigh Fading) ────┘
```

#### Parameters

| Parameter | Symbol | Typical Range | Description |
|-----------|--------|---------------|-------------|
| Doppler Spread | fd | 0.1 - 10 Hz | -3dB bandwidth of Doppler spectrum |
| Delay Spread | τ | 0.5 - 3 ms | Differential delay between paths |
| Path Gains | g1, g2 | -6 to 0 dB | Relative power of each path |

#### Implementation Details

**Gaussian Doppler Spectrum**

The fading tap coefficients have a Gaussian-shaped power spectrum:

```
G(f) = exp(-f² / (2σ²))
```

where σ = spread_hz / 2.35 (so spread_hz is the -3dB bandwidth).

Implemented using a 2nd-order Butterworth IIR filter applied to complex Gaussian noise:

```
H(s) = ω₀² / (s² + √2·ω₀·s + ω₀²)
```

**Rayleigh Fading**

Each path has a complex fading coefficient where:
- Real and imaginary parts are independent Gaussian processes
- Magnitude follows Rayleigh distribution
- Phase is uniformly distributed [0, 2π]
- Temporal correlation follows Gaussian Doppler spectrum

**Tap Update Rate**

Fading coefficients are updated at 100 Hz by default. This is sufficient for Doppler spreads up to ~10 Hz while keeping computational load reasonable.

#### Source Code

```
src/channel/watterson.h
  - GaussianDopplerFilter    IIR filter for Doppler shaping
  - RayleighFadingGenerator  Complex fading coefficient generator
  - WattersonChannel         Complete two-path channel model
```

### 2. AWGN Model

Additive White Gaussian Noise with calibrated SNR.

#### SNR Definitions

| Metric | Formula | Use Case |
|--------|---------|----------|
| SNR | Psignal / Pnoise | General signal quality |
| Es/N0 | Energy per symbol / Noise density | Modulation analysis |
| Eb/N0 | Energy per bit / Noise density | FEC analysis |

#### Relationships

```
Es/N0 = SNR × (Bandwidth / Symbol_Rate)
Eb/N0 = Es/N0 / (bits_per_symbol × code_rate)
```

For M110A at 2400 symbol/sec with 8-PSK (3 bits/symbol) and rate-1/2 FEC:
```
Eb/N0 = Es/N0 / (3 × 0.5) = Es/N0 / 1.5
```

#### Source Code

```
src/channel/awgn.h
  - AWGNChannel::add_noise_snr()    Add noise for target SNR
  - AWGNChannel::add_noise_es_n0()  Add noise for target Es/N0
  - AWGNChannel::add_noise_eb_n0()  Add noise for target Eb/N0
```

### 3. Simple Multipath Model

A simpler delay-line model for basic multipath simulation without fading.

#### Structure

```
Input ──┬──► Tap 0 (gain, phase) ──────────────────────┬──► Output
        │                                               │
        ├──► Delay τ1 ──► Tap 1 (gain, phase) ────────┤
        │                                               │
        └──► Delay τ2 ──► Tap 2 (gain, phase) ────────┘
```

#### Source Code

```
src/channel/multipath.h
  - MultipathRFChannel           N-tap delay line model
  - MultipathRFChannel::itu_good/moderate/poor()  Preset configurations
```

## Standard Channel Profiles

### CCIR/ITU-R Profiles

Based on CCIR Report 549 and ITU-R F.520:

| Profile | Doppler | Delay | Path 2 Gain | Conditions |
|---------|---------|-------|-------------|------------|
| Good | 0.5 Hz | 0.5 ms | -3 dB | Quiet daytime, short paths |
| Moderate | 1.0 Hz | 1.0 ms | 0 dB | Typical mid-latitude |
| Poor | 2.0 Hz | 2.0 ms | 0 dB | Disturbed conditions |
| Flutter | 10.0 Hz | 0.5 ms | 0 dB | Near-vertical incidence |

### Extended Profiles

| Profile | Doppler | Delay | Path 2 Gain | Conditions |
|---------|---------|-------|-------------|------------|
| Mid-lat Disturbed | 1.0 Hz | 2.0 ms | 0 dB | Geomagnetic disturbance |
| High-lat Disturbed | 5.0 Hz | 3.0 ms | 0 dB | Polar/auroral paths |

## Using hfchansim

The `hfchansim` command-line tool applies channel impairments to PCM files.

### Basic Usage

```powershell
# Apply CCIR Moderate channel to reference PCM
.\hfchansim.exe --ref 600S --preset moderate

# Custom settings
.\hfchansim.exe --ref 2400L --model watterson --doppler 2 --delay 1.5 --snr 12
```

### Output

Each run produces:
- `<name>.pcm` - Degraded audio
- `<name>.json` - Metadata with all channel settings

See `hfchansim/README.md` for complete documentation.

## Integration with Modem Testing

### Exhaustive Test Integration

The exhaustive test framework can use channel-degraded files:

```powershell
# 1. Generate degraded file
.\hfchansim.exe --ref 600S --preset moderate

# 2. Test modem decode
.\exhaustive_test.exe --rx-inject hfchansim_out/600S_moderate_*.pcm --modes 600S
```

### Programmatic Usage

```cpp
#include "src/channel/watterson.h"
#include "src/channel/awgn.h"

// Create Watterson channel
m110a::WattersonChannel::Config cfg;
cfg.sample_rate = 48000.0f;
cfg.doppler_spread_hz = 1.0f;  // CCIR Moderate
cfg.delay_ms = 1.0f;
cfg.path1_gain_db = 0.0f;
cfg.path2_gain_db = 0.0f;
cfg.seed = 42;

m110a::WattersonChannel channel(cfg);

// Process signal
std::vector<float> tx_signal = /* modem output */;
std::vector<float> faded = channel.process(tx_signal);

// Add AWGN
m110a::AWGNChannel awgn(123);
awgn.add_noise_snr(faded, 15.0f);  // 15 dB SNR

// faded now contains channel-degraded signal
```

## Performance Expectations

### BER vs Eb/N0 (AWGN only)

Theoretical uncoded BER for PSK modulations:

| Eb/N0 | BPSK | QPSK | 8-PSK |
|-------|------|------|-------|
| 0 dB | 7.9e-2 | 1.6e-1 | 2.8e-1 |
| 5 dB | 6.0e-3 | 2.3e-2 | 7.8e-2 |
| 10 dB | 3.9e-6 | 7.8e-4 | 1.9e-2 |
| 15 dB | ~0 | 3.0e-7 | 1.4e-3 |

With rate-1/2 convolutional FEC (as in MIL-STD-188-110A), effective BER is significantly lower.

### Modem Performance Under Fading

Expected decode success rates with MIL-STD-188-110A modem:

| Channel | 600 BPS | 1200 BPS | 2400 BPS |
|---------|---------|----------|----------|
| AWGN 15dB | >99% | >99% | >95% |
| CCIR Good | >95% | >90% | >80% |
| CCIR Moderate | >85% | >75% | >60% |
| CCIR Poor | >60% | >40% | >20% |

Note: Actual performance depends on specific implementation, message length, and interleave setting.

## References

1. Watterson, C.C., et al., "Experimental Confirmation of an HF Channel Model," IEEE Trans. Comm., 1970

2. CCIR Report 549-2, "HF Ionospheric Channel Simulators"

3. ITU-R Recommendation F.520-2, "Use of High Frequency Ionospheric Channel Simulators"

4. MIL-STD-188-110A, "Interoperability and Performance Standards for Data Modems"

5. Proakis, J.G., "Digital Communications," 5th Ed., McGraw-Hill, 2008

## See Also

- `hfchansim/README.md` - Command-line tool documentation
- `src/channel/watterson.h` - Watterson implementation
- `src/channel/awgn.h` - AWGN implementation
- `test/` - Test framework integration
