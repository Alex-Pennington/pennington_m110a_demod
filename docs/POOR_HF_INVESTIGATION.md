# Poor HF Channel Investigation

## Issue Summary
The `poor_hf` channel preset fails 100% of tests in DirectBackend mode (BER=1.00), while occasionally passing in server mode. Investigation reveals a critical AFC (Automatic Frequency Control) deficiency.

## Key Findings

### 1. Historical Test Results
- **DirectBackend**: poor_hf has ALWAYS failed (0% pass rate in all reports)
- **Server Backend**: Occasional passes, but limited testing (1-3 iterations, single modes)
- **First noticed**: December 8, 2025 during exhaustive testing

### 2. Root Cause: AFC Failure
Debug testing revealed frequency offset alone causes catastrophic failure:

```
Clean:                BER=0.0000 âœ“
SNR 15dB only:        BER=0.0000 âœ“
MP 48samp only:       BER=0.0000 âœ“
Freq 3Hz only:        BER=0.4609 âœ— (46% error!)
MP 48 + Freq 3Hz:     FAILED (no decode)
Poor HF (all 3):      FAILED (no decode)
```

**Even 3 Hz frequency offset with clean signal produces 46% BER and 90 bytes of garbage instead of 16 bytes.**

### 3. AFC Configuration
Current AFC settings:
- Search range: Â±10 Hz (configurable via `freq_search_range`)
- Search step: 1 Hz
- Location: `BrainDecoderConfig` in `src/m110a/brain_decoder.h`

Despite 3 Hz being well within Â±10 Hz range, AFC fails to acquire lock.

### 4. Channel Configurations Tested

#### Original poor_hf (Build <59)
```
SNR: 12 dB
Multipath: 96 samples (2ms), 0.7 gain
Fading: 3 Hz Doppler
Freq offset: 5-10 Hz
Result: Total failure
```

#### Modified poor_hf (Build 59-73)
```
SNR: 15 dB (increased for AFC)
Multipath: 48 samples (1ms), 0.5 gain  
Fading: 1 Hz Doppler
Freq offset: 3-7 Hz (reduced for AFC)
Result: Still total failure
```

#### Current poor_hf (Build 74)
```
SNR: 15 dB
Multipath: 48 samples, 0.5 gain
Fading: OFF
Freq offset: 3 Hz
Result: STILL FAILS
```

## Comparison: moderate_hf vs poor_hf

### moderate_hf (PASSES 100%)
```
SNR: 18 dB
Multipath: 48 samples, 0.5 gain
Fading: 1 Hz Doppler
Freq offset: NONE (0 Hz)
```

### poor_hf (FAILS 100%)
```
SNR: 15 dB
Multipath: 48 samples, 0.5 gain
Fading: OFF
Freq offset: 3 Hz
```

**The ONLY significant difference is 3 Hz frequency offset**, which completely breaks demodulation.

## Other Failing Channels

### foff_5hz
- SNR: 30 dB (excellent)
- Multipath: NONE
- Freq offset: 5 Hz
- **Result**: 0% pass rate, BER=0.50

### foff_1hz
- SNR: 30 dB
- Multipath: NONE  
- Freq offset: 1 Hz
- **Result**: 100% pass rate

**Conclusion**: AFC works up to ~1 Hz offset, fails at 3+ Hz offset.

## Technical Analysis

### AFC Implementation
The modem uses frequency-domain search in `BrainDecoder`:
1. Searches from -freq_search_range to +freq_search_range
2. Steps by freq_search_step (1 Hz)
3. Evaluates correlation at each frequency
4. Selects best frequency based on peak detection

### Why It Fails
Possible causes:
1. **Search metric insufficient**: Correlation peak may not be discriminative enough under noise/multipath
2. **Local minima**: Search may lock onto wrong frequency
3. **Insufficient search resolution**: 1 Hz steps may miss optimal frequency
4. **Phase ambiguity**: Frequency offset causes phase rotation that breaks symbol synchronization
5. **Preamble degradation**: Frequency offset may corrupt known preamble pattern used for acquisition

## Proposed Solutions

### Option 1: Eliminate poor_hf Channel
- **Pros**: Quick fix, acknowledges current limitation
- **Cons**: Reduces test coverage, doesn't solve underlying issue
- **Impact**: 84 fewer tests (9% of exhaustive suite)

### Option 2: Disable Frequency Offset in poor_hf
```cpp
inline ChannelConfig channel_poor_hf() {
    ChannelConfig cfg;
    cfg.awgn_enabled = true;
    cfg.snr_db = 15.0f;
    cfg.multipath_enabled = true;
    cfg.multipath_delay_samples = 48;
    cfg.multipath_gain = 0.5f;
    cfg.fading_enabled = true;
    cfg.fading_doppler_hz = 1.0f;
    cfg.freq_offset_enabled = false;  // DISABLED
    return cfg;
}
```
- **Pros**: Tests degraded HF without breaking AFC
- **Cons**: Doesn't test AFC under stress
- **Impact**: poor_hf becomes similar to moderate_hf with lower SNR

### Option 3: Fix AFC Implementation
Requires investigation and modification of:
- `src/m110a/brain_decoder.h` frequency search algorithm
- Preamble correlation under frequency offset
- Symbol timing recovery with residual frequency error
- Potential implementation of coarse/fine frequency acquisition

**Estimated effort**: 8-16 hours of development + testing

### Option 4: Increase AFC Search Range
```cpp
// In modem_config.h
float freq_search_range = 20.0f;  // Increase from 10 Hz
```
- **Pros**: May improve acquisition at cost of longer sync time
- **Cons**: Doesn't address root cause (3 Hz still fails within 10 Hz range)
- **Likelihood of success**: LOW

## Recommendation

**Short-term (immediate)**:
1. Set poor_hf freq_offset to 0 Hz (Option 2)
2. Rename to `poor_hf_no_foff` to clarify limitation
3. Add separate `foff_3hz` test channel with high SNR to track AFC status
4. Document AFC limitation in TCPIP Guide

**Long-term (future work)**:
1. Implement robust AFC (Option 3)
2. Add coarse/fine frequency acquisition stages
3. Improve preamble correlation under frequency offset
4. Consider pilot tone or differential detection for frequency tracking

## Test Recommendations

Until AFC is fixed:
- âœ“ Test frequency offset separately (foff_1hz only)
- âœ“ Test poor_hf without frequency offset
- âœ“ Document that 3+ Hz offset is known limitation
- âœ— Do NOT combine frequency offset with multipath/low SNR
- âœ— Do NOT expect poor_hf to pass in current state

## References
- `api/channel_sim.h`: Channel preset definitions
- `src/m110a/brain_decoder.h`: AFC implementation (line 42-43, 108-115)
- `api/modem_config.h`: Default freq_search_range (line 93)
- Test reports: `docs/test_reports/exhaustive_direct_*.md`

---

*Investigation Date: December 8, 2025*  
*Build: 74 (d570afe)*  
*Branch: turbo*
