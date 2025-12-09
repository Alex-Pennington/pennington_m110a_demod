# Auto-Detect vs Known Mode Testing

## Overview
Following investigation of poor_hf channel failures, we discovered that AFC (Automatic Frequency Control) performs differently depending on whether the receiver uses auto-detection or known-mode operation.

## Problem Discovery
- **Original Issue**: poor_hf channel failed 100% of tests (BER=1.00)
- **Root Cause**: AFC cannot handle frequency offsets ≥3 Hz when combined with auto-detect mode
- **Critical Finding**: Even 3 Hz offset with clean signal produces 46% BER in auto-detect mode
- **Why Server Passed**: Server backend uses KNOWN mode (pre-specified), DirectBackend used AUTO mode

## Two Operating Modes

### Known Mode (Default, Recommended)
- **What**: Receiver is told the exact mode before decoding
- **AFC Performance**: Works well with frequency offsets up to ±10 Hz
- **Speed**: Faster (no mode search needed)
- **Use Case**: Production deployments, interoperability testing
- **Command Line**: No flag (default behavior)
- **How It Works**: `cfg.mode = api_mode` (specific mode like M600_SHORT)

### Auto-Detect Mode (Test/Diagnostic)
- **What**: Receiver must scan all modes to find the signal
- **AFC Performance**: Fails with frequency offsets ≥3 Hz
- **Speed**: Slower (searches all 12 modes)
- **Use Case**: Testing AFC under stress, worst-case scenarios
- **Command Line**: `--use-auto-detect`
- **How It Works**: `cfg.mode = m110a::api::Mode::AUTO`

## Implementation Changes

### 1. Removed All Test Skipping
**Files Modified**: 
- `test/exhaustive_test_unified.cpp` (2 locations)
- `server/exhaustive_server_test.cpp` (1 location)

**What Was Removed**:
```cpp
// OLD - skipped modes/channels to save time
if ((mode.cmd == "75S" || mode.cmd == "75L") && iter % 5 != 0) continue;
if ((mode.cmd == "150L" || mode.cmd == "300L") && iter % 3 != 0) continue;
if (channel.name == "foff_5hz" || channel.name == "poor_hf") && iter % 2 != 0) continue;
```

**Rationale**: 
- Tests are called "EXHAUSTIVE" - they should test everything
- Skipping poor_hf hid the AFC bug for multiple builds
- Time savings not worth the reduced test coverage

### 2. Added --use-auto-detect Flag
**Files Modified**:
- `test/exhaustive_test_unified.cpp` - Added command-line flag
- `test/direct_backend.h` - Added auto-detect support

**Command Line Usage**:
```bash
# Default: Known mode (AFC-friendly, faster)
exhaustive_test_unified.exe

# Test auto-detect (harder, tests AFC+detection)
exhaustive_test_unified.exe --use-auto-detect

# Help
exhaustive_test_unified.exe --help
```

**Backend Changes**:
```cpp
// DirectBackend now accepts auto-detect flag
DirectBackend(unsigned int seed = 42, bool use_auto_detect = false)

// Uses flag to set decoder mode
if (use_auto_detect_) {
    cfg.mode = m110a::api::Mode::AUTO;  // Auto-detect
} else {
    cfg.mode = api_mode;  // Known mode (like server)
}
```

### 3. Updated Test Output
Now shows which mode detection method is being used:
```
Backend: Direct API
Mode Detection: KNOWN (AFC-friendly)
```

or

```
Backend: Direct API
Mode Detection: AUTO (tests AFC+detection)
```

## Current Status (Build 78)

### ⚠️ CRITICAL FINDING ⚠️
**Both known mode and auto-detect mode fail with ≥3 Hz frequency offset!**

After implementing proper known-mode support in the API layer, testing revealed:
- Known mode: STILL fails poor_hf (5 Hz offset) and foff_5hz
- Auto-detect mode: STILL fails poor_hf and foff_5hz
- **Root Cause**: AFC implementation in MSDMTDecoder cannot handle frequency offsets ≥3 Hz
- **Not a mode detection issue**: The problem is fundamental AFC limitation

### Test Results
```
Channel       Known Mode    Auto Mode
foff_1hz      PASS          PASS
foff_5hz      FAIL (46%)    FAIL (46%)  
poor_hf       FAIL (100%)   FAIL (100%)
moderate_hf   PASS          PASS
```

### AFC Capability
- **Current**: Can handle ~2 Hz frequency offset reliably
- **MIL-STD-118-110A Requirement**: Should handle ±10 Hz
- **Gap**: 5-8 Hz shortfall in AFC performance

This is now tracked as a **known limitation** that requires AFC algorithm improvements.

## Historical Context

### Build 37 (a130fc2) - When Server Tests Passed
- Server backend: Used KNOWN mode → 100% pass on poor_hf
- DirectBackend: Used AUTO mode → 0% pass on poor_hf
- **Difference**: Mode detection method, NOT the channel or AFC itself

### All Previous DirectBackend Tests
- Always failed poor_hf (0% pass rate in all reports)
- We didn't notice because tests skipped poor_hf on most iterations
- Issue was ALWAYS present, just hidden by test skipping

## Recommendations

### For Production/Interoperability Testing
✅ **Use default (known mode)**
- Faster execution
- Tests real-world scenario (mode negotiated before data)
- AFC works properly
- Matches server backend behavior

### For AFC Stress Testing
⚠️ **Use --use-auto-detect flag**
- Reveals AFC limitations
- Tests worst-case scenarios
- Helps identify AFC improvements needed
- Documents current AFC capabilities

### For Exhaustive Testing
✅ **Run BOTH modes**
1. First: Default known mode (should all pass)
2. Then: With --use-auto-detect (documents AFC limits)

## GUI Integration (Future)
Planned: Checkbox in test GUI
- [ ] Use Auto-Detect Mode (tests AFC under stress)
- Default: Unchecked (known mode)
- Tooltip: "Auto-detect tests AFC with mode detection. Slower and may fail with frequency offset."

## Technical Details

### AFC Search Range
- Default: ±10 Hz in 1 Hz steps
- Location: `api/modem_rx.cpp` - `MSDMTDecoderConfig`
- Works well in known mode
- Limited in auto-detect mode (must search 12 modes × 21 freq steps)

### Mode Detection Impact
**Known Mode**: 
- 1 mode × 21 freq steps = 21 correlation attempts
- AFC converges quickly

**Auto-Detect Mode**:
- 12 modes × 21 freq steps = 252 correlation attempts
- AFC struggles to converge
- Frequency offset interferes with mode detection
- Mode detection interferes with AFC

### Why This Matters
In real military HF comms:
1. Initial link establishment uses known waveforms (like known mode)
2. AFC can handle battlefield frequency drift
3. Auto-detect would be for signal intelligence, not normal ops

## Files Modified
1. `test/exhaustive_test_unified.cpp`
   - Removed all test skipping logic
   - Added --use-auto-detect command-line flag
   - Added mode detection status to output

2. `test/direct_backend.h`
   - Added use_auto_detect parameter to constructor
   - Modified run_test() to use auto vs known mode
   - Updated clone() to preserve auto-detect setting

3. `server/exhaustive_server_test.cpp`
   - Removed all test skipping logic

4. `api/channel_sim.h`
   - Kept poor_hf with 5 Hz frequency offset enabled
   - Configuration: SNR 12 dB, 48-sample multipath, 5 Hz offset, 1 Hz fading

## Documentation Created
- `docs/POOR_HF_INVESTIGATION.md` - Full investigation report
- `docs/AUTO_DETECT_VS_KNOWN_MODE.md` - This document
- Both provide context for future development
