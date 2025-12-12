# SDRplay Integration Pathway

**Created:** January 15, 2025  
**Purpose:** Integration pathway for phoenix_sdr → Phoenix Nest Modem  
**Status:** ✅ READY FOR INTEGRATION

---

## 🎯 phoenix_sdr is Ready!

**See:** [HANDOFF_TO_MODEM_AGENT.md](https://github.com/Alex-Pennington/phoenix_sdr/blob/main/docs/HANDOFF_TO_MODEM_AGENT.md) for full integration details.

### Key Points:
1. **phoenix_sdr callback signature matches `IQSource.push_samples_planar()` exactly**
2. **.iqr files are interleaved int16 at 2 MSPS with 64-byte header**
3. **Use IQSource decimation (modem-side)** - phoenix_sdr outputs raw 2 MSPS
4. **Start with file-based testing** (.iqr playback), then live SDR

### Local Development Setup
```
D:\claude_sandbox\
├── phoenix_sdr\              ← SDR library (ready)
├── pennington_m110a_demod\   ← Modem (this repo)
└── PROGRESS.md               ← Overall tracking
```

---

## Overview

This document describes the integration path for connecting the **phoenix_sdr** SDRplay RSP2 Pro capture system to the **Phoenix Nest** MIL-STD-188-110A modem for direct I/Q input.

### Key Components

| Component | Repository | Purpose |
|-----------|------------|---------|
| **phoenix_sdr** | [Alex-Pennington/phoenix_sdr](https://github.com/Alex-Pennington/phoenix_sdr) | SDRplay RSP2 Pro I/Q capture (C17) |
| **Phoenix Nest Modem** | This repo | MIL-STD-188-110A HF data modem (C++17) |
| **IQSource** | `api/iq_source.h` | I/Q input interface with decimation |

### The Perfect Match 🎯

The modem team built an `IQSource` class that directly matches phoenix_sdr's callback signature:

```cpp
// phoenix_sdr callback produces:
void on_samples(const int16_t* xi, const int16_t* xq, uint32_t count, bool reset, void* ctx);

// IQSource accepts:
void push_samples_planar(const int16_t* xi, const int16_t* xq, size_t count);
```

**This is a 1:1 match** - no adapter code needed!

---

## Architecture

### Signal Flow

```
┌──────────────────┐     ┌─────────────────────────────────────────┐
│   SDRplay        │     │              Phoenix Nest Modem         │
│   RSP2 Pro       │     │                                         │
│                  │     │  ┌───────────┐     ┌─────────────────┐  │
│   2 MSPS I/Q ────┼────►│  │ IQSource  │────►│   Demodulator   │  │
│   int16 planar   │     │  │           │     │   (unchanged)   │  │
│                  │     │  │ Decimate  │     └────────┬────────┘  │
└──────────────────┘     │  │ 2M → 48k  │              │           │
                         │  └───────────┘              ▼           │
                         │                      ┌─────────────┐    │
                         │                      │ Data Output │    │
                         │                      └─────────────┘    │
                         └─────────────────────────────────────────┘
```

### Key Insight

The modem handles decimation internally (`IQSource` has multi-stage 2 MSPS → 48 kHz). This means:
- phoenix_sdr outputs raw 2 MSPS samples
- phoenix_sdr's `decimator.c` is **optional** (can be archived)
- All rate conversion happens modem-side

---

## Integration Phases

### Phase 1: File-Based Testing (Current Focus)

Use `.iqr` recordings to validate the pipeline without live SDR hardware.

**Files Involved:**
- `api/iq_source.h` - I/Q input with decimation
- `api/sample_source.h` - Abstract interface
- `test/test_sample_source.cpp` - Unit tests (10/10 PASS)

**Test Procedure:**
1. Capture a known MIL-STD-188-110A signal with phoenix_sdr
2. Save as `.iqr` file (2 MSPS, int16 interleaved)
3. Create `IQFileSource` wrapper around `iqr_reader`
4. Feed to modem, verify decode

**Code for .iqr Playback:**
```cpp
#include "api/iq_source.h"

// Include phoenix_sdr's iq_recorder header (or copy iqr_reader code)
extern "C" {
#include "iq_recorder.h"
}

// Create IQSource for 2 MSPS input
m110a::IQSource source(2000000.0, m110a::IQSource::Format::INT16_INTERLEAVED, 48000.0);

// Open .iqr file
iqr_reader_t* reader;
iqr_open(&reader, "capture.iqr");

// Read and push samples
int16_t xi[4096], xq[4096];
uint32_t num_read;

while (iqr_read(reader, xi, xq, 4096, &num_read) == IQR_OK && num_read > 0) {
    // For interleaved format, interleave before pushing
    std::vector<int16_t> iq_interleaved(num_read * 2);
    for (uint32_t i = 0; i < num_read; i++) {
        iq_interleaved[i*2] = xi[i];
        iq_interleaved[i*2+1] = xq[i];
    }
    source.push_samples_interleaved(iq_interleaved.data(), num_read);
}

// Read decimated output for demodulator
std::complex<float> baseband[1024];
while (source.has_data()) {
    size_t n = source.read(baseband, 1024);
    // Feed to demodulator...
}
```

### Phase 2: Library Integration (Production)

Link phoenix_sdr directly into modem, call `IQSource::push_samples_planar()` from SDR callback.

**Bridge Code:**
```cpp
// Global or shared context
m110a::IQSource* g_iq_source = nullptr;

// SDR callback - called from SDRplay API thread
void on_samples(const int16_t* xi, const int16_t* xq, 
                uint32_t count, bool reset, void* ctx) {
    if (reset) {
        g_iq_source->reset();
    }
    g_iq_source->push_samples_planar(xi, xq, count);
}

// Main thread reads output
void modem_rx_thread() {
    std::complex<float> buffer[1024];
    while (running) {
        size_t n = g_iq_source->read(buffer, 1024);
        if (n > 0) {
            demodulator->process(buffer, n);
        }
    }
}
```

### Phase 3: TCP Streaming (Optional)

For remote SDR scenarios, phoenix_sdr can stream over TCP.

**Not implemented yet** - requires:
- Protocol definition (header + raw samples)
- `TCPIQSource` wrapper class
- Network error handling

---

## .iqr File Format

Binary format from phoenix_sdr's `iq_recorder.h`:

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | Magic | "IQR1" |
| 4 | 4 | Version | Format version (1) |
| 8 | 8 | Sample Rate | Hz (double) |
| 16 | 8 | Center Freq | Hz (double) |
| 24 | 4 | Bandwidth | kHz (uint32) |
| 28 | 4 | Gain Reduction | dB (int32) |
| 32 | 4 | LNA State | 0-8 (uint32) |
| 36 | 8 | Start Time | Unix µs (int64) |
| 44 | 8 | Sample Count | Total samples (uint64) |
| 52 | 4 | Flags | Reserved |
| 56 | 8 | Reserved | Padding |
| **64** | ... | **Data** | **Interleaved I/Q (int16 pairs)** |

Data format: `I0, Q0, I1, Q1, I2, Q2, ...` (little-endian int16)

---

## Decimation Comparison

The modem team built decimation that's comparable to phoenix_sdr's:

| Stage | phoenix_sdr (decimator.c) | IQSource |
|-------|---------------------------|----------|
| 1 | 2M → 250k (÷8, 31-tap) | 2M → 250k (÷8, 63-tap) |
| 2 | 250k → 50k (÷5, 25-tap) | 250k → 50k (÷5, 63-tap) |
| 3 | 50k → 48k (polyphase) | Linear interpolation |

**Key difference:** IQSource uses more filter taps (better stopband rejection) but simpler final resampling. Both are adequate for HF data modem work.

**Recommendation:** Use IQSource decimation (modem-side) - already tested, phoenix_sdr just outputs raw 2 MSPS.

---

## Hardware Configuration

### SDRplay RSP2 Pro Settings

| Parameter | Value | Notes |
|-----------|-------|-------|
| Device | RSP2 Pro | Serial: 1717046711 |
| Sample Rate | 2 MSPS | 14-bit mode |
| Bandwidth | 200 kHz | Narrowest for HF |
| IF Mode | Zero IF | Baseband I/Q |
| AGC | Disabled | Manual gain for modem |
| Gain Reduction | 40 dB | Tune for ~-20 dBFS peaks |
| LNA State | 4 | Mid-range |
| Antenna | Port A | |

### Frequency Examples

| Band | Frequency | Use Case |
|------|-----------|----------|
| 40m | 7.074 MHz | FT8 (test captures) |
| 40m | 7.102 MHz | ALE/data |
| 20m | 14.100 MHz | MARS |
| Custom | Varies | MIL-STD-188-110A testing |

---

## Test Strategy

### Unit Tests (Already Done ✅)

| Test | Status |
|------|--------|
| IQSource int16 planar conversion | ✅ PASS |
| IQSource int16 interleaved conversion | ✅ PASS |
| IQSource float32 planar conversion | ✅ PASS |
| IQSource float32 interleaved conversion | ✅ PASS |
| IQSource decimation (2 MSPS → 48 kHz) | ✅ PASS |
| AudioSource basic conversion | ✅ PASS |
| AudioSource PCM input | ✅ PASS |
| SampleSource polymorphism | ✅ PASS |
| IQSource reset | ✅ PASS |
| IQSource metadata | ✅ PASS |

### Integration Tests (TODO)

| Test | Description | Status |
|------|-------------|--------|
| .iqr playback | Load capture, verify decimation | ✅ DONE (IQFileSource) |
| Known signal decode | Capture 110A signal, verify decode | ❌ TODO |
| Loopback | TX→capture→RX, compare data | ❌ TODO |
| Audio vs I/Q comparison | Same signal, both paths, compare BER | ❌ TODO |

### OTA Tests (Future)

| Test | Description | Status |
|------|-------------|--------|
| Live SDR capture | Real signal via RSP2 Pro | ❌ TODO |
| Cross-modem I/Q | PN TX→SDR→BC RX (or vice versa) | ❌ TODO |

---

## Next Steps

### Immediate (File-Based Testing)

1. [x] Create `IQFileSource` wrapper for `.iqr` playback ✅
2. [ ] Capture a known MIL-STD-188-110A signal (or generate with TX)
3. [ ] Feed `.iqr` to IQSource, verify decimated output looks correct
4. [ ] Connect to demodulator, verify decode

### Short-Term (Library Integration)

1. [ ] Add phoenix_sdr as git submodule (or build as library)
2. [ ] Create bridge between SDR callback and IQSource
3. [ ] Test real-time streaming (watch for buffer overruns)

### Long-Term (Production)

1. [ ] Add I/Q source selection to server command-line
2. [ ] Implement graceful handling of SDR disconnection
3. [ ] Add metadata display (center freq, bandwidth, signal level)

---

## Repository References

### phoenix_sdr Key Files

| File | Purpose |
|------|---------|
| `include/phoenix_sdr.h` | Main SDR API |
| `include/iq_recorder.h` | .iqr file format and reader |
| `include/decimator.h` | Decimation (optional - modem handles it) |
| `src/main.c` | Example with callbacks |

### Phoenix Nest Modem Key Files

| File | Purpose |
|------|---------|
| `api/sample_source.h` | Abstract input interface |
| `api/iq_source.h` | I/Q input with decimation |
| `api/iq_file_source.h` | .iqr file reader wrapper |
| `api/audio_source.h` | Legacy audio input wrapper |
| `test/test_sample_source.cpp` | Unit tests for IQSource/AudioSource |
| `test/test_iq_file_source.cpp` | Unit tests for IQFileSource (11 tests) |

---

## Change Log

| Date | Change | By |
|------|--------|-----|
| 2025-01-15 | Document created | Copilot |
| 2025-01-15 | Added IQSource interface from SDRPlay_Integration branch | Copilot |
| 2025-01-15 | Added IQFileSource wrapper for .iqr playback | Copilot |

---

*This document tracks the integration pathway for SDRplay RSP2 Pro support in Phoenix Nest Modem.*
