# D1/D2 Mode Detection - Progress Tracker

## Overview
MIL-STD-188-110A uses D1 and D2 sequences in the preamble to identify the data mode.
This has failed twice - breaking into small testable chunks.

## Previous Failure Analysis
- Need to understand exactly what failed before proceeding
- Check existing code for D1/D2 handling

## Chunked Implementation Plan

### Phase 1: Understand D1/D2 Structure
- [x] 1.1 Document D1/D2 values for each mode from mode_config.h
- [x] 1.2 Understand where D1/D2 appear in preamble structure
- [ ] 1.3 Review MIL-STD-188-110A preamble format

## Preamble Structure (per MIL-STD-188-110A)

```
Frame 1 (480 symbols):
  Symbols 0-287:   Scrambled sync (288 symbols)
  Symbols 288-335: D1 = (d1_value + scrambler) % 8  (48 symbols)
  Symbols 336-383: D1 repeated (48 symbols)
  Symbols 384-479: Scrambled sync (96 symbols)

Frame 2 (480 symbols):
  Symbols 0-47:    D2 = (d2_value + scrambler) % 8  (48 symbols)
  Symbols 48-95:   D2 repeated (48 symbols)
  Symbols 96-479:  Scrambled sync (384 symbols)

Frames 3+: All scrambled sync
```

**D1/D2 Detection Algorithm:**
1. Demodulate preamble to symbol indices (0-7)
2. At D1 positions (frame 1, sym 288-383): `d1_est = (symbol - scrambler) % 8`
3. At D2 positions (frame 2, sym 0-95): `d2_est = (symbol - scrambler) % 8`
4. Majority vote over 96 symbols for robustness
5. Lookup (d1_est, d2_est) in mode table

### Phase 2: D1/D2 Generation (TX side)
- [x] 2.1 Verify TX generates correct D1/D2 in preamble ✅ 48/48 matches
- [x] 2.2 Create test to extract D1/D2 from clean symbols ✅ All 6 modes
- [x] 2.3 Validate D1/D2 extraction from RF ✅ FIXED

## Test Results So Far

```
Phase 2.1: test_d1d2_in_preamble_symbols - PASS
Phase 2.2: test_d1d2_extraction_clean - PASS  
Phase 2.3: test_d1d2_extraction_from_rf - PASS ✅
Phase 4:   test_mode_lookup - PASS
```

## Key Fixes Applied

**RF extraction fix:**
1. Start sample = 2 * filter_delay (TX + RX SRRC delays)
2. Phase correction from first 20 preamble symbols

**Result:**
```
D1 votes: 0 0 0 0 0 0 96 0  (100% for D1=6)
D2 votes: 0 0 0 0 96 0 0 0  (100% for D2=4)
```

### Phase 3: D1/D2 Detection (RX side)  
- [x] 3.1 Create D1/D2 extractor from baseband symbols ✅
- [x] 3.2 Test extraction on clean loopback signal ✅ 11/11 modes
- [x] 3.3 Test extraction with AWGN ✅ Works at 5 dB SNR

### Phase 4: Mode Lookup
- [x] 4.1 Create D1/D2 to ModeId mapping table ✅
- [x] 4.2 Test mode identification accuracy ✅ 11/11 modes
- [x] 4.3 Handle ambiguous/unknown D1/D2 ✅ Fallback to M2400S

### Phase 5: Integration
- [x] 5.1 Integrate into MultiModeRx ✅
- [x] 5.2 Auto-detection mode in RX ✅
- [x] 5.3 Full system test ✅ 6/6 modes

## Final Test Results: 7/7 PASS ✅

```
--- Phase 2: D1/D2 Generation ---
test_d1d2_in_preamble_symbols: PASS (48/48 matches)
test_d1d2_extraction_clean: PASS (6/6 modes)
test_d1d2_extraction_from_rf: PASS (96/96 votes)

--- Phase 3: ModeDetector Class ---
test_mode_detector_class: PASS (11/11 modes, 96/96 confidence)
test_mode_detector_with_noise: PASS (works at 5 dB SNR)

--- Phase 4: Mode Lookup ---
test_mode_lookup: PASS (11/11 D1/D2 combinations)

--- Phase 5: RX Integration ---
test_auto_detect_integration: PASS (6/6 modes auto-detected)
```

## ModeDetector Class Created

**File:** `/home/claude/m110a_demod/src/m110a/mode_detector.h`

**Usage:**
```cpp
ModeDetector detector;
auto result = detector.detect(preamble_symbols);
if (result.detected && result.d1_confidence >= ModeDetector::min_confidence()) {
    // result.mode contains detected ModeId
}
```

## Current Status
✅ Phase 1.1 Complete

## D1/D2 Values per Mode

| Mode | D1 | D2 | Notes |
|------|----|----|-------|
| M75NS | 0 | 0 | No probes, special case |
| M75NL | 0 | 0 | No probes, special case |
| M150S | 7 | 4 | |
| M150L | 5 | 4 | |
| M300S | 6 | 7 | |
| M300L | 4 | 7 | |
| M600S | 6 | 6 | |
| M600L | 4 | 6 | |
| M600V | 6 | 6 | Same as M600S |
| M1200S | 6 | 5 | |
| M1200L | 4 | 5 | |
| M1200V | 6 | 5 | Same as M1200S |
| M2400S | 6 | 4 | |
| M2400L | 4 | 4 | |
| M2400V | 6 | 4 | Same as M2400S |
| M4800S | 7 | 6 | |

**Observations:**
- D1 = 6 for SHORT interleave, D1 = 4 for LONG interleave (except special cases)
- D2 encodes data rate (4=2400, 5=1200, 6=600, 7=300/150)
- 75 bps is special (D1=D2=0)
- VOICE modes share D1/D2 with SHORT modes

## Notes
- Need to find where D1/D2 appear in preamble structure
- Check TX preamble generation code
