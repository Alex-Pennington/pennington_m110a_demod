# M110A Channel Simulation API

## Overview

The M110A modem includes a comprehensive channel simulation API for testing receiver performance under realistic HF conditions. This document describes how to use the channel simulation functions both programmatically via the C++ API and interactively via the MS-DMT server interface.

---

## Part 1: C++ API (`api/channel_sim.h`)

### Include the Header

```cpp
#include "api/modem.h"
#include "api/channel_sim.h"

using namespace m110a::api;
using namespace m110a::api::channel;
```

---

### Core Impairment Functions

#### 1. AWGN (Additive White Gaussian Noise)

Models thermal noise in the receiver. The noise is white (flat spectrum) and Gaussian distributed.

```cpp
// Basic usage - auto-seeded RNG
add_awgn(samples, 20.0f);  // Add noise at 20 dB SNR

// With explicit RNG for reproducibility
std::mt19937 rng(42);
add_awgn(samples, 15.0f, rng);

// With fixed seed
add_awgn_seeded(samples, 15.0f, 12345);
```

**Theory:**
```
SNR = P_signal / P_noise
P_noise = P_signal / 10^(SNR_dB/10)
noise_std = sqrt(P_noise)
```

**Typical SNR Values:**
| Condition | SNR (dB) |
|-----------|----------|
| Excellent HF | 30+ |
| Good HF | 20-25 |
| Marginal | 12-15 |
| Poor | 6-10 |

---

#### 2. Static Multipath (Single Echo)

Models a single reflection arriving after the direct path. Common in HF groundwave + ionospheric reflection.

```cpp
// Add echo at 48 samples delay (~1ms at 48kHz), 0.5 gain (-6dB)
add_multipath(samples, 48, 0.5f);

// Use milliseconds instead of samples
add_multipath_ms(samples, 1.5f, 0.4f, 48000.0f);  // 1.5ms delay
```

**Output Model:**
```
y[n] = x[n] + echo_gain * x[n - delay]
```

**Delay Conversion (48kHz):**
| Delay (ms) | Samples |
|------------|---------|
| 0.5 | 24 |
| 1.0 | 48 |
| 2.0 | 96 |
| 3.0 | 144 |

**Note:** Symbol period at 2400 baud = 20 samples (0.42ms). Multipath delays longer than this cause inter-symbol interference (ISI).

---

#### 3. Two-Path Multipath

Models two reflection paths for more realistic HF skywave propagation.

```cpp
// Two echoes: 0.5ms at -6dB, 1.5ms at -10dB
add_two_path(samples, 24, 0.5f, 72, 0.3f);
```

**Output Model:**
```
y[n] = x[n] + gain1*x[n-delay1] + gain2*x[n-delay2]
```

---

#### 4. Frequency Offset

Models frequency error between TX and RX oscillators, or Doppler shift from ionospheric motion.

```cpp
// Add +5 Hz frequency offset
add_freq_offset(samples, 5.0f, 48000.0f);

// With initial phase
add_freq_offset_phased(samples, 3.0f, 0.5f, 48000.0f);
```

**Output Model:**
```
y[n] = x[n] * cos(2*π*offset*n/fs)
```

**Typical Values:**
| Source | Offset (Hz) |
|--------|-------------|
| Crystal drift | ±1-5 |
| Ionospheric Doppler | ±0.1-2 |
| Worst case combined | ±10 |

---

#### 5. Phase Noise

Models oscillator instability and phase jitter.

```cpp
std::mt19937 rng(42);
add_phase_noise(samples, 0.05f, rng);  // 0.05 rad std deviation
```

**Typical Values:**
| Oscillator Quality | Phase Noise (rad) |
|--------------------|-------------------|
| Good | 0.01-0.05 |
| Poor | 0.1-0.2 |

---

#### 6. Rayleigh Fading

Models rapid amplitude fluctuations from ionospheric scintillation and multipath interference.

```cpp
std::mt19937 rng(42);
add_rayleigh_fading(samples, 1.0f, 48000.0f, rng);  // 1 Hz Doppler spread
```

**Doppler Spread Values:**
| Condition | Doppler (Hz) |
|-----------|--------------|
| Slow fading | 0.1-0.5 |
| Moderate | 0.5-2 |
| Fast fading | 2-5 |

---

### Combined Channel Models

The `ChannelConfig` structure and `apply_channel()` function allow combining multiple impairments:

```cpp
// Manual configuration
ChannelConfig cfg;
cfg.awgn_enabled = true;
cfg.snr_db = 18.0f;
cfg.multipath_enabled = true;
cfg.multipath_delay_samples = 48;
cfg.multipath_gain = 0.5f;
cfg.fading_enabled = true;
cfg.fading_doppler_hz = 1.0f;
cfg.seed = 42;  // For reproducibility

apply_channel(samples, cfg);
```

**Application Order:**
1. Frequency offset (before multipath for realism)
2. Multipath
3. Rayleigh fading
4. Phase noise
5. AWGN (always last)

---

### Preset Channel Models

Ready-to-use presets based on ITU-R standards:

```cpp
// Good HF conditions
auto samples_good = encode_result.value();
apply_channel(samples_good, channel_good_hf());

// Moderate HF conditions
apply_channel(samples_mod, channel_moderate_hf());

// Poor HF conditions  
apply_channel(samples_poor, channel_poor_hf());

// ITU-R F.520 standard channels
apply_channel(samples, channel_ccir_good());
apply_channel(samples, channel_ccir_moderate());
apply_channel(samples, channel_ccir_poor());
```

**Preset Specifications:**

| Preset | SNR (dB) | MP Delay | MP Gain | Fading | Freq Offset |
|--------|----------|----------|---------|--------|-------------|
| `channel_good_hf()` | 25 | 0.5ms | 0.3 | No | No |
| `channel_moderate_hf()` | 18 | 1ms | 0.5 | 1Hz | No |
| `channel_poor_hf()` | 12 | 2ms | 0.7 | 3Hz | 5Hz |
| `channel_ccir_good()` | 20 | 0.5ms | 0.5 | 0.1Hz | No |
| `channel_ccir_moderate()` | 15 | 1ms | 0.5 | 0.5Hz | No |
| `channel_ccir_poor()` | 10 | 2ms | 0.5 | 1Hz | No |

---

### Analysis Functions

#### Calculate Bit Error Rate (BER)

```cpp
std::vector<uint8_t> tx_data = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
std::vector<uint8_t> rx_data = decode_result.data;

double ber = calculate_ber(tx_data, rx_data);
std::cout << "BER: " << ber << std::endl;  // 0.0 = perfect
```

#### Calculate Symbol Error Rate (SER)

```cpp
// For 8-PSK (3 bits/symbol)
double ser = calculate_ser(tx_data, rx_data, 3);

// For QPSK (2 bits/symbol)
double ser_qpsk = calculate_ser(tx_data, rx_data, 2);
```

#### Estimate Signal Power

```cpp
float power = estimate_signal_power(samples);
float power_dbm = 10.0f * log10(power) + 30.0f;
```

#### Estimate SNR

```cpp
float snr_est = estimate_snr(samples);
std::cout << "Estimated SNR: " << snr_est << " dB" << std::endl;
```

---

### Complete Example

```cpp
#include "api/modem.h"
#include "api/channel_sim.h"
#include <iostream>

using namespace m110a::api;
using namespace m110a::api::channel;

int main() {
    // Test data
    std::vector<uint8_t> test_data = {'T', 'E', 'S', 'T', ' ', 'D', 'A', 'T', 'A'};
    
    // Encode
    auto encode_result = encode(test_data, Mode::M2400_SHORT);
    if (!encode_result.ok()) {
        std::cerr << "Encode failed!" << std::endl;
        return 1;
    }
    
    auto samples = encode_result.value();
    
    // Apply moderate HF channel
    apply_channel(samples, channel_moderate_hf());
    
    // Decode
    RxConfig rx_cfg;
    rx_cfg.equalizer = Equalizer::DFE;
    auto decode_result = decode(samples, rx_cfg);
    
    // Calculate BER
    double ber = calculate_ber(test_data, decode_result.data);
    
    std::cout << "Mode: 2400S" << std::endl;
    std::cout << "Channel: MODERATE_HF" << std::endl;
    std::cout << "BER: " << std::scientific << ber << std::endl;
    std::cout << "Success: " << (decode_result.success ? "YES" : "NO") << std::endl;
    
    return 0;
}
```

---

## Part 2: Server Commands

The MS-DMT server provides interactive channel simulation commands on the control port (TCP 4999).

### Command Reference

#### `CMD:CHANNEL CONFIG`

Display current channel configuration.

```
>> CMD:CHANNEL CONFIG
<< OK:CHANNEL CONFIG:CHANNEL CONFIG:
   Enabled: YES
   AWGN: ON (SNR=20dB)
   Multipath: ON (delay=48 samples, gain=0.5)
   FreqOffset: OFF
```

---

#### `CMD:CHANNEL PRESET:<preset>`

Apply a predefined channel model.

**Available Presets:**
- `GOOD` / `GOOD_HF` - Good HF conditions
- `MODERATE` / `MODERATE_HF` - Typical HF conditions  
- `POOR` / `POOR_HF` - Disturbed ionosphere
- `CCIR_GOOD` - ITU-R F.520 good channel
- `CCIR_MODERATE` - ITU-R F.520 moderate channel
- `CCIR_POOR` - ITU-R F.520 poor channel
- `CLEAN` / `OFF` - No impairments

```
>> CMD:CHANNEL PRESET:MODERATE
<< OK:CHANNEL PRESET:MODERATE_HF (SNR=18dB, MP=48samp, FOFF=0Hz)

>> CMD:CHANNEL PRESET:CLEAN
<< OK:CHANNEL PRESET:CLEAN (no impairments)
```

---

#### `CMD:CHANNEL AWGN:<snr_db>`

Enable AWGN with specified SNR.

```
>> CMD:CHANNEL AWGN:15
<< OK:CHANNEL AWGN:AWGN enabled at 15 dB SNR
```

**Valid Range:** 0-60 dB

---

#### `CMD:CHANNEL MULTIPATH:<delay>[,<gain>]`

Enable static multipath echo.

```
>> CMD:CHANNEL MULTIPATH:48
<< OK:CHANNEL MULTIPATH:Multipath enabled: delay=48 samples (1ms), gain=0.5

>> CMD:CHANNEL MULTIPATH:96,0.3
<< OK:CHANNEL MULTIPATH:Multipath enabled: delay=96 samples (2ms), gain=0.3
```

**Parameters:**
- `delay`: 1-500 samples (at 48kHz, 48 samples = 1ms)
- `gain`: 0.0-1.0 (optional, default 0.5)

---

#### `CMD:CHANNEL FREQOFFSET:<offset_hz>`

Enable frequency offset.

```
>> CMD:CHANNEL FREQOFFSET:5
<< OK:CHANNEL FREQOFFSET:Frequency offset enabled: 5 Hz

>> CMD:CHANNEL FREQOFFSET:-3.5
<< OK:CHANNEL FREQOFFSET:Frequency offset enabled: -3.5 Hz
```

**Valid Range:** -50 to +50 Hz

---

#### `CMD:CHANNEL OFF`

Disable all channel impairments.

```
>> CMD:CHANNEL OFF
<< OK:CHANNEL OFF:All channel impairments disabled
```

---

#### `CMD:CHANNEL APPLY:<input>[,<output>]`

Apply channel impairments to a PCM file. Useful for offline testing.

```
>> CMD:CHANNEL AWGN:15
<< OK:CHANNEL AWGN:AWGN enabled at 15 dB SNR

>> CMD:CHANNEL APPLY:tx_pcm_out/signal.pcm,impaired.pcm
<< OK:CHANNEL APPLY:Applied [AWGN=15dB ] to 240000 samples -> impaired.pcm
```

**Parameters:**
- `input`: Input PCM file path
- `output`: Output PCM file path (optional, defaults to overwrite input)

**Note:** You must configure channel impairments first using other `CMD:CHANNEL` commands.

---

### Example Server Session

```
# Connect to control port
$ nc localhost 4999

# Set mode
>> CMD:DATA RATE:1200S
<< OK:DATA RATE:1200S

# Configure channel
>> CMD:CHANNEL AWGN:15
<< OK:CHANNEL AWGN:AWGN enabled at 15 dB SNR

>> CMD:CHANNEL MULTIPATH:48
<< OK:CHANNEL MULTIPATH:Multipath enabled: delay=48 samples (1ms), gain=0.5

# Check config
>> CMD:CHANNEL CONFIG
<< OK:CHANNEL CONFIG:CHANNEL CONFIG:
   Enabled: YES
   AWGN: ON (SNR=15dB)
   Multipath: ON (delay=48 samples, gain=0.5)
   FreqOffset: OFF

# Inject a PCM file for decode (channel will be applied automatically)
>> CMD:RXAUDIOINJECT:tx_pcm_out/signal.pcm
<< OK:RXAUDIOINJECT:STARTED:tx_pcm_out/signal.pcm
<< STATUS:RX:1200 BPS SHORT
<< STATUS:RX:NO DCD
<< OK:RXAUDIOINJECT:COMPLETE:240000 samples (channel sim applied)

# Or apply channel to a file for later testing
>> CMD:CHANNEL APPLY:tx_pcm_out/signal.pcm,impaired.pcm
<< OK:CHANNEL APPLY:Applied [AWGN=15dB MP=48samp ] to 240000 samples -> impaired.pcm

# Try with preset
>> CMD:CHANNEL PRESET:CCIR_MODERATE
<< OK:CHANNEL PRESET:CCIR_MODERATE (SNR=15dB, MP=48samp, FOFF=0Hz)

# Disable channel simulation
>> CMD:CHANNEL OFF
<< OK:CHANNEL OFF:All channel impairments disabled
```

**Note:** When channel simulation is enabled, `CMD:RXAUDIOINJECT` will automatically apply the configured impairments to the PCM before decoding. This allows testing the modem's performance under various channel conditions.

---

## Appendix: Quick Reference

### API Functions

| Function | Purpose |
|----------|---------|
| `add_awgn(samples, snr_db)` | Add Gaussian noise |
| `add_multipath(samples, delay, gain)` | Add single echo |
| `add_multipath_ms(samples, delay_ms, gain)` | Add echo (ms units) |
| `add_two_path(samples, d1, g1, d2, g2)` | Add two echoes |
| `add_freq_offset(samples, offset_hz)` | Add carrier offset |
| `add_phase_noise(samples, std_rad, rng)` | Add phase jitter |
| `add_rayleigh_fading(samples, doppler, fs, rng)` | Add fading |
| `apply_channel(samples, config)` | Apply combined model |
| `calculate_ber(tx, rx)` | Compute bit error rate |
| `calculate_ser(tx, rx, bits_per_sym)` | Compute symbol error rate |
| `estimate_snr(samples)` | Estimate SNR from signal |

### Server Commands

| Command | Purpose |
|---------|---------|
| `CMD:CHANNEL CONFIG` | Show current settings |
| `CMD:CHANNEL PRESET:<name>` | Apply preset model |
| `CMD:CHANNEL AWGN:<snr>` | Enable AWGN |
| `CMD:CHANNEL MULTIPATH:<delay>[,<gain>]` | Enable multipath |
| `CMD:CHANNEL FREQOFFSET:<hz>` | Enable freq offset |
| `CMD:CHANNEL OFF` | Disable all impairments |
| `CMD:CHANNEL APPLY:<in>[,<out>]` | Apply channel to PCM file |

### Preset Summary

| Preset | SNR | Delay | Fading | Use Case |
|--------|-----|-------|--------|----------|
| GOOD_HF | 25dB | 0.5ms | No | Daytime, short path |
| MODERATE_HF | 18dB | 1ms | 1Hz | Typical conditions |
| POOR_HF | 12dB | 2ms | 3Hz | Disturbed ionosphere |
| CCIR_GOOD | 20dB | 0.5ms | 0.1Hz | ITU-R standard good |
| CCIR_MODERATE | 15dB | 1ms | 0.5Hz | ITU-R standard moderate |
| CCIR_POOR | 10dB | 2ms | 1Hz | ITU-R standard poor |
