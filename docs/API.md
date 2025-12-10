# API Reference

## Overview

The M110A modem API provides a simple, thread-safe interface for encoding and decoding MIL-STD-188-110A waveforms.

```cpp
#include "api/modem.h"
using namespace m110a::api;
```

## Core Types

### Result<T>

All API methods return `Result<T>` for error handling:

```cpp
auto result = tx.encode(data);

if (result.ok()) {
    auto& samples = result.value();
    // Use samples
} else {
    std::cerr << "Error: " << result.error().message << "\n";
}
```

### Error Codes

```cpp
enum class ErrorCode {
    // Configuration (100-199)
    INVALID_MODE = 100,
    INVALID_SAMPLE_RATE = 101,
    INVALID_CARRIER_FREQ = 102,
    
    // TX (200-299)
    TX_DATA_TOO_LARGE = 200,
    TX_DATA_EMPTY = 201,
    
    // RX (300-399)
    RX_NO_SIGNAL = 300,
    RX_SYNC_FAILED = 301,
    RX_MODE_DETECT_FAILED = 302,
    RX_DECODE_FAILED = 303,
    
    // I/O (400-499)
    FILE_NOT_FOUND = 400,
    FILE_READ_ERROR = 401,
};
```

### Mode

```cpp
enum class Mode {
    AUTO,           // RX only: auto-detect mode
    M75_SHORT,      // 75 bps, short interleave
    M75_LONG,       // 75 bps, long interleave
    M150_SHORT,     // 150 bps, short interleave
    M150_LONG,      // ...
    M300_SHORT,
    M300_LONG,
    M600_SHORT,
    M600_LONG,
    M1200_SHORT,
    M1200_LONG,
    M2400_SHORT,
    M2400_LONG,
    M4800_SHORT,    // 4800 bps uncoded
};

// Helper functions
std::string mode_name(Mode m);           // "M2400_SHORT"
int mode_data_rate(Mode m);              // 2400
bool mode_is_long_interleave(Mode m);    // false
```

### Equalizer

```cpp
enum class Equalizer {
    NONE,       // No equalization (use for clean channels)
    DFE,        // Decision Feedback Equalizer (default)
    MLSE_L2,    // MLSE with L=2 (8 states)
    MLSE_L3,    // MLSE with L=3 (64 states)
};
```

## LicenseManager (`license_manager` activation flow)

`m110a::LicenseManager` (see `src/common/license.h`) implements the shared `license_manager` activation pipeline that every executable in this repository runs before accessing the modem.

```cpp
#include "src/common/license.h"

LicenseInfo info;
LicenseStatus status = LicenseManager::load_license_file("license.key", info);
if (status != LicenseStatus::VALID) {
    std::cout << "Hardware ID: " << LicenseManager::get_hardware_id() << "\n";
    throw std::runtime_error(LicenseManager::get_status_message(status));
}
```

Activation steps:

1. Call `LicenseManager::get_hardware_id()` (or run `license_gen.exe --hwid`) to read the fingerprint produced by the `license_manager` flow.
2. Go to https://www.organicengineer.com/projects and submit the hardware ID to obtain a license key.
3. Store the returned key as `license.key` next to the binary that will link against the modem API.
4. During startup, invoke `LicenseManager::load_license_file()` and block modem usage until it returns `LicenseStatus::VALID`.

Any failure mode (missing file, mismatched hardware, expired key) should surface the status string from `LicenseManager::get_status_message()` and stop the operation just as the server and exhaustive test binaries do.

---

## ModemTX

Transmitter class for encoding data to audio samples.

### Construction

```cpp
ModemTX tx;                      // Default config (M2400_SHORT)
ModemTX tx(TxConfig::for_mode(Mode::M1200_SHORT));
```

### TxConfig

```cpp
struct TxConfig {
    Mode mode = Mode::M2400_SHORT;    // Operating mode
    float sample_rate = 8000.0f;      // 8000 or 48000 Hz
    float carrier_freq = 1800.0f;     // 500-3000 Hz
    float amplitude = 0.8f;           // 0.0-1.0
    bool include_preamble = true;     // Add sync preamble
    bool include_eom = true;          // Add end-of-message marker
    bool use_pulse_shaping = false;   // RRC pulse shaping
};
```

### Methods

#### encode()
```cpp
Result<Samples> encode(const std::vector<uint8_t>& data);
```
Encode data bytes to PCM audio samples.

**Example:**
```cpp
std::vector<uint8_t> data = {'T', 'E', 'S', 'T'};
auto result = tx.encode(data);
if (result.ok()) {
    // result.value() contains float samples
}
```

#### generate_preamble()
```cpp
Result<Samples> generate_preamble();
```
Generate preamble only (for testing sync).

#### generate_tone()
```cpp
Result<Samples> generate_tone(float duration, float freq = 0);
```
Generate carrier tone (default: carrier_freq).

#### set_mode()
```cpp
Result<void> set_mode(Mode mode);
```
Change operating mode.

#### config()
```cpp
const TxConfig& config() const;
```
Get current configuration.

---

## ModemRX

Receiver class for decoding audio samples to data.

### Construction

```cpp
ModemRX rx;                      // Default config (AUTO mode)
ModemRX rx(RxConfig::for_mode(Mode::M2400_SHORT));
```

### RxConfig

```cpp
struct RxConfig {
    Mode mode = Mode::AUTO;           // AUTO for auto-detection
    float sample_rate = 8000.0f;      // 8000 or 48000 Hz
    float carrier_freq = 1800.0f;     // Expected carrier
    float freq_search_range = 100.0f; // Search ±100 Hz
    Equalizer equalizer = Equalizer::DFE;
    bool use_nlms = false;            // Normalized LMS (for fading)
    bool phase_tracking = true;       // Adaptive phase tracking
    bool agc_enabled = true;          // Automatic gain control
    float min_snr_db = 3.0f;          // Minimum SNR to decode
};
```

### DecodeResult

```cpp
struct DecodeResult {
    bool success;                     // Decode succeeded
    Mode mode;                        // Detected/used mode
    std::vector<uint8_t> data;        // Decoded bytes
    bool eom_detected;                // End-of-message found
    float snr_db;                     // Estimated SNR
    float ber_estimate;               // Estimated BER
    float freq_offset_hz;             // Detected frequency offset
    std::optional<Error> error;       // Error if !success
    
    std::string as_string() const;    // Convert data to string
};
```

### Methods

#### decode()
```cpp
DecodeResult decode(const Samples& samples);
```
Decode audio samples to data.

**Example:**
```cpp
auto result = rx.decode(samples);
if (result.success) {
    std::cout << "Received: " << result.as_string() << "\n";
    std::cout << "SNR: " << result.snr_db << " dB\n";
    if (result.eom_detected) {
        std::cout << "End of message detected\n";
    }
}
```

#### decode_file()
```cpp
DecodeResult decode_file(const std::string& filename);
```
Decode from WAV or raw PCM file.

**Supported formats:**
- `.wav` - Standard WAV (any sample rate, mono)
- `.pcm` - Raw 16-bit signed PCM

#### set_mode()
```cpp
Result<void> set_mode(Mode mode);
```
Set fixed mode (disables auto-detection).

#### config()
```cpp
const RxConfig& config() const;
```
Get current configuration.

#### state()
```cpp
RxState state() const;
```
Get receiver state (IDLE, SEARCHING, RECEIVING, etc.).

---

## Usage Patterns

### Simple Loopback
```cpp
TxConfig tx_cfg;
tx_cfg.mode = Mode::M1200_SHORT;
tx_cfg.sample_rate = 48000.0f;

RxConfig rx_cfg;
rx_cfg.mode = Mode::M1200_SHORT;
rx_cfg.sample_rate = 48000.0f;

ModemTX tx(tx_cfg);
ModemRX rx(rx_cfg);

std::vector<uint8_t> data = {1, 2, 3, 4, 5};
auto encoded = tx.encode(data);
auto decoded = rx.decode(encoded.value());

assert(decoded.success);
assert(decoded.data == data);
```

### Auto-Detection
```cpp
RxConfig rx_cfg;
rx_cfg.mode = Mode::AUTO;  // Will detect mode from preamble

ModemRX rx(rx_cfg);
auto result = rx.decode(samples);

std::cout << "Detected mode: " << mode_name(result.mode) << "\n";
```

### HF Channel with Equalization
```cpp
RxConfig rx_cfg;
rx_cfg.equalizer = Equalizer::DFE;
rx_cfg.use_nlms = true;  // For fading channels
rx_cfg.phase_tracking = true;

ModemRX rx(rx_cfg);
auto result = rx.decode(noisy_samples);
```

### File Operations
```cpp
// Decode from file
ModemRX rx;
auto result = rx.decode_file("received.wav");

// Encode to file (manual)
ModemTX tx;
auto samples = tx.encode(data).value();
// Write samples to WAV file...
```

---

## Thread Safety

Both `ModemTX` and `ModemRX` are thread-safe:
- Multiple threads can call methods on the same instance
- Internal mutex protects all state
- Config changes are atomic

```cpp
ModemRX rx;  // Shared receiver

// Thread 1
auto result1 = rx.decode(samples1);

// Thread 2 (concurrent)
auto result2 = rx.decode(samples2);
```

---

## Performance Tips

1. **Use 48000 Hz sample rate** for better timing recovery
2. **Enable NLMS** on fading channels (`use_nlms = true`)
3. **Use DFE** as default equalizer
4. **Use MLSE_L2/L3** only when DFE isn't sufficient
5. **Disable phase_tracking** when using DFE/MLSE (they handle phase)
6. **Pre-allocate buffers** when processing many messages

---

## Error Handling

```cpp
auto result = tx.encode(data);

// Check success
if (!result.ok()) {
    auto& err = result.error();
    std::cerr << "Error " << static_cast<int>(err.code) 
              << ": " << err.message << "\n";
    return;
}

// Use value
auto& samples = result.value();
```

Common errors:
- `TX_DATA_EMPTY` - Empty data vector
- `RX_SYNC_FAILED` - No preamble found
- `RX_MODE_DETECT_FAILED` - Couldn't identify mode
- `RX_DECODE_FAILED` - Viterbi decode failed
